/* Overlay L5 (DAT 81, level index 8): Dracula / grandma — coins into the flying pig, the key
 * to the tower.  Spec: teardown/60-overlays.md §9; listing extracted/ovl_dis/L5.asm.
 * L5:373b (F-STATE copy, table 3763, cases 377d/37ab/37d7/3801/380d) is unreferenced: not ported. */
#include "ovl_story.h"

/* L5:2639 — per-frame entry (+8): coin icon (desc 2670), 9 - [0x1475]. */
static void l5_entry(void) { ovl_hud(9, 0x2670); }

/* L5:2cd9 — DROPPER: drops an object (template 2d38 / 2d4c) when the player is below
 * within +/-20 px, while in anims 0 / 6. */
static void l5_dropper(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2cd6 */
    A6_touch();                                               /* 2cf2 */
    if (!(S16(BPW(0x14f)) > 6)) {                             /* 2cf5 cmp [bp+14f],6; jg 2d31 */
        R.dx = (uint16_t)(R.dx - dgw(0x00ec));                /* 2cfd sub dx,[ec]; jns 2d31 */
        if (R.dx & 0x8000) {
            R.ax = cwd_abs((uint16_t)(R.ax - dgw(0x00e8)));   /* 2d03 */
            if (!(R.ax > 0x14)) {                             /* 2d0c cmp ax,14h; ja (unsigned) */
                R.si = (BPW(0x14f) == 0) ? 0x2d38 : 0x2d4c;   /* 2d11 */
                R.ax = BPW(0x153);                            /* 2d1f */
                R.dx = BPW(0x157);
                A21_spawn();                                  /* 2d2a */
                A7_next_anim();                               /* 2d2e */
            }
        }
    }
    A2_anim_step();                                           /* 2d32 */
    TAIL(A1_draw);
}

/* L5:2d99 — dropped object (templates 2d38 / 2d4c): F-PATHCOL without API2. */
static void l5_drop(void) { ovl_pathcol(0); }

/* L5:2dfb — COIN: quest item (anim 0); with anim != 0 (template 3459) the coin falls into the
 * pig and despawns at the pig's y ([0x513d]). */
static void l5_coin(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 2df8 */
    if (BPW(0x14f) != 0) {                                    /* 2e13 */
        ovl_grav(0x2000);                                     /* 2e1b call 3872 */
        if (S16(R.cx) >= S16(dgw(0x513d))) TAIL(A10_despawn); /* 2e1e cmp cx,[513d]; jge 2df8 */
        BPW_SET(0x157, R.cx);
        TAIL(A1_draw);
    }
    A6_touch();                                               /* 2e2d */
    if (!R.zf) {                                              /* 2e30 je 2e44 */
        R.bx = 0x20;
        A20_add_score();                                      /* 2e36 */
        dgw_set(0x5141, (uint16_t)(dgw(0x5141) + 1));         /* coins carried */
        dgw_set(0x1475, (uint16_t)(dgw(0x1475) + 1));         /* coins found (HUD) */
        TAIL(A11_kill);
    }
    uint16_t ax = R.ax, dx = R.dx;                            /* 2e44 */
    R.ax = (uint16_t)(R.ax - 8);
    R.dx = (uint16_t)(R.dx - 8);
    A19_sparkle();                                            /* 2e4d */
    R.dx = dx; R.ax = ax;
    TAIL(A1_draw);
}

/* L5:2eab — KEY (tower key).  param 0: the fixed key, takeable once 9 coins are fed and no
 * dropped key is alive; param 1 (template 346d): the key dropped by the pig, falls first. */
static void l5_key(void) {
    if (!ovl_keep()) {                                        /* 2e9a */
        if (BPW(0x16b) != 0) dgw_set(0x5143, 0);
        TAIL(A10_despawn);
    }
    if (BPW(0x16b) != 0) {                                    /* 2ec3 */
        A3_motion_step();                                     /* 2ecc */
        A4_map_move();                                        /* 2ed0 */
        if (R.bx & 1) goto store;                             /* 2ed3 still falling */
    } else {
        if (!(dgw(0x513b) == 9 && dgw(0x5143) == 0)) return;  /* 2ef7..2f05 retf (invisible) */
    }
    A6_touch();                                               /* 2ed9 */
    if (R.bx != 0) {                                          /* 2edd or bx,bx; je 2eea */
        dgw_set(0x513b, 0x0a);                                /* key taken */
        TAIL(A10_despawn);                                    /* 2ee7 */
    }
store:
    BPW_SET(0x153, R.ax);                                     /* 2eea */
    BPW_SET(0x157, R.dx);
    TAIL(A1_draw);
}

/* L5:319b — GRANDMA (goal): touching her starts her anim; 201 ticks later the level is done. */
static void l5_grandma(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 3198 */
    if (BPW(0x14f) != 0) {                                    /* 31b3 jne 31ed */
        uint16_t v = (uint16_t)(BPW(0x16b) - 1);              /* 31ed dec; jns 31ea */
        BPW_SET(0x16b, v);
        if (v & 0x8000) dgw_set(0x00c2, 2);                   /* level completed */
        TAIL(A1_draw);
    }
    A6_touch();                                               /* 31bc */
    if (R.bx != 0) {                                          /* 31bf or bx,bx; je 31ea */
        DB(0x00bd) |= 8;
        dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x180));
        BPW_SET(0x16b, 0xc8);
        A7_next_anim();                                       /* 31d6 */
        R.bx = 0x24;
        R.ax = BPW(0x153);
        R.dx = BPW(0x157);
        A20_add_score();                                      /* 31e7 */
    }
    TAIL(A1_draw);
}

/* L5:3261 — "DRACULA": watches the player (frame 1 within 60 px); after the API17 object is
 * taken ([0x2d84]) it waits param (150) ticks, hands its y-0x40 to the pig and is removed. */
static void l5_dracula(void) {
    if (dgw(0x513f) != 0) TAIL(A10_despawn);                  /* 3261 */
    if (dgw(0x2d84) != 0) {                                   /* 3268 jne 32b8 */
        BPB_SET(0x13c, 1);                                    /* 32b8 */
        uint16_t v = (uint16_t)(BPW(0x16b) - 1);              /* 32be dec; jns 32b5 */
        BPW_SET(0x16b, v);
        if (!(v & 0x8000)) TAIL(A1_draw);
        dgw_set(0x513f, 1);                                   /* 32c5 tell the pig */
        R.dx = (uint16_t)(R.dx - 0x40);
        dgw_set(0x513d, R.dx);
        TAIL(A11_kill);                                       /* 32d2 */
    }
    A6_touch();                                               /* 3270 */
    BPB_SET(0x173, 0x0f);                                     /* hits: never dies */
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 3279: window check after A6 */
    R.cx = 0;                                                 /* 3291 */
    R.bx = R.dx;
    R.ax = cwd_abs1((uint16_t)(R.ax - dgw(0x00e8)));          /* 3295 cwd; xor ax,dx */
    if (!(S16(R.ax) > 0x3c)) {                                /* 329c jg 32b0 */
        R.ax = cwd_abs1((uint16_t)(R.bx - dgw(0x00ec)));      /* 32a1 */
        if (!(S16(R.ax) > 0x3c)) R.cx = (uint16_t)(R.cx + 1); /* 32af inc cx */
    }
    BPB_SET(0x13c, (uint8_t)R.cx);                            /* 32b0 */
    TAIL(A1_draw);
}

/* L5:33ae — PIG: state in [0x513f]: 0 dormant (RETF, invisible), 1 appear at Dracula's y-0x40,
 * 2 fly up along the script to y = 0x43f, >= 3 feedable (one coin per contact tick). */
static void l5_pig(void) {
    if (!ovl_keep()) {                                        /* 339e */
        if (dgw(0x513f) != 0) dgw_set(0x513f, 3);
        TAIL(A10_despawn);
    }
    R.bx = (uint16_t)(dgw(0x513f) - 1);                       /* 33c6 mov bx,[513f]; dec bx */
    if (R.bx & 0x8000) return;                                /* 33cb js 3423: retf */
    if (R.bx == 0) {                                          /* 33cd je 3424: appear */
        uint16_t ax = R.ax;
        R.ax = 0x0c;
        A8_set_anim();                                        /* 3429 */
        R.ax = ax;
        R.dx = dgw(0x513d);                                   /* 342d */
        dgw_set(0x513f, (uint16_t)(dgw(0x513f) + 1));
        dgw_set(0x513d, 0);
        goto rise;
    }
    R.bx = (uint16_t)(R.bx - 1);                              /* 33cf dec bx; je 343b */
    if (R.bx == 0) goto rise;
    dgw_set(0x513d, R.dx);                                    /* 33d2 feedable */
    A6_touch();                                               /* 33d7 */
    if (R.bx != 0) {                                          /* 33da or bx,bx; je 340e */
        BPB_SET(0x173, 0x0f);
        if (dgw(0x5141) != 0) {                               /* 33e4 */
            dgw_set(0x5141, (uint16_t)(dgw(0x5141) - 1));
            dgw_set(0x513b, (uint16_t)(dgw(0x513b) + 1));
            uint16_t ax = R.ax, dx = R.dx;                    /* 33f3 push ax; push dx */
            R.si = 0x3459;                                    /* falling coin */
            if (dgw(0x513b) == 9) {                           /* 33f8 */
                R.si = 0x346d;                                /* the key */
                dgw_set(0x5143, 1);
            }
            A21_spawn();                                      /* 3409 */
            R.dx = dx; R.ax = ax;
        }
    }
    A3_motion_step();                                         /* 340e */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    A2_anim_step();                                           /* 341c */
    TAIL(A1_draw);
rise:
    A3_motion_step();                                         /* 343b */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    if (R.cx == 0x43f) {                                      /* 3449 */
        dgw_set(0x513f, (uint16_t)(dgw(0x513f) + 1));
        A7_next_anim();                                       /* 3454 */
    }
    A2_anim_step();                                           /* 341c */
    TAIL(A1_draw);
}

/* L5:353c — F-LIFT: two lifts (param 1 / 2), only one may exist (owner [0x5149]).  The lift
 * runs its script only while the player stands on it (353c..3570: API22 ZF -> straight to
 * API1), and then carries the player by the same delta. */
static void l5_lift(void) {
    if (!ovl_keep()) goto release;                            /* 3546 jae 3524 */
    R.cx = BPW(0x16b);                                        /* 3554 */
    R.bx = dgw(0x5149);
    if (R.bx != 0 && R.cx != R.bx) goto release;              /* 355d / 3561 */
    dgw_set(0x5149, R.cx);                                    /* 3565 */
    A22_platform();                                           /* 356a */
    R.cx = 0;
    if (R.zf) TAIL(A1_draw);                                  /* 3570 je 359d */
    dgw_set(0x00ec, R.si);                                    /* player y = top - 1 */
    R.cx = 1;
    {
        uint16_t ax = R.ax, dx = R.dx, cx = R.cx;             /* 3579 push ax,dx,cx */
        A3_motion_step();                                     /* 357d */
        BPW_SET(0x153, R.bx);
        BPW_SET(0x157, R.cx);
        R.si = cx; R.dx = dx; R.ax = ax;                      /* 358a pop si,dx,ax */
    }
    if (R.si != 0) {                                          /* 358d */
        R.bx = (uint16_t)(R.bx - R.ax);
        R.cx = (uint16_t)(R.cx - R.dx);
        dgw_set(0x00e8, (uint16_t)(dgw(0x00e8) + R.bx));      /* carry the player */
        dgw_set(0x00ec, (uint16_t)(dgw(0x00ec) + R.cx));
    }
    TAIL(A1_draw);                                            /* 359d */
release:
    R.bx = dgw(0x5149);                                       /* 3524 */
    if (R.bx != 0 && BPW(0x16b) == R.bx) dgw_set(0x5149, 0);
    TAIL(A10_despawn);                                        /* 3539 */
}

static void l5_door2(void) { ovl_door(DOOR_L5); }                 /* L5:273d */
static void l5_towerdoor(void) { ovl_door(DOOR_TOWER); }          /* L5:2835 */
static void l5_pathcol(void) { ovl_pathcol(1); }                  /* L5:2b93 */

void ovl_L5_register(void) {
    const int L = LVL_L5;
    farcall_register_overlay(L, 0x0008, l5_entry);            /* +8 JMP 2639 */
    farcall_register_overlay(L, 0x259e, A14_checkpoint);
    farcall_register_overlay(L, 0x25c4, A15_energy);
    farcall_register_overlay(L, 0x25ea, A16_extra_life);
    farcall_register_overlay(L, 0x2610, A17_big_bonus);
    farcall_register_overlay(L, 0x2636, A18_time_bonus);
    farcall_register_overlay(L, 0x26ae, ovl_spring);
    farcall_register_overlay(L, 0x273d, l5_door2);
    farcall_register_overlay(L, 0x2835, l5_towerdoor);
    farcall_register_overlay(L, 0x2921, ovl_seeker);
    farcall_register_overlay(L, 0x2b93, l5_pathcol);
    farcall_register_overlay(L, 0x2cd9, l5_dropper);
    farcall_register_overlay(L, 0x2d99, l5_drop);             /* templates 2d38 / 2d4c */
    farcall_register_overlay(L, 0x2dfb, l5_coin);             /* + template 3459 */
    farcall_register_overlay(L, 0x2eab, l5_key);              /* + template 346d */
    farcall_register_overlay(L, 0x30a7, ovl_charger);
    farcall_register_overlay(L, 0x319b, l5_grandma);
    farcall_register_overlay(L, 0x3261, l5_dracula);
    farcall_register_overlay(L, 0x33ae, l5_pig);
    farcall_register_overlay(L, 0x353c, l5_lift);
}
