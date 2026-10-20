/* Overlay L2 (DAT 78, level index 2): Betty — bring her 3 cauliflowers.
 * Spec: teardown/60-overlays.md §6; listing extracted/ovl_dis/L2.asm (offsets cited per line). */
#include "ovl_story.h"

/* L2:0f2d — per-frame entry (+8): icon desc 0f64, 3 - [0x1475]. */
static void l2_entry(void) { ovl_hud(3, 0x0f64); }

/* L2:10a1 — LURKER: follows its script, rests, notices the player in front, then chases. */
static void l2_lurker(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 109e */
    A6_touch();                                               /* 10ba */
    if (!(BPW(0x16d) & 1)) {                                  /* 10bd */
        R.bx = BPW(0x14f);
        if (R.bx == 6 || R.bx == 0x18) {                      /* 10ce / 10d3 */
            uint16_t v = (uint16_t)(BPW(0x16f) - 1);          /* 10d8 dec [bp+16f]; jns */
            BPW_SET(0x16f, v);
            if (v & 0x8000) { A7_next_anim(); TAIL(A1_draw); }   /* 10df */
            A3_motion_step();                                 /* 10e6 */
            BPW_SET(0x153, R.bx);
            BPW_SET(0x157, R.cx);
            R.cx = (uint16_t)(R.bx - dgw(0x00e8));            /* 10f4 */
            R.bx = BPW(0x14f);
            if (S16(R.bx) < 0x12) {                           /* 10ff cmp bx,12h; jge */
                if (R.cx > 0x60) TAIL(A1_draw);               /* 1104 cmp cx,60h; ja (unsigned) */
                BPW_SET(0x16d, 1);
                R.ax = 0x24;
            } else {
                if (R.cx < 0xffa0) TAIL(A1_draw);             /* 1116 cmp cx,-60h; jb (unsigned) */
                BPW_SET(0x16d, 3);
                R.ax = 0x2a;
            }
            BPW_SET(0x171, R.cx);                             /* 1125 */
            A8_set_anim();
            TAIL(A1_draw);                                    /* 112e jmp 1206 */
        }
        R.bx = BPW(0x16b);                                    /* 1131 reload rest timer */
        BPW_SET(0x16f, R.bx);
        A3_motion_step();                                     /* 113c */
        BPW_SET(0x153, R.bx);
        BPW_SET(0x157, R.cx);
        TAIL(A1_draw);
    }
    /* 114c chase */
    R.bx = R.dx;
    R.cx = R.dx;
    R.bx = (uint16_t)(R.bx - dgw(0x00ec));
    if (S16(R.bx) > 8) {                                      /* 1157 jg 116b */
        uint16_t m = BPW(0x155);                              /* sub [bp+155],8000h; sbb cx,0 */
        BPW_SET(0x155, (uint16_t)(m - 0x8000));
        R.cx = (uint16_t)(R.cx - (m < 0x8000));
    } else if (!(S16(R.bx) > -8)) {                           /* 1159 cmp bx,-8; jg 1175 */
        uint32_t s = (uint32_t)BPW(0x155) + 0x8000;           /* 115e add; adc cx,0 */
        BPW_SET(0x155, (uint16_t)s);
        R.cx = (uint16_t)(R.cx + (s >> 16));
    }
    uint16_t cx = R.cx;                                       /* 1175 push cx */
    R.bx = BPW(0x171);
    int gt = S16(R.bx) > 0;                                   /* cmp bx,0 (flags kept over mov) */
    R.bx = R.ax;
    if (gt) {                                                 /* 118f sub [bp+151],4000h; sbb bx,1 */
        uint16_t m = BPW(0x151);
        BPW_SET(0x151, (uint16_t)(m - 0x4000));
        R.bx = (uint16_t)(R.bx - 1 - (m < 0x4000));
    } else {                                                  /* 1182 add [bp+151],4000h; adc bx,1 */
        uint32_t s = (uint32_t)BPW(0x151) + 0x4000;
        BPW_SET(0x151, (uint16_t)s);
        R.bx = (uint16_t)(R.bx + 1 + (s >> 16));
    }
    R.cx = cx;                                                /* 1199 pop cx */
    A4_map_move();                                            /* 119b */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    uint16_t v = (uint16_t)(BPW(0x16f) - 1);                  /* 11a8 dec [bp+16f]; jns */
    BPW_SET(0x16f, v);
    if (v & 0x8000) {
        BPW_SET(0x16f, 0x10);                                 /* re-aim */
        R.bx = (uint16_t)(R.ax - dgw(0x00e8));
        BPW_SET(0x171, R.bx);
    }
    R.bx = BPW(0x171);                                        /* 11c1 */
    if (R.bx == 0) TAIL(A1_draw);                             /* 11c9 je 11ff -> 1206 */
    int near_;
    if (S16(R.bx) > 0) { R.cx = 0; near_ = !(S16(R.bx) > 0x30); }   /* 11f5 */
    else               { R.cx = 6; near_ = !(S16(R.bx) < -0x30); }  /* 11cd */
    if (near_) {                                              /* 11d5 */
        R.cx = (uint16_t)(R.cx + 0x0c);
        BPW_SET(0x14d, R.cx);
        BPB_SET(0x13b, 2);                                    /* byte: last frame = 2 */
    } else {                                                  /* 11e6 */
        BPW_SET(0x14d, R.cx);
        BPW_SET(0x13b, 0);                                    /* word: last frame AND frame = 0 */
    }
    A2_anim_step();                                           /* 1202 */
    TAIL(A1_draw);
}

/* L2:1265 — DRIFTER: constant 16.16 drift (x speed = param), reflects off walls/floors. */
static void l2_drifter(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1262 */
    R.bx = BPW(0x16d);
    if (R.bx == 0) {                                          /* 1282 */
        R.bx = (uint16_t)((dgw(0x00d6) & 0x4000) - 0x2000);
        BPW_SET(0x16d, R.bx);
    }
    A6_touch();                                               /* 1298 */
    R.bx = BPW(0x16b);
    {
        uint32_t s = (uint32_t)BPW(0x151) + R.bx;             /* add [bp+151],bx */
        if (!(R.bx & 0x8000)) {
            BPW_SET(0x14d, 6);                                /* 12a4 */
            BPW_SET(0x151, (uint16_t)s);
            R.bx = (uint16_t)(R.ax + (s >> 16));              /* adc bx,0 */
        } else {
            BPW_SET(0x14d, 0);                                /* 12b8 */
            BPW_SET(0x151, (uint16_t)s);
            R.bx = (uint16_t)(R.ax - 1 + (s >> 16));          /* adc bx,-1 */
        }
    }
    R.cx = BPW(0x16d);
    {
        uint32_t s = (uint32_t)BPW(0x155) + R.cx;             /* add [bp+155],cx */
        BPW_SET(0x155, (uint16_t)s);
        if (!(R.cx & 0x8000)) R.cx = (uint16_t)(R.dx + (s >> 16));       /* 12d2 adc cx,0 */
        else                  R.cx = (uint16_t)(R.dx - 1 + (s >> 16));   /* 12df adc cx,-1 */
    }
    A4_map_move();                                            /* 12ea */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    if (R.bx & 4) BPW_SET(0x16b, (uint16_t)-BPW(0x16b));      /* 12f4 neg x speed */
    if (R.bx & 8) BPW_SET(0x16d, (uint16_t)-BPW(0x16d));      /* 1302 neg y speed */
    A2_anim_step();                                           /* 130e */
    TAIL(A1_draw);
}

/* L2:1435 — THROWER: walks until the player is near, then throws (template 1532 / 1546) on
 * the anim's one-shot trigger (A2 AH bit 2); param = number of throws. */
static void l2_thrower(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1432 */
    A6_touch();                                               /* 144e */
    uint16_t x = R.ax;                                        /* 1451 push ax */
    R.bx = BPW(0x16d);
    if (R.bx == 0) {                                          /* 1457: first frame, face the player */
        if (S16(R.ax) < S16(dgw(0x00e8))) {                   /* 145d sub bx,[e8]; jl */
            BPW_SET(0x16d, 0x8001);
            R.ax = 6;
        } else {
            BPW_SET(0x16d, 0x8000);
            R.ax = 0;
        }
        R.bx = (uint16_t)(x - dgw(0x00e8));
        A8_set_anim();                                        /* 147a */
    }
    R.ax = x;                                                 /* 147d pop ax; push ax */
    R.bx = BPW(0x14f);
    if (S16(R.bx) < 0x0c) {                                   /* 1484 cmp bx,0ch; jge */
        R.bx = (uint16_t)(R.ax - dgw(0x00e8));                /* 1489 */
        if (!(BPW(0x16d) & 1)) {
            if (!(S16(R.bx) > 0x80)) A7_next_anim();          /* 1498 cmp bx,80h; jg */
        } else {
            if (!(S16(R.bx) < -0x80)) A7_next_anim();         /* 14a5 cmp bx,-80h; jl */
        }
        R.ax = x;                                             /* 14ae pop ax */
        A3_motion_step();
        BPW_SET(0x153, R.bx);
        BPW_SET(0x157, R.cx);
        TAIL(A1_draw);
    }
    R.ax = x;                                                 /* 14c0 pop ax */
    if (S16(R.bx) >= 0x30) {                                  /* 14c1 cmp bx,30h; jge 1517 */
        A3_motion_step();                                     /* 1517 */
        BPW_SET(0x153, R.bx);
        BPW_SET(0x157, R.cx);
        A2_anim_step();
        if (R.cf) A7_next_anim();
        TAIL(A1_draw);
    }
    A2_anim_step();                                           /* 14c6 push ax; call API2 */
    if (R.cf) {
        uint16_t a = R.ax;
        A7_next_anim();                                       /* 14cf (push ax / pop ax) */
        R.ax = a;
    }
    int trig = AH(R) & 4;                                     /* 14d3 test ah,4 */
    R.ax = x;                                                 /* 14d6 pop ax */
    if (trig) {
        R.bx = BPW(0x16d);
        R.si = (R.bx & 1) ? 0x1546 : 0x1532;                  /* 14de */
        A21_spawn();                                          /* 14ee at (x, y) */
        uint16_t v = (uint16_t)(BPW(0x16b) - 1);              /* 14f1 dec [bp+16b]; jns 152f */
        BPW_SET(0x16b, v);
        if (!(v & 0x8000)) TAIL(A1_draw);
        R.ax = (uint16_t)(BPW(0x14f) + 0x0c);                 /* 14f9 */
        A8_set_anim();
        R.ax = x;
    }
    A3_motion_step();                                         /* 1506 */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    TAIL(A1_draw);
}

/* L2:195d — DROPPER: arms at random (p = 1/64), drops a ball (templates 1a1f / 1a0b). */
static void l2_dropper(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 195a */
    A6_touch();                                               /* 1976 */
    A3_motion_step();                                         /* 197a */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    R.bx = BPW(0x14f);
    if (R.bx == 0) {                                          /* 198e je 19d0 */
        if (BPW(0x14b) & 1) {                                 /* 19d0 */
            A2_anim_step();
            if (R.cf) {
                R.ax = (uint16_t)(dgw(0x00d8) & 2);           /* 19df */
                R.bx = R.ax;
                R.ax = (uint16_t)(R.ax + R.ax + R.bx + 6);    /* anim 6 or 0xc */
                A8_set_anim();
            }
        } else {
            A2_anim_step();                                   /* 19f5 */
            R.bx = (uint16_t)(dgw(0x00d6) & 0x3f);
            if (R.bx == 0) BPW_SET(0x14b, (uint16_t)(BPW(0x14b) | 1));   /* 1a02 arm */
        }
        TAIL(A1_draw);
    }
    if (S16(R.bx) >= 0x12) {                                  /* 1990 cmp bx,12h; jl 19a2 */
        A2_anim_step();
        if (R.cf) A7_next_anim();
        TAIL(A1_draw);
    }
    A2_anim_step();                                           /* 19a2 */
    if (R.cf) {
        R.bx = (uint16_t)(BPW(0x14f) - 6);
        R.si = (R.bx == 0) ? 0x1a1f : 0x1a0b;                 /* 19ad */
        R.ax = BPW(0x153);                                    /* 19bb */
        R.dx = BPW(0x157);
        A21_spawn();                                          /* 19c6 */
        A7_next_anim();                                       /* 19ca */
    }
    TAIL(A1_draw);
}

/* L2:1aa1 — BALL (spawned): horizontal drag with saw-tooth clamp, half gravity, frame from vy
 * (table cs:1b7b), no tile collision. */
static void l2_ball(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1a9e */
    A6_touch();                                               /* 1aba */
    if (BPW(0x16b) & 1) { R.bx = 0xf800; R.cx = 0xffff; }     /* 1ace */
    else                { R.bx = 0x0800; R.cx = 0; }          /* 1ac6 */
    uint32_t s = (uint32_t)R.bx + BPW(0x159);                 /* 1ad4 */
    R.bx = (uint16_t)s;
    BPW_SET(0x159, R.bx);
    R.cx = (uint16_t)(R.cx + BPW(0x15b) + (s >> 16));
    if (!(BPW(0x16b) & 1)) {                                  /* 1ae3 */
        if (!(S16(R.cx) <= -1)) { R.cx = 0xffff; R.bx = 0; }  /* 1aec cmp cx,-1; jle */
    } else {
        if (!(S16(R.cx) >= 1)) { R.cx = 1; R.bx = 0; }        /* 1af7 cmp cx,1; jge */
    }
    BPW_SET(0x159, R.bx);                                     /* 1b01 */
    BPW_SET(0x15b, R.cx);
    s = (uint32_t)R.bx + BPW(0x151);                          /* 1b0b */
    BPW_SET(0x151, (uint16_t)s);
    R.bx = (uint16_t)(R.cx + R.ax + (s >> 16));               /* mov bx,cx; adc bx,ax */
    uint16_t nx = R.bx;                                       /* 1b19 push bx */
    ovl_grav(0x1000);                                         /* 1b1a..1b4c (gravity 1/16) */
    R.bx = nx;                                                /* 1b4e pop bx */
    R.ax = R.bx;
    R.dx = R.cx;
    {
        unsigned c = BPW(0x15d) >= 0x8000;                    /* 1b53 add bx,8000h -> CF */
        R.bx = (uint16_t)(BPW(0x15f) + 3 + c);                /* mov bx,[bp+15f]; adc bx,3 */
    }
    R.bx = (uint16_t)((R.bx & 0xff00) | CSB((uint16_t)(R.bx + 0x1b7b)));   /* 1b64 mov bl,cs:[bx+1b7b] */
    BPB_SET(0x13c, (uint8_t)R.bx);
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    TAIL(A1_draw);
}

/* L2:1c63 — BETTY (goal): with 3 cauliflowers, touching her makes her sink to y = param
 * (1 px / 4 ticks), 210 ticks later the level is done. */
static void l2_betty(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1c60 */
    R.bx = BPW(0x14f);
    if (R.bx == 0) {                                          /* 1c82 jne 1cc6 */
        A6_touch();                                           /* 1c85 */
        if (!R.zf && dgw(0x1475) == 3) {                      /* 1c88 je / 1c8a */
            DB(0x00bd) |= 8;
            dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) | 0x180));
            BPW_SET(0x16d, 0xd2);
            uint16_t ax = R.ax, dx = R.dx;
            A7_next_anim();                                   /* 1ca6 */
            R.dx = dx; R.ax = ax;
            R.bx = 0x24;
            A20_add_score();                                  /* 1caf */
            TAIL(A1_draw);
        }
        A3_motion_step();                                     /* 1cb5 */
        BPW_SET(0x153, R.bx);
        BPW_SET(0x157, R.cx);
        TAIL(A1_draw);
    }
    if (R.dx != BPW(0x16b)) {                                 /* 1cc6 */
        uint16_t v = (uint16_t)(BPW(0x16f) - 1);              /* 1ccd dec; jns */
        BPW_SET(0x16f, v);
        if (!(v & 0x8000)) TAIL(A1_draw);
        BPW_SET(0x16f, 3);
        R.dx = (uint16_t)(R.dx + 1);
        BPW_SET(0x157, R.dx);
        TAIL(A1_draw);
    }
    uint16_t v = (uint16_t)(BPW(0x16d) - 1);                  /* 1ce4 dec [bp+16d]; jne */
    BPW_SET(0x16d, v);
    if (v == 0) dgw_set(0x00c2, 2);                           /* level completed */
    TAIL(A1_draw);
}

/* L2:1dbd — F-HOPWALKER, L2 version: re-faces the player every 33 ticks while supported,
 * jumps when it walks off a ledge; facing seq = v2 * 2. */
static void l2_hopwalker(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1dba */
    A6_touch();                                               /* 1dd6 */
    if (!(BPB(0x16f) & 1)) {                                  /* 1dd9 */
        uint8_t t = (uint8_t)(BPB(0x170) - 1);                /* 1de1 */
        BPB_SET(0x170, t);
        if (t & 0x80) {
            BPB_SET(0x170, 0x20);
            R.cx = (uint16_t)(R.ax - dgw(0x00e8));            /* 1dee; js 1dfb */
            R.bx = (R.cx & 0x8000) ? 3 : 0;
            BPW_SET(0x171, R.bx);                             /* 1dfe */
            R.bx = sar16((uint16_t)(R.bx - 1), 1);
            BPW_SET(0x15b, R.bx);
        }
    }
    ovl_grav(0x2000);                                         /* 1e0b..1e3d (inline) */
    R.bx = (uint16_t)(BPW(0x15b) + R.ax);                     /* 1e3f */
    A4_map_move();                                            /* 1e47 */
    if (R.bx & 1) {                                           /* 1e4a */
        int old = BPB(0x16f) & 1;
        BPB_SET(0x16f, (uint8_t)R.bx);
        if (!old) {
            uint16_t ax = R.ax;
            A7_next_anim();                                   /* 1e5f */
            R.ax = ax;
            R.bx = 0xfffd;
            BPW_SET(0x15f, R.bx);
            R.bx = 0;
            BPW_SET(0x15d, 0);
        }
    } else {                                                  /* 1e8f */
        int old = BPB(0x16f) & 1;
        BPB_SET(0x16f, (uint8_t)R.bx);
        if (old) {
            uint16_t ax = R.ax;
            A7_next_anim();                                   /* 1e9e */
            R.ax = ax;
        }
    }
    BPW_SET(0x153, R.ax);                                     /* 1e72 */
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 1e7d */
    R.bx = (uint16_t)(BPW(0x171) + BPW(0x171));               /* 1e80 */
    BPW_SET(0x14d, R.bx);
    TAIL(A1_draw);
}

static void l2_quest(void) { ovl_quest(0x1c, 8, Q_SFX_AFTER); }   /* L2:1baf */
static void l2_door(void) { ovl_door(DOOR_KEY); }                 /* L2:1709 */

void ovl_L2_register(void) {
    const int L = LVL_L2;
    farcall_register_overlay(L, 0x0008, l2_entry);            /* +8 JMP 0f2d */
    farcall_register_overlay(L, 0x0e92, A14_checkpoint);
    farcall_register_overlay(L, 0x0eb8, A15_energy);
    farcall_register_overlay(L, 0x0ede, A16_extra_life);
    farcall_register_overlay(L, 0x0f04, A17_big_bonus);
    farcall_register_overlay(L, 0x0f2a, A18_time_bonus);
    farcall_register_overlay(L, 0x10a1, l2_lurker);
    farcall_register_overlay(L, 0x1265, l2_drifter);
    farcall_register_overlay(L, 0x1435, l2_thrower);
    farcall_register_overlay(L, 0x1594, ovl_path);            /* templates 1532 / 1546 */
    farcall_register_overlay(L, 0x1610, ovl_path);
    farcall_register_overlay(L, 0x167a, ovl_spring);
    farcall_register_overlay(L, 0x1709, l2_door);
    farcall_register_overlay(L, 0x195d, l2_dropper);
    farcall_register_overlay(L, 0x1aa1, l2_ball);             /* templates 1a0b / 1a1f */
    farcall_register_overlay(L, 0x1baf, l2_quest);
    farcall_register_overlay(L, 0x1c63, l2_betty);
    farcall_register_overlay(L, 0x1dbd, l2_hopwalker);
}
