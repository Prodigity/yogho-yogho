/* Player character: per-tick update + camera, controls, life-start placement.
 * Spec: teardown/40-player-physics.md §3.1, §4.1–4.3, §4.9; 20-timing-input.md §3.8;
 * 10-flow-frontend.md §3.17.  Addresses are Ghidra (seg 160a unless noted).
 *
 * All state stays in DGROUP at the original offsets.  16.16 values are two words, fraction (lo)
 * first, integer (hi) at +2; the asm adds them with add/adc and compares signed hi, then unsigned
 * lo — i.e. a signed 32-bit compare, which is what L()/SL() + int32 casts below do.
 * Constants (accelerations, limits, gravity) are the DGROUP words new_game (160a:058c) stores at
 * 0x10a..0x120; the tables 0x1380 (controls), 0x1390 (phase -> frame), 0x13b4 (phase wrap),
 * 0x12f (phase speed) and the switch table CS:1ba4 are read from MEM (EXE image).
 * Not reset anywhere (P5): 0x122, 0x12e, 0x1377, 0x137d/137e/137f — kept in MEM as they are. */
#include "game.h"
#include "adlib.h"
#include "log.h"
#include "player_ext.h"

#define W(o)     dgw((uint16_t)(o))
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))
#define S16(o)   dgs((uint16_t)(o))

/* 16.16 globals (offset of the fraction word) */
enum {
    X_ = 0xe6, Y_ = 0xea, VX = 0xf2, AX_ = 0xf6, VXL = 0xfa, VY = 0xfe, AY = 0x102, VYL = 0x106,
    ACC_R = 0x10a, ACC_L = 0x10e, VMAX_R = 0x112, VMAX_L = 0x116, GRAV = 0x11a, VFALL = 0x11e
};
static uint32_t L(uint16_t o) { return (uint32_t)W(o) | (uint32_t)W(o + 2) << 16; }
static int32_t  LS(uint16_t o) { return (int32_t)L(o); }
static void SL(uint16_t o, uint32_t v) { SW(o, (uint16_t)v); SW(o + 2, (uint16_t)(v >> 16)); }
/* sar hi,1 ; rcr lo,1 (32-bit arithmetic shift) */
static uint32_t sar32(uint32_t v) { return (v >> 1) | (v & 0x80000000u); }

/* Inlined animation step (160a:17c2, 1a27, 1add, 1b24, 1b64): u8 phase += [12f]; wrap with 0x13b4. */
static void anim_step(uint8_t limit) {
    DB(0x12e) = (uint8_t)(DB(0x12e) + DB(0x12f));
    if (DB(0x12e) >= limit) DB(0x12e) = DB(0x13b4 + DB(0x12e));
}
static uint16_t anim_frame(void) { return DB(0x1390 + DB(0x12e)); }   /* phase / 6 */

void read_input(void) {                                     /* 160a:1cb4 */
    uint8_t raw = DB(0x3510);                               /* R 1, L 2, D 4, U 8, jump 0x80 */
    if (DB(0x12c) == 0xff) raw |= (uint8_t)joy_to_dirbits();   /* 1cc7 lcall 2a33:007d, OR AL */
    DB(0x137d) = DB(0x1380 + (raw & 0x0f));                 /* control combo table */
    if (!(DB(0x137e) & 0x80)) DB(0x137d) |= (uint8_t)(raw & 0x80);   /* jump on the press edge only */
    DB(0x137e) = raw;
}

void player_update(void) {                                  /* 160a:1388 .. 1ba3 */
    /* ---- (a) hurt / death, 1388..148c ---- */
    if (DB(0xb3) != 0 && !(DB(0xbd) & 0x08)) {
        if ((int8_t)DB(0xb2) < 0) {                         /* 139c: energy < 0 -> death */
            MusicPlay(W(0x2d76), W(0x2d74), 2);             /* 13a3: 1000:47bb(far [2d74], 2): death jingle */
            DB(0xbd) |= 0x11;
            SW(0xb7, 0xc8);
        } else {
            SW(0xb7, 0x23);
            DB(0xbd) |= 0x01;
        }
        SW(0x2c9c, W(0x2c9c) | 0x100);                      /* lock input */
        DB(0xb6) = 0;
        if (S16(0xec) < S16(0x514b)) {                      /* 13d8: hop */
            SW(0x100, 0xfffe); SW(0xfe, 0);
            SL(AY, L(GRAV)); SL(VYL, L(VFALL));
        }
        if (DB(0xbd) & 0x20) SL(VX, (DB(0xbd) & 0x40) ? 0xfffd0000u : 0x00030000u);   /* knockback */
        else SL(VX, 0);
        SL(AX_, 0); SL(VXL, 0);
        DB(0xbd) &= 0x9f;
        DB(0xb3) = 0;
    }

    /* ---- (b) controls + horizontal intent, 1491..153d ---- */
    read_input();
    if (W(0x2c9c) & 0x100) DB(0x137d) = 1;                  /* 1496: locked -> idle, no jump */
    {
        uint16_t i = (uint16_t)((DB(0x137d) & 0x1f) - 1);   /* 14a3..14ab */
        uint16_t target = i <= 0x13 ? rw(SEG_G(0x160a), (uint16_t)(0x1ba4 + 2 * i)) : 0x153d;
        switch (target) {                                   /* 14b8 JMP CS:[1ba4+2i] */
        case 0x14bd:                                        /* no horizontal input: decelerate */
            SL(VXL, 0);
            if (S16(0xf4) >= 0) SL(AX_, L(ACC_L));          /* vx.hi >= 0 (jae: any lo) */
            else SL(AX_, L(ACC_R));
            break;
        case 0x14f9:                                        /* left */
            DB(0xb5) = 0x81;
            SL(AX_, L(ACC_L)); SL(VXL, L(VMAX_L));
            break;
        case 0x151c:                                        /* right */
            DB(0xb5) = 0x01;
            SL(AX_, L(ACC_R)); SL(VXL, L(VMAX_R));
            break;
        case 0x153d:
            break;
        default:
            log_gap("player_update: unexpected switch target 160a:%04X", target);
            break;
        }
    }

    /* ---- (c) hazard (previous tick's last probe attribute) + own cell, 153d..157c ---- */
    if (S16(0xec) < S16(0x514b)) {
        if ((DB(0x2db0) & 0x40) && DB(0xb4) == 0 && W(0xb7) == 0) {
            DB(0xb2) = (uint8_t)(DB(0xb2) - 1);
            DB(0xb3) = 1;
        }
        R.ax = W(0xe8); R.dx = W(0xec);
        probe_point();                                      /* 1570: result unused, refreshes 0x2db0 */
    } else {
        DB(0x2db0) = 0;
        SW(0x122, 1);
    }

    /* ---- (d) ladder, 1582..16cb ---- */
    if (DB(0xb6) != 0) {
        if (DB(0x2db0) & 0x20) {
            if ((DB(0x137d) & 0x80) && (DB(0x137d) & 0x06)) {   /* 15a4: jump off sideways */
                SW(0x100, 0xfffc); SW(0xfe, 0);
                SL(AY, L(GRAV)); SL(VYL, L(VFALL));
                DB(0xb6) = 0;
            } else {
                DB(0x137d) &= 0x79;                         /* 15d4: drop jump/left/right */
                SL(AY, 0);
                switch (DB(0x137d) & 0x19) {
                case 0x01: SW(0x100, 0); SW(0xfe, 0); SW(0x108, 0); SW(0x106, 0); break;          /* 1635 */
                case 0x08: SW(0x100, 1); SW(0xfe, 0); SW(0x108, 1); SW(0x106, 0); break;          /* 15ff */
                case 0x10: SW(0x100, 0xffff); SW(0xfe, 0); SW(0x108, 0xffff); SW(0x106, 0); break; /* 161a */
                default: break;
                }
            }
        } else {
            DB(0xb6) = 0;                                   /* 1650 */
        }
    } else if ((DB(0x2db0) & 0x20) && S16(0x100) > -2 && (DB(0x137d) & 0x10)) {   /* 1657: grab */
        if ((uint16_t)((W(0xe8) & 0x1f) + 0xfff8) < 0x10) {     /* 166c: x mod 32 in [8,24) */
            SW(0xe8, W(0xe8) & 0xffe0);
            SW(0xe8, W(0xe8) + 0x10);
            DB(0xb6) = 1;
            SL(VX, 0); SL(AX_, 0); SL(VXL, 0); SL(VY, 0); SL(AY, 0); SL(VYL, 0);
        }
    }

    /* ---- (e) crawl cells / solid-attribute mask, 16d1..16f7 ---- */
    if (DB(0x2db0) & 0x80) {
        DB(0x137d) |= 0x08;
        DB(0x137f) = 0x40;
        SW(0x122, W(0x122) | 0x80);
    } else {
        DB(0x137f) = (DB(0x137d) & 0x08) ? 0x40 : 0xc0;
    }

    /* ---- (f) jump, 16fa..1734 (0x122 = previous tick's move result) ---- */
    if (!(W(0x122) & 0x81) && ((DB(0x137d) & 0x80) || (DB(0xbd) & 0x08))) {
        SW(0x100, 0xfffc); SW(0xfe, 0);
        SL(AY, L(GRAV)); SL(VYL, L(VFALL));
    }

    /* ---- (g) move, 1738..1791 ---- */
    if (S16(0xec) < S16(0x514b)) {
        player_move_collide();                              /* 1741 lcall 1000:1dd1 */
        SW(0x122, R.bx);
        SW(0xe8, R.ax);
        SW(0xec, R.dx);
    } else {                                                /* 1753: free fall below the map */
        SL(X_, L(X_) + L(VX));
        SL(Y_, L(Y_) + L(VY));
        SW(0x122, 1);
    }
    if (!((int16_t)(W(0x514b) + 0x30) > S16(0xec))) {      /* 1777: fell out of the level */
        DB(0xb3) = 1;
        DB(0xb2) = 0xff;
        SW(0xc2, 1);
    }

    /* ---- (h) ladder: leave at the ground, else climbing animation, 1793..1808 ---- */
    if (DB(0xb6) != 0) {
        if (!(W(0x122) & 1) && LS(VY) > 0) {
            DB(0xb6) = 0;                                   /* 17b2 -> 1809 */
        } else {
            if (L(VY) != 0) anim_step(0x24);                /* 17c2 */
            else if (DB(0x12e) >= 0x24) DB(0x12e) = DB(0x13b4 + DB(0x12e));   /* 17e0 */
            SW(0xae, anim_frame() + 0x26);
            return;                                         /* 1808: no forces while climbing */
        }
    }

    /* ---- (i) vertical forces, 1809..18d4 ---- */
    if (W(0x122) & 1) {                                     /* airborne */
        SL(AY, L(GRAV)); SL(VYL, L(VFALL));
    } else if (S16(0x100) < 0) {                            /* grounded and rising */
        SL(AY, 0); SL(VY, 0); SL(VYL, 0);
    }                                                       /* grounded, vy >= 0: vy/ay kept (P3) */
    if (W(0x122) & 8) SL(VY, 0);                            /* head bump */
    SL(VX, L(VX) + L(AX_));                                 /* 1877 add/adc */
    SL(VY, L(VY) + L(AY));
    if (LS(AY) > 0 && LS(VY) > LS(VYL)) { SL(VY, L(VYL)); SL(AY, 0); }   /* 1895 */

    /* ---- (j) crouch speed limit, 18d4..193e ---- */
    if (!(W(0x122) & 1) && (DB(0x137d) & 0x08)) {
        if (DB(0xb5) & 0x80) {
            uint32_t t = sar32(L(VMAX_L));
            if ((int32_t)t > LS(VXL)) SL(VXL, t);
        } else {
            uint32_t t = sar32(L(VMAX_R));
            if ((int32_t)t < LS(VXL)) SL(VXL, t);
        }
    }

    /* ---- (k) horizontal clamp, 1942..19f3 ---- */
    if ((LS(AX_) < 0 && LS(VX) < LS(VXL)) || (LS(AX_) > 0 && LS(VX) > LS(VXL))) {
        SL(VX, L(VXL));
        SL(AX_, 0);
        if (!(W(0x122) & 1) && L(VX) == 0) {                /* stopped on the ground (or f2,f4) */
            if (DB(0xbd) & 0x10) SW(0xae, 0x2d);            /* dead */
            else if (DB(0x137d) & 0x08) SW(0xae, (DB(0xb5) & 0x80) ? 0x12 : 0x10);
            else SW(0xae, (DB(0xb5) & 0x80) ? 0x01 : 0x00);
            return;                                         /* 0x1377 not reset on this path */
        }
    }

    /* ---- (l) animation, 19f4..1ba3 ---- */
    if (W(0x122) & 1) {                                     /* airborne */
        if (W(0xb7) != 0) { SW(0xae, 0x2c); return; }       /* hurt / dying */
        if (LS(VY) > 0x00018000) {                          /* 1a0d: falling faster than 1.5 */
            int16_t fc = S16(0x1377);
            SW(0x1377, (uint16_t)(fc - 1));                 /* 1a21 dec ; jge (true result) */
            if ((int32_t)fc - 1 < 0) {
                anim_step(0x0c);
                if (LS(VX) < -0x4000 || LS(VX) > 0x4000)    /* |vx| > 0.25 */
                    SW(0xae, anim_frame() + ((DB(0xb5) & 0x80) ? 0x20 : 0x22));
                else
                    SW(0xae, anim_frame() + 0x24);
                return;
            }
        }
        if (DB(0xbd) & 0x08) { SW(0xae, 0x2e); return; }    /* level-exit hop */
        SW(0xae, (DB(0xb5) & 0x80) ? 0x0f : 0x0e);
        return;
    }
    SW(0x1377, 0x1e);                                       /* 1abe: grounded */
    if (L(AX_) != 0 && (int16_t)(W(0xf4) ^ W(0xf8)) < 0 && !(DB(0x137d) & 0x08)) {   /* turning / slowing */
        anim_step(0x12);
        SW(0xae, anim_frame() + ((S16(0xf4) >= 0) ? 0x14 : 0x17));
        return;
    }
    if (DB(0x137d) & 0x08) {                                /* crouch walk */
        anim_step(0x0c);
        SW(0xae, anim_frame() + (((int8_t)DB(0xb5) > 0) ? 0x10 : 0x12));
        return;
    }
    anim_step(0x24);                                        /* walk */
    SW(0xae, anim_frame() + (((int8_t)DB(0xb5) > 0) ? 0x02 : 0x08));
}

/* Camera clamp as compiled (160a:1194 / 1c66):  ADD AX,imm ; MOV DX,AX ; JLE -> 0 ;
 * CMP AX,max ; JGE -> max ; OR DX,DX ; JLE -> 0 ; else DX.  JLE after the ADD tests the true
 * signed sum (ZF or SF!=OF), the later tests the wrapped value — reproduced literally. */
static uint16_t cam_clamp(uint16_t a, int16_t add, uint16_t maxv) {
    uint16_t dx = (uint16_t)(a + (uint16_t)add);
    uint16_t ax = ((int32_t)(int16_t)a + add <= 0) ? 0 : dx;
    if ((int16_t)ax >= (int16_t)maxv) return maxv;
    return (int16_t)dx <= 0 ? 0 : dx;
}

void player_tick(void) {                                    /* 160a:1c5b */
    player_update();
    SW(0x2cb4, cam_clamp((uint16_t)(W(0xe8) + W(0xde)), -0xa0, W(0x5151)));   /* 1c5f */
    SW(0x2cb6, cam_clamp((uint16_t)(W(0xec) + W(0xe0)), -0x64, W(0x514f)));   /* 1c89 */
}

/* `t = v - n; if (t <= 0 unsigned) t = 0` (OR AX,AX ; JBE): only an exact 0 is "clamped". */
static uint16_t win_lo(uint16_t v, uint16_t n) { uint16_t t = (uint16_t)(v - n); return t == 0 ? 0 : t; }
/* `t = v + n; if (t >= lim unsigned) t = lim` (CMP ; JNC) */
static uint16_t win_hi(uint16_t v, uint16_t n, uint16_t lim) { uint16_t t = (uint16_t)(v + n); return t >= lim ? lim : t; }

void set_player_pos_and_camera(uint16_t xf, uint16_t x, uint16_t yf, uint16_t y) {   /* 160a:1174 */
    SW(0xe8, x); SW(0xe6, xf);
    SW(0xec, y); SW(0xea, yf);
    SW(0x2cb4, cam_clamp(W(0xe8), -0xa0, W(0x5151)));       /* 1194 */
    SW(0x2cb6, cam_clamp(W(0xec), -0x64, W(0x514f)));       /* 11ba */
    SW(0x2cb8, W(0x2cb4) & 0xfff0);
    SW(0x2cba, W(0x2cb6) & 0xfff0);
    /* object activation window, start variant (11f2..129b) */
    SW(0x5134, win_lo(W(0x2cb4), 0xa0));
    SW(0x5132, win_hi(W(0x2cb4), 0x1e0, W(0x514d)));
    SW(0x5130, win_lo(W(0x2cb6), 0xa0));
    SW(0x512e, win_hi(W(0x2cb6), 0x140, W(0x514b)));
    SW(0x512c, win_lo(W(0x2cb4), 0x40));
    SW(0x512a, win_hi(W(0x2cb4), 0x180, W(0x514d)));
    SW(0x5128, win_lo(W(0x2cb6), 0x40));
    SW(0x5126, win_hi(W(0x2cb6), 0xe0, W(0x514b)));
}
