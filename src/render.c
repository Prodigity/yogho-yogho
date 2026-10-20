/* Renderer, engine part (Ghidra segment 1000).  Spec: teardown/30-render.md, 20-timing-input.md
 * §3.1.2-3.1.3, 80-data.md §2.2-2.6.
 *
 * Everything is ported instruction by instruction against disasm_fixed.txt:
 *  - VGA access goes through the port model (io_out/io_outw, wb/rb on A000-AFFF) with the same
 *    register writes and plane order as the original, so VRAM contents are bit-identical;
 *  - the system-RAM background buffer ([1402], 4 reversed plane blocks of 0x600 paragraphs,
 *    96-byte rows, helical x addressing) lives in MEM where the original has it;
 *  - the self-modified CS variables (CS:100f..1025, CS:2814/2816) and the CS tables (2bb8/3158/
 *    3838 built by timer_install, the tile dispatch CS:1027, the plane block table CS:0c4a) are
 *    read/written as words in MEM at SEG_EXE:offset.
 * DGROUP-relative accesses use SEG_DG: every caller of these routines runs with DS = DGROUP
 * (C code of 160a, the ISR, engine_step).  The exceptions where DS is an argument (17b5 / 17e9)
 * use R.ds. */
#include "game.h"
#include "log.h"

#define CSW(o)        rw(SEG_EXE, (uint16_t)(o))
#define CSW_SET(o, v) ww(SEG_EXE, (uint16_t)(o), (uint16_t)(v))

/* --- CS-resident variables / tables (offsets in seg 1000) --- */
#define CS_NTILES    0x100f   /* tiles left - 1 for bg_tile_row (DEC / JS) */
#define CS_MAPSEG    0x1011   /* [2cda] */
#define CS_METASEG   0x1013   /* [2ce2] */
#define CS_TILESEG   0x1015   /* [2cde] */
#define CS_MAPSTRIDE 0x1017   /* [2da6] */
#define CS_FILLROW   0x1019   /* written by bg_fill, never read in the paths ported here */
#define CS_COLY      0x101b   /* bg_tile_col: first tile y */
#define CS_COLX      0x101d   /* bg_tile_col: map x offset (bytes) */
#define CS_CAMX      0x101f
#define CS_CAMY      0x1021
#define CS_CAMX16    0x1023
#define CS_BUFSEG    0x1025   /* [1402] */
#define CS_TILEFN    0x1027   /* {102f,1082,117f,11d6} indexed by W&6 */
#define CS_BLKTAB    0x0c4a   /* {1200,0c00,0600,0000} indexed by (x&3)*2 */
#define CS_ROWTAB    0x2c18   /* = 2bb8 + 48 words: y_rel -> 6*(y_rel+48) */
#define CS_SPRSEG    0x2814   /* [2da2] */
#define CS_SPRSEG12  0x2816   /* [2da4] */
#define CS_COLTAB    0x31b8   /* = 3158 + 48 words: x_rel -> 16 + x_rel/4 */
#define CS_MASKTAB   0x3898   /* = 3838 + 48 words: x_rel -> map-mask OUT word */

static uint8_t rol8(uint8_t v, unsigned n) { n &= 7; return (uint8_t)(n ? (v << n) | (v >> (8 - n)) : v); }

/* =========================================================================================
 * Setup
 * ========================================================================================= */

void video_init(void) {                                     /* 1000:02c8 (Ghidra mis-split) */
    uint16_t a = 0xa000;                                    /* 02cf: DG:2ea6[0x128] = A000+6r */
    for (unsigned i = 0; i < 0x128; i++) { dgw_set((uint16_t)(0x2ea6 + 2 * i), a); a = (uint16_t)(a + 6); }
    a = dgw(0x1402);                                        /* 02de: DG:30f6[0x100] = [1402]+6r */
    for (unsigned i = 0; i < 0x100; i++) { dgw_set((uint16_t)(0x30f6 + 2 * i), a); a = (uint16_t)(a + 6); }
    /* 02ed..031d: INT 10h AH=0F (mode -> [2cac]), AH=1C state save into a DOS block ([2cb0] size,
     * [2cae] segment).  The port has no BIOS state to save: text mode 3, no buffer (so 1000:03a2
     * skips the state restore).  0320: INT 10h AX=0013h = reset the VGA model to mode 13h. */
    DB(0x2cac) = 0x03;
    dgw_set(0x2cb0, 0);
    dgw_set(0x2cae, 0);
    vga_reset();
    SET_AL(R, 0xff);                                        /* 0325: success */
}

void vga_reglist(uint16_t off, uint16_t seg) {              /* 1000:0349 */
    uint16_t si = off;
    uint8_t h = rb(seg, si++);
    for (;;) {
        uint8_t port = h & 3, op = (uint8_t)((h & 0x0c) >> 2);
        uint8_t idx = rb(seg, si++);
        uint16_t dx = port == 0 ? 0x3c0 : port == 1 ? 0x3ce : port == 2 ? 0x3d4 : 0x3c4;
        io_out(dx, idx);
        if (port != 0) dx++;                                /* 039c: AC data goes to 3C0 as well */
        uint8_t v = rb(seg, si++);
        if (op == 0) io_out(dx, (uint8_t)(io_in(dx) & v));  /* 0379 */
        else if (op == 1) io_out(dx, (uint8_t)(io_in(dx) | v)); /* 0382 */
        else io_out(dx, v);                                 /* 0375 */
        h = rb(seg, si++);
        if (h & 0x80) break;                                /* 038a OR AL,AL / JNS */
    }
}

void vram_clear(void) {                                     /* 1000:0490 */
    io_outw(0x3c4, 0x0f02);
    for (uint16_t di = 0; di != 0xffff; di++) wb(0xa000, di, 0);   /* REP STOSB, CX=FFFF */
}

void patch_render_segs(void) {                              /* 1000:04aa */
    CSW_SET(CS_MAPSEG, dgw(0x2cda));
    CSW_SET(CS_TILESEG, dgw(0x2cde));
    CSW_SET(CS_METASEG, dgw(0x2ce2));
    CSW_SET(CS_MAPSTRIDE, dgw(0x2da6));
    CSW_SET(CS_BUFSEG, dgw(0x1402));
}

void dac_schedule(uint16_t ptr, uint16_t start, uint16_t count) {   /* 1000:0c52 */
    dgw_set(0x2d6c, start);
    dgw_set(0x2d6e, count);
    dgw_set(0x2d6a, ptr);                                   /* written last: 0935 tests it */
}

/* =========================================================================================
 * Blitters (C convention, far src)
 * ========================================================================================= */

/* 1000:0a98: linear row-major source (row stride = ((w-1)>>2)+1 dwords), opaque. */
void blit_linear_vga(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h) {
    uint16_t es = (uint16_t)(0xa000 + (uint16_t)(6u * (uint16_t)y));   /* 0aa5: (0x60>>4)*y */
    uint16_t src = soff, px = (uint16_t)x;
    int8_t bh = 3;
    for (;;) {
        uint16_t si = src;
        uint16_t di = (uint16_t)(px >> 2);
        io_outw(0x3c4, (uint16_t)(0x0002 | (uint16_t)((uint8_t)(1u << (px & 3)) << 8)));   /* 0abe */
        uint16_t n = (uint16_t)((((uint16_t)w - 1u) >> 2) + 1u);   /* 0ad2..0adc */
        uint16_t adv = (uint16_t)(0x60 - n);
        uint8_t bl = (uint8_t)h;
        do {                                                /* 0ae3: rows (DEC BL / JNZ) */
            for (uint16_t c = n; c; c--) { wb(es, di, rb(sseg, si)); di++; si = (uint16_t)(si + 4); }
            di = (uint16_t)(di + adv);
        } while (--bl);
        src++;                                              /* 0af2 INC [BP+6] */
        px++;                                               /* 0af5 INC [BP+a] */
        if ((int16_t)px < 0) break;                         /* 0af8 JS */
        if (--bh < 0) break;                                /* 0afa DEC BH / JNS */
    }
}

/* 1000:0b04 / 0b63 common: column-major planar source (80-data §2.2), VGA destination. */
static void blit_planar(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h,
                        uint16_t skip, int transparent) {
    uint16_t es = (uint16_t)(0xa000 + (uint16_t)(6u * (uint16_t)y));
    uint16_t di = (uint16_t)((uint16_t)x >> 2);
    uint16_t ax = (uint16_t)(0x0002 | (uint16_t)((uint8_t)(0x11u << ((uint16_t)x & 3)) << 8));   /* SHL AH,CL */
    uint16_t si = soff;
    uint16_t wv = (uint16_t)w;
    for (int p = 0; p < 4; p++) {                           /* CX = 4 / LOOP */
        io_outw(0x3c4, ax);
        uint16_t bx = (uint16_t)(wv - 1);
        uint16_t d = di;
        do {                                                /* columns: SUB BX,4 / JNS */
            uint16_t dd = d, cx = (uint16_t)h;
            do {                                            /* rows: LOOP (h = 0 -> 65536) */
                uint8_t v = rb(sseg, si); si++;
                if (!transparent || v) wb(es, dd, v);
                dd = (uint16_t)(dd + 0x60);
            } while (--cx);
            if (!transparent) si = (uint16_t)(si + skip);   /* 0b46 ADD SI,[BP+12] */
            d++;
            bx = (uint16_t)(bx - 4);
        } while ((int16_t)bx >= 0);
        uint8_t ah = (uint8_t)(ax >> 8), cf = ah >> 7;      /* ROL AH,1 / ADC DI,0 */
        ax = (uint16_t)((ax & 0xff) | (uint16_t)((uint8_t)((ah << 1) | cf) << 8));
        di = (uint16_t)(di + cf);
        wv--;                                               /* DEC [BP+e] */
    }
}

void blit_planar_vga(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h, int16_t skip) {
    blit_planar(soff, sseg, x, y, w, h, (uint16_t)skip, 0);     /* 1000:0b04 */
}

void blit_planar_vga_t(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h) {
    blit_planar(soff, sseg, x, y, w, h, 0, 1);                  /* 1000:0b63 */
}

/* 1000:0bc6: same source layout, colour 0 transparent, into the background buffer at screen
 * (x,y) + camera; rows wrap & 0xFF, plane block from CS:0c4a[(x+sx+i)&3]. */
void blit_planar_buf_t(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h) {
    uint16_t row2 = (uint16_t)(((uint16_t)y + dgw(0x2cb6)) * 2u);   /* [BP-2] */
    uint16_t xx = (uint16_t)((uint16_t)x + dgw(0x2cb4));
    uint16_t di = (uint16_t)(xx >> 2);
    uint16_t ph = (uint16_t)(xx * 2u);                              /* [BP-4] */
    uint16_t si = soff;
    uint16_t wv = (uint16_t)w;
    for (int p = 0; p < 4; p++) {
        uint16_t blk = CSW(CS_BLKTAB + (ph & 6));                   /* [BP-6] */
        uint16_t dx = (uint16_t)(wv - 1);
        uint16_t d = di;
        do {
            uint16_t cx = (uint16_t)h, bx = row2;
            do {
                bx &= 0x1fe;
                uint16_t es = (uint16_t)(dgw((uint16_t)(0x30f6 + bx)) + blk);
                uint8_t v = rb(sseg, si); si++;
                if (v) wb(es, d, v);
                bx = (uint16_t)(bx + 2);
            } while (--cx);
            d++;
            dx = (uint16_t)(dx - 4);
        } while ((int16_t)dx >= 0);
        ph = (uint16_t)(ph + 2);                                    /* 0c33 */
        if (!(ph & 6)) di++;
        wv--;
    }
}

/* =========================================================================================
 * Menu mouse cursor (menu mode, script 34e5: screen = VGA rows 0..199, shadow at +0x4B00)
 * ========================================================================================= */

void cursor_restore(void) {                                 /* 1000:0d0f */
    uint16_t ax = dgw(0x2c92), bx = dgw(0x2c94), di = dgw(0x2c96);
    if (!bx) return;
    uint16_t es = bx, ds = (uint16_t)(bx + 0x4b0);
    uint8_t ah = (uint8_t)(ax >> 8), bh = 0;                /* 0d2d: plane = first set bit of AH */
    if (!(ah & 1)) { bh++; if (!(ah & 2)) { bh++; if (!(ah & 4)) bh++; } }
    uint16_t si = di;
    for (int8_t bl = 3; bl >= 0; bl--) {
        io_outw(0x3c4, ax);
        io_outw(0x3ce, (uint16_t)(((bh & 3) << 8) | 0x04));     /* GC4 read map */
        for (int r = 0; r < 0x1b; r++) {
            for (int k = 0; k < 8; k++) { wb(es, di, rb(ds, si)); di++; si++; }
            si = (uint16_t)(si + 0x58); di = (uint16_t)(di + 0x58);
        }
        bh++;
        ah = (uint8_t)(ax >> 8);
        uint8_t cf = ah >> 7;
        ax = (uint16_t)((ax & 0xff) | (uint16_t)((uint8_t)((ah << 1) | cf) << 8));
        if (cf) { si++; di++; }
        si = (uint16_t)(si - 0xa20); di = (uint16_t)(di - 0xa20);
    }
}

void cursor_row16(uint16_t y) {                             /* 1000:0d84 */
    uint16_t es = (uint16_t)(0xa000 + (uint16_t)((uint8_t)y * 6u));   /* MUL BL: 8-bit */
    uint16_t ds = (uint16_t)(es + 0x4b0);
    uint16_t ax = 0x1102, di = 0, si = 0;
    for (int8_t bl = 3; bl >= 0; bl--) {
        io_outw(0x3c4, ax);
        io_outw(0x3ce, (uint16_t)(((uint8_t)(~bl) & 3) << 8 | 0x04));
        for (int r = 0; r < 0x10; r++) {
            for (int k = 0; k < 0x1b; k++) { wb(es, di, rb(ds, si)); di++; si++; }
            si = (uint16_t)(si + 0x45); di = (uint16_t)(di + 0x45);
        }
        ax = (uint16_t)((ax & 0xff) | (uint16_t)rol8((uint8_t)(ax >> 8), 1) << 8);
        si = (uint16_t)(si - 0x600); di = (uint16_t)(di - 0x600);
    }
}

void cursor_draw(void) {                                    /* 1000:0ddf */
    Regs save = R;                                          /* PUSHA / DS / ES */
    uint16_t cx = dgw(0x2c86), ay = dgw(0x2c88);
    uint16_t di = (uint16_t)(cx >> 2);
    uint16_t es = (uint16_t)(0xa000 + (uint16_t)(ay * 6u));
    uint16_t ax = (uint16_t)(0x0002 | (uint16_t)rol8(0x11, cx & 3) << 8);
    dgw_set(0x2c92, ax); dgw_set(0x2c94, es); dgw_set(0x2c96, di);
    R.ax = ax; R.bx = es; R.cx = (uint16_t)(cx & 3); R.dx = 0x3c4; R.es = es; R.di = di;
    farcall(SEG_G(0x2363), 0x00e0);                         /* 0e11 CALLF 2363:00e0 (21x27 cursor) */
    R = save;
}

/* =========================================================================================
 * Tile background (30-render §3)
 * ========================================================================================= */

/* 1000:102f / 1082 / 117f / 11d6: one 16x16 tile (4 blocks of 64 bytes, 4 bytes per row) into
 * the buffer at ES:DI, ES += 0x600 per block.  h: source blocks 3..0 and bytes reversed (XCHG);
 * v: destination rows 15..0.  Byte order of the writes does not matter (no overlap). */
static void tile_blit(uint16_t es, uint16_t di, uint16_t ds, uint16_t si, int hf, int vf) {
    for (int blk = 0; blk < 4; blk++) {
        uint16_t sb = (uint16_t)(si + (hf ? 3 - blk : blk) * 64);
        for (int r = 0; r < 16; r++) {
            uint16_t d = (uint16_t)(di + (vf ? 15 - r : r) * 0x60);
            uint16_t s = (uint16_t)(sb + r * 4);
            for (int c = 0; c < 4; c++)
                wb(es, (uint16_t)(d + c), rb(ds, (uint16_t)(s + (hf ? 3 - c : c))));
        }
        es = (uint16_t)(es + 0x600);
    }
}

/* CALL CS:[BX+1027]: dispatch through the table in the exe image. */
static void tile_dispatch(uint16_t sel, uint16_t es, uint16_t di, uint16_t ds, uint16_t si) {
    uint16_t fn = CSW(CS_TILEFN + sel);
    switch (fn) {
    case 0x102f: tile_blit(es, di, ds, si, 0, 0); break;
    case 0x1082: tile_blit(es, di, ds, si, 1, 0); break;
    case 0x117f: tile_blit(es, di, ds, si, 0, 1); break;
    case 0x11d6: tile_blit(es, di, ds, si, 1, 1); break;
    default: log_gap("tile dispatch CS:1027[%u] = %04X", sel, fn); break;
    }
}

/* 0e9f..0ed4 (also 0f43.., 0fd5..): map cell -> metatile word -> tile, drawn at ES:DI.
 * sub = CL selector (y bit4 * 8 + x bit4 * 4, i.e. quadrant << 2 in BH bits 2..3).
 * quirk (0f9c only): CL = C_hi >> 1 without the "& 6" (map bit 13 flips the tile bank). */
static void cell_tile(uint16_t si, uint8_t sub, int quirk, uint16_t es, uint16_t di) {
    uint16_t c = rw(CSW(CS_MAPSEG), si);                    /* MOV BX,[SI] (DS = map) */
    uint8_t al = (uint8_t)(c >> 8);
    uint16_t bx = (uint16_t)((c ^ ((uint16_t)sub << 8)) & 0x0ffe);   /* XOR BH,CL / AND 0FFE */
    uint8_t cl = (uint8_t)(al >> 1);
    if (!quirk) cl &= 6;                                    /* 0eb7 (absent at 0feb) */
    uint16_t w = (uint16_t)(cl ^ rw(CSW(CS_METASEG), bx));  /* CH = 0: XOR CX,[BX] */
    uint8_t bl = (uint8_t)w;
    uint16_t tsi = (uint16_t)(w & 0xff00);                  /* SI = tile number low * 256 */
    uint16_t ax = (uint16_t)(((uint16_t)bl << 8) & 0x1000);   /* W bit 4 = bank (+64 KB) */
    uint16_t ds = (uint16_t)(ax + CSW(CS_TILESEG));
    tile_dispatch((uint16_t)(bl & 6), es, di, ds, tsi);
}

static uint8_t quad_sel(uint16_t x, uint16_t y) {          /* 0e7d..0e8c */
    return (uint8_t)(((y >> 4) & 1) * 8 + ((x >> 4) & 1) * 4);
}

void bg_tile_row(void) {                                    /* 1000:0e7c  AX=x BX=y ES DI, CS:100f */
    uint16_t ax = R.ax, bx = R.bx, es = R.es, di = R.di;
    uint8_t cl = quad_sel(ax, bx);
    uint16_t si = (uint16_t)(dgw((uint16_t)(0x32f6 + ((bx >> 5) << 1))) + ((ax >> 5) << 1));
    for (;;) {
        cell_tile(si, cl, 0, es, di);
        di = (uint16_t)(di + 4);
        cl ^= 4;
        uint16_t n = (uint16_t)(CSW(CS_NTILES) - 1);        /* DEC CS:[100f] / JS */
        CSW_SET(CS_NTILES, n);
        if ((int16_t)n < 0) break;
        if (!(cl & 4)) si = (uint16_t)(si + 2);
    }
    R.di = di; R.si = si; R.cx = cl;
}

void bg_tile_col(void) {                                    /* 1000:0ef6  AX=x BX=sy&~15 DI */
    uint16_t y0 = (uint16_t)(R.bx - 0x30);
    CSW_SET(CS_COLY, y0);
    uint8_t cl = quad_sel(R.ax, y0);
    CSW_SET(CS_COLX, (uint16_t)((R.ax >> 5) << 1));
    uint16_t si = 0;
    for (uint16_t k = 0;;) {                                /* DS = DGROUP (0f1d) */
        uint16_t y = (uint16_t)(k + CSW(CS_COLY));
        uint16_t es = dgw((uint16_t)(0x30f6 + ((y & 0xf0) << 1)));
        si = (uint16_t)(dgw((uint16_t)(0x32f6 + ((y >> 5) << 1))) + CSW(CS_COLX));
        cell_tile(si, cl, 0, es, R.di);
        k = (uint16_t)(k + 0x10);
        if (k == 0x100) break;
        cl ^= 8;
        if (!(cl & 8)) si = (uint16_t)(si + CSW(CS_MAPSTRIDE));   /* 0f93: dead (SI recomputed) */
    }
    R.si = si; R.cx = cl;
}

void bg_fill(void) {                                        /* 1000:0e1a */
    uint16_t sdi = R.di, ssi = R.si, ses = R.es;
    CSW_SET(CS_FILLROW, (uint16_t)(((dgw(0x2cb6) - 0x30) & 0xf0) * 4));
    for (uint16_t k = 0; k != 0x100; k = (uint16_t)(k + 0x10)) {
        uint16_t y = (uint16_t)(k + dgw(0x2cb6) - 0x30);
        R.es = dgw((uint16_t)(0x30f6 + ((y & 0xf0) << 1)));
        R.bx = y;
        uint16_t dx = 0x17;
        uint16_t ax = (uint16_t)(dgw(0x2cb4) & 0xfff0);
        if (ax < 0x20) { ax = (uint16_t)(0x20 - ax); dx = (uint16_t)(dx - (ax >> 4)); ax = 0; }   /* JNC / NEG */
        else ax = (uint16_t)(ax - 0x20);
        CSW_SET(CS_NTILES, dx);
        R.ax = ax; R.di = (uint16_t)(ax >> 2);
        bg_tile_row();
    }
    R.di = sdi; R.si = ssi; R.es = ses;
}

void bg_tile_one(void) {                                    /* 1000:0f9c  AX=x BX=y */
    uint16_t ax = R.ax, bx = R.bx;
    uint16_t di = (uint16_t)(ax >> 2);
    uint16_t es = dgw((uint16_t)(0x30f6 + ((bx & 0xf0) << 1)));
    uint8_t cl = quad_sel(ax, bx);
    uint16_t si = (uint16_t)(dgw((uint16_t)(0x32f6 + ((bx >> 5) << 1))) + ((ax >> 5) << 1));
    cell_tile(si, cl, 1, es, di);                           /* quirk §3.4 */
}

/* 1000:12e3: buffer -> draw page (AX = page segment), 160 rows x 84 bytes per plane, block k
 * (= VGA plane 3-k) with map mask 8,4,2,1; rows from sy & 0xFF, wrapping to buffer row 0. */
void bg_to_page(void) {
    static const uint8_t mask[4] = {8, 4, 2, 1};
    uint16_t es = R.ax;
    uint8_t r0 = (uint8_t)dgw(0x2cb6);
    uint16_t ds = (uint16_t)(r0 * 6u + CSW(CS_BUFSEG));
    uint16_t si0 = (uint16_t)((dgw(0x2cb4) >> 4) << 2);
    uint16_t n1 = (uint16_t)((uint8_t)~r0 + 1u);            /* NOT CL / INC CX: 256 - r0 */
    if ((int16_t)n1 >= 0xa0) n1 = 0xa0;
    uint16_t di = 0;
    for (int b = 0; b < 4; b++) {
        io_outw(0x3c4, (uint16_t)(0x0002 | mask[b] << 8));
        uint16_t si = si0;
        di = 0;
        for (uint16_t r = 0; r < n1; r++) {
            for (int k = 0; k < 84; k++) wb(es, (uint16_t)(di + k), rb(ds, (uint16_t)(si + k)));
            si = (uint16_t)(si + 96); di = (uint16_t)(di + 96);
        }
        ds = (uint16_t)(ds + 0x600);
    }
    uint16_t n2 = (uint16_t)(0xa0 - n1);
    if (n2) {                                               /* 1396: rest from buffer row 0 */
        uint16_t d0 = di;                                   /* = n1 * 96 (DI not reset) */
        ds = CSW(CS_BUFSEG);
        for (int b = 0; b < 4; b++) {
            io_outw(0x3c4, (uint16_t)(0x0002 | mask[b] << 8));
            uint16_t si = si0, d = d0;
            for (uint16_t r = 0; r < n2; r++) {
                for (int k = 0; k < 84; k++) wb(es, (uint16_t)(d + k), rb(ds, (uint16_t)(si + k)));
                si = (uint16_t)(si + 96); d = (uint16_t)(d + 96);
            }
            ds = (uint16_t)(ds + 0x600);
        }
    }
}

/* =========================================================================================
 * ISR parts (1000:07a2..0816 + 08d8, 0819..0862)
 * ========================================================================================= */

void edge_strip_update(void) {
    R.ds = SEG_DG;
    uint16_t bx = (uint16_t)(dgw(0x2cb6) & 0xfff0);          /* 07a2 */
    int16_t oldy = dgs(0x2cba);
    dgw_set(0x2cba, bx);
    if (bx != (uint16_t)oldy) {                             /* 07b1 JZ */
        if ((int16_t)bx > oldy) bx = (uint16_t)(bx + 0xe0); /* 07b3 JLE (signed) */
        bx = (uint16_t)(bx - 0x20);
        uint16_t dx = 0x17;
        R.es = dgw((uint16_t)(0x30f6 + ((bx & 0xf0) << 1)));
        R.bx = bx;
        uint16_t ax = (uint16_t)(dgw(0x2cb4) & 0xfff0);
        if (ax < 0x20) { ax = (uint16_t)(0x20 - ax); dx = (uint16_t)(dx - (ax >> 4)); ax = 0; }
        else ax = (uint16_t)(ax - 0x20);
        CSW_SET(CS_NTILES, dx);
        R.ax = ax; R.di = (uint16_t)(ax >> 2);
        bg_tile_row();                                      /* 07ec; then JMP 0819 (no X check) */
        return;
    }
    uint16_t ax = (uint16_t)(dgw(0x2cb4) & 0xfff0);          /* 07f3 */
    int16_t oldx = dgs(0x2cb8);
    dgw_set(0x2cb8, ax);
    if (ax == (uint16_t)oldx) return;
    R.bx = bx;                                              /* BX = sy & ~15 for 0ef6 */
    if ((int16_t)ax > oldx) {                               /* 0802 JLE -> 0807, else 08d8 */
        ax = (uint16_t)(ax + 0x150);
        R.ax = ax; R.di = (uint16_t)(ax >> 2);
        bg_tile_col();
    } else {
        if (ax < 0x20) return;                              /* 0807 SUB / JC */
        ax = (uint16_t)(ax - 0x20);
        R.ax = ax; R.di = (uint16_t)(ax >> 2);
        bg_tile_col();
    }
}

void set_draw_page_vars(void) {
    uint16_t ax = (dgw(0x2d9c) & 0x80) ? 0xa5d0 : 0xa0f0;   /* 0819 */
    uint16_t bx = dgw(0x2cb4);
    CSW_SET(CS_CAMX, bx);
    CSW_SET(CS_CAMX16, bx & 0xfff0);
    CSW_SET(CS_CAMY, dgw(0x2cb6));
    ax--;                                                   /* 0841 */
    bx = (uint16_t)(ax & 0x0fff);
    dgw_set(0x2da2, ax); dgw_set(0x2da4, bx);
    CSW_SET(CS_SPRSEG, ax); CSW_SET(CS_SPRSEG12, bx);
    ax = (uint16_t)(ax + 0x121); bx = (uint16_t)(bx + 0x121);
    dgw_set(0x2d9e, ax); dgw_set(0x2da0, bx);
    R.ax = ax; R.bx = bx;                                   /* AX = page start for 1411/12e3 */
}

/* =========================================================================================
 * Compiled-sprite callers (30-render §6.2)
 * ========================================================================================= */

void draw_cs_abs(uint16_t off, uint16_t seg, int16_t x, int16_t y) {   /* 1000:174a */
    uint16_t sdi = R.di, ssi = R.si, sds = R.ds, ses = R.es, sbp = R.bp;
    uint16_t cx = (uint16_t)x;
    uint16_t es = (uint16_t)(0xa000 + (uint16_t)((uint16_t)y * 6u));   /* MUL BX (DX clobbered) */
    R.di = (uint16_t)(cx >> 2);
    R.cx = (uint16_t)(cx & 3);
    R.ax = (uint16_t)(0x0002 | (uint16_t)rol8(0x11, cx & 3) << 8);
    R.bx = es; R.es = es; R.dx = 0x3c4;
    farcall(seg, off);                                      /* CALLF [BP+6] */
    R.di = sdi; R.si = ssi; R.ds = sds; R.es = ses; R.bp = sbp;
}

/* 177d / 17b5 body.  ptrseg = segment of the far pointer at SI (ES for 177d, DS for 17b5).
 * Whole-sprite cull by anchor through the CS tables (0xFFFF = out of range, OR / JS). */
static void draw_cs_world(uint16_t ptrseg) {
    uint16_t di = (uint16_t)((uint16_t)(R.di - CSW(CS_CAMY)) << 1);
    uint16_t bx = (uint16_t)((uint16_t)(R.bx - CSW(CS_CAMX16)) << 1);
    uint16_t cx = CSW((uint16_t)(CS_ROWTAB + di));
    R.di = CSW((uint16_t)(CS_COLTAB + bx));
    R.bx = bx; R.cx = cx;
    R.ax = (uint16_t)(R.di | cx);
    if ((int16_t)R.ax < 0) return;                          /* 1799 JS */
    uint16_t ses = R.es, sds = R.ds;
    cx = (uint16_t)(cx + CSW(CS_SPRSEG));                   /* row segment of the anchor */
    R.ax = CSW((uint16_t)(CS_MASKTAB + bx));
    uint16_t fo = rw(ptrseg, R.si), fs = rw(ptrseg, (uint16_t)(R.si + 2));
    R.ds = ptrseg; R.es = cx; R.cx = cx; R.dx = 0x3c4;
    farcall(fs, fo);                                        /* CALLF [SI] */
    R.ds = sds; R.es = ses;
}

void draw_cs_world_es(void) { draw_cs_world(R.es); }        /* 1000:177d (API 24) */
void draw_cs_world_ds(void) { draw_cs_world(R.ds); }        /* 1000:17b5 */

void draw_cs_screen_glyph(void) {                           /* 1000:17e9 (API 25) */
    if (R.si == 0) return;
    R.bx = (uint16_t)(R.bx + rw(R.ds, 0x2cb4));             /* screen -> world */
    R.di = (uint16_t)(R.di + rw(R.ds, 0x2cb6));
    R.si = (uint16_t)(0x2720 + ((R.si & 0x0f) << 2));       /* glyph far ptr DG:2720[SI&15] */
    draw_cs_world_ds();
}
