/* Tile-map collision: player point move (1000:1dd1), player probes (1b03 / 1c16 / 1cbe) and the
 * per-object probes used by A4_map_move (20f7 / 21a3 / 2274 / 232a).
 * Spec: teardown/40-player-physics.md §3.2, §3.3, §4.4–4.6; 50-objects-api.md §3k; 80-data §2.5–2.7.
 * Addresses are Ghidra (seg 1000).  All routines are REG: arguments and results in R, exactly the
 * registers the asm leaves (AX/BX/CX/DX, ZF for the bit probes) — callers test them like the asm.
 *
 * Data, all read from MEM: map seg [2cda] (u16 per 32x32 cell), metatile seg [2ce2] (4 x 512 u16),
 * DGROUP 0x32f6 row offsets, 0x4778 tile -> shape class*8, 0x3534 shape pointers, 0x3514 column
 * bit masks, shape planes at ptr+0 (row masks), +0x340 (A row extent), +0x680 (B col extent),
 * +0x9c0 (C step x), +0xd00 (D step y).  Player probe cache: 0x2da8 shape, 0x2dac/0x2dae key,
 * 0x2db0 attribute byte; solid-attribute mask 0x137f.  Object cache: slot +0x0e/+0x10/+0x12
 * (DGROUP [BP+0x13f/0x141/0x143]).  None of the caches is ever invalidated (P5). */
#include "game.h"

#define W(o)     dgw((uint16_t)(o))
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))

/* Shared tile -> shape lookup of the uncached paths (1000:1b25..1b89, 1c38..1c96, 211b..2178,
 * 21c7..221e, 2298..22ef).  tx/ty = x>>4 / y>>4 as computed by the caller.  Returns the shape
 * pointer; *dh = the cell's high byte (attribute), *cxo = CX as the asm leaves it.
 * and_cl: the player probes mask the cell flip bits (`mov cl,dh; shr cl,1; and cl,6`) before
 * XORing them into the metatile word; the object probes (20f7/21a3/2274) omit the `and cl,6`, so
 * attribute bits 5..7 of the cell leak into bits 4..6 of the word — bit 4 is the tile bank bit,
 * i.e. on a climbable (attr 0x20) cell an object probe reads the tile from the other bank. */
static uint16_t shape_lookup(uint16_t x, uint16_t y, uint16_t tx, uint16_t ty, int and_cl,
                             uint8_t *dh, uint16_t *cxo) {
    uint8_t quad = (uint8_t)(((y & 0x10) ? 8 : 0) + ((x & 0x10) ? 4 : 0));     /* 1b2b..1b3a */
    uint16_t di = (uint16_t)((tx & 0xfffe) + W((ty & 0xfffe) + 0x32f6));         /* 1b3c */
    uint16_t cell = rw(W(0x2cda), di);                                           /* 1b54 */
    uint8_t attr = (uint8_t)(cell >> 8);                                         /* 1b57 mov dh,bh */
    uint16_t bx = (uint16_t)((cell ^ ((uint16_t)quad << 8)) & 0x0ffe);           /* 1b59..1b5b */
    uint8_t cl = (uint8_t)(attr >> 1);                                           /* 1b63..1b65 */
    if (and_cl) cl &= 6;                                                         /* 1b67 */
    uint16_t cx = (uint16_t)(cl ^ rw(W(0x2ce2), bx));                            /* 1b6a (CH = 0) */
    bx = (uint16_t)(((uint16_t)(((uint8_t)cx) >> 4) << 8 | (cx >> 8)) & 0x1ff); /* 1b6d xchg; shr bh,4; and 1ff */
    bx = W((uint16_t)(bx * 2 + 0x4778));                                         /* 1b7a */
    cx = (uint16_t)((cx & 0xff00) | (cx & 6));                                   /* 1b7e and cl,6 */
    bx = (uint8_t)((uint8_t)bx + (uint8_t)cx);                                   /* 1b81 add bl,cl ; xor bh,bh */
    *dh = attr;
    *cxo = cx;
    return W(0x3534 + bx);                                                       /* 1b85 */
}

/* ---- player probes (cache 0x2da8/0x2dac/0x2dae/0x2db0, mask 0x137f) ---- */

void probe_point(void) {                                    /* 1000:1b03  AX=x DX=y -> ZF clear = solid */
    uint16_t x = R.ax, y = R.dx;
    uint16_t ty = (uint16_t)((int16_t)y >> 4);              /* sar bx,4 */
    if ((int16_t)ty < 0) {                                  /* 1b08 js 1afb: above the map = empty */
        R.ax = 0; DB(0x2db0) = 0; R.bx = 0; R.zf = 1; R.sf = 0;
        return;
    }
    uint16_t tx = (uint16_t)(x >> 4);                       /* shr di,4 */
    uint16_t c2 = (uint16_t)((x & 15) * 2), r2 = (uint16_t)((y & 15) * 2);
    if (tx == W(0x2dac) && ty == W(0x2dae)) {               /* cached: 1ad7 */
        uint8_t bl = (uint8_t)(DB(0x2db0) & DB(0x137f));
        if (bl) { R.bx = (uint16_t)((ty & 0xff00) | bl); R.zf = 0; R.sf = 0; return; }   /* 1ae0 jne retf */
        R.ax = W(0x3514 + c2);
        R.dx = (uint16_t)(r2 + W(0x2da8));                  /* 16-bit add here (1af2) */
        R.bx = R.dx;
        R.ax &= W(R.bx);
        flags16(R.ax);
        return;
    }
    SW(0x2dac, tx); SW(0x2dae, ty);                         /* 1b1d */
    uint8_t dh; uint16_t cx;
    uint16_t sh = shape_lookup(x, y, tx, ty, 1, &dh, &cx);
    R.ax = W(0x3514 + c2);                                  /* 1b4c column bit */
    R.cx = cx;
    SW(0x2da8, sh);                                         /* 1b89 */
    DB(0x2db0) = dh;                                        /* 1b8d */
    uint8_t m = (uint8_t)(dh & DB(0x137f));                 /* 1b91 */
    R.dx = (uint16_t)((uint16_t)m << 8 | r2);
    if (m) { R.bx = 0xffff; R.zf = 0; R.sf = 1; return; }   /* 1b9e: xor bx,bx ; dec bx */
    R.bx = (uint16_t)((sh & 0xff00) | (uint8_t)((uint8_t)sh + (uint8_t)r2));   /* 1b97 QUIRK: add bl,dl (8-bit) */
    R.ax &= W(R.bx);
    flags16(R.ax);
}

void probe_extent(void) {                                   /* 1000:1c16  AX=x DX=y -> AX=2col DX=2row CX=B BX=A */
    uint16_t x = R.ax, y = R.dx;
    uint16_t ty = (uint16_t)((int16_t)y >> 4);
    uint16_t c2 = (uint16_t)((x & 15) * 2), r2 = (uint16_t)((y & 15) * 2);
    if ((int16_t)ty < 0) {                                  /* 1c1b js 1c03 (cache key not touched) */
        R.cx = 0; R.bx = 0; DB(0x2db0) = 0; R.ax = c2; R.dx = r2;
        return;
    }
    uint16_t tx = (uint16_t)(x >> 4);
    uint16_t sh;
    R.ax = c2; R.dx = r2;
    if (tx == W(0x2dac) && ty == W(0x2dae)) {               /* cached: 1bd4 */
        if (DB(0x2db0) & DB(0x137f)) { R.bx = R.cx = 0xfff0; return; }   /* 1bfc: -16 */
        sh = W(0x2da8);
    } else {
        SW(0x2dac, tx); SW(0x2dae, ty);                     /* 1c30 */
        uint8_t dh; uint16_t cx;
        sh = shape_lookup(x, y, tx, ty, 1, &dh, &cx);
        SW(0x2da8, sh);                                     /* 1c96 */
        DB(0x2db0) = dh;                                    /* 1c9b */
        if (dh & DB(0x137f)) { R.bx = R.cx = 0xfff0; return; }   /* 1cb5 (dh cleared -> DX = 2row) */
    }
    R.cx = W((uint16_t)(sh + c2 + 0x680));                  /* B: column extent */
    R.bx = W((uint16_t)(sh + r2 + 0x340));                  /* A: row extent */
}

void probe_step(void) {                                     /* 1000:1cbe  AX=x DX=y BX=shape -> CX=C BX=D */
    if ((int16_t)R.dx < 0) {                                /* 1cc0 js 1d1d (AX/DX left as given) */
        R.bx = 0; DB(0x2db0) = 0; R.cx = 0;
        return;
    }
    uint16_t sh = R.bx;
    R.ax = (uint16_t)((R.ax & 15) * 2);
    R.dx = (uint16_t)((R.dx & 15) * 2);
    if (DB(0x2db0) & DB(0x137f)) { R.bx = R.cx = 0xfff0; return; }   /* 1ce7: uses the CURRENT attribute */
    R.cx = W((uint16_t)(sh + R.ax + 0x9c0));
    R.bx = W((uint16_t)(sh + R.dx + 0xd00));
}

/* ---- player point move: 1000:1dd1 ----
 * In: globals x (0xe6/0xe8), y (0xea/0xec), vx (0xf2/0xf4), vy (0xfe/0x100).  Writes the fractions
 * 0xe6/0xea, temporaries 0x2db6/0x2db8, and 0xec during slope steps.  Out: AX = new x, DX = new y,
 * BX = flags (1 airborne, 2 blocked/step end, 4 clipped, 8 ceiling, 0x1234 stuck).
 * Branch arithmetic below is on small values (extents -16..16, columns 0..15, |d| < W), so the
 * JL/JG/JLE/JGE tests on the true results equal the 16-bit flag tests. */
void player_move_collide(void) {
    uint16_t ax, dx, ox, oy;
    int si = 0, di = 0, a, f;
    uint16_t bp = 0, sh;
    uint32_t t;

    ox = W(0xe8);                                           /* 1dd1 */
    t = (uint32_t)W(0xe6) + W(0xf2);
    uint16_t nx = (uint16_t)(ox + W(0xf4) + (t >> 16));     /* add/adc */
    if ((int16_t)nx < 0) nx = 0;                            /* 1de3 js (sign of the result) */
    else if ((int16_t)nx >= (int16_t)W(0x514d)) nx = (uint16_t)(W(0x514d) - 1);
    SW(0xe6, (uint16_t)t);
    oy = W(0xec);
    t = (uint32_t)W(0xea) + W(0xfe);
    SW(0xea, (uint16_t)t);
    uint16_t ny = (uint16_t)(oy + W(0x100) + (t >> 16));
    if ((int16_t)ny >= (int16_t)W(0x514b)) {                /* 1e15: below the map */
        R.ax = nx; R.dx = ny;
        SW(0x2da8, 0); DB(0x2db0) = 0;
        R.bx = 1;
        return;
    }
    SW(0x2db6, nx); SW(0x2db8, ny);                         /* 1e28 */
    int d = (int)(int16_t)nx - (int)(int16_t)ox;
    ax = ox; dx = oy;
    if (d == 0) goto vert_fx;                               /* 1e3b -> 1fe2 with AX=ox DX=oy */
    si = di = d;
    if (d > 0) {
        /* ---- moving right, 1e3e ---- */
        R.ax = ox; R.dx = oy; probe_extent();
        int col = (int16_t)R.ax >> 1;                       /* sar ax,1 */
        a = (int16_t)R.bx;
        if (a != 0) {
            if (a > 0) {                                    /* 1e63 */
                if (a - col > 0) goto stuck;
            } else {                                        /* 1e4e */
                f = a - col + 15;
                if (f < 0) goto stuck;
                f -= si;
                if (f >= 0) goto hdone_r;
                bp |= 4; si += f;
                if (si >= 0) goto hdone_r;
                goto stuck;
            }
        }
        if (col + si - 15 <= 0) goto hdone_r;               /* 1e67 */
        R.ax = W(0x2db6); R.dx = W(0xec); probe_extent();   /* 1e6e: target column */
        int c2 = (int16_t)R.ax >> 1;
        a = (int16_t)R.bx;
        if (a == 0) goto hdone_r;
        if (a > 0) {                                        /* 1e81: stop at the cell edge */
            bp |= 4; si -= c2 + 1;
            if (si >= 0) goto hdone_r;
            goto stuck;
        }
        f = a - c2 + 15;                                    /* 1e8c */
        if (f > 0) goto hdone_r;
        bp |= 4; si += f;
        if (si < 0) goto stuck;
    hdone_r:                                                /* 1e9a */
        di -= si;
        if (di == 0) goto vert;
        sh = W(0x2da8);                                     /* 1ea7 push [2da8] */
        R.ax = (uint16_t)(W(0xe8) + si); R.dx = (uint16_t)(W(0xec) + 1); probe_point();
        if (R.zf) goto vert;                                /* nothing underfoot: just stop */
        for (;;) {                                          /* 1eb7: slope step loop */
            R.ax = (uint16_t)(W(0xe8) + si + 1); R.dx = W(0xec); R.bx = sh; probe_step();
            if ((int16_t)(R.cx | R.bx) < 0) { bp |= 2; break; }   /* 1ec9 js */
            SW(0xec, W(0xec) - (uint16_t)(int16_t)(int8_t)R.bx);  /* 1ece: y.hi -= i8(D) */
            int c = (int8_t)R.cx;
            si += c; di -= c;
            if (di <= 0) { si += di; break; }               /* 1ed9 -> 1efd */
            if ((uint8_t)R.bx == 2) {                       /* 1edb */
                di--;
                if (di == 0) { bp |= 2; break; }
            }
            R.ax = (uint16_t)(W(0xe8) + si + 1); R.dx = W(0xec); probe_point();   /* 1ee3 */
            sh = W(0x2da8);
            if (R.zf) { bp |= 2; break; }                   /* path ahead free: stop here */
        }
    } else {
        /* ---- moving left, 1f02 ---- */
        R.ax = ox; R.dx = oy; probe_extent();
        int col = (int16_t)R.ax >> 1;
        a = (int16_t)R.bx;
        if (a != 0) {
            if (a < 0) {                                    /* 1f24 */
                if (a - col + 15 < 0) goto stuck;
            } else {                                        /* 1f12 */
                f = a - col;
                if (f > 0) goto stuck;
                f -= si;
                if (f < 0) goto hdone_l;
                bp |= 4; si += f;
                if (si <= 0) goto hdone_l;
                goto stuck;
            }
        }
        if (col + si >= 0) goto hdone_l;                    /* 1f2b */
        R.ax = W(0x2db6); R.dx = W(0xec); probe_extent();   /* 1f2f */
        int c2 = (int16_t)R.ax >> 1;
        a = (int16_t)R.bx;
        if (a == 0) goto hdone_l;
        if (a < 0) {                                        /* 1f50: stop at the cell edge */
            bp |= 4; si -= c2 - 16;
            if (si > 0) goto stuck;
            goto hdone_l;
        }
        f = a - c2;                                         /* 1f42 */
        if (f < 0) goto hdone_l;
        bp |= 4; si += f;
        if (si > 0) goto stuck;
    hdone_l:                                                /* 1f5a */
        di -= si;
        if (di == 0) goto vert;
        sh = W(0x2da8);
        R.ax = (uint16_t)(W(0xe8) + si); R.dx = (uint16_t)(W(0xec) + 1); probe_point();
        if (R.zf) goto vert;
        for (;;) {                                          /* 1f77 */
            R.ax = (uint16_t)(W(0xe8) + si - 1); R.dx = W(0xec); R.bx = sh; probe_step();
            if ((int16_t)(R.cx | R.bx) < 0) { bp |= 2; break; }
            SW(0xec, W(0xec) - (uint16_t)(int16_t)(int8_t)R.bx);
            int c = (int8_t)R.cx;
            si += c; di -= c;
            if (di >= 0) { si += di; break; }               /* 1f99 -> 1fbd */
            if ((uint8_t)R.bx == 2) {
                di++;
                if (di == 0) { bp |= 2; break; }
            }
            R.ax = (uint16_t)(W(0xe8) + si - 1); R.dx = W(0xec); probe_point();   /* 1fa3 */
            sh = W(0x2da8);
            if (R.zf) { bp |= 2; break; }
        }
    }
vert:                                                       /* 1fd9 */
    ax = (uint16_t)(W(0xe8) + si);
    dx = W(0xec);
vert_fx:                                                    /* 1fe2 */
    SW(0x2db6, ax);
    {
        int dv = (int)(int16_t)W(0x2db8) - (int)(int16_t)dx;
        if (dv == 0) {                                      /* 1fc2: ground probe */
            R.ax = ax; R.dx = (uint16_t)(dx + 1); probe_point();
            if (R.zf) bp++;
            R.ax = W(0x2db6); R.dx = W(0x2db8); R.bx = bp;
            return;
        }
        if (dv > 0) {
            /* ---- down, 1ff2 ---- */
            if (W(0xd4) != 0) { bp |= 4; si = 0; goto land; }   /* riding a platform */
            si = dv;
            R.ax = ax; R.dx = dx; probe_extent();
            int row = (int16_t)R.dx >> 1;                   /* sar dx,1 */
            int b = (int16_t)R.cx;
            if (b != 0) {
                if (b > 0) {                                /* 2026 */
                    if (b - row > 0) goto stuck;
                } else {                                    /* 2011 */
                    f = b - row + 15;
                    if (f < 0) goto stuck;
                    f -= si;
                    if (f >= 0) goto air;
                    bp |= 4; si += f;
                    if (si >= 0) goto land;
                    goto stuck;
                }
            }
            if (row + si - 15 <= 0) goto air;               /* 202a */
            R.ax = W(0x2db6); R.dx = W(0x2db8); probe_extent();   /* 2031 */
            int r2 = (int16_t)R.dx >> 1;
            b = (int16_t)R.cx;
            if (b == 0) goto air;
            if (b > 0) {                                    /* 2055 */
                bp |= 4; si -= r2 + 1;
                if (si < 0) goto stuck;
                goto land;
            }
            f = r2 - b - 15;                                /* 2044 */
            if (f < 0) goto air;
            bp |= 4; si -= f;
            if (si >= 0) goto land;
            goto stuck;
        } else {
            /* ---- up, 207c ---- */
            si = dv;
            R.ax = ax; R.dx = dx; probe_extent();
            int row = (int16_t)R.dx >> 1;
            int b = (int16_t)R.cx;
            if (b != 0) {
                if (b > 0) {                                /* 208c */
                    f = b - row;
                    if (f > 0) goto stuck;
                    f -= si;
                    if (f < 0) goto air;
                    bp |= 8; si += f;
                    if (si <= 0) goto air;
                    goto stuck;
                }
                if (b - row + 15 < 0) goto stuck;           /* 209e */
            }
            if (row + si >= 0) goto air;                    /* 20a5 */
            R.ax = W(0x2db6); R.dx = W(0x2db8); probe_extent();   /* 20a9 */
            int r2 = (int16_t)R.dx >> 1;
            b = (int16_t)R.cx;
            if (b == 0) goto air;
            if (b < 0) {                                    /* 20cd: no ceiling flag */
                si -= r2 - 16;
                if (si > 0) goto stuck;
                goto air;
            }
            f = b - r2;                                     /* 20bc */
            if (f < 0) goto air;
            bp |= 8; si += f;
            if (si <= 0) goto air;
            goto stuck;
        }
    }
land:                                                       /* 205d */
    R.ax = W(0x2db6); R.dx = (uint16_t)(W(0xec) + si); R.bx = bp;
    return;
air:                                                        /* 206c */
    R.ax = W(0x2db6); R.dx = (uint16_t)(W(0xec) + si); R.bx = (uint16_t)(bp + 1);
    return;
stuck:                                                      /* 20e5 */
    R.ax = W(0xe8); R.dx = W(0xec); R.bx = 0x1234;
    DB(0x3513) = 0x34;
}

/* ---- object probes (A4_map_move): per-slot cache at DGROUP [BP+0x13f] shape, [BP+0x141] x>>4,
 * [BP+0x143] y>>4.  No attribute mask, no above-the-map test, unsigned y>>4. ---- */

static uint16_t obj_shape(uint16_t x, uint16_t y, int *cached, uint8_t *dh, uint16_t *cx) {
    uint16_t bp = R.bp;
    uint16_t ty = (uint16_t)(y >> 4), tx = (uint16_t)(x >> 4);   /* shr bx,4 ; shr di,4 */
    if (tx == W(bp + 0x141) && ty == W(bp + 0x143)) { *cached = 1; return W(bp + 0x13f); }
    *cached = 0;
    SW(bp + 0x141, tx); SW(bp + 0x143, ty);
    uint16_t sh = shape_lookup(x, y, tx, ty, 0, dh, cx);    /* no `and cl,6` (see shape_lookup) */
    SW(bp + 0x13f, sh);
    return sh;
}

void map_bit(void) {                                        /* 1000:20f7  AX=x DX=y BP=slot -> ZF clear = solid */
    uint16_t x = R.ax, y = R.dx;
    uint16_t c2 = (uint16_t)((x & 15) * 2), r2 = (uint16_t)((y & 15) * 2);
    int cached; uint8_t dh = 0; uint16_t cx = R.cx;
    uint16_t sh = obj_shape(x, y, &cached, &dh, &cx);
    R.ax = W(0x3514 + c2);
    if (cached) {                                           /* 2188 */
        R.dx = (uint16_t)(r2 + sh);                         /* 16-bit add */
        R.bx = R.dx;
    } else {
        R.cx = cx;
        R.dx = (uint16_t)((uint16_t)dh << 8 | r2);
        R.bx = (uint16_t)((sh & 0xff00) | (uint8_t)((uint8_t)sh + (uint8_t)r2));   /* 2181 add bl,dl (8-bit) */
    }
    R.ax &= W(R.bx);
    flags16(R.ax);
}

void map_probe(void) {                                      /* 1000:21a3  AX=x DX=y BP -> AX=2col DX=2row CX=B BX=A */
    uint16_t x = R.ax, y = R.dx;
    uint16_t c2 = (uint16_t)((x & 15) * 2), r2 = (uint16_t)((y & 15) * 2);
    int cached; uint8_t dh = 0; uint16_t cx = 0;
    uint16_t sh = obj_shape(x, y, &cached, &dh, &cx);
    R.ax = c2; R.dx = r2;                                   /* 2228 xor dh,dh on the uncached path */
    R.cx = W((uint16_t)(sh + c2 + 0x680));
    R.bx = W((uint16_t)(sh + r2 + 0x340));
}

void map_probe2(void) {                                     /* 1000:2274  AX=x DX=y BP -> AX=2col DX=2row CX=C BX=D */
    uint16_t x = R.ax, y = R.dx;
    uint16_t c2 = (uint16_t)((x & 15) * 2), r2 = (uint16_t)((y & 15) * 2);
    int cached; uint8_t dh = 0; uint16_t cx = 0;
    uint16_t sh = obj_shape(x, y, &cached, &dh, &cx);
    R.ax = c2; R.dx = r2;
    R.cx = W((uint16_t)(sh + c2 + 0x9c0));
    R.bx = W((uint16_t)(sh + r2 + 0xd00));
}

void map_step(void) {                                       /* 1000:232a  AX=x DX=y BX=shape -> CX=C BX=D (no cache) */
    uint16_t sh = R.bx;
    R.ax = (uint16_t)((R.ax & 15) * 2);
    R.dx = (uint16_t)((R.dx & 15) * 2);
    R.cx = W((uint16_t)(sh + R.ax + 0x9c0));
    R.bx = W((uint16_t)(sh + R.dx + 0xd00));
}
