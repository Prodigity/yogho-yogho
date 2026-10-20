/* The 26 engine API routines reached through the overlay stubs ovl:0x0b+5*i (table CS:050f).
 * Spec: teardown/50-objects-api.md §3e-3l; every routine was re-read from the instructions
 * (capstone sweep of the EXE image, 1000:2347-2813, 1000:3b7c-478b, 1000:05da).
 *
 * All routines are REG: arguments/results in R exactly like the registers.  Where the asm leaves
 * a register with a meaningful value (or unchanged) the port does the same, because overlay
 * handlers continue with those registers (e.g. A6 leaves DI = sprite descriptor, AX/DX kept).
 * "ES:" accesses use R.es (the overlay segment set by the engine before each handler call);
 * "DS:" accesses are DGROUP (the engine always runs handlers with DS = DGROUP).
 * Flags that callers test are set explicitly: ZF/SF (flags16) for A5/A6/A21/A22, CF for A2.
 * Signed conditional jumps after ADD/SUB test the sign of the exact (non-wrapped) result, which is
 * what SF^OF means; JS/JNS test bit 15 of the wrapped result. */
#include "game.h"
#include "objects_ext.h"
#include "log.h"

#define SW(f)      slot_w(R.bp, (f))
#define SS(f, v)   slot_set(R.bp, (f), (uint16_t)(v))
#define SB(f)      slot_b(R.bp, (f))
#define SBS(f, v)  slot_bset(R.bp, (f), (uint8_t)(v))
#define ESW(o)     rw(R.es, (uint16_t)(o))
#define ESB(o)     rb(R.es, (uint16_t)(o))
#define CSW(o)     rw(SEG_EXE, (uint16_t)(o))

static inline int sx(uint16_t v) { return (int16_t)v; }
static inline uint16_t cbw(uint16_t v) { return (uint16_t)(int16_t)(int8_t)(uint8_t)v; }
/* ADD/SUB to a 16-bit register; returns the exact signed result (for JL/JGE/JG/JLE). */
static inline int add16(uint16_t *r, int v) { int t = (int16_t)*r + v; *r = (uint16_t)t; return t; }
/* ADC: a + b + *c, carry out in *c. */
static inline uint16_t adc16(uint16_t a, uint16_t b, unsigned *c) {
    uint32_t t = (uint32_t)a + b + *c; *c = (unsigned)(t >> 16); return (uint16_t)t;
}
#define SET_BL(r, v) ((r).bx = (uint16_t)(((r).bx & 0xff00) | (uint8_t)(v)))
#define SET_CL(r, v) ((r).cx = (uint16_t)(((r).cx & 0xff00) | (uint8_t)(v)))

/* ===================================================================== despawn A10-A13 */
void A10_despawn(void) {                                    /* 1000:3b8b */
    R.bx = 0;
    SS(S_CODE, 0);
    R.si = SW(S_REC);
    R.bx = (uint16_t)(ESW(R.si + REC_FLAGS) & 0x77ff);      /* respawnable */
    ww(R.es, (uint16_t)(R.si + REC_FLAGS), R.bx);
}
void A12_despawn_unride(void) {                             /* 1000:3b7c */
    if (SW(S_W3C) & 1) dgw_set(0xd4, 0);
    A10_despawn();
}
void A11_kill(void) {                                       /* 1000:3bd6 */
    R.bx = 0;
    SS(S_CODE, 0);
    R.si = SW(S_REC);
    ww(R.es, (uint16_t)(R.si + REC_FLAGS), (uint16_t)(ESW(R.si + REC_FLAGS) & 0xa7ff));  /* keeps 0x8000 */
}
void A13_kill_unride(void) {                                /* 1000:3bc7 */
    if (SW(S_W3C) & 1) dgw_set(0xd4, 0);
    A11_kill();
}

/* ===================================================================== A2 anim step */
void A2_anim_step(void) {                                   /* 1000:3be9 */
    uint8_t al, ah;
    R.ax = SW(S_AFLAGS);
    al = (uint8_t)R.ax;
    if (R.ax & 0x100) goto clc;                             /* hold */
    {
        uint8_t d = (uint8_t)(SB(S_DCNT) - 1);              /* dec byte; jns */
        SBS(S_DCNT, d);
        if (!(d & 0x80)) goto clc;
    }
    ah = SB(S_DELAY);                                       /* 3bfa */
    SBS(S_DCNT, ah);
    ah = al;                                                /* flags lo */
    if (!(ah & 0x80)) {                                     /* add al,al: CF = bit7 */
        if (!(ah & 0x40)) goto end;                         /* mode 0x00 static */
        al = SB(S_FRAME);                                   /* 3c29 mode 0x40 once */
        if (al == SB(S_LAST)) goto end;
        SBS(S_FRAME, al + 1);
        goto clc;
    }
    if (!(ah & 0x40)) {                                     /* 3c59 mode 0x80 loop */
        al = SB(S_FRAME);
        if (al == SB(S_LAST)) { SBS(S_FRAME, 0); goto end; }
        SBS(S_FRAME, al + 1);
        goto clc;
    }
    if (!(ah & 0x20)) {                                     /* 3c92 ping-pong forward */
        al = SB(S_FRAME);
        if (al != SB(S_LAST)) { SBS(S_FRAME, al + 1); goto clc; }
        ah ^= 0x20;                                         /* 3cb0 */
        if (al) SBS(S_FRAME, SB(S_FRAME) - 1);
        SBS(S_AFLAGS, ah);
        goto clc;
    }
    al = SB(S_FRAME);                                       /* 3cce ping-pong back */
    if (al) { SBS(S_FRAME, al - 1); goto clc; }
    ah ^= 0x20;                                             /* 3ce9 */
    if (al != SB(S_LAST)) SBS(S_FRAME, SB(S_FRAME) + 1);
    SBS(S_AFLAGS, ah);
end:                                                        /* mov al,ah; mov ah,[14c]; and [14c],fb; shr al,1 */
    al = ah;
    ah = SB(S_AFLAGS + 1);
    SBS(S_AFLAGS + 1, ah & 0xfb);
    R.cf = al & 1;
    al >>= 1;
    R.ax = (uint16_t)(al | ah << 8);
    return;
clc:                                                        /* mov ah,[14c]; and [14c],fb; clc */
    ah = SB(S_AFLAGS + 1);
    SBS(S_AFLAGS + 1, ah & 0xfb);
    R.cf = 0;
    R.ax = (uint16_t)(al | ah << 8);
}

/* ===================================================================== A3 motion scripts */
static void fetch_next(void);

static void mot_end(void) {                                 /* 1000:3fbe  (tests CX as left by the caller path) */
    R.bx = 0;
    SS(S_SCRIPT, 0);
    if (R.cx & 0x200) R.bx++;
    SS(S_CNT, R.bx);
    R.bx = R.ax; R.cx = R.dx;
}
static void mot_wait_cont(void) {                           /* 1000:3d25 */
    uint16_t c = (uint16_t)(SW(S_CNT) - 1);
    SS(S_CNT, c);
    if (c & 0x8000) { fetch_next(); return; }
    R.bx = R.ax; R.cx = R.dx;
}
static void mot_vel_cont(void) {                            /* 1000:3dfb */
    R.bx = (uint16_t)(R.ax + SW(S_VX));
    R.cx = (uint16_t)(R.dx + SW(S_VY));
}
static void mot_integ(void) {                               /* 1000:3e0a */
    unsigned c = 0;
    R.cx = adc16(SW(S_VXF), SW(S_XF), &c); SS(S_XF, R.cx);
    R.bx = adc16(SW(S_VX), R.ax, &c);
    c = 0;
    R.cx = adc16(SW(S_YF), SW(S_VYF), &c); SS(S_YF, R.cx);
    R.cx = adc16(SW(S_VY), R.dx, &c);
}
static void mot_acc(void) {                                 /* 1000:3e37 */
    unsigned c = 0;
    R.cx = SW(S_AXF); R.bx = SW(S_AX);
    R.cx = adc16(R.cx, SW(S_VXF), &c); SS(S_VXF, R.cx);
    R.bx = adc16(R.bx, SW(S_VX), &c);  SS(S_VX, R.bx);
    c = 0;
    R.cx = adc16(R.cx, SW(S_XF), &c);  SS(S_XF, R.cx);
    R.bx = adc16(R.bx, R.ax, &c);
    uint16_t nx = R.bx;                                     /* push bx */
    c = 0;
    R.bx = SW(S_AYF); R.cx = SW(S_AY);
    R.bx = adc16(R.bx, SW(S_VYF), &c); SS(S_VYF, R.bx);
    R.cx = adc16(R.cx, SW(S_VY), &c);  SS(S_VY, R.cx);
    c = 0;
    R.bx = adc16(R.bx, SW(S_YF), &c);  SS(S_YF, R.bx);
    R.cx = adc16(R.cx, R.dx, &c);
    R.bx = nx;                                              /* pop bx */
}
static void mot_goto(void) {                                /* 1000:3e8e */
    R.si = ESW(R.si + 2);
    SS(S_SCRIPT, R.si);
    fetch_next();
}
static void mot_wait(void) {                                /* 1000:3e9a */
    R.cx = ESW(R.si + 2);
    R.si = (uint16_t)(R.si + 4);
    SS(S_SCRIPT, R.si);
    SS(S_CNT, R.cx);
    R.bx = R.ax; R.cx = R.dx;
}
static void mot_vel(void) {                                 /* 1000:3eb0 */
    R.cx = ESW(R.si + 2); SS(S_CNT, R.cx);
    R.cx = 0; SS(S_VXF, 0); SS(S_VYF, 0);
    R.bx = ESW(R.si + 4); R.cx = ESW(R.si + 6);
    R.si = (uint16_t)(R.si + 8); SS(S_SCRIPT, R.si);
    SS(S_VX, R.bx); SS(S_VY, R.cx);
    R.bx = (uint16_t)(R.bx + R.ax); R.cx = (uint16_t)(R.cx + R.dx);
}
static void mot_velfx(void) {                               /* 1000:3ee4 */
    R.cx = ESW(R.si + 2);   SS(S_CNT, R.cx);
    R.bx = ESW(R.si + 8);   SS(S_VYF, R.bx);
    R.bx = ESW(R.si + 0xa); SS(S_VY, R.bx);
    R.cx = ESW(R.si + 4);   SS(S_VXF, R.cx);
    R.bx = ESW(R.si + 6);   SS(S_VX, R.bx);
    R.si = (uint16_t)(R.si + 0xc); SS(S_SCRIPT, R.si);
    mot_integ();                                            /* 3f19.. is the INTEG body (same values) */
}
static void mot_accel(void) {                               /* 1000:3f3c */
    R.cx = ESW(R.si + 2);   SS(S_CNT, R.cx);
    R.bx = ESW(R.si + 8);   SS(S_AYF, R.bx);
    R.bx = ESW(R.si + 0xa); SS(S_AY, R.bx);
    R.cx = ESW(R.si + 4);   SS(S_AXF, R.cx);
    R.bx = ESW(R.si + 6);   SS(S_AX, R.bx);
    R.si = (uint16_t)(R.si + 0xc); SS(S_SCRIPT, R.si);
    mot_acc();                                              /* 3f71.. is the ACC body (same values) */
}

/* JMP CS:[BX+table]: the op tables at CS:3d0d (continue) and CS:3d19 (fetch) overlap and the
 * fetch table runs into code (BX=0xc/0xe -> 0xff3e/0x478e); dispatch by the address found. */
static void mot_jump(uint16_t table) {
    uint16_t h = CSW(table + R.bx);
    switch (h) {
    case 0x3fbe: mot_end(); break;
    case 0x3d25: mot_wait_cont(); break;
    case 0x3dfb: mot_vel_cont(); break;
    case 0x3e0a: mot_integ(); break;
    case 0x3e37: mot_acc(); break;
    case 0x3e8e: mot_goto(); break;
    case 0x3e9a: mot_wait(); break;
    case 0x3eb0: mot_vel(); break;
    case 0x3ee4: mot_velfx(); break;
    case 0x3f3c: mot_accel(); break;
    default:                                                /* original crashes; no data reaches it */
        log_gap("motion op jump CS:%04X+%X -> %04X (garbage table entry)", table, R.bx, h);
        R.bx = R.ax; R.cx = R.dx;
        break;
    }
}

static void fetch_next(void) {                              /* 1000:3d41  SI = script */
    R.bx = ESW(R.si);
    SS(S_OP, R.bx);
    R.cx = (uint16_t)((SW(S_AFLAGS) & 0xfcff) | (((R.bx >> 8) & 3) << 8));
    SS(S_AFLAGS, R.cx);
    if (R.bx & 0x21) {                                      /* 3d8e */
        R.di = SW(S_ANIMS);
        if (R.bx & 1) {                                     /* 3ded: switch to next anim, script replaced */
            uint16_t ax = R.ax, dx = R.dx;
            A7_next_anim();
            R.dx = dx; R.ax = ax;
            R.bx = R.ax; R.cx = R.dx;
            return;
        }
        R.di = (uint16_t)(R.di + SW(S_ANIMOFS));            /* 0x20: load next anim entry, keep script */
        uint16_t ax = R.ax;
        R.ax = cbw(ESB(R.di + 1));
        SS(S_ANIMOFS, R.ax);
        R.di = (uint16_t)(SW(S_ANIMS) + R.ax);
        R.cx = ESW(R.di + 2); SS(S_AFLAGS, R.cx);
        SET_AL(R, ESB(R.di)); SS(S_FRAMESET, R.ax);
        R.di = (uint16_t)(SW(S_SPRITE) + R.ax);
        R.ax = ESW(R.di + 0x16); SS(S_LAST, R.ax);          /* word: last, frame */
        R.ax = ESW(R.di + 0x18); SS(S_DELAY, R.ax);         /* word: delay, counter */
        R.ax = ax;
        R.bx &= 0xe;
        mot_jump(0x3d19);
        return;
    }
    if (R.bx & 0x10) {                                      /* 3de2 near CALL 3bfa -> RETF: crash */
        log_gap("motion op bit 0x10 at %04X:%04X (original crashes)", R.es, R.si);
        R.bx = R.ax; R.cx = R.dx;
        return;
    }
    R.bx &= 0xe;
    mot_jump(0x3d19);
}

void A3_motion_step(void) {                                 /* 1000:3d31  AX,DX -> BX,CX */
    R.si = SW(S_SCRIPT);
    if (R.si == 0) {                                        /* 3d7d */
        R.cx = SW(S_CNT);
        if (R.cx) { mot_integ(); return; }
        R.bx = R.ax; R.cx = R.dx;
        return;
    }
    uint16_t c = (uint16_t)(SW(S_CNT) - 1);
    SS(S_CNT, c);
    if (!(c & 0x8000)) {                                    /* 3d6e continue current op */
        R.cx = SW(S_OP);
        R.bx = R.cx & 0xe;
        mot_jump(0x3d0d);
        return;
    }
    fetch_next();
}

/* ===================================================================== A7/A8/A9 */
static void restart_motion(void) {                          /* 1000:4025  BX = new aflags (A8) / 0 (A9) */
    R.di = (uint16_t)(SW(S_ANIMS) + SW(S_ANIMOFS));
    R.ax = SW(S_AFLAGS);
    if (R.ax & 0x10) {
        if (R.bx & 0x200) return;
        R.si = ESW(R.di + 4);
        R.bx = ESW(R.si);
        SS(S_OP, R.bx);
        R.ax = (uint16_t)((R.ax & 0xfeff) | (((R.bx >> 8) & 1) << 8));
        SS(S_AFLAGS, R.ax);
        R.bx &= 0xe;
        mot_jump(0x3d19);                                   /* first op runs with AX=aflags, CX/DX as left */
        return;
    }
    if (R.bx & 0x200) return;                               /* 4060 */
    R.ax = cbw(ESB(R.di + 5)); SS(S_VX, R.ax);
    R.ax = cbw(ESB(R.di + 4)); SS(S_VY, R.ax);
    R.ax = 0;
    SS(S_VXF, 0); SS(S_VYF, 0); SS(S_SCRIPT, 0);
    R.ax = 1;
    SS(S_CNT, 1);
}
void A9_restart_motion(void) { R.bx = 0; restart_motion(); }   /* 1000:4023 */

void A8_set_anim(void) {                                    /* 1000:3fe5  AX = anim entry offset */
    R.di = (uint16_t)(SW(S_ANIMS) + R.ax);
    R.cx = SW(S_AFLAGS);
    SS(S_ANIMOFS, R.ax);
    R.bx = ESW(R.di + 2);
    SS(S_AFLAGS, R.bx);
    SET_AL(R, ESB(R.di));                                   /* MOV AL only: AH of the offset kept */
    SS(S_FRAMESET, R.ax);
    R.si = (uint16_t)(SW(S_SPRITE) + R.ax);
    R.ax = ESW(R.si + 0x16); SS(S_LAST, R.ax);
    R.ax = ESW(R.si + 0x18); SS(S_DELAY, R.ax);
    restart_motion();
}
void A7_next_anim(void) {                                   /* 1000:3fd6 */
    R.di = (uint16_t)(SW(S_ANIMS) + SW(S_ANIMOFS));
    R.ax = cbw(ESB(R.di + 1));
    A8_set_anim();
}

/* ===================================================================== A1 draw */
/* Origin transform + clip + CALLF CS:[3b78] (1000:2689..26c0 and the copies in the other paths).
 * y2/x2 are doubled page-relative coordinates (word-table indices). */
static void a1_call(uint16_t code, uint16_t y2, uint16_t x2) {
    ww(SEG_EXE, 0x3b78, code);
    R.cx = CSW(0x2c18 + y2);
    R.di = CSW(0x31b8 + x2);
    R.ax = R.di | R.cx;
    R.bx = x2;
    if (R.ax & 0x8000) return;                              /* anchor outside [-48,336)x[-48,160) */
    R.cx = (uint16_t)(R.cx + CSW(0x2814));
    R.ax = CSW(0x3898 + x2);
    R.dx = 0x3c4;
    R.es = R.cx;
    farcall(CSW(0x3b7a), CSW(0x3b78));
}

void A1_draw(void) {                                        /* 1000:2648 */
    if (dgw(0x2ca0)) return;                                /* catch-up iteration */
    uint16_t es = R.es;
    uint16_t d = SW(S_SPRITE);
    ww(SEG_EXE, 0x3b7a, rw(es, (uint16_t)(d + 1)));         /* sprite code segment */
    uint16_t si = rw(es, (uint16_t)(d + 3));                /* part offset list */
    uint8_t n2 = rb(es, d);
    uint16_t ft = rw(es, (uint16_t)(d + SW(S_FRAMESET) + 0x14));
    unsigned off = ft & 1;
    ft &= 0xfffe;
    uint8_t frame = SB(S_FRAME);
    R.si = si;
    if (!off) {
        if (n2) return;                                     /* 2675 JNZ 26c6 = RETF: multi-part w/o offsets never draws */
        uint16_t e = (uint16_t)(ft + 2 * frame);
        uint16_t y2 = (uint16_t)((SW(S_Y) - CSW(0x1021)) * 2), x2 = (uint16_t)((SW(S_X) - CSW(0x1023)) * 2);
        a1_call(rw(es, e), y2, x2);
        return;
    }
    if (!n2) {                                              /* 2738 single part with offsets */
        uint16_t e = (uint16_t)(ft + 6 * frame);
        uint16_t y2 = (uint16_t)((SW(S_Y) - CSW(0x1021)) * 2), x2 = (uint16_t)((SW(S_X) - CSW(0x1023)) * 2);
        x2 = (uint16_t)(x2 + rw(es, (uint16_t)(e + 2)));
        y2 = (uint16_t)(y2 + rw(es, (uint16_t)(e + 4)));
        a1_call(rw(es, e), y2, x2);
        return;
    }
    /* 2793 multi part with offsets: entry = 3*n2 bytes per frame (AX = n2*frame, added 3x) */
    uint16_t e = (uint16_t)(ft + 3 * (uint16_t)(n2 * frame));
    uint16_t y2 = (uint16_t)((SW(S_Y) - CSW(0x1021)) * 2), x2 = (uint16_t)((SW(S_X) - CSW(0x1023)) * 2);
    uint8_t al = n2;
    R.dx = 0x3c4;
    for (;;) {                                              /* 27c2 */
        uint16_t bx = (uint16_t)(x2 + rw(es, (uint16_t)(e + 2)) + rw(es, si));
        uint16_t di = (uint16_t)(y2 + rw(es, (uint16_t)(e + 4)) + rw(es, (uint16_t)(si + 2)));
        si = (uint16_t)(si + 4);
        uint16_t code = rw(es, e);
        e = (uint16_t)(e + 6);
        if (code) { R.si = si; a1_call(code, di, bx); R.es = es; }   /* push/pop si, es around the call */
        int t = (int8_t)al - 2;                             /* sub al,2; jg */
        al = (uint8_t)t;
        if (!(t > 0)) break;
    }
    R.si = si;
}

/* ===================================================================== A4 map move */
#define F_OR(v) ww(SEG_EXE, 0x2345, (uint16_t)(rw(SEG_EXE, 0x2345) | (v)))
#define SAR1(r) ((r) = (uint16_t)((int16_t)(r) >> 1))

void A4_map_move(void) {                                    /* 1000:2347  AX,DX from -> BX,CX to */
    uint16_t sdi = R.di, ssi = R.si;                        /* push di, si */
    uint16_t sh;
    dgw_set(0x2dba, R.ax); dgw_set(0x2dbc, R.dx); dgw_set(0x2db6, R.bx); dgw_set(0x2db8, R.cx);
    R.si = 0;
    ww(SEG_EXE, 0x2345, 0);
    {
        int t = add16(&R.bx, -sx(R.ax));
        if (R.bx == 0) goto L252e;
        if (t > 0) goto L236b;
        goto L2437;
    }
L236b:                                                      /* x pass, moving right */
    R.si = R.bx; R.di = R.bx;
    map_probe(); SAR1(R.ax);
    if (R.bx == 0) goto L2391;
    if (sx(R.bx) > 0) goto L238d;
    add16(&R.bx, -sx(R.ax));
    if (add16(&R.bx, 0xf) < 0) goto FAIL;
    if (add16(&R.bx, -sx(R.si)) >= 0) goto L23ca;
    if (add16(&R.si, sx(R.bx)) >= 0) goto L23ca;
    goto FAIL;
L238d:
    if (add16(&R.bx, -sx(R.ax)) > 0) goto FAIL;
L2391:
    add16(&R.ax, sx(R.si));
    if (add16(&R.ax, -0xf) <= 0) goto L23ca;
    R.ax = dgw(0x2db6); R.dx = dgw(0x2dbc);
    map_probe(); SAR1(R.ax);
    if (R.bx == 0) goto L23ca;
    if (sx(R.bx) < 0) goto L23b9;
    R.ax++;
    F_OR(4);
    if (add16(&R.si, -sx(R.ax)) >= 0) goto L23ca;
    goto FAIL;
L23b9:
    add16(&R.bx, -sx(R.ax));
    if (add16(&R.bx, 0xf) > 0) goto L23ca;
    F_OR(4);
    if (add16(&R.si, sx(R.bx)) < 0) goto FAIL;
L23ca:
    add16(&R.di, -sx(R.si));
    if (R.di == 0) goto L2525;
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
    sh = SW(S_SHAPE);                                       /* push [bp+13f] (shape before the test) */
    R.dx++;
    map_bit();                                              /* ZF = (AX == 0) after AND AX,[BX] */
    R.bx = sh;
    if (R.ax == 0) { R.dx--; goto L2525; }
L23e8:
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
    R.ax++;
    map_step();
    R.ax = R.cx | R.bx;
    if (R.ax & 0x8000) goto L2429;
    R.ax = cbw(R.bx);
    dgw_set(0x2dbc, (uint16_t)(dgw(0x2dbc) - R.ax));
    R.ax = cbw(R.cx);
    add16(&R.si, sx(R.ax));
    if (add16(&R.di, -sx(R.ax)) <= 0) goto L2432;
    if ((uint8_t)R.bx == 2) { R.di--; if (R.di == 0) goto L2429; }
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
    R.ax++;
    map_bit();
    { uint16_t z = R.ax; R.bx = SW(S_SHAPE); if (z != 0) goto L23e8; }
L2429:
    F_OR(2);
    goto L2525;
L2432:
    R.si = (uint16_t)(R.si + R.di);
    goto L2525;

L2437:                                                      /* x pass, moving left (mirror) */
    R.si = R.bx; R.di = R.bx;
    map_probe(); SAR1(R.ax);
    if (R.bx == 0) goto L2463;
    if (sx(R.bx) < 0) goto L245c;
    if (add16(&R.bx, -sx(R.ax)) > 0) goto FAIL;
    if (add16(&R.bx, -sx(R.si)) < 0) goto L249b;
    F_OR(4);
    if (add16(&R.si, sx(R.bx)) <= 0) goto L249b;
    goto FAIL;
L245c:
    add16(&R.bx, -sx(R.ax));
    if (add16(&R.bx, 0xf) < 0) goto FAIL;
L2463:
    if (add16(&R.ax, sx(R.si)) >= 0) goto L249b;
    R.ax = dgw(0x2db6); R.dx = dgw(0x2dbc);
    map_probe(); SAR1(R.ax);
    if (R.bx == 0) goto L249b;
    if (sx(R.bx) < 0) goto L248e;
    if (add16(&R.bx, -sx(R.ax)) < 0) goto L249b;
    F_OR(4);
    if (add16(&R.si, sx(R.bx)) <= 0) goto L249b;
    goto FAIL;
L248e:
    add16(&R.ax, -0x10);
    F_OR(4);
    if (add16(&R.si, -sx(R.ax)) > 0) goto FAIL;
L249b:
    add16(&R.di, -sx(R.si));
    if (R.di == 0) goto L2525;
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
    sh = SW(S_SHAPE);
    R.dx++;
    map_bit();
    R.bx = sh;
    if (R.ax == 0) goto L2525;
L24b5:
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
    R.ax--;
    map_step();
    R.ax = R.cx | R.bx;
    if (R.ax & 0x8000) goto L24f6;
    R.ax = cbw(R.bx);
    dgw_set(0x2dbc, (uint16_t)(dgw(0x2dbc) - R.ax));
    R.ax = cbw(R.cx);
    add16(&R.si, sx(R.ax));
    if (add16(&R.di, -sx(R.ax)) >= 0) goto L24ff;
    if ((uint8_t)R.bx == 2) { R.di++; if (R.di == 0) goto L24f6; }
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
    R.ax--;
    map_bit();
    { uint16_t z = R.ax; R.bx = SW(S_SHAPE); if (z != 0) goto L24b5; }
L24f6:
    F_OR(2);
    goto L2525;
L24ff:
    R.si = (uint16_t)(R.si + R.di);

L2525:                                                      /* y pass */
    R.ax = (uint16_t)(dgw(0x2dba) + R.si); R.dx = dgw(0x2dbc);
L252e:
    dgw_set(0x2db6, R.ax);
    R.cx = dgw(0x2db8);
    {
        int t = add16(&R.cx, -sx(R.dx));
        if (R.cx == 0) goto L2504;
        if (t > 0) goto L253e;
        goto L25c6;
    }
L2504:                                                      /* no vertical move: floor test only */
    R.dx++;
    map_bit();
    {
        uint16_t z = R.ax;
        R.ax = dgw(0x2db6); R.dx = dgw(0x2db8);
        R.bx = rw(SEG_EXE, 0x2345);
        if (z == 0) R.bx++;
    }
    goto RET;
L253e:                                                      /* moving down */
    R.si = R.cx; R.di = R.cx;
    map_probe(); SAR1(R.dx);
    if (R.cx == 0) goto L256a;
    if (sx(R.cx) > 0) goto L2566;
    add16(&R.cx, -sx(R.dx));
    if (add16(&R.cx, 0xf) < 0) goto FAIL;
    if (add16(&R.cx, -sx(R.si)) >= 0) goto RET1;
    F_OR(8);
    if (add16(&R.si, sx(R.cx)) >= 0) goto RET0;
    goto FAIL;
L2566:
    if (add16(&R.cx, -sx(R.dx)) > 0) goto FAIL;
L256a:
    add16(&R.dx, sx(R.si));
    if (add16(&R.dx, -0xf) <= 0) goto RET1;
    R.ax = dgw(0x2db6); R.dx = dgw(0x2db8);
    map_probe(); SAR1(R.dx);
    if (R.cx == 0) goto RET1;
    if (sx(R.cx) > 0) goto L2598;
    add16(&R.dx, -sx(R.cx));
    if (add16(&R.dx, -0xf) < 0) goto RET1;
    F_OR(8);
    if (add16(&R.si, -sx(R.dx)) >= 0) goto RET0;
    goto FAIL;
L2598:
    R.dx++;
    F_OR(8);
    if (add16(&R.si, -sx(R.dx)) < 0) goto FAIL;
    goto RET0;
L25c6:                                                      /* moving up */
    R.si = R.cx; R.di = R.cx;
    map_probe(); SAR1(R.dx);
    if (R.cx == 0) goto L25f2;
    if (sx(R.cx) < 0) goto L25eb;
    if (add16(&R.cx, -sx(R.dx)) > 0) goto FAIL;
    if (add16(&R.cx, -sx(R.si)) < 0) goto RET1;
    F_OR(8);
    if (add16(&R.si, sx(R.cx)) <= 0) goto RET1;
    goto FAIL;
L25eb:
    add16(&R.cx, -sx(R.dx));
    if (add16(&R.cx, 0xf) < 0) goto FAIL;
L25f2:
    if (add16(&R.dx, sx(R.si)) >= 0) goto RET1;
    R.ax = dgw(0x2db6); R.dx = dgw(0x2db8);
    map_probe(); SAR1(R.dx);
    if (R.cx == 0) goto RET1;
    if (sx(R.cx) < 0) goto L261d;
    if (add16(&R.cx, -sx(R.dx)) < 0) goto RET1;
    F_OR(8);
    if (add16(&R.si, sx(R.cx)) <= 0) goto RET1;
    goto FAIL;
L261d:
    add16(&R.dx, -0x10);
    if (add16(&R.si, -sx(R.dx)) > 0) goto FAIL;
    goto RET1;

RET0:                                                       /* 25a3 */
    R.ax = dgw(0x2db6); R.dx = (uint16_t)(dgw(0x2dbc) + R.si);
    R.bx = rw(SEG_EXE, 0x2345);
    goto RET;
RET1:                                                       /* 25b4 */
    R.ax = dgw(0x2db6); R.dx = (uint16_t)(dgw(0x2dbc) + R.si);
    R.bx = (uint16_t)(rw(SEG_EXE, 0x2345) + 1);
    goto RET;
FAIL:                                                       /* 2637 */
    R.ax = dgw(0x2dba); R.dx = dgw(0x2dbc);
    R.bx = 0xffff;
    DB(0x3513) = 0xff;
RET:
    R.si = ssi; R.di = sdi;                                 /* pop si, di */
}

/* ===================================================================== A5 / A6 player contact */
void A5_overlap(void) {                                     /* 1000:4092 */
    R.di = SW(S_SPRITE);
    R.bx = (uint16_t)(ESW(R.di + 5) + R.ax - dgw(0xee));
    if (R.bx > ESW(R.di + 9)) goto none;                    /* ja (unsigned) */
    R.cx = (uint16_t)(ESW(R.di + 7) + R.dx - dgw(0xf0));
    if (R.cx > ESW(R.di + 0xb)) goto none;
    R.bx = (uint16_t)(ESW(R.di + 0xb) >> 1);
    R.bx = (R.bx >= R.cx) ? 1 : 0xffff;
    flags16(R.bx);
    return;
none:
    R.bx = 0;
    flags16(0);
}

void A6_touch(void) {                                       /* 1000:41a0  AX,DX kept; DI = sprite desc */
    R.di = SW(S_SPRITE);
    R.bx = (uint16_t)(ESW(R.di + 5) + R.ax - dgw(0xee));
    if (R.bx & 0x8000) {                                    /* js 41c1 */
        R.bx = (uint16_t)(R.bx + ESW(R.di + 9));
        if (R.bx & 0x8000) goto miss;
    } else if (R.bx > 0x10) goto miss;                      /* cmp bx,10; jbe 41c7 */
    R.cx = (uint16_t)(ESW(R.di + 7) + R.dx - dgw(0xf0));    /* 41c7 */
    if (!(R.cx & 0x8000) && R.cx > 0x1a) goto miss;
    R.cx = (uint16_t)(R.cx + ESW(R.di + 0xb));              /* 41d8: also for 0<=T<=26 */
    if (R.cx & 0x8000) goto miss;
    R.cx = SW(S_LATCH);
    if (R.cx) { R.bx = 0; flags16(0); return; }             /* 41be */
    R.bx = dgw(0x100);
    if (!(R.bx & 0x8000) && (dgw(0x122) & 1)) goto stomp;
    if (SB(S_HITS) & 0x80) { R.bx = 0xffff; flags16(R.bx); return; }   /* harmless side contact */
    SET_BL(R, ESB(R.di + 0x13));
    if (DB(0xb4) != 0 || (dgw(0x2c9c) & 0x80)) { R.bx = 0; flags16(0); return; }
    DB(0xb2) = (uint8_t)(DB(0xb2) - (uint8_t)R.bx);
    dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x80));
    {
        uint16_t b = (uint16_t)(dgw(0xe8) - R.ax);
        uint8_t bh = (uint8_t)((((b >> 8) & 0x40) | 0x20) + 1);    /* 0x21 / 0x61: knock-back dir */
        DB(0xbd) = bh;
        DB(0xb3) |= bh;
    }
    R.bx = 0xffff;
    flags16(R.bx);
    return;
stomp:                                                      /* 4241 */
    R.bx = 1;
    SS(S_LATCH, 1);
    {
        uint8_t bl = SB(S_HITS);
        R.bx = bl;                                          /* mov bl,[173] with BH = 0 */
        SET_CL(R, bl);                                      /* CX was 0 (latch) */
        if (bl & 0x80) { R.bx = 1; flags16(R.bx); return; } /* add bl,bl; jb: unstompable */
        SET_BL(R, bl << 1);
        if (!(bl & 0x40)) {                                 /* bounce */
            R.bx = ESW(R.di + 0xd); dgw_set(0xfe, R.bx);
            R.bx = ESW(R.di + 0xf); dgw_set(0x100, R.bx);
        }
    }
    R.cx--;                                                 /* dec cx (word) */
    SBS(S_HITS, (uint8_t)R.cx);
    if (R.cx & 0x3f) { R.bx = 1; flags16(R.bx); return; }
    {
        uint16_t ax = R.ax;                                 /* push ax */
        A23_random();
        R.ax = cbw(ESB(R.di + 0x11));
        R.bx = R.ax;
        R.ax = ax;                                          /* pop ax */
    }
    A20_add_score();
    R.bx = 0x000b; SS(S_CODE, R.bx);                        /* handler -> ovl:000b = A0 */
    R.bx = 0; SS(S_PARAM, 0);
    R.bx = dgw(0xd6); SS(S_VYF, R.bx);
    R.bx = (uint16_t)(-(int)((dgw(0xd8) & 1) + 2)); SS(S_VY, R.bx);
    R.bx = dgw(0xda); SS(S_VXF, R.bx);
    /* 42b4: shr; and bx,1 clears CF; inc/inc keep CF=0 -> JB never taken, always NEG (leftward) */
    R.bx = (uint16_t)(((dgw(0xd6) >> 1) & 1) + 2);
    R.bx = (uint16_t)(-(int)R.bx);
    SS(S_VX, R.bx);
    SS(S_X, R.ax); SS(S_Y, R.dx);
    R.bx = 1;
    flags16(R.bx);
    return;
miss:                                                       /* 41b6 */
    R.bx = 0;
    SS(S_LATCH, 0);
    flags16(0);
}

/* ===================================================================== A0 dying fall */
void A0_dying_fall(void) {                                  /* 1000:4636 (handler ovl:000b) */
    R.bx = (uint16_t)(R.ax - dgw(0x2ccc));
    if (R.bx >= 0x1c0) { A10_despawn(); return; }           /* respawnable */
    R.bx = (uint16_t)(R.dx - dgw(0x2cce));
    if (R.bx >= 0x120) { A10_despawn(); return; }
    if (!(SW(S_PARAM) & 1)) {
        unsigned c = 0, up = (SW(S_VX) & 0xf) != 0;
        if (up) { R.bx = 0; R.cx = 0x400; }                 /* 4660: (vx:vxf) += 0x0400 */
        else    { R.bx = 0xffff; R.cx = 0xfc00; }           /* 4690: (vx:vxf) -= 0x0400 */
        R.cx = adc16(R.cx, SW(S_VXF), &c); SS(S_VXF, R.cx);
        R.bx = adc16(R.bx, SW(S_VX), &c);  SS(S_VX, R.bx);
        if (up ? !(R.bx & 0x8000) : (R.bx & 0x8000)) SS(S_PARAM, SW(S_PARAM) | 1);
        c = 0;
        R.cx = adc16(R.cx, SW(S_XF), &c);  SS(S_XF, R.cx);
        R.ax = adc16(R.ax, R.bx, &c);
    }
    if (SW(S_PARAM) & 2) {
        R.dx = (uint16_t)(R.dx + 3);
    } else {
        unsigned c = 0;
        R.bx = adc16(0x2000, SW(S_VYF), &c); SS(S_VYF, R.bx);
        R.cx = adc16(0, SW(S_VY), &c);
        if (sx(R.cx) >= 3) {                                /* terminal fall; vy int not stored */
            SS(S_PARAM, SW(S_PARAM) | 2);
            R.dx = (uint16_t)(R.dx + 3);
        } else {
            SS(S_VY, R.cx);
            c = 0;
            R.bx = adc16(R.bx, SW(S_YF), &c); SS(S_YF, R.bx);
            R.dx = adc16(R.dx, R.cx, &c);
        }
    }
    SS(S_X, R.ax); SS(S_Y, R.dx);
    A1_draw();                                              /* jmp 2648 */
}

/* ===================================================================== A20 score, A21 spawn, A23 */
/* ADD/ADC AL,b followed by DAA (x86 semantics, AF from the low-nibble carry). */
static uint8_t add_daa(uint8_t a, uint8_t b, unsigned *cf) {
    unsigned s = (unsigned)a + b + *cf;
    unsigned af = ((a & 0xf) + (b & 0xf) + *cf) > 0xf;
    unsigned c = s > 0xff;
    uint8_t al = (uint8_t)s, old = al;
    if ((al & 0xf) > 9 || af) al = (uint8_t)(al + 6);
    if (old > 0x99 || c) { al = (uint8_t)(al + 0x60); c = 1; } else c = 0;
    *cf = c;
    return al;
}

void A20_add_score(void) {                                  /* 1000:40d0  BX=type, AX=x, DX=y */
    unsigned c = 0;
    uint16_t t = R.bx;
    DB(0xca) = add_daa(DB((uint16_t)(0x2dd4 + t)), DB(0xca), &c);
    DB(0xcb) = add_daa(DB((uint16_t)(0x2dd5 + t)), DB(0xcb), &c);
    DB(0xcc) = add_daa(DB((uint16_t)(0x2dd6 + t)), DB(0xcc), &c);
    DB(0xcd) = add_daa(0, DB(0xcd), &c);                    /* carry out of [cd] lost */
    for (uint16_t bp = 0xb4; !(bp & 0x8000); bp = (uint16_t)(bp - 0xc)) {   /* BP pushed/popped */
        R.cx = pop_w(bp, Q_TYPE);
        if (R.cx == 0) {
            pop_set(bp, Q_TYPE, R.bx);
            R.bx = R.dx;
            pop_set(bp, Q_Y, R.bx);
            R.bx = 0;
            pop_set(bp, Q_YF, 0);
            pop_set(bp, Q_SP, 1);
            pop_set(bp, Q_SPF, 0x8000);                     /* 1.5 px/tick */
            pop_set(bp, Q_X, R.ax);
            return;
        }
    }
}

void A21_spawn(void) {                                      /* 1000:1aa8  ES:SI template, AX,DX base */
    uint16_t bp = R.bp, ax = R.ax, dx = R.dx;               /* push bp, ax, dx */
    R.ax = (uint16_t)(R.ax + ESW(R.si));
    R.bx = (uint16_t)(ESW(R.si + 2) + R.dx);
    R.bp = 0x83c;
    for (;;) {
        R.dx = slot_w(R.bp, S_CODE);
        if (R.dx == 0) { slot_init_big(); R.si = R.bp; break; }
        R.bp = (uint16_t)(R.bp - 0x44);
        if (R.bp & 0x8000) { R.si = 0xffff; break; }
    }
    R.dx = dx; R.ax = ax; R.bp = bp;
    flags16(R.si);                                          /* or si,si: SF = none free, ZF if slot 0 */
}

void A23_random(void) { R.ax = rng_step(); }                /* 1000:05da: only AX + RNG words change */

/* ===================================================================== A22 platform */
void A22_platform(void) {                                   /* 1000:4710  AX,DX kept */
    R.di = SW(S_SPRITE);
    R.bx = (uint16_t)(ESW(R.di + 5) + R.ax);
    if (SW(S_W3C) & 1) {                                    /* carrying */
        if (add16(&R.bx, -sx(dgw(0xe8))) > 0) goto drop;
        if (add16(&R.bx, sx(ESW(R.di + 9))) < 0) goto drop;
        R.si = (uint16_t)(ESW(R.di + 7) + R.dx);
        R.bx = R.si;
        add16(&R.bx, -sx(dgw(0xec)));
        R.bx--;
        if (R.bx == 0) goto ride;
    drop:                                                   /* 473f */
        R.bx = 0;
        SS(S_W3C, 0);
        dgw_set(0xd4, 0);
        R.bx = 0;
        flags16(0);
        return;
    }
    if (add16(&R.bx, -sx(dgw(0xe8))) > 0) goto none;
    if (add16(&R.bx, sx(ESW(R.di + 9))) < 0) goto none;
    R.bx = dgw(0x100);
    if (sx(R.bx) < 0) goto none;                            /* moving up */
    R.cx = dgw(0xec);
    add16(&R.cx, -sx(R.bx));
    R.bx = (uint16_t)(ESW(R.di + 7) + R.dx);
    if (sx(R.bx) < sx(R.cx)) goto none;                     /* was already below the top */
    R.si = R.bx;
    if (add16(&R.bx, -sx(dgw(0xec))) > 0) goto none;        /* not reached yet */
    SS(S_W3C, 1);
    dgw_set(0xd4, 1);
ride:                                                       /* 4787 */
    R.si--;
    R.bx |= 1;
    flags16(R.bx);
    return;
none:                                                       /* 474a */
    R.bx = 0;
    flags16(0);
}

/* ===================================================================== stock handlers A14-A19 */
static int in_keep(void) {                                  /* x-[2cbc] u< [2cc0] && y-[2cbe] u< [2cc2] */
    R.bx = (uint16_t)(R.ax - dgw(0x2cbc));
    if (R.bx >= dgw(0x2cc0)) return 0;
    R.bx = (uint16_t)(R.dx - dgw(0x2cbe));
    if (R.bx >= dgw(0x2cc2)) return 0;
    return 1;
}
static int in_vis(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if ((uint16_t)(x - dgw(0x2cc4)) >= w) return 0;
    if ((uint16_t)(y - dgw(0x2cc6)) >= h) return 0;
    return 1;
}

void A19_sparkle(void) {                                    /* 1000:4328 */
    if (!in_vis(R.ax, R.dx, 0x170, 0xd0)) return;
    R.si = (uint16_t)((dgw(0x2c9a) & 0x38) >> 2);
    R.ax = (uint16_t)(R.ax + dgw((uint16_t)(0x2e60 + R.si)));
    R.dx = (uint16_t)(R.dx + dgw((uint16_t)(0x2e70 + R.si)));
    R.si = (uint16_t)(R.si * 2 + 0x2e40);
    R.bx = R.ax; R.di = R.dx;
    draw_cs_world_ds();
}

void A14_checkpoint(void) {                                 /* 1000:4364 */
    if (!in_keep()) { A10_despawn(); return; }
    A6_touch();
    if (!R.zf && !(dgw(0xe2) == R.ax && dgw(0xe4) == R.dx)) {
        dgw_set(0xe2, R.ax); dgw_set(0xe4, R.dx);
        dgw_set(0x2ca4, 5);
    }
    R.si = 0;
    if (dgw(0xe2) == R.ax && dgw(0xe4) == R.dx) {
        uint16_t v = (uint16_t)(dgw(0x2d96) - 1);
        dgw_set(0x2d96, v);
        if (v & 0x8000) { dgw_set(0x2d96, 5); dgw_set(0x2d98, (uint16_t)(dgw(0x2d98) + 1)); }
        R.si = dgw(0x2d98);
    }
    R.si = (uint16_t)((R.si & 7) * 2);
    R.ax = (uint16_t)(R.ax + dgw((uint16_t)(0x2e20 + R.si)));
    R.dx = (uint16_t)(R.dx + dgw((uint16_t)(0x2e30 + R.si)));
    R.bx = (uint16_t)(R.ax - dgw(0x2cc4));
    if (R.bx >= 0x160) return;
    R.bx = (uint16_t)(R.dx - dgw(0x2cc6));
    if (R.bx >= 0xc0) return;
    R.si = (uint16_t)(R.si * 2 + 0x2e00);
    R.bx = R.ax; R.di = R.dx;
    draw_cs_world_ds();
}

void A15_energy(void) {                                     /* 1000:4466 */
    if (!in_keep()) { A10_despawn(); return; }
    A6_touch();
    if (!R.zf) { DB(0xb2) = 5; A11_kill(); return; }
    R.bx = R.ax; R.di = (uint16_t)(R.dx - 0x2d);
    R.cx = (uint16_t)(R.bx - dgw(0x2cc4));
    if (R.cx >= 0x170) return;
    R.cx = (uint16_t)(R.dx - dgw(0x2cc6));
    if (R.cx >= 0xd0) return;
    R.si = 0x2700;
    draw_cs_world_ds();
}

void A16_extra_life(void) {                                 /* 1000:43f1 */
    if (!in_keep()) { A10_despawn(); return; }
    A6_touch();
    if (!R.zf) { dgw_set(0xbe, (uint16_t)(dgw(0xbe) + 1)); A11_kill(); return; }
    {
        uint16_t v = (uint16_t)(dgw(0x2ea2) - 1);
        dgw_set(0x2ea2, v);
        if (v & 0x8000) {
            dgw_set(0x2ea2, 7);
            uint16_t k = (uint16_t)(dgw(0x2ea4) - 1);
            dgw_set(0x2ea4, k);
            if (k & 0x8000) dgw_set(0x2ea4, 2);
        }
    }
    if (!in_vis(R.ax, R.dx, 0x170, 0xd0)) return;
    R.si = (uint16_t)(dgw(0x2ea4) * 4);
    R.bx = (uint16_t)(R.ax + dgw((uint16_t)(0x2e86 + R.si)));
    R.di = (uint16_t)(R.dx + dgw((uint16_t)(0x2e88 + R.si)));
    R.si = (uint16_t)(R.si + 0x2704);
    draw_cs_world_ds();
}

static void draw4584(void) {                                /* 1000:4584  BX=x, DI=y, SI=frame*4; DX=y */
    R.bx = (uint16_t)(R.bx + dgw((uint16_t)(0x2e92 + R.si)));
    R.di = (uint16_t)(R.di + dgw((uint16_t)(0x2e94 + R.si)));
    R.cx = (uint16_t)(R.bx - dgw(0x2cc4));
    if (R.cx >= 0x170) return;
    R.cx = (uint16_t)(R.dx - dgw(0x2cc6));                  /* tests the unshifted y */
    if (R.cx >= 0xd0) return;
    R.si = (uint16_t)(R.si + 0x2710);
    draw_cs_world_ds();
}

void A17_big_bonus(void) {                                  /* 1000:44b6 */
    if (SW(S_W3C) & 1) goto running;
    if (!in_keep()) { A10_despawn(); return; }
    A6_touch();
    if (!R.zf) {
        R.bx = 0x24;                                        /* 25000 points */
        A20_add_score();
        SS(S_W3C, SW(S_W3C) | 1);
        SS(S_T3E, 0x000a);                                  /* word: timer 10, step 0 */
        dgw_set(0x2d84, 1);
        if (ESW(6)) dgw_set(0x2db2, 0xc0);                  /* white flash */
        return;
    }
    R.bx = R.ax; R.di = R.dx; R.si = 0;
    draw4584();
    return;
running:                                                    /* 45ad */
    if (!in_keep()) { A11_kill(); return; }
    {
        uint8_t t = (uint8_t)(SB(S_T3E) - 1);
        SBS(S_T3E, t);
        if (t & 0x80) { SBS(S_T3F, SB(S_T3F) + 1); SBS(S_T3E, 0x14); }
    }
    R.bx = SB(S_T3F);
    if ((uint8_t)R.bx == 0xd) { A11_kill(); return; }
    R.si = dgw((uint16_t)(0x33da + 2 * R.bx));
    R.cx = ESW(6);
    if (R.cx == 0) return;
    while (dgw(R.si) != 0) {                                /* {idx, dx, dy} lists, DS:[si] */
        R.bx = R.ax; R.di = R.dx;
        uint16_t si = R.si, ax = R.ax, cx = R.cx, dx = R.dx, ds = R.ds;
        R.cx = (uint16_t)(R.cx + dgw(R.si) - 1);
        R.ax = cbw(SB(S_PARAM + 1)); R.bx = (uint16_t)(R.bx + R.ax);
        R.ax = cbw(SB(S_PARAM));     R.di = (uint16_t)(R.di + R.ax);
        R.bx = (uint16_t)(R.bx + dgw((uint16_t)(si + 2)));
        R.di = (uint16_t)(R.di + dgw((uint16_t)(si + 4)));
        R.ds = dgw(0x2d64);                                 /* far ptr table in the overlay */
        R.si = R.cx;
        draw_cs_world_ds();
        R.ds = ds; R.dx = dx; R.cx = cx; R.ax = ax; R.si = si;
        R.si = (uint16_t)(R.si + 6);
    }
}

void A18_time_bonus(void) {                                 /* 1000:4515 */
    if (!in_keep()) { A10_despawn(); return; }
    A6_touch();
    if (!R.zf) {
        dgw_set(0x2d9a, (uint16_t)(dgw(0x2d9a) + 0xf));
        R.bx = 0x28;
        A20_add_score();
        A11_kill();
        return;
    }
    R.si = 0;
    {
        uint16_t v = (uint16_t)(dgw(0x2dc2) - 1);
        dgw_set(0x2dc2, v);
        if (v & 0x8000) {
            uint16_t k = (uint16_t)(dgw(0x2dc4) - 1);
            dgw_set(0x2dc4, k);
            if (k & 0x8000) {
                R.cx = (uint16_t)((dgw(0xd6) & 0x1f) + 0x20);
                dgw_set(0x2dc2, R.cx);
                dgw_set(0x2dc4, 0x40);
            }
            R.si = (uint16_t)((dgw(0x2dc4) & 0xc) >> 1);
        }
    }
    R.bx = R.ax; R.di = R.dx;
    R.si = CSW(0x457c + R.si);                              /* CS table {8,4,8,0xc} */
    draw4584();
}

/* ===================================================================== dispatch / registration */
static const struct { uint16_t addr; code_fn f; } api_tab[26] = {
    {0x4636, A0_dying_fall},   {0x2648, A1_draw},          {0x3be9, A2_anim_step},     {0x3d31, A3_motion_step},
    {0x2347, A4_map_move},     {0x4092, A5_overlap},       {0x41a0, A6_touch},         {0x3fd6, A7_next_anim},
    {0x3fe5, A8_set_anim},     {0x4023, A9_restart_motion},{0x3b8b, A10_despawn},      {0x3bd6, A11_kill},
    {0x3b7c, A12_despawn_unride}, {0x3bc7, A13_kill_unride}, {0x4364, A14_checkpoint}, {0x4466, A15_energy},
    {0x43f1, A16_extra_life},  {0x44b6, A17_big_bonus},    {0x4515, A18_time_bonus},   {0x4328, A19_sparkle},
    {0x40d0, A20_add_score},   {0x1aa8, A21_spawn},        {0x4710, A22_platform},     {0x05da, A23_random},
    {0x177d, draw_cs_world_es},{0x17e9, draw_cs_screen_glyph},
};

void api_call(int slot) {
    if (slot >= 0 && slot < 26) api_tab[slot].f();
    else log_gap("api_call(%d) out of range", slot);
}

void objects_register(void) {
    /* table CS:050f (= the targets above, 1000:xxxx); overlay stubs ovl:000b+5i for all levels */
    for (int i = 0; i < 26; i++) {
        farcall_register_exe(0x1000, api_tab[i].addr, api_tab[i].f);
        for (int lv = 0; lv < 12; lv++)
            farcall_register_overlay(lv, (uint16_t)(0x000b + 5 * i), api_tab[i].f);
    }
}
