/* Overlay L4 (DAT 80, level index 6): Indian — feed the llama 5 worms.
 * Spec: teardown/60-overlays.md §8; listing extracted/ovl_dis/L4.asm. */
#include "ovl_story.h"

/* L4:32b9 — per-frame entry (+8): animated worm icon (4 frames @3315), 5 - [0x1475]. */
static void l4_entry(void) { ovl_hud3(5, 4, 0x3315); }

/* L4:3765 — JUMPER: crouch (anims >= 0xc, once), then jump toward the player with
 * vx = +/-param (bytes swapped into the fraction), vy = -4; frame 1 while falling. */
static void l4_jumper(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 3762 */
    A6_touch();                                               /* 377e */
    R.cx = BPW(0x14f);
    if (S16(R.cx) >= 0x0c) {                                  /* 3786 cmp cx,0ch; jl 37d6 */
        uint16_t x = R.ax;                                    /* 378b push ax */
        A2_anim_step();                                       /* 378d */
        if (R.cf) {
            R.ax = BPW(0x16b);                                /* 3792 */
            R.cx = 0;
            R.bx = (uint16_t)(dgw(0x00e8) - BPW(0x153));      /* 3799; jns 37a9 */
            if (R.bx & 0x8000) { R.ax = (uint16_t)-R.ax; R.cx = 6; }
            R.ax = xchg_ahal(R.ax);                           /* 37a9 */
            BPW_SET(0x159, R.ax);
            R.ax = cbw16(R.ax);                               /* 37b2 CBW */
            BPW_SET(0x15b, R.ax);
            BPW_SET(0x171, R.cx);
            R.ax = R.cx;
            A8_set_anim();                                    /* 37c0 */
            R.bx = 0xfffc;
            BPW_SET(0x15f, R.bx);                             /* vy = -4 */
            BPW_SET(0x15d, 0);
        }
        R.ax = x;                                             /* 37d2 pop ax */
        goto store;                                           /* jmp 3837 */
    }
    {                                                         /* 37d6 inline gravity with frame 1 */
        uint32_t s = (uint32_t)0x2000 + BPW(0x15d);
        R.bx = (uint16_t)s;
        BPW_SET(0x15d, R.bx);
        R.cx = (uint16_t)(BPW(0x15f) + (s >> 16));            /* xor cx,cx; adc cx,[15f]; js 37f2 */
        if (!(R.cx & 0x8000)) BPB_SET(0x13c, 1);              /* 37ec falling frame */
        if (S16(R.cx) >= 3) { R.cx = 3; R.bx = 0; BPW_SET(0x15d, 0); }   /* 37f2 */
        BPW_SET(0x15f, R.cx);                                 /* 3801 */
        s = (uint32_t)R.bx + BPW(0x155);
        R.bx = (uint16_t)s;
        BPW_SET(0x155, R.bx);
        R.cx = (uint16_t)(R.cx + R.dx + (s >> 16));
    }
    {                                                         /* 3812 x += vx (16.16) */
        uint32_t s = (uint32_t)BPW(0x151) + BPW(0x159);
        BPW_SET(0x151, (uint16_t)s);
        R.bx = (uint16_t)(R.ax + BPW(0x15b) + (s >> 16));
    }
    A4_map_move();                                            /* 3829 */
    if (R.bx & 1) {
        BPB_SET(0x16f, (uint8_t)R.bx);                        /* 3832 */
    } else {                                                  /* 3844 landed -> crouch */
        uint16_t ax = R.ax;
        R.ax = (uint16_t)(BPW(0x171) + 0x0c);
        A8_set_anim();
        R.ax = ax;
    }
store:
    BPW_SET(0x153, R.ax);                                     /* 3837 */
    BPW_SET(0x157, R.dx);
    TAIL(A1_draw);
}

/* L4:3981 — WATCHER: follows its script, turns once the player has passed by 64 px. */
static void l4_watcher(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 397e */
    A6_touch();                                               /* 399a */
    R.bx = (uint16_t)(dgw(0x00e8) - R.ax);                    /* 399d */
    int16_t an = S16(BPW(0x14f));
    int turn;
    if (an > 6) turn = 0;                                     /* 39a3 cmp [bp+14f],6; jg 39c0 */
    else if (an == 6) turn = !(S16(R.bx) > -0x40);            /* 39ad cmp bx,-40h; jg */
    else turn = !(S16(R.bx) < 0x40);                          /* 39b5 cmp bx,40h; jl */
    if (turn) {
        uint16_t ax = R.ax;
        A7_next_anim();                                       /* 39bc */
        R.ax = ax;
    }
    A3_motion_step();                                         /* 39c1 */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    A2_anim_step();                                           /* 39cf */
    TAIL(A1_draw);
}

/* ---- L4:3b71 F-STATE: jmp cs:[3b99 + anim/2]; the table is read from MEM ---- */

/* L4:3bb3 (anim 0) — wait until the player is within 0x40 (y) / 0x50 (x). */
static void l4_st_wait(void) {
    R.bx = R.ax;                                              /* 3bb3 */
    R.ax = cwd_abs((uint16_t)(dgw(0x00ec) - R.dx));           /* 3bb5 (CWD clobbers DX) */
    if (R.ax > 0x40) TAIL(A1_draw);                           /* 3bbf cmp ax,40h; ja (unsigned) */
    R.ax = cwd_abs((uint16_t)(dgw(0x00e8) - R.bx));           /* 3bc4 */
    if (R.ax > 0x50) TAIL(A1_draw);                           /* 3bce */
    BPW_SET(0x16b, 2);                                        /* 3bd3 */
    A7_next_anim();                                           /* 3bda */
    TAIL(A1_draw);                                            /* 3c0a */
}

/* L4:3be1 (anim 6) — taunt param+1 cycles, then jump (anim 0xc, vy = -2, x step v0 = 1). */
static void l4_st_taunt(void) {
    A2_anim_step();                                           /* 3be1 */
    if (!R.cf) TAIL(A1_draw);
    uint16_t v = (uint16_t)(BPW(0x16b) - 1);                  /* 3be7 dec [bp+16b]; jns 3bda */
    BPW_SET(0x16b, v);
    if (!(v & 0x8000)) { A7_next_anim(); TAIL(A1_draw); }
    R.ax = 0x0c;
    A8_set_anim();                                            /* 3bf2 */
    BPW_SET(0x15f, 0xfffe);
    BPW_SET(0x16b, 0);
    BPW_SET(0x16d, 1);
    TAIL(A1_draw);
}

/* L4:3c0d (anims 0xc, 0x24, 0x30) — jump: x += v0, gravity, collide; landed -> next anim. */
static void l4_st_jump(void) {
    R.bx = (uint16_t)(R.ax + BPW(0x16d));                     /* 3c0d */
    ovl_grav(0x2000);                                         /* 3c14 call 3f4d */
    A4_map_move();                                            /* 3c18 */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    uint16_t bx = R.bx;                                       /* push bx */
    A2_anim_step();                                           /* 3c27 */
    R.bx = bx;                                                /* pop bx */
    if (R.bx & 1) TAIL(A1_draw);                              /* 3c2b test bx,1; jne 3c0a */
    A7_next_anim();                                           /* 3c32 */
    TAIL(A1_draw);
}

/* L4:3c37 (anims 0x12, 0x1e, 0x2a) — play the anim, next at its end. */
static void l4_st_anim(void) {
    A2_anim_step();                                           /* 3c38 */
    if (R.cf) A7_next_anim();                                 /* 3c3e */
    TAIL(A1_draw);
}

/* L4:3c43 (anim 0x18) — walk left while the player is to the left, then pick the next anim. */
static void l4_st_walk(void) {
    R.cx = 0x1e;                                              /* 3c43 */
    BPW_SET(0x16d, 1);
    R.bx = (uint16_t)(dgw(0x00e8) - R.ax);
    if (!(R.bx & 0x8000)) goto set;                           /* 3c53 jns 3c9f */
    R.bx = R.ax;                                              /* 3c55 */
    R.cx = (uint16_t)(R.dx + 3);
    R.bx = (uint16_t)(R.bx - 1);
    A4_map_move();                                            /* 3c5e */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 3c6c */
    if (!R.cf) TAIL(A1_draw);                                 /* 3c6f jae 3c0a */
    BPW_SET(0x16d, 1);                                        /* 3c71 */
    R.cx = 0x1e;
    R.bx = (uint16_t)(dgw(0x00e8) - BPW(0x153));
    if (!(R.bx & 0x8000)) goto set;                           /* 3c84 jns 3c9f */
    BPW_SET(0x16d, 0xffff);                                   /* 3c86 */
    R.cx = 0x2a;
    if (dgw(0x00d6) & 3) { A7_next_anim(); TAIL(A1_draw); }   /* 3c90 test [d6],3; je */
set:
    R.ax = R.cx;                                              /* 3c9f */
    A8_set_anim();
    TAIL(A1_draw);
}

/* L4:3b71 — F-STATE dispatcher. */
static void l4_state(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 3b6e */
    A6_touch();                                               /* 3b8a */
    R.bx = (uint16_t)(BPW(0x14f) >> 1);                       /* 3b8d shr bx,1 */
    uint16_t target = CSW((uint16_t)(R.bx + 0x3b99));         /* 3b94 jmp cs:[bx+3b99] */
    switch (target) {
    case 0x3bb3: l4_st_wait(); return;
    case 0x3be1: l4_st_taunt(); return;
    case 0x3c0d: l4_st_jump(); return;
    case 0x3c37: l4_st_anim(); return;
    case 0x3c43: l4_st_walk(); return;
    default:
        log_gap("L4:3b94 jump table: anim %04X -> unported target %04X", BPW(0x14f), target);
        return;
    }
}

/* L4:3e53 — LLAMA (goal): with 5 worms, touching it starts its anim chain; in anim 0x12 it
 * counts param (0x12c) down and then ends the level. */
static void l4_llama(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 3e50 */
    R.bx = BPW(0x14f);
    if (R.bx == 0x12) {                                       /* 3e70 je 3ea7 */
        uint16_t v = (uint16_t)(BPW(0x16b) - 1);              /* 3ea7 dec; jns 3eb4 */
        BPW_SET(0x16b, v);
        if (v & 0x8000) dgw_set(0x00c2, 2);                   /* level completed */
    } else {
        R.cx = dgw(0x1475);
        if (R.cx == 5) {                                      /* 3e79 */
            A6_touch();                                       /* 3e7f */
            if (!R.zf) {                                      /* 3e82 je 3eb4 */
                DB(0x00bd) |= 8;
                dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x180));
                A7_next_anim();                               /* 3e90 */
                R.ax = BPW(0x153);
                R.dx = BPW(0x157);
                R.bx = 0x24;
                A20_add_score();                              /* 3ea1 */
                TAIL(A1_draw);                                /* 3ea4 jmp 3ebe */
            }
        }
    }
    A2_anim_step();                                           /* 3eb4 */
    if (R.cf) A7_next_anim();
    TAIL(A1_draw);
}

static void l4_door(void) { ovl_door(DOOR_KEY); }                 /* L4:3403 */
static void l4_quest(void) { ovl_quest(0x20, 0x10, Q_SFX_BEFORE | Q_ANIM); }   /* L4:3f05 */

void ovl_L4_register(void) {
    const int L = LVL_L4;
    farcall_register_overlay(L, 0x0008, l4_entry);            /* +8 JMP 32b9 */
    farcall_register_overlay(L, 0x321e, A14_checkpoint);
    farcall_register_overlay(L, 0x3244, A15_energy);
    farcall_register_overlay(L, 0x326a, A16_extra_life);
    farcall_register_overlay(L, 0x3290, A17_big_bonus);
    farcall_register_overlay(L, 0x32b6, A18_time_bonus);
    farcall_register_overlay(L, 0x3374, ovl_spring);
    farcall_register_overlay(L, 0x3403, l4_door);
    farcall_register_overlay(L, 0x35e1, ovl_charger);
    farcall_register_overlay(L, 0x3765, l4_jumper);
    farcall_register_overlay(L, 0x3981, l4_watcher);
    farcall_register_overlay(L, 0x3b71, l4_state);
    farcall_register_overlay(L, 0x3e53, l4_llama);
    farcall_register_overlay(L, 0x3f05, l4_quest);
}
