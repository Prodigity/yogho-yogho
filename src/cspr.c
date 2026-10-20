/* Compiled-sprite interpreter.  Spec: teardown/30-render.md §6.
 *
 * The game draws sprites by far-calling generated x86 routines (exe segments 192b/1dee/2363/
 * 238e/2818/28f2/294c and the overlay sprite banks).  All 889 routines of the exe and the 12
 * overlays use one fixed prologue and a small instruction set (checked by a linear capstone
 * sweep of every routine):
 *
 *     8C C9  MOV CX,CS       8E D9  MOV DS,CX      BE iw  MOV SI,data      33 C9  XOR CX,CX   FC  CLD
 *     EF     OUT DX,AX       47     INC DI         83 C7 ib / 81 C7 iw  ADD DI,imm
 *     A4     MOVSB           A5     MOVSW          B1 ib  MOV CL,n         F3 A5  REP MOVSW
 *     D0 C4  ROL AH,1        83 D7 ib / 81 D7 iw  ADC DI,imm
 *     8C C0  MOV AX,ES       05 iw  ADD AX,imm     8E C0  MOV ES,AX        CB  RETF
 *
 * The routine is executed on the register model exactly as the CPU would: entry state
 * R.ax/R.dx/R.es/R.di, pixels copied DS:SI (DS = CS) -> ES:DI through wb() (so VGA-type sprites
 * go through the map mask written by their own OUT DX,AX, RAM-type ones into the background
 * buffer), and the exit registers (AX CX SI DI DS ES, CF) are left in R.
 * Each routine is decoded once and cached by linear address; the cache entry keeps a copy of
 * the code bytes and is re-decoded when they change (overlays are reloaded at the same segment). */
#include "cpu.h"
#include "mem.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

enum {
    OP_OUT, OP_INC_DI, OP_ADD_DI, OP_ADC_DI, OP_MOVSB, OP_MOVSW, OP_MOV_CL, OP_REP_MOVSW,
    OP_ROL_AH, OP_MOV_AX_ES, OP_ADD_AX, OP_MOV_ES_AX, OP_RETF, OP_BAD
};

typedef struct { uint8_t op; uint16_t imm; } CsOp;

typedef struct {
    uint32_t lin;        /* entry, linear (0 = empty slot) */
    uint32_t len;        /* code bytes from the entry through RETF / the bad opcode */
    uint8_t *code;       /* copy of those bytes, for validation */
    CsOp *ops;
    uint32_t nops;
    uint16_t si;         /* MOV SI,imm of the prologue */
} CsEntry;

#define CACHE_BITS 12
#define CACHE_SIZE (1u << CACHE_BITS)
static CsEntry cache[CACHE_SIZE];

#define PROLOGUE_LEN 10
#define MAX_CODE     0x10000u

int cspr_is_sprite(uint16_t seg, uint16_t off) {
    uint32_t a = lin(seg, off);
    if (a + PROLOGUE_LEN > 0xa0000u) return 0;              /* never in VRAM / ROM */
    const uint8_t *p = &MEM[a];
    return p[0] == 0x8c && p[1] == 0xc9 && p[2] == 0x8e && p[3] == 0xd9 && p[4] == 0xbe &&
           p[7] == 0x33 && p[8] == 0xc9 && p[9] == 0xfc;
}

static uint32_t slot_of(uint32_t a) { return (uint32_t)((a * 2654435761u) >> (32 - CACHE_BITS)); }

static void entry_free(CsEntry *e) {
    free(e->code); free(e->ops);
    memset(e, 0, sizeof *e);
}

/* Decode the body of the routine at linear a (prologue already checked). */
static int decode(CsEntry *e, uint32_t a, uint16_t seg, uint16_t off) {
    uint32_t cap = 256, n = 0;
    CsOp *ops = malloc(cap * sizeof *ops);
    if (!ops) return 0;
    uint32_t pc = PROLOGUE_LEN;
    for (;;) {
        if (n + 1 >= cap) {
            cap *= 2;
            CsOp *t = realloc(ops, cap * sizeof *ops);
            if (!t) { free(ops); return 0; }
            ops = t;
        }
        if (pc + 4 > MAX_CODE || a + pc + 4 > MEM_SIZE) {
            log_gap("compiled sprite %04X:%04X runs past 64 KB without RETF", seg, off);
            ops[n++].op = OP_BAD;
            break;
        }
        const uint8_t *p = &MEM[a + pc];
        CsOp o = {OP_BAD, 0};
        uint32_t sz = 0;
        switch (p[0]) {
        case 0xef: o.op = OP_OUT; sz = 1; break;
        case 0x47: o.op = OP_INC_DI; sz = 1; break;
        case 0xa4: o.op = OP_MOVSB; sz = 1; break;
        case 0xa5: o.op = OP_MOVSW; sz = 1; break;
        case 0xcb: o.op = OP_RETF; sz = 1; break;
        case 0xb1: o.op = OP_MOV_CL; o.imm = p[1]; sz = 2; break;
        case 0x05: o.op = OP_ADD_AX; o.imm = (uint16_t)(p[1] | p[2] << 8); sz = 3; break;
        case 0x83:   /* group 1, imm8 sign-extended */
            if (p[1] == 0xc7) { o.op = OP_ADD_DI; o.imm = (uint16_t)(int16_t)(int8_t)p[2]; sz = 3; }
            else if (p[1] == 0xd7) { o.op = OP_ADC_DI; o.imm = (uint16_t)(int16_t)(int8_t)p[2]; sz = 3; }
            break;
        case 0x81:
            if (p[1] == 0xc7) { o.op = OP_ADD_DI; o.imm = (uint16_t)(p[2] | p[3] << 8); sz = 4; }
            else if (p[1] == 0xd7) { o.op = OP_ADC_DI; o.imm = (uint16_t)(p[2] | p[3] << 8); sz = 4; }
            break;
        case 0xf3: if (p[1] == 0xa5) { o.op = OP_REP_MOVSW; sz = 2; } break;
        case 0xd0: if (p[1] == 0xc4) { o.op = OP_ROL_AH; sz = 2; } break;
        case 0x8c: if (p[1] == 0xc0) { o.op = OP_MOV_AX_ES; sz = 2; } break;
        case 0x8e: if (p[1] == 0xc0) { o.op = OP_MOV_ES_AX; sz = 2; } break;
        default: break;
        }
        if (o.op == OP_BAD) {
            log_gap("compiled sprite %04X:%04X: unsupported opcode %02X %02X at +%X",
                    seg, off, p[0], p[1], (unsigned)pc);
            ops[n++] = o;
            pc += 1;
            break;
        }
        ops[n++] = o;
        pc += sz;
        if (o.op == OP_RETF) break;
    }
    e->code = malloc(pc);
    if (!e->code) { free(ops); return 0; }
    memcpy(e->code, &MEM[a], pc);
    e->len = pc;
    e->ops = ops;
    e->nops = n;
    e->lin = a;
    e->si = (uint16_t)(MEM[a + 5] | MEM[a + 6] << 8);
    return 1;
}

static CsEntry *lookup(uint16_t seg, uint16_t off) {
    uint32_t a = lin(seg, off);
    CsEntry *e = &cache[slot_of(a)];
    if (e->lin == a && a + e->len <= MEM_SIZE && memcmp(e->code, &MEM[a], e->len) == 0) return e;
    if (e->lin) entry_free(e);
    if (!decode(e, a, seg, off)) { entry_free(e); return NULL; }
    return e;
}

void cspr_exec(uint16_t seg, uint16_t off) {
    if (!cspr_is_sprite(seg, off)) { log_gap("cspr_exec on non-sprite %04X:%04X", seg, off); return; }
    CsEntry *e = lookup(seg, off);
    if (!e) return;
    /* prologue: CX = CS, DS = CX, SI = data, XOR CX,CX (CF = 0), CLD */
    uint16_t ax = R.ax, dx = R.dx, es = R.es, di = R.di, ds = seg, si = e->si, cx = 0;
    unsigned cf = 0;
    for (uint32_t i = 0; i < e->nops; i++) {
        const CsOp *o = &e->ops[i];
        uint32_t t;
        switch (o->op) {
        case OP_OUT: io_outw(dx, ax); break;
        case OP_INC_DI: di++; break;                        /* INC keeps CF */
        case OP_ADD_DI: t = (uint32_t)di + o->imm; cf = t >> 16; di = (uint16_t)t; break;
        case OP_ADC_DI: t = (uint32_t)di + o->imm + cf; cf = t >> 16; di = (uint16_t)t; break;
        case OP_MOVSB: wb(es, di, rb(ds, si)); si++; di++; break;
        case OP_MOVSW:
            wb(es, di, rb(ds, si)); wb(es, (uint16_t)(di + 1), rb(ds, (uint16_t)(si + 1)));
            si = (uint16_t)(si + 2); di = (uint16_t)(di + 2);
            break;
        case OP_MOV_CL: cx = (uint16_t)((cx & 0xff00) | o->imm); break;
        case OP_REP_MOVSW:
            for (; cx; cx--) {
                wb(es, di, rb(ds, si)); wb(es, (uint16_t)(di + 1), rb(ds, (uint16_t)(si + 1)));
                si = (uint16_t)(si + 2); di = (uint16_t)(di + 2);
            }
            break;
        case OP_ROL_AH: {
            uint8_t ah = (uint8_t)(ax >> 8);
            cf = ah >> 7;
            ax = (uint16_t)((ax & 0xff) | (uint16_t)((uint8_t)((ah << 1) | cf) << 8));
            break;
        }
        case OP_MOV_AX_ES: ax = es; break;
        case OP_ADD_AX: t = (uint32_t)ax + o->imm; cf = t >> 16; ax = (uint16_t)t; break;
        case OP_MOV_ES_AX: es = ax; break;
        case OP_RETF: goto done;
        default: goto done;                                 /* OP_BAD: logged at decode */
        }
    }
done:
    R.ax = ax; R.cx = cx; R.si = si; R.di = di; R.ds = ds; R.es = es; R.cf = (uint8_t)cf;
}
