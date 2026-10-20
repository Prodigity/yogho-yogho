/* Overlay L6 (DAT 82, level index 10): the mermaid — find 9 music notes; game finale.
 * Spec: teardown/60-overlays.md §10; listing extracted/ovl_dis/L6.asm.
 * Every L6 handler except 2d97, 307f, 3173 starts with "cmp [5143],0; jne -> jmp API10":
 * the whole cast disappears once the ending starts. */
#include "ovl_story.h"

/* "cmp word [5143],0; jne despawn" prefix (L6:27ba, 2863, 2915, 2a0d, 2c3b, 32ef, 3401, 367f). */
static int l6_ending(void) {
    if (dgw(0x5143) != 0) { A10_despawn(); return 1; }
    return 0;
}

/* L6:2745 — per-frame entry (+8): note icon (desc 277c), 9 - [0x1475]. */
static void l6_entry(void) { ovl_hud(9, 0x277c); }

static void l6_spring(void) { if (l6_ending()) return; ovl_spring(); }        /* L6:27ba */
static void l6_door(void)   { if (l6_ending()) return; ovl_door(DOOR_KEY); }  /* L6:2915 */
static void l6_seeker(void) { if (l6_ending()) return; ovl_seeker(); }        /* L6:2a0d */
static void l6_quest(void)  { ovl_quest(0x20, 8, Q_TESTBX); }                 /* L6:3173 */

/* L6:2863 — CLOUD: while the player is inside the column, lifts him by
 * max(0, rand(0..15) - 8 + depth) * param / 4096 px per tick (16.16 on [0xea]/[0xec]). */
static void l6_cloud(void) {
    if (l6_ending()) return;                                  /* 2863 */
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2860 */
    A6_touch();                                               /* 2883 */
    if (R.bx != 0) {                                          /* 2886 or bx,bx; je 28c4 */
        BPW_SET(0x169, 0);                                    /* clear the touch latch */
        R.cx = (uint16_t)(rw(ovl_seg(), (uint16_t)(R.di + 7)) + R.dx - dgw(0x00ec));   /* 2891 DI = S.sprite (A6) */
        R.cx = (uint16_t)-R.cx;                               /* 289b neg cx: depth */
        A23_random();                                         /* 289e */
        R.ax &= 0x0f;
        R.ax = (uint16_t)(R.ax - 8);
        R.ax = (uint16_t)(R.ax + R.cx);                       /* 28a7 add ax,cx; jns */
        if (R.ax & 0x8000) R.ax = 0;
        R.dx = BPW(0x16b);                                    /* 28ad */
        {
            uint32_t p = (uint32_t)R.ax * R.dx;               /* 28b2 mul dx */
            R.ax = (uint16_t)p;
            R.dx = (uint16_t)(p >> 16);
        }
        R.cx = R.ax;
        R.ax = (uint16_t)(R.ax << 4);                         /* 28b6 */
        R.cx = (uint16_t)(R.cx >> 12);                        /* 28b9 shr cx,0ch */
        {
            uint16_t m = dgw(0x00ea);                         /* 28bc sub [ea],ax; sbb [ec],cx */
            dgw_set(0x00ea, (uint16_t)(m - R.ax));
            dgw_set(0x00ec, (uint16_t)(dgw(0x00ec) - R.cx - (m < R.ax)));
        }
    }
    A2_anim_step();                                           /* 28c5 */
    TAIL(A1_draw);
}

/* L6:2c3b — ALBATROSS: waits (anim 0) until the player lands on it, then flies its script
 * carrying the player; after param ticks (if param >= 0) next anim. */
static void l6_albatross(void) {
    if (l6_ending()) return;                                  /* 2c3b */
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2c38 */
    if (BPW(0x14f) == 0) {                                    /* 2c5a jne 2c73 */
        A22_platform();                                       /* 2c63 */
        if (!R.zf) {                                          /* 2c66 je 2cbb */
            dgw_set(0x00ec, R.si);
            A7_next_anim();                                   /* 2c6d take off */
        }
    } else {
        A22_platform();                                       /* 2c74 */
        R.cx = 0;
        if (!R.zf) {                                          /* 2c7a je 2c83 */
            dgw_set(0x00ec, R.si);
            R.cx = 1;
        }
        {
            uint16_t ax = R.ax, dx = R.dx, cx = R.cx;         /* 2c83 push ax,dx,cx */
            A3_motion_step();                                 /* 2c87 */
            BPW_SET(0x153, R.bx);
            BPW_SET(0x157, R.cx);
            R.si = cx; R.dx = dx; R.ax = ax;                  /* 2c94 pop si,dx,ax */
        }
        if (R.si != 0) {                                      /* 2c97 carry the player */
            R.bx = (uint16_t)(R.bx - R.ax);
            R.cx = (uint16_t)(R.cx - R.dx);
            dgw_set(0x00e8, (uint16_t)(dgw(0x00e8) + R.bx));
            dgw_set(0x00ec, (uint16_t)(dgw(0x00ec) + R.cx));
        }
        R.bx = BPW(0x16b);                                    /* 2ca7 */
        if (!(R.bx & 0x8000)) {                               /* or bx,bx; js 2cbb */
            uint16_t v = (uint16_t)(BPW(0x16b) - 1);          /* 2cb0 dec; jne 2cbb */
            BPW_SET(0x16b, v);
            if (v == 0) A7_next_anim();                       /* 2cb8 */
        }
    }
    A2_anim_step();                                           /* 2cbb */
    TAIL(A1_draw);
}

/* L6:2d97 — MERMAID (goal): with 9 notes, walking within 48 px pans the camera (via the player
 * camera offset [0xde]/[0xe0]) to (0x9b0, 0x110), then starts the ending ([0x5143] = 1). */
static void l6_mermaid(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2d94 */
    if (BPW(0x14f) == 0) {                                    /* 2daf je 2e1c */
        R.ax = cwd_abs1((uint16_t)(R.ax - dgw(0x00e8)));      /* 2e1c cwd; xor ax,dx */
        if (S16(R.ax) > 0x30) TAIL(A1_draw);                  /* 2e23 jg 2e19 */
        if (dgw(0x1475) == 9) {                               /* 2e28 je 2e41 */
            dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x180));
            R.ax = 0x0c;
            A8_set_anim();                                    /* 2e4b */
            TAIL(A1_draw);
        }
        A7_next_anim();                                       /* 2e30 */
        TAIL(A1_draw);
    }
    if (BPW(0x14f) == 6) {                                    /* 2db7 je 2e35 */
        A2_anim_step();
        if (R.cf) A7_next_anim();
        TAIL(A1_draw);
    }
    if (BPW(0x16d) == 0) {                                    /* 2dbf jne 2e0f */
        R.bx = 0;
        if (dgw(0x2cb4) != 0x9b0) {                           /* 2dc9 */
            if (S16(dgw(0x2cb4)) > 0x9b0) dgw_set(0x00de, (uint16_t)(dgw(0x00de) - 1));   /* 2ddb */
            else                          dgw_set(0x00de, (uint16_t)(dgw(0x00de) + 1));   /* 2dd3 */
            R.bx++;
        }
        if (dgw(0x2cb6) != 0x110) {                           /* 2de0 */
            if (S16(dgw(0x2cb6)) > 0x110) dgw_set(0x00e0, (uint16_t)(dgw(0x00e0) - 1));   /* 2df2 */
            else                          dgw_set(0x00e0, (uint16_t)(dgw(0x00e0) + 1));   /* 2dea */
            R.bx++;
        }
        if (R.bx == 0) {                                      /* 2df7 */
            BPW_SET(0x16d, (uint16_t)(BPW(0x16d) + 1));
            dgw_set(0x5143, 1);                               /* ending: engine 160a:0c62 takes over */
            dgw_set(0x5145, 0x64);
        }
        TAIL(A1_draw);
    }
    A2_anim_step();                                           /* 2e0f */
    if (R.cf) A7_next_anim();
    TAIL(A1_draw);                                            /* 2e19 */
}

/* L6:307f — FINALE: invisible until the engine sets [0x5143] = 2; shows frames 1..8 for
 * cs:30f3[f]+1 ticks each, holds 8 while [0x5145] counts down, then the level is done. */
static void l6_finale(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 307c */
    if (dgw(0x5143) == 0 || dgw(0x5143) == 1) return;         /* 3097 / 309e: retf */
    R.bx = 8;                                                 /* 30a5 */
    R.cx = BPW(0x16d);
    if (R.cx == 9) {                                          /* 30ad je 30e5 */
        uint16_t v = (uint16_t)(dgw(0x5145) - 1);             /* 30e5 dec [5145]; jns 30cd */
        dgw_set(0x5145, v);
        if (v & 0x8000) dgw_set(0x00c2, 2);                   /* 30eb level (game) completed */
        goto reload;
    }
    R.bx = R.cx;                                              /* 30b2 */
    {
        uint16_t v = (uint16_t)(BPW(0x16b) - 1);              /* 30b4 dec [bp+16b]; jns 30db */
        BPW_SET(0x16b, v);
        if (!(v & 0x8000)) goto show;
    }
    BPW_SET(0x16d, (uint16_t)(BPW(0x16d) + 1));               /* 30bb */
    R.bx = BPW(0x16d);
    if (R.bx == 9) R.bx = 8;                                  /* 30c5 */
reload:
    R.si = (uint16_t)(R.bx + R.bx);                           /* 30cd */
    R.cx = CSW((uint16_t)(R.si + 0x30f3));
    BPW_SET(0x16b, R.cx);
show:
    BPB_SET(0x13c, (uint8_t)R.bx);                            /* 30db */
    A1_draw();                                                /* 30e1 push cs; call API1 */
    return;                                                   /* 30e4 retf */
}

/* L6:32ef — SHOOTER: walks its script with collision (turns at walls / ledges), fires a shot
 * (template 3357 / 336b) every 31 ticks. */
static void l6_shooter(void) {
    if (l6_ending()) return;                                  /* 32ef */
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 32ec */
    A6_touch();                                               /* 330f */
    A3_motion_step();                                         /* 3313 */
    A4_map_move();                                            /* 3317 */
    if (R.bx & 3) {                                           /* 331a */
        uint16_t ax = R.ax;
        A7_next_anim();                                       /* 3322 */
        R.ax = ax;
    }
    uint16_t v = (uint16_t)(BPW(0x16b) - 1);                  /* 3326 dec; jns 3346 */
    BPW_SET(0x16b, v);
    if (v & 0x8000) {
        BPW_SET(0x16b, 0x1e);
        R.si = (BPW(0x14f) == 0) ? 0x3357 : 0x336b;           /* 3334 */
        A21_spawn();                                          /* 3343 at the new (AX,DX) */
    }
    BPW_SET(0x153, R.ax);                                     /* 3346 */
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 3351 */
    TAIL(A1_draw);
}

/* L6:3401 — SHOT (templates 3357 / 336b): follows its script for one anim cycle; harmless. */
static void l6_shot(void) {
    if (l6_ending()) return;                                  /* 3401 */
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 33fe */
    A3_motion_step();                                         /* 3421 */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    A2_anim_step();                                           /* 342f */
    if (R.cf) TAIL(A10_despawn);                              /* 3434 */
    TAIL(A1_draw);
}

/* ---- L6:367f F-STATE variant: jmp cs:[36ae + anim/2]; the table is read from MEM ---- */

/* L6:36c6 (anims 0x18, 0x1e) — leap: script x + gravity; frame from vy (table cs:3710). */
static void l6_st_leap(void) {
    A3_motion_step();                                         /* 36c7 */
    ovl_grav(0x2000);                                         /* 36ca call 37a1 */
    A4_map_move();                                            /* 36ce */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    {
        unsigned c = BPW(0x15d) >= 0x8000;                    /* 36db add si,8000h -> CF */
        R.si = (uint16_t)(BPW(0x15f) + 4 + c);                /* mov si,[bp+15f]; adc si,4 */
    }
    R.cx = (uint16_t)((R.cx & 0xff00) | CSB((uint16_t)(R.si + 0x3710)));   /* 36ec mov cl,cs:[si+3710] */
    BPB_SET(0x13c, (uint8_t)R.cx);
    if (!(R.bx & 1)) A7_next_anim();                          /* 36f6 landed */
    TAIL(A1_draw);                                            /* 370d -> 379e */
}

/* L6:3703 (anims 0x0c, 0x12, 0x24, 0x2a) — play the anim, next at its end. */
static void l6_st_anim(void) {
    A2_anim_step();                                           /* 3704 */
    if (R.cf) A7_next_anim();                                 /* 370a */
    TAIL(A1_draw);
}

/* L6:371e (anims 0, 6) — patrol along the script; random pauses; after a pause face the player
 * and attack (anim + 0xc) if already facing him within 120 px. */
static void l6_st_patrol(void) {
    if (BPW(0x16f) != 0) {                                    /* 371e je 375b */
        uint16_t v = (uint16_t)(BPW(0x16f) - 1);              /* 3726 dec; jne 379e */
        BPW_SET(0x16f, v);
        if (v != 0) TAIL(A1_draw);
        uint16_t ax = R.ax;                                   /* 372d push ax */
        R.bx = dgw(0x00e8);
        R.cx = 0;
        R.bx = (uint16_t)(R.bx - R.ax);                       /* 3734 sub bx,ax; js 373b */
        if (!(R.bx & 0x8000)) R.cx = 6;
        if (BPW(0x14f) == R.cx) {                             /* 373b */
            uint16_t dx = R.dx;                               /* push dx */
            R.ax = cwd_abs1(R.bx);                            /* 3743 cwd; xor ax,dx */
            R.dx = dx;                                        /* pop dx */
            if (!(R.ax > 0x78)) R.cx = (uint16_t)(R.cx + 0x0c);   /* 3749 cmp ax,78h; ja */
        }
        R.ax = R.cx;                                          /* 3751 */
        A8_set_anim();
        R.ax = ax;                                            /* 3757 pop ax */
        goto store;                                           /* jmp 3790 */
    }
    {
        uint16_t v = (uint16_t)(BPW(0x16d) - 1);              /* 375b dec; jns 377b */
        BPW_SET(0x16d, v);
        if (v & 0x8000) {
            R.bx = (uint16_t)((dgw(0x00d6) & 0x3f) + 0x40);   /* 3762 */
            BPW_SET(0x16d, R.bx);
            BPW_SET(0x16f, 0x20);
            goto anim;                                        /* 3778 jmp 379a */
        }
    }
    A3_motion_step();                                         /* 377b */
    A4_map_move();                                            /* 3780 */
    if (R.bx & 1) { A7_next_anim(); goto anim; }              /* 3783 / 378a */
store:
    BPW_SET(0x153, R.ax);                                     /* 3790 */
    BPW_SET(0x157, R.dx);
anim:
    A2_anim_step();                                           /* 379a */
    TAIL(A1_draw);                                            /* 379e */
}

/* L6:367f — F-STATE dispatcher. */
static void l6_state(void) {
    if (l6_ending()) return;                                  /* 367f */
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 367c */
    A6_touch();                                               /* 369f */
    R.bx = (uint16_t)(BPW(0x14f) >> 1);                       /* 36a2 shr bx,1 */
    uint16_t target = CSW((uint16_t)(R.bx + 0x36ae));         /* 36a9 jmp cs:[bx+36ae] */
    switch (target) {
    case 0x36c6: l6_st_leap(); return;
    case 0x3703: l6_st_anim(); return;
    case 0x371e: l6_st_patrol(); return;
    default:
        log_gap("L6:36a9 jump table: anim %04X -> unported target %04X", BPW(0x14f), target);
        return;
    }
}

void ovl_L6_register(void) {
    const int L = LVL_L6;
    farcall_register_overlay(L, 0x0008, l6_entry);            /* +8 JMP 2745 */
    farcall_register_overlay(L, 0x26aa, A14_checkpoint);
    farcall_register_overlay(L, 0x26d0, A15_energy);
    farcall_register_overlay(L, 0x26f6, A16_extra_life);
    farcall_register_overlay(L, 0x271c, A17_big_bonus);
    farcall_register_overlay(L, 0x2742, A18_time_bonus);
    farcall_register_overlay(L, 0x27ba, l6_spring);
    farcall_register_overlay(L, 0x2863, l6_cloud);
    farcall_register_overlay(L, 0x2915, l6_door);
    farcall_register_overlay(L, 0x2a0d, l6_seeker);
    farcall_register_overlay(L, 0x2c3b, l6_albatross);
    farcall_register_overlay(L, 0x2d97, l6_mermaid);
    farcall_register_overlay(L, 0x307f, l6_finale);
    farcall_register_overlay(L, 0x3173, l6_quest);
    farcall_register_overlay(L, 0x32ef, l6_shooter);
    farcall_register_overlay(L, 0x3401, l6_shot);             /* templates 3357 / 336b */
    farcall_register_overlay(L, 0x367f, l6_state);
}
