/* Overlay L1 (DAT 77, level index 0): "Prijskoe" — the cow lost her 3 spots.
 * Spec: teardown/60-overlays.md §5; listing extracted/ovl_dis/L1.asm (offsets cited per line). */
#include "ovl_story.h"

/* L1:0cad — per-frame entry (+8): quest icon (desc 0ce4) + remaining spots 3 - [0x1475]. */
static void l1_entry(void) { ovl_hud(3, 0x0ce4); }

/* L1:0e6a — SCRIPTWALKER: script motion with tile collision; airborne -> vy = 2. */
static void l1_scriptwalker(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 0eac */
    A6_touch();                                               /* 0e83 */
    A3_motion_step();                                         /* 0e87 */
    A4_map_move();                                            /* 0e8b */
    R.bx &= 1;                                                /* 0e8e and bx,1 */
    if (R.bx) {
        R.bx = 2;
        BPW_SET(0x15f, R.bx);                                 /* 0e96 vy = 2 */
    }
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 0ea6 */
    TAIL(A1_draw);
}

/* L1:1237 — F-HOPWALKER, L1 version ("trotting udder"): idle until the player is within 16 px,
 * a scripted pre-jump, then walks toward the player and jumps off ledges. */
static void l1_hopwalker(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1234 */
    A6_touch();                                               /* 1250 */
    R.cx = BPW(0x14f);
    if (R.cx == 0) {                                          /* 1258 je 1270: idle */
        R.bx = (uint16_t)(R.ax + 0x10 - dgw(0x00e8));         /* 1270 */
        if (R.bx < 0x20) A7_next_anim();                      /* 1279 jae 1282 / 127e */
        goto anim_draw;                                       /* 1282 jmp 134b */
    }
    R.cx = (uint16_t)(R.cx - 6);
    if (R.cx == 0) {                                          /* 125c anim 6: scripted hop */
        A3_motion_step();                                     /* 1262 */
        A4_map_move();                                        /* 1266 */
        int c = R.bx & 1;
        R.bx = (uint16_t)(R.bx >> 1);                         /* 1269 shr bx,1; jae 127e */
        if (!c) { A7_next_anim(); goto anim_draw; }           /* landed: position not stored */
        goto store;                                           /* 126d jmp 1341 */
    }
    /* 1285: common walking part */
    if (!(BPB(0x16f) & 1)) {                                  /* supported */
        uint8_t t = (uint8_t)(BPB(0x170) - 1);                /* 128d dec byte [bp+170]; jns */
        BPB_SET(0x170, t);
        if (t & 0x80) {
            BPB_SET(0x170, 0x20);
            R.cx = (uint16_t)(R.ax - dgw(0x00e8));            /* 129a; js 12aa */
            if (R.cx & 0x8000) { R.bx = 3; R.cx = 0x12; }     /* 12aa */
            else               { R.bx = 0; R.cx = 0x0c; }     /* 12a2 */
            BPW_SET(0x171, R.bx);                             /* 12b0 */
            BPW_SET(0x14d, R.cx);
            R.bx = sar16((uint16_t)(R.bx - 1), 1);            /* dec bx; sar bx,1 */
            BPW_SET(0x15b, R.bx);                             /* vx = -1 / +1 */
        }
    }
    ovl_grav(0x2000);                                         /* 12c2..12f4 (inline) */
    R.bx = (uint16_t)(BPW(0x15b) + R.ax);                     /* 12f6 */
    A4_map_move();                                            /* 12fe */
    if (R.bx & 1) {                                           /* 1301: airborne */
        int old = BPB(0x16f) & 1;                             /* 1307 test; mov [bp+16f],bl */
        BPB_SET(0x16f, (uint8_t)R.bx);
        if (!old) {                                           /* left the ground: jump */
            uint16_t ax = R.ax;
            A7_next_anim();                                   /* 1316 */
            R.ax = ax;
            R.bx = 0xfffd;
            BPW_SET(0x15f, R.bx);
            R.bx = 0;
            BPW_SET(0x15d, 0);
        }
        R.bx = (uint16_t)(BPW(0x15f) + 3);                    /* 1329 */
        if (R.bx != 0) R.bx = (uint16_t)((R.bx >> 2) + 1);    /* shr bx,2; inc bx */
        R.bx = (uint16_t)(R.bx + BPW(0x171));                 /* 1337 */
        BPB_SET(0x13c, (uint8_t)R.bx);                        /* frame */
    } else {                                                  /* 1352 */
        int old = BPB(0x16f) & 1;
        BPB_SET(0x16f, (uint8_t)R.bx);
        if (old) {                                            /* landed */
            uint16_t ax = R.ax;
            A7_next_anim();                                   /* 1361 */
            R.ax = ax;
        }
    }
store:
    BPW_SET(0x153, R.ax);                                     /* 1341 */
    BPW_SET(0x157, R.dx);
anim_draw:
    A2_anim_step();                                           /* 134b */
    TAIL(A1_draw);
}

/* L1:1431 — LEAPER: crouch (anims >= 0xc), jump straight up with vy = -5, land, repeat.
 * Quirk kept: in the crouch branch API2 runs twice per tick (1459 and 14d1). */
static void l1_leaper(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 142e */
    A6_touch();                                               /* 144a */
    R.cx = BPW(0x14f);
    if (S16(R.cx) >= 0x0c) {                                  /* 1452 cmp cx,0ch; jl */
        uint16_t ax = R.ax;                                   /* 1457 push ax */
        A2_anim_step();                                       /* 1459 */
        if (R.cf) {
            R.ax = (uint16_t)(BPW(0x171) + BPW(0x171));       /* 145e (v2 is never set: anim 0) */
            A8_set_anim();
            R.bx = 0xfffb;
            BPW_SET(0x15f, R.bx);                             /* vy = -5 */
            R.bx = 0;
            BPW_SET(0x15d, 0);
        }
        R.ax = ax;                                            /* 1478 pop ax */
        goto store;                                           /* 1479 jmp 14c6 */
    }
    ovl_grav(0x2000);                                         /* 147c..14ae (inline) */
    R.bx = (uint16_t)(R.ax + BPW(0x15b));                     /* 14b0 */
    A4_map_move();                                            /* 14b8 */
    if (R.bx & 1) {
        BPB_SET(0x16f, (uint8_t)R.bx);                        /* 14c1 airborne */
    } else {                                                  /* 14d7 landed -> crouch */
        uint16_t ax = R.ax;
        R.ax = (uint16_t)(BPW(0x171) + BPW(0x171) + 0x0c);
        A8_set_anim();
        R.ax = ax;
    }
store:
    BPW_SET(0x153, R.ax);                                     /* 14c6 */
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 14d1 */
    TAIL(A1_draw);
}

/* L1:178f — COW (goal): with 3 spots, touching her plays the happy anim 0xc; at its end
 * the last frame is frozen and the level is done ([0xc2] = 2). */
static void l1_cow(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 178c */
    R.bx = BPW(0x14f);
    if (R.bx == 0x0c) {                                       /* 17ac je 17e4 */
        A2_anim_step();                                       /* 17e5 */
        if (R.cf) {
            BPW_SET(0x14b, (uint16_t)(BPW(0x14b) | 0x100));   /* 17ea hold */
            dgw_set(0x00c2, 2);                               /* level completed */
        }
        TAIL(A1_draw);
    }
    if (dgw(0x1475) == 3) {                                   /* 17b1 */
        A6_touch();                                           /* 17b9 */
        if (!R.zf) {                                          /* 17bc je 17fa */
            dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x180));
            DB(0x00bd) |= 8;
            R.ax = 0x0c;
            A8_set_anim();                                    /* 17cd */
            R.ax = BPW(0x153);
            R.dx = BPW(0x157);
            R.bx = 0x24;
            A20_add_score();                                  /* 17de */
            TAIL(A1_draw);
        }
    }
    A2_anim_step();                                           /* 17fb */
    if (R.cf) {
        uint16_t v = (uint16_t)(BPW(0x16d) - 1);              /* 1800 dec [bp+16d]; jns 182f */
        BPW_SET(0x16d, v);
        if (!(v & 0x8000)) {
            A7_next_anim();                                   /* 182f */
        } else {
            R.ax = BPW(0x14f);                                /* 1807 */
            R.dx = 2;
            R.cx = 7;
            R.ax ^= 6;                                        /* 1812 xor ax,6; je 181d */
            if (R.ax != 0) { R.dx = 1; R.cx = 1; }
            R.cx &= dgw(0x00d6);                              /* 181d */
            R.cx = (uint16_t)(R.cx + R.dx);
            BPW_SET(0x16d, R.cx);
            A8_set_anim();                                    /* 1829: AX = anim ^ 6 */
        }
    }
    TAIL(A1_draw);                                            /* 1833 */
}

/* L1:1861 — F-QUEST cow spot. */
static void l1_quest(void) { ovl_quest(0x1c, 8, Q_SFX_BEFORE); }
/* L1:19f1 — F-DOOR. */
static void l1_door(void) { ovl_door(DOOR_KEY); }

void ovl_L1_register(void) {
    const int L = LVL_L1;
    farcall_register_overlay(L, 0x0008, l1_entry);            /* +8 JMP 0cad */
    farcall_register_overlay(L, 0x0c12, A14_checkpoint);      /* jmp API14 */
    farcall_register_overlay(L, 0x0c38, A15_energy);          /* jmp API15 */
    farcall_register_overlay(L, 0x0c84, A17_big_bonus);       /* jmp API17 */
    farcall_register_overlay(L, 0x0caa, A18_time_bonus);      /* jmp API18 */
    farcall_register_overlay(L, 0x0e6a, l1_scriptwalker);
    farcall_register_overlay(L, 0x0ee6, ovl_spring);
    farcall_register_overlay(L, 0x1025, ovl_roller);
    farcall_register_overlay(L, 0x1237, l1_hopwalker);
    farcall_register_overlay(L, 0x1431, l1_leaper);
    farcall_register_overlay(L, 0x178f, l1_cow);
    farcall_register_overlay(L, 0x1861, l1_quest);
    farcall_register_overlay(L, 0x1967, ovl_turnwalker);
    farcall_register_overlay(L, 0x19f1, l1_door);
}
