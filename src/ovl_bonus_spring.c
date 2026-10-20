/* Bonus overlays: the B2/B4/B6 spring / bumper / wind library.
 * Spec: teardown/60-overlays.md §11 "B2 / B4 / B6"; API semantics teardown/50-objects-api.md
 * §3f (A2/A7), §3i (A6), §3j (A20, A23).
 * The library is byte-identical in B2, B4 and B6 (only its offsets differ; see ovl_B2.c,
 * ovl_B4.c, ovl_B6.c).  Each routine is quoted from the overlay that uses it: F-BSPRING and
 * F-BBUMPD from B2, F-BBUMPH and F-BBUMPC from B4, the three wind routines from B6.
 *
 * Player-state writes done here (40-player-physics.md Q12): vx [0xf2]/[0xf4], ax [0xf6]/[0xf8],
 * vy [0xfe]/[0x100], ay [0x102]/[0x104] (bumpers, springs) and the position words
 * [0xe6]/[0xe8] / [0xea]/[0xec] (wind).  All fraction/integer pairs are written separately as in
 * the asm: NEG is applied to the two words independently (not a 32-bit negate), SHL/SHR/SAR of
 * `param` give the fraction and the integer halves.
 *
 * Register dependencies on the engine API (as in the original): A6 preserves AX/DX and leaves
 * DI = sprite descriptor (1000:41a0); A23 changes only AX (1000:05da). */
#include "ovl_bonus.h"

static uint16_t neg16(uint16_t v) { return (uint16_t)(0u - v); }

/* Tail shared by every bumper/spring: `call API2; jae 1f; call API7; 1: jmp API1`. */
static void anim_once_then_draw(void) {
    A2_anim_step();
    if (R.cf) A7_next_anim();
    A1_draw();
}

/* F-BSPRING B2:18b6 — vertical spring: vy = param/2048 (signed), score type 4. */
void bonus_spring(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 18b6..18cc -> 1905 jmp API10 */
    A6_touch();                                                /* 18cf */
    if (R.bx != 0) {                                           /* 18d2 or bx,bx; je 18f8 */
        BPW_SET(0x14b, BPW(0x14b) | 0x41);                     /* 18d6 aflags: play once, CF at end */
        R.bx = BPW(0x16b);                                     /* 18dc param */
        R.cx = R.bx;
        R.bx = (uint16_t)(R.bx << 5);                          /* 18e3 shl bx,5 */
        dgw_set(0x00fe, R.bx);                                 /* vy fraction */
        R.cx = (uint16_t)((int16_t)R.cx >> 11);                /* 18ea sar cx,0b */
        dgw_set(0x0100, R.cx);                                 /* vy integer */
        R.bx = 4;                                              /* 18f1 score type 4 (10 pts) */
        A20_add_score();                                       /* 18f5 AX,DX = x,y (kept by A6) */
    }
    anim_once_then_draw();                                     /* 18f8..1902 */
}

/* F-BBUMPH B4:1912 (B2:194e, B6:1aca dead) — horizontal bumper: anim 0 pushes right, else left;
 * vx = ±(param>>12 : param<<4), ax = (0 : same integer). */
void bonus_bump_h(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1912..1928 -> 1977 jmp API10 */
    A6_touch();                                                /* 192b */
    if (R.bx != 0) {                                           /* 192e or bx,bx; je 196a */
        BPW_SET(0x14b, BPW(0x14b) | 0x41);                     /* 1932 */
        R.bx = BPW(0x16b);
        R.cx = R.bx;
        R.bx = (uint16_t)(R.bx << 4);                          /* 193f shl bx,4 */
        R.cx = (uint16_t)(R.cx >> 12);                         /* 1942 shr cx,0c (logical) */
        if (BPW(0x14f) != 0) {                                 /* 1945 cmp anim,0; je 1951 */
            R.cx = neg16(R.cx);
            R.bx = neg16(R.bx);
        }
        dgw_set(0x00f2, R.bx);                                 /* 1951 vx fraction */
        dgw_set(0x00f4, R.cx);                                 /*      vx integer */
        R.bx = 0;
        dgw_set(0x00f6, R.bx);                                 /* 195b ax fraction = 0 */
        dgw_set(0x00f8, R.cx);                                 /*      ax integer */
        R.bx = 4;                                              /* 1963 */
        A20_add_score();                                       /* 1967 */
    }
    anim_once_then_draw();                                     /* 196a..1974 */
}

/* F-BBUMPD B2:19fc (B4:19c0, B6:1b78 dead) — diagonal bumper: anim 0 pushes left, else right;
 * always up.  No score. */
void bonus_bump_d(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 19fc..1a12 -> 1a72 jmp API10 */
    A6_touch();                                                /* 1a15 */
    if (R.bx != 0) {                                           /* 1a18 or bx,bx; je 1a65 */
        BPW_SET(0x14b, BPW(0x14b) | 0x41);                     /* 1a1c */
        R.bx = BPW(0x16b);
        R.cx = R.bx;
        R.bx = (uint16_t)(R.bx << 4);                          /* 1a29 shl bx,4 */
        R.cx = (uint16_t)(R.cx >> 12);                         /* 1a2c shr cx,0c */
        uint16_t s_bx = R.bx, s_cx = R.cx;                     /* 1a2f push bx; push cx */
        if (BPW(0x14f) == 0) {                                 /* 1a31 cmp anim,0; jne 1a3d */
            R.cx = neg16(R.cx);
            R.bx = neg16(R.bx);
        }
        dgw_set(0x00f2, R.bx);                                 /* 1a3d vx */
        dgw_set(0x00f4, R.cx);
        R.cx = s_cx; R.bx = s_bx;                              /* 1a45 pop cx; pop bx */
        R.bx = neg16(R.bx);                                    /* 1a47 */
        R.cx = neg16(R.cx);
        dgw_set(0x00fe, R.bx);                                 /* 1a4b vy = -(…) : up */
        dgw_set(0x0100, R.cx);
        R.bx = 0;
        dgw_set(0x00f6, R.bx);                                 /* 1a55 ax = 0 : -i */
        dgw_set(0x00f8, R.cx);
        dgw_set(0x0102, R.bx);                                 /* 1a5d ay = 0 : -i */
        dgw_set(0x0104, R.cx);
    }
    anim_once_then_draw();                                     /* 1a65..1a6f */
}

/* F-BBUMPC B4:1a6b (B2:1aa7, B6:1c23 dead) — box-geometry bumper, no API6: pushes the player
 * away depending on which band of a 32x32 area (relative to the hitbox origin) the player is in. */
void bonus_bump_c(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1a6b..1a81 -> 1a68 jmp API10 */
    R.di = BPW(0x137);                                         /* 1a83 sprite descriptor */
    R.bx = (uint16_t)(bonus_esw((uint16_t)(R.di + 5)) + R.ax - dgw(0x00e8));   /* 1a88 box x - player x */
    if ((int16_t)R.bx >= 0) goto tail;                         /* 1a92 jns */
    if ((int16_t)R.bx <= -0x20) goto tail;                     /* 1a94 cmp bx,-20; jle */
    R.cx = (uint16_t)(bonus_esw((uint16_t)(R.di + 7)) + R.dx - dgw(0x00ec) + 0x0d);  /* 1a99 */
    if ((int16_t)R.cx >= 0) goto tail;                         /* 1aa6 jns */
    if ((int16_t)R.cx <= -0x20) goto tail;                     /* 1aa8 jle */
    R.ax = BPW(0x16b);                                         /* 1aad param */
    R.dx = R.ax;
    R.ax = (uint16_t)(R.ax << 4);
    R.dx = (uint16_t)(R.dx >> 12);
    if ((int16_t)R.cx <= -0x14) goto set_vy;                   /* 1aba: low band, downward (+) */
    if ((int16_t)R.cx < -0x0c) goto horiz;                     /* 1abf: middle band, no vy change */
    R.ax = neg16(R.ax);                                        /* 1ac4: high band, upward (−) */
    R.dx = neg16(R.dx);
set_vy:
    BPW_SET(0x14b, BPW(0x14b) | 0x41);                         /* 1ac8 */
    dgw_set(0x00fe, R.ax);
    dgw_set(0x0100, R.dx);
horiz:
    R.ax = BPW(0x16b);                                         /* 1ad5 */
    R.dx = R.ax;
    R.ax = (uint16_t)(R.ax << 4);
    R.dx = (uint16_t)(R.dx >> 12);
    if ((int16_t)R.bx <= -0x14) goto set_vx;                   /* 1ae2 */
    if ((int16_t)R.bx < -0x0c) goto tail;                      /* 1ae7 */
    R.ax = neg16(R.ax);                                        /* 1aec */
    R.dx = neg16(R.dx);
set_vx:
    BPW_SET(0x14b, BPW(0x14b) | 0x41);                         /* 1af0 */
    dgw_set(0x00f2, R.ax);
    dgw_set(0x00f4, R.dx);
tail:
    anim_once_then_draw();                                     /* 1afd..1b07 */
}

/* Wind columns' own 3-frame animation (B6:1d73 / 1e40 / 1f0e): advance by 1 or 2 frames
 * (random bit [0xd6]&1) modulo 3 every delay+1 ticks, then jmp API1. */
static void wind_anim_draw(void) {
    uint8_t c = (uint8_t)(BPB(0x13e) - 1);                     /* 1d73 dec byte dcnt; jns 1d9c */
    BPB(0x13e) = c;
    if ((int8_t)c < 0) {
        SET_AL(R, BPB(0x13d));                                 /* 1d7a al = delay reload */
        BPB(0x13e) = AL(R);
        R.ax = dgw(0x00d6);                                    /* 1d84 */
        R.ax &= 1;
        R.ax = (uint16_t)(R.ax + 1);                           /* AH = 0, AL = 1 or 2 */
        SET_AL(R, (uint8_t)(AL(R) + BPB(0x13c)));              /* 1d8b add al,frame (byte add) */
        if ((int16_t)R.ax >= 3)                                /* 1d90 cmp ax,3; jl */
            SET_AL(R, (uint8_t)(AL(R) - 3));
        BPB(0x13c) = AL(R);                                    /* 1d97 frame */
    }
    A1_draw();                                                 /* 1d9c jmp API1 */
}

/* F-BWINDUP B6:1d13 (B2:1b97, B4:1b5b dead) — updraft: lifts the player by
 * max(0, depth + rand(-8..7)) * param / 4096 px per contact tick ([2d8e] = depth, write-only). */
void bonus_wind_up(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1d13..1d29 -> 1d10 jmp API10 */
    A6_touch();                                                /* 1d2c (DI = sprite descriptor) */
    if (R.bx != 0) {                                           /* 1d2f or bx,bx; je 1d73 */
        BPW_SET(0x169, 0);                                     /* 1d33 clear the touch latch */
        R.cx = (uint16_t)(bonus_esw((uint16_t)(R.di + 7)) + R.dx);   /* 1d3a hit oy + y */
        R.cx = (uint16_t)(R.cx - dgw(0x00ec));
        R.cx = neg16(R.cx);                                    /* 1d44 depth = py - box top */
        dgw_set(0x2d8e, R.cx);
        A23_random();                                          /* 1d4b AX = rand */
        R.ax &= 0x0f;
        R.ax = (uint16_t)(R.ax - 8);
        R.ax = (uint16_t)(R.ax + R.cx);
        if ((int16_t)R.ax < 0) R.ax = 0;                       /* 1d56 or ax,ax; jns; xor ax,ax */
        R.dx = BPW(0x16b);                                     /* 1d5c param */
        uint32_t m = (uint32_t)R.ax * R.dx;                    /* 1d61 mul dx */
        R.ax = (uint16_t)m; R.dx = (uint16_t)(m >> 16);
        R.cx = R.ax;
        R.ax = (uint16_t)(R.ax << 4);                          /* fraction */
        R.cx = (uint16_t)(R.cx >> 12);                         /* integer */
        uint16_t yf = dgw(0x00ea);                             /* 1d6b sub [ea],ax; sbb [ec],cx */
        unsigned borrow = yf < R.ax;
        dgw_set(0x00ea, (uint16_t)(yf - R.ax));
        dgw_set(0x00ec, (uint16_t)(dgw(0x00ec) - R.cx - borrow));
    }
    wind_anim_draw();
}

/* F-BWINDR B6:1df1 (B2:1c75, B4:1c39 dead) — wind blowing right.  Quirk (60-overlays Q8):
 * the horizontal depth is computed into CX but the MUL uses AX, which still holds the object's
 * x, so the push is (x * param) mod 65536 / 4096 px per contact tick. */
void bonus_wind_right(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1df1..1e07 -> 1dee jmp API10 */
    A6_touch();                                                /* 1e0a */
    if (R.bx != 0) {                                           /* 1e0d or bx,bx; je 1e40 */
        BPW_SET(0x169, 0);                                     /* 1e11 clear the touch latch */
        R.cx = (uint16_t)(bonus_esw((uint16_t)(R.di + 5)) + R.ax);   /* 1e18 hit ox + x */
        R.cx = (uint16_t)(R.cx - dgw(0x00e8));
        R.cx = neg16(R.cx);
        R.cx = (uint16_t)(R.cx + 8);
        R.cx = (uint16_t)(R.cx >> 1);                          /* 1e27 depth (unused, see above) */
        R.dx = BPW(0x16b);                                     /* 1e29 param */
        uint32_t m = (uint32_t)R.ax * R.dx;                    /* 1e2e mul dx — AX = object x */
        R.ax = (uint16_t)m; R.dx = (uint16_t)(m >> 16);
        R.cx = R.ax;
        R.ax = (uint16_t)(R.ax << 4);
        R.cx = (uint16_t)(R.cx >> 12);
        uint32_t xf = (uint32_t)dgw(0x00e6) + R.ax;            /* 1e38 add [e6],ax; adc [e8],cx */
        dgw_set(0x00e6, (uint16_t)xf);
        dgw_set(0x00e8, (uint16_t)(dgw(0x00e8) + R.cx + (xf >> 16)));
    }
    wind_anim_draw();                                          /* 1e40..1e69 */
}

/* F-BWINDL B6:1ebd (B2:1d41, B4:1d05 dead) — wind blowing left: push = depth * param / 4096,
 * depth = ((player x − box left) + 8) >> 1 (logical). */
void bonus_wind_left(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1ebd..1ed3 -> 1eba jmp API10 */
    A6_touch();                                                /* 1ed6 */
    if (R.bx != 0) {                                           /* 1ed9 or bx,bx; je 1f0e */
        BPW_SET(0x169, 0);                                     /* 1edd */
        R.cx = (uint16_t)(bonus_esw((uint16_t)(R.di + 5)) + R.ax);   /* 1ee4 */
        R.cx = (uint16_t)(R.cx - dgw(0x00e8));
        R.cx = neg16(R.cx);
        R.cx = (uint16_t)(R.cx + 8);
        R.cx = (uint16_t)(R.cx >> 1);                          /* 1ef3 */
        R.ax = R.cx;                                           /* 1ef5 mov ax,cx */
        R.dx = BPW(0x16b);
        uint32_t m = (uint32_t)R.ax * R.dx;                    /* 1efc mul dx */
        R.ax = (uint16_t)m; R.dx = (uint16_t)(m >> 16);
        R.cx = R.ax;
        R.ax = (uint16_t)(R.ax << 4);
        R.cx = (uint16_t)(R.cx >> 12);
        uint16_t xf = dgw(0x00e6);                             /* 1f06 sub [e6],ax; sbb [e8],cx */
        unsigned borrow = xf < R.ax;
        dgw_set(0x00e6, (uint16_t)(xf - R.ax));
        dgw_set(0x00e8, (uint16_t)(dgw(0x00e8) - R.cx - borrow));
    }
    wind_anim_draw();                                          /* 1f0e..1f37 */
}
