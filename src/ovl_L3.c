/* Overlay L3 (DAT 79, level index 4): Eskimo — thaw the ice lock with the kettle, set the
 * thermostat.  Spec: teardown/60-overlays.md §7; listing extracted/ovl_dis/L3.asm. */
#include "ovl_story.h"

/* L3:2625 — per-frame entry (+8): animated kettle icon (3 frames @2681), 1 - [0x1475]. */
static void l3_entry(void) { ovl_hud3(1, 3, 0x2681); }

/* L3:2b99 — SPAWNER: at the end of each anim cycle throws a faller (template 2c07) and picks
 * a random next anim.  SELF-MODIFYING: the template's anim byte (2c07+0x0b = cs:2c12) is
 * overwritten with the current anim before the spawn; the byte lives in MEM like the original. */
static void l3_spawner(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2b96 */
    A6_touch();                                               /* 2bb2 */
    A2_anim_step();                                           /* 2bb6 */
    if (!R.cf) TAIL(A1_draw);                                 /* 2bb9 jae 2c04 */
    R.ax = BPW(0x14f);
    CSB_SET(0x2c12, (uint8_t)R.ax);                           /* 2bc0 mov cs:[2c12],al */
    R.cx = (uint16_t)((dgw(0x00d8) & 3) * 2);                 /* 2bc4 */
    R.ax = (uint16_t)(R.cx + R.cx + R.cx);                    /* anim 0/6/0xc/0x12 */
    A8_set_anim();                                            /* 2bd4 */
    R.ax = BPW(0x153);
    R.dx = BPW(0x157);
    R.si = 0x2c07;
    A21_spawn();                                              /* 2be5 */
    if (R.sf) TAIL(A1_draw);                                  /* 2be8 js: no free slot */
    {                                                         /* 2bea: vy of the child */
        uint16_t d6 = dgw(0x00d6);
        R.dx = 0;
        R.ax = (uint16_t)(d6 + d6);                           /* add ax,ax */
        unsigned c1 = d6 >> 15;
        R.dx = (uint16_t)(R.dx + R.dx + c1);                  /* adc dx,dx */
        uint32_t s = (uint32_t)R.ax + 0x8000;                 /* add ax,8000h */
        R.ax = (uint16_t)s;
        R.dx = (uint16_t)(R.dx + R.dx + (s >> 16));           /* adc dx,dx */
        R.ax = (uint16_t)-R.ax;                               /* two independent NEGs */
        R.dx = (uint16_t)-R.dx;
        dgw_set((uint16_t)(R.si + 0x15d), R.ax);              /* new slot vyf */
        dgw_set((uint16_t)(R.si + 0x15f), R.dx);              /* new slot vy */
    }
    TAIL(A1_draw);                                            /* 2c04 */
}

/* L3:2c7d — FALLER (template 2c07): 2 px/tick left + gravity, no collision, no API2. */
static void l3_faller(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2c7a */
    A6_touch();                                               /* 2c96 */
    R.bx = (uint16_t)(R.ax - 2);                              /* 2c99 */
    uint16_t nx = R.bx;                                       /* push bx */
    ovl_grav(0x2000);                                         /* 2c9f..2cd1 (inline) */
    R.bx = nx;                                                /* 2cd3 pop bx */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    TAIL(A1_draw);
}

/* L3:2d29 — SLIDER: follows its script until any clip (A4 bits 1..3), then stops for good. */
static void l3_slider(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2d26 */
    A6_touch();                                               /* 2d42 */
    if (!(BPW(0x16b) & 1)) {                                  /* 2d45 */
        A3_motion_step();                                     /* 2d4f */
        A4_map_move();                                        /* 2d53 */
        R.bx = (uint16_t)((R.bx & 0xff00) | (R.bx & 0x0e));   /* 2d56 and bl,0eh */
        if (R.bx & 0xff) BPW_SET(0x16b, (uint16_t)(BPW(0x16b) | 1));   /* 2d5b: stop */
        else { BPW_SET(0x153, R.ax); BPW_SET(0x157, R.dx); }  /* 2d64 */
    }
    TAIL(A1_draw);                                            /* 2d6e */
}

/* L3:2ee9 — SLOPEHOPPER: F-HOPWALKER variant; frames from the airborne vy or from the
 * collision shape under it (table cs:3008); no API2. */
static void l3_slopehopper(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2ee6 */
    A6_touch();                                               /* 2f02 */
    if (!(BPB(0x16f) & 1)) {                                  /* 2f05 */
        uint8_t t = (uint8_t)(BPB(0x170) - 1);                /* 2f0d dec byte; jns */
        BPB_SET(0x170, t);
        if (t & 0x80) {
            BPB_SET(0x170, 0x40);
            R.cx = (uint16_t)(R.ax - dgw(0x00e8));            /* 2f1a; js 2f27 */
            R.cx = (R.cx & 0x8000) ? 6 : 0;
            BPW_SET(0x171, R.cx);                             /* 2f2a */
            BPW_SET(0x14d, R.cx);
            R.cx = sar16((uint16_t)(R.cx - 1), 2);            /* dec cx; sar cx,2 -> -1 / +1 */
            BPW_SET(0x15b, R.cx);
        }
    }
    ovl_grav(0x2000);                                         /* 2f3d..2f6f (inline) */
    R.bx = (uint16_t)(BPW(0x15b) + R.ax);                     /* 2f71 */
    A4_map_move();                                            /* 2f79 */
    if (R.bx & 1) {                                           /* 2f7c: airborne */
        int old = BPB(0x16f) & 1;
        BPB_SET(0x16f, (uint8_t)R.bx);
        if (!old) {
            uint16_t ax = R.ax;
            A7_next_anim();                                   /* 2f91 */
            R.ax = ax;
            R.bx = 0xfffd;
            BPW_SET(0x15f, R.bx);
            R.bx = 0;
            BPW_SET(0x15d, 0);
        }
        R.bx = (uint16_t)(BPW(0x15f) + 3);                    /* 2fa4 */
        if (R.bx != 0) R.bx = (uint16_t)((R.bx >> 2) + 1);
        R.bx = (uint16_t)(R.bx + BPW(0x171));                 /* 2fb2 */
        BPB_SET(0x13c, (uint8_t)R.bx);
    } else {                                                  /* 2fe9 */
        int old = BPB(0x16f) & 1;
        BPB_SET(0x16f, (uint8_t)R.bx);
        if (old) {                                            /* landed */
            uint16_t ax = R.ax;
            A7_next_anim();                                   /* 2ff8 */
            R.ax = ax;
            R.bx = BPW(0x171);
            BPW_SET(0x14d, R.bx);
        }
        R.bx = (uint16_t)(((uint16_t)(BPW(0x13f) - 0x3738) >> 5) & 0x1f);   /* 2fbf */
        R.bx = (uint16_t)((R.bx & 0xff00) | CSB((uint16_t)(R.bx + 0x3008)));
        if (!(R.bx & 0x80)) BPB_SET(0x13c, (uint8_t)R.bx);    /* 2fd3 or bl,bl; js */
    }
    BPW_SET(0x153, R.ax);                                     /* 2fdc */
    BPW_SET(0x157, R.dx);
    TAIL(A1_draw);
}

/* L3:33e5 — THERMOSTAT part (goal, 3 records, bit = param.lo 1/4/2).  When all three are set
 * ([0x513b] == 7) the part that completed counts 140 ticks and ends the level. */
static void l3_thermostat(void) {
    R.cx = dgw(0x513b);                                       /* 33e5 */
    if (R.cx == 7) {                                          /* 33ec je 3457 */
        if (BPW(0x16d) != 0) {                                /* 3457 */
            uint16_t v = (uint16_t)(BPW(0x16d) - 1);          /* 345f dec; jne */
            BPW_SET(0x16d, v);
            if (v == 0) dgw_set(0x00c2, 2);                   /* level completed */
        }
        if (!ovl_keep()) return;                              /* 346c..3482: retf, no despawn */
        TAIL(A1_draw);                                        /* 3484 jmp 3454 */
    }
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 33e2 */
    R.bx = BPB(0x16b);                                        /* 3406 xor bx,bx; mov bl,[bp+16b] */
    if (!(dgw(0x513b) & R.bx)) {                              /* 340d test [513b],bx; jne 3448 */
        A6_touch();                                           /* 3414 */
        if (R.bx == 0) {                                      /* 3417 or bx,bx; je 344d */
            BPB_SET(0x13c, 0);
            TAIL(A1_draw);
        }
        R.bx = (uint16_t)(BPW(0x16b) & 0xff);                 /* 341b */
        dgw_set(0x513b, (uint16_t)(dgw(0x513b) | R.bx));
        R.bx = 0x1c;
        A20_add_score();                                      /* 342c */
        if (dgw(0x513b) == 7) {                               /* 342f */
            DB(0x00bd) |= 8;
            dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x180));
            BPW_SET(0x16d, 0x8c);
        }
    }
    R.bx = (uint16_t)((R.bx & 0xff00) | 1);                   /* 3448 mov bl,1 */
    BPB_SET(0x13c, 1);
    TAIL(A1_draw);
}

static void l3_icelock(void) { ovl_door(DOOR_ICE); }              /* L3:2831 */
static void l3_door(void) { ovl_door(DOOR_KEY); }                 /* L3:275f */
static void l3_pathcol(void) { ovl_pathcol(1); }                /* L3:3105 */
static void l3_quest(void) { ovl_quest(0x1c, 0x14, Q_ANIM | Q_NOPUSH); }   /* L3:34bf */

void ovl_L3_register(void) {
    const int L = LVL_L3;
    farcall_register_overlay(L, 0x0008, l3_entry);            /* +8 JMP 2625 */
    farcall_register_overlay(L, 0x258a, A14_checkpoint);
    farcall_register_overlay(L, 0x25b0, A15_energy);
    farcall_register_overlay(L, 0x25d6, A16_extra_life);
    farcall_register_overlay(L, 0x25fc, A17_big_bonus);
    farcall_register_overlay(L, 0x2622, A18_time_bonus);
    farcall_register_overlay(L, 0x26d0, ovl_spring);
    farcall_register_overlay(L, 0x275f, l3_door);
    farcall_register_overlay(L, 0x2831, l3_icelock);
    farcall_register_overlay(L, 0x295f, ovl_roller);
    farcall_register_overlay(L, 0x2b99, l3_spawner);
    farcall_register_overlay(L, 0x2c7d, l3_faller);           /* template 2c07 */
    farcall_register_overlay(L, 0x2d29, l3_slider);
    farcall_register_overlay(L, 0x2ee9, l3_slopehopper);
    farcall_register_overlay(L, 0x3105, l3_pathcol);
    farcall_register_overlay(L, 0x31f7, ovl_turnwalker);
    farcall_register_overlay(L, 0x33e5, l3_thermostat);
    farcall_register_overlay(L, 0x34bf, l3_quest);
}
