/* Story overlays L1..L6: routines shared (byte-identical or near-identical) across overlays.
 * Spec: teardown/60-overlays.md §4; listings extracted/ovl_dis/L*.asm (cited per routine);
 * identity of the copies from extracted/ovl_dis/shared_groups.txt.  See ovl_story.h for the
 * register conventions.  Where copies differ, the difference is a parameter (or done by the
 * per-level wrapper in ovl_Ln.c, e.g. the L6 "[0x5143] != 0 -> despawn" prefix). */
#include "ovl_story.h"

/* Keep-window prologue present in almost every handler (e.g. L1:0e6a..0e80):
 *   mov bx,ax; sub bx,[2cbc]; cmp bx,[2cc0]; jae despawn
 *   mov bx,dx; sub bx,[2cbe]; cmp bx,[2cc2]; jae despawn          (unsigned compares) */
int ovl_keep(void) {
    R.bx = (uint16_t)(R.ax - dgw(0x2cbc));
    if (R.bx >= dgw(0x2cc0)) return 0;
    R.bx = (uint16_t)(R.dx - dgw(0x2cbe));
    if (R.bx >= dgw(0x2cc2)) return 0;
    return 1;
}

/* GRAV: L4:3f4d = L5:3872 = L6:37a1 (push bx ... pop bx; ret), the same sequence inline in
 * L1:1025/12c2/147c, L2:1e0b, L3:2c9f/2f3d/29d7, L5:29a3, L6:2a96, and with acc 0x1000 in L2:1b1a.
 * In: DX = y.  Out: CX = new y; vyf/vy/yf updated; BX preserved (the inline copies leave BX =
 * new yf but every one of them reloads/pops BX right after, so this is equivalent). */
void ovl_grav(uint16_t acc) {
    uint16_t bx_save = R.bx;
    uint32_t s = (uint32_t)acc + BPW(0x15d);            /* mov bx,acc; add bx,[15d] */
    uint16_t bx = (uint16_t)s;
    BPW_SET(0x15d, bx);
    uint16_t cx = (uint16_t)(BPW(0x15f) + (s >> 16));   /* xor cx,cx; adc cx,[15f] */
    if (S16(cx) >= 3) {                                 /* cmp cx,3; jl */
        cx = 3; bx = 0; BPW_SET(0x15d, 0);
    }
    BPW_SET(0x15f, cx);
    s = (uint32_t)bx + BPW(0x155);                      /* add bx,[155] */
    bx = (uint16_t)s;
    BPW_SET(0x155, bx);
    R.cx = (uint16_t)(cx + R.dx + (s >> 16));          /* adc cx,dx */
    R.bx = bx_save;
}

/* F-HUD per-frame entry (overlay +8): L1:0cad (n=3, desc 0ce4), L2:0f2d (3, 0f64),
 * L5:2639 (9, 2670), L6:2745 (9, 277c).  Called with ES = overlay; ends RETF / jmp API25. */
void ovl_hud(uint16_t n, uint16_t desc) {
    if (dgw(0x1475) == n && !(dgw(0x2c9a) & 0x10)) return;   /* 0cad..0cba: retf (blink off) */
    R.si = desc;                                              /* 0cbc */
    R.bx = (uint16_t)(dgw(0x2cb4) + CSW((uint16_t)(desc + 4)));
    R.di = (uint16_t)(dgw(0x2cb6) + CSW((uint16_t)(desc + 6)));
    draw_cs_world_es();                                       /* 0cd0 API24 */
    R.bx = 0x2a; R.di = 0x94;
    R.si = (uint16_t)(n - dgw(0x1475));
    TAIL(draw_cs_screen_glyph);                               /* 0ce0 jmp API25 */
}

/* F-HUD3 per-frame entry with an animated icon: L3:2625 (n=1, 3 frames @2681),
 * L4:32b9 (n=5, 4 frames @3315). */
void ovl_hud3(uint16_t n, uint16_t nf, uint16_t base) {
    if (dgw(0x1475) == n && !(dgw(0x2c9a) & 0x10)) return;   /* 2625..2632 */
    uint16_t t = (uint16_t)(dgw(0x2dc0) - 1);                 /* 2634 dec [2dc0]; jns */
    dgw_set(0x2dc0, t);
    if (t & 0x8000) { dgw_set(0x2dc0, 0x0a); dgw_set(0x2dbe, (uint16_t)(dgw(0x2dbe) + 1)); }
    if (S16(dgw(0x2dbe)) >= S16(nf)) dgw_set(0x2dbe, 0);      /* 2644 cmp [2dbe],nf; jl */
    R.si = (uint16_t)((dgw(0x2dbe) << 3) + base);             /* 2651 shl si,3; add si,base */
    R.bx = (uint16_t)(dgw(0x2cb4) + CSW((uint16_t)(R.si + 4)));
    R.di = (uint16_t)(dgw(0x2cb6) + CSW((uint16_t)(R.si + 6)));
    draw_cs_world_es();                                       /* 266d API24 */
    R.bx = 0x2a; R.di = 0x94;
    R.si = (uint16_t)(n - dgw(0x1475));
    TAIL(draw_cs_screen_glyph);                               /* 267d jmp API25 */
}

/* F-SPRING: L1:0ee6 = L2:167a (param via DS:[bp+16b]) = L3:26d0 = L4:3374 = L5:26ae = L6:27ba
 * body (param via [bp+16b] without override = SS:, Q1: taken as SS == DGROUP). */
void ovl_spring(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 0f29 */
    A6_touch();                                               /* 0eff */
    if (R.bx == 1) {                                          /* 0f02 cmp bx,1; jne */
        R.bx = BPW(0x16b);
        R.cx = R.bx;
        R.bx = (uint16_t)(R.bx << 4);
        dgw_set(0x00fe, R.bx);                                /* player vy fraction */
        R.cx = sar16(R.cx, 12);
        dgw_set(0x0100, R.cx);                                /* player vy integer */
    }
    A2_anim_step();                                           /* 0f1d */
    if (R.cf) A7_next_anim();                                 /* 0f20 jae / 0f23 */
    TAIL(A1_draw);                                            /* 0f26 */
}

/* F-DOOR and its variants (L1:19f1 = L2:1709 = L3:275f = L4:3403 = L6:2915 body: DOOR_KEY;
 * L3:2831 ice lock: DOOR_ICE; L5:2835 tower door: DOOR_TOWER; L5:273d: DOOR_L5).
 * Offsets below are L1:19f1. */
void ovl_door(int mode) {
    uint16_t seg = ovl_seg();
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 19ee */
    R.si = BPW(0x139);                                        /* record */
    if (!(rw(seg, (uint16_t)(R.si + 8)) & 1)) {               /* 1a0e test es:[si+8],1 */
        R.di = BPW(0x137);                                    /* sprite header */
        R.bx = (uint16_t)(rw(seg, (uint16_t)(R.di + 5)) + R.ax - dgw(0x00ee));
        if (R.bx & 0x8000) {                                  /* 1a25 js */
            R.bx = (uint16_t)(R.bx + rw(seg, (uint16_t)(R.di + 9)));
            if (R.bx & 0x8000) goto done;                     /* 1a33 js */
        } else if (R.bx > 0x10) goto done;                    /* 1a27 cmp bx,10h; jbe */
        R.cx = (uint16_t)(rw(seg, (uint16_t)(R.di + 7)) + R.dx - dgw(0x00f0));
        if (R.cx & 0x8000) {                                  /* 1a3f js */
            R.cx = (uint16_t)(R.cx + rw(seg, (uint16_t)(R.di + 0xb)));
            if (R.cx & 0x8000) goto done;                     /* 1a4a js */
        } else if (R.cx > 0x1a) goto done;                    /* 1a41 cmp cx,1ah; ja */
        /* boxes overlap: open condition */
        int open = 0, usekey = 0;
        switch (mode) {
        case DOOR_KEY:   usekey = 1; break;                   /* 1a4c mov bx,[c0]; or bx,bx; jne */
        case DOOR_ICE:   open = dgw(0x1475) == 1; break;      /* L3:288c cmp [1475],1; je */
        case DOOR_TOWER: open = dgw(0x513b) == 0x0a; break;   /* L5:2890 cmp [513b],0ah; je */
        default:                                              /* L5:2798 cmp [bp+16b],0 */
            if (BPW(0x16b) != 0) open = dgw(0x513b) == 0x0a;  /* L5:27a0 */
            else usekey = 1;                                  /* L5:27aa */
            break;
        }
        if (usekey) {
            R.bx = dgw(0x00c0);
            if (R.bx != 0) {                                  /* 1a7a dec [c0] */
                dgw_set(0x00c0, (uint16_t)(R.bx - 1));
                open = 1;
            }
        }
        if (open) {
            ww(seg, (uint16_t)(R.si + 8), (uint16_t)(rw(seg, (uint16_t)(R.si + 8)) | 1));   /* 1a7e */
        } else {                                              /* 1a54: push the player out sideways */
            uint16_t side = (uint16_t)(dgw(0x00e8) - dgw(0x00f4) - R.ax);   /* sub bx,[f4]; sub bx,ax */
            R.bx = rw(seg, (uint16_t)(R.di + 5));
            if (side & 0x8000) R.bx = (uint16_t)(R.bx - 9);   /* 1a62 jns / 1a64 */
            else R.bx = (uint16_t)(R.bx + rw(seg, (uint16_t)(R.di + 9)) + 9);   /* 1a6a */
            R.bx = (uint16_t)(R.bx + R.ax);                   /* 1a71 */
            dgw_set(0x00e8, R.bx);
        }
    }
done:
    R.bx = (uint16_t)(rw(seg, (uint16_t)(R.si + 8)) & 1);     /* 1a83 */
    BPB_SET(0x13c, (uint8_t)R.bx);                            /* frame 0 closed / 1 open */
    TAIL(A1_draw);
}

/* F-QUEST quest item pickup.  L1:1861 (0x1c, 8, SFX before API20), L2:1baf (0x1c, 8, SFX after),
 * L3:34bf (0x1c, 0x14, no SFX, API2, no push/pop around API19), L4:3f05 (0x20, 0x10, SFX before,
 * API2), L6:3173 (0x20, 8, no SFX, "or bx,bx" test). */
void ovl_quest(uint16_t score, uint16_t d, unsigned flags) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 185e */
    A6_touch();                                               /* 187a */
    int touched = (flags & Q_TESTBX) ? (R.bx != 0) : !R.zf;   /* je (ZF from A6) / L6:318f or bx,bx */
    if (touched) {
        if (flags & Q_SFX_BEFORE) dgw_set(0x2ca4, 5);         /* 187f */
        R.bx = score;
        A20_add_score();                                      /* 1889 (AX,DX = x,y) */
        if (flags & Q_SFX_AFTER) dgw_set(0x2ca4, 5);          /* L2:1bd4 */
        dgw_set(0x1475, (uint16_t)(dgw(0x1475) + 1));         /* 188c */
        TAIL(A11_kill);                                       /* 1890 */
    }
    uint16_t ax = R.ax, dx = R.dx;                            /* 1893 push ax; push dx */
    R.ax = (uint16_t)(R.ax - d);
    R.dx = (uint16_t)(R.dx - d);
    A19_sparkle();                                            /* 189c */
    if (!(flags & Q_NOPUSH)) { R.dx = dx; R.ax = ax; }        /* 189f pop dx; pop ax */
    if (flags & Q_ANIM) A2_anim_step();                       /* L3:34f6, L4:3f46 */
    TAIL(A1_draw);
}

/* F-PATH: L2:1594 (thrown object) = L2:1610 (path mover). */
void ovl_path(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1641 */
    A6_touch();                                               /* 1629 */
    A3_motion_step();                                         /* 162d */
    BPW_SET(0x153, R.bx);
    BPW_SET(0x157, R.cx);
    A2_anim_step();                                           /* 163b */
    TAIL(A1_draw);
}

/* F-PATHCOL: L3:3105 = L5:2b93 (with_a2 = 1); L5:2d99 dropped object (with_a2 = 0). */
void ovl_pathcol(int with_a2) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 3102 */
    A6_touch();                                               /* 311e */
    A3_motion_step();                                         /* 3122 */
    A4_map_move();                                            /* 3126 */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    if (with_a2) A2_anim_step();                              /* 3134 */
    TAIL(A1_draw);
}

/* F-TURNWALKER: L1:1967 = L3:31f7. */
void ovl_turnwalker(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1964 */
    A6_touch();                                               /* 1980 */
    A3_motion_step();                                         /* 1984 */
    A4_map_move();                                            /* 1988 */
    if (R.bx & 3) {                                           /* 198b test bx,3 */
        uint16_t ax = R.ax;
        A7_next_anim();                                       /* 1993 (push ax / pop ax) */
        R.ax = ax;
    }
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 19a2 */
    TAIL(A1_draw);
}

/* F-ROLLER: L1:1025 (rolling cheese) = L3:295f.  Offsets are L1. */
void ovl_roller(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 1022 */
    R.bx = BPW(0x16b);
    if (R.bx != 0) {                                          /* 1042 first frame: param -> vx */
        R.cx = R.ax;
        R.ax = xchg_ahal(R.bx);                               /* 104a */
        BPW_SET(0x159, R.ax);                                 /* vxf = swapped param (sic) */
        R.ax = cbw16(R.ax);                                   /* 1051 CBW */
        BPW_SET(0x15b, R.ax);
        R.ax = R.cx;
        R.bx = 0;
        BPW_SET(0x16b, 0);
    }
    A6_touch();                                               /* 1061 */
    uint32_t s = (uint32_t)BPW(0x161) + BPW(0x159);           /* 1064 vx += ax (32-bit) */
    R.cx = (uint16_t)s;
    BPW_SET(0x159, R.cx);
    R.bx = (uint16_t)(BPW(0x163) + BPW(0x15b) + (s >> 16));
    BPW_SET(0x15b, R.bx);
    R.si = (R.bx & 0x8000) ? 0 : 6;                           /* 1082 facing */
    BPW_SET(0x14d, R.si);
    s = (uint32_t)R.cx + BPW(0x151);                          /* 1090 x += vx */
    R.cx = (uint16_t)s;
    BPW_SET(0x151, R.cx);
    R.bx = (uint16_t)(R.bx + R.ax + (s >> 16));               /* 109a adc bx,ax */
    uint16_t nx = R.bx;                                       /* 109c push bx */
    ovl_grav(0x2000);                                         /* 109d..10cf */
    R.bx = nx;                                                /* 10d1 pop bx */
    A4_map_move();                                            /* 10d3 */
    R.cx = BPW(0x16d);
    if (!(R.cx & 1)) {                                        /* 10db: was supported */
        R.si = (uint16_t)((dgw(0x00d8) ^ R.ax) & 0x1f);
        if (R.si == 0) {                                      /* random hop */
            R.si = dgw(0x00da);
            BPW_SET(0x15d, R.si);
            R.si = 0xfffe;
            BPW_SET(0x15f, R.si);
        }
    } else if (!(R.bx & 1)) {                                 /* 1100: just landed */
        R.cx = BPW(0x15f);
        if (!(R.cx & 0x8000)) {                               /* 110b or cx,cx; js */
            R.cx = (uint16_t)-R.cx;
            BPW_SET(0x15f, R.cx);
            R.cx = 0;
            BPW_SET(0x15d, 0);
            R.cx = (uint16_t)((dgw(0x00d6) & 3) * 2 + 1);     /* 111d */
            BPB_SET(0x13d, (uint8_t)R.cx);                    /* delay reload 1/3/5/7 */
        }
    }
    BPW_SET(0x16d, R.bx);                                     /* 112e */
    R.bx &= 2;                                                /* 1133 and bx,2 */
    if (R.bx) BPW_SET(0x15b, (uint16_t)-BPW(0x15b));          /* 1138 neg integer vx only */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 1148 */
    TAIL(A1_draw);
}

/* F-SEEKER: L5:2921 (= L6:2a0d after its [0x5143] check).  Offsets are L5. */
void ovl_seeker(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 291e */
    uint8_t t = (uint8_t)(BPB(0x172) - 1);                    /* 2939 dec byte [bp+172]; jns */
    BPB_SET(0x172, t);
    if (t & 0x80) {
        BPB_SET(0x172, 0x10);
        R.bx = (uint16_t)(dgw(0x00e8) - R.ax);
        BPB_SET(0x171, (uint8_t)(R.bx >> 8));                 /* 294c mov [bp+171],bh */
    }
    R.bx = BPW(0x16b);                                        /* 2951 */
    R.cx = R.ax;
    R.ax = R.bx;
    R.bx = (uint16_t)((R.bx & 0x00ff) | ((uint16_t)BPB(0x171) << 8));   /* mov bh,[bp+171] */
    if (R.bx & 0x8000) R.ax = (uint16_t)-R.ax;                /* 295f or bh,bh; jns / neg ax */
    R.ax = xchg_ahal(R.ax);                                   /* 2965 */
    BPW_SET(0x159, R.ax);
    R.ax = cbw16(R.ax);                                       /* 296c CBW */
    BPW_SET(0x15b, R.ax);
    R.ax = R.cx;
    A6_touch();                                               /* 2975 */
    uint32_t s = (uint32_t)BPW(0x161) + BPW(0x159);           /* 2978 vx += ax */
    R.cx = (uint16_t)s;
    BPW_SET(0x159, R.cx);
    R.bx = (uint16_t)(BPW(0x163) + BPW(0x15b) + (s >> 16));
    BPW_SET(0x15b, R.bx);
    s = (uint32_t)R.cx + BPW(0x151);                          /* 2996 x += vx */
    R.cx = (uint16_t)s;
    BPW_SET(0x151, R.cx);
    R.bx = (uint16_t)(R.bx + R.ax + (s >> 16));
    uint16_t nx = R.bx;                                       /* 29a2 push bx */
    ovl_grav(0x2000);                                         /* 29a3..29d5 */
    R.bx = nx;
    A4_map_move();                                            /* 29d9 */
    R.cx = BPW(0x16d);
    if (!(R.cx & 1)) {                                        /* 29e1 */
        R.si = (uint16_t)((dgw(0x00d8) ^ R.ax) & 0x1f);
        if (R.si == 0) {
            R.si = dgw(0x00da);
            BPW_SET(0x15d, R.si);
            BPW_SET(0x15f, 0xfffe);
        }
    } else if (!(R.bx & 1)) {                                 /* 2a05 */
        R.cx = BPW(0x15f);
        if (!(R.cx & 0x8000)) {                               /* landed: hop */
            R.cx = (uint16_t)((dgw(0x00d6) & 1) - 3);
            BPW_SET(0x15f, R.cx);
            BPW_SET(0x15d, 0);
        }
    }
    BPW_SET(0x16d, R.bx);                                     /* 2a2a */
    BPW_SET(0x153, R.ax);
    BPW_SET(0x157, R.dx);
    A2_anim_step();                                           /* 2a3a */
    if (R.cf) {
        A7_next_anim();                                       /* 2a40 */
        uint16_t v = (uint16_t)(BPW(0x16f) - 1);              /* 2a43 dec [bp+16f]; je */
        BPW_SET(0x16f, v);
        if (v == 0) goto face;
        goto burst;
    }
    if (BPW(0x16f) != 0) goto face;                           /* 2a4d */
    A23_random();                                             /* 2a56 */
    R.ax &= 0x3f;
    if (R.ax != 0) goto face;
    R.bx = (uint16_t)((dgw(0x00d8) & 7) + 2);                 /* 2a5e */
    BPW_SET(0x16f, R.bx);
burst:
    BPW_SET(0x14b, (uint16_t)(BPW(0x14b) | 0x41));            /* 2a6d */
face:
    R.si = 0;                                                 /* 2a73 */
    SET_AH(R, BPB(0x171));
    if (!(BPB(0x171) & 0x80)) R.si = 6;
    BPW_SET(0x14d, R.si);
    TAIL(A1_draw);
}

/* F-CHARGER: L4:35e1 = L5:30a7 (GRAV helper L4:3f4d / L5:3872).  Offsets are L4. */
void ovl_charger(void) {
    if (!ovl_keep()) TAIL(A10_despawn);                       /* 35de */
    A6_touch();                                               /* 35fa */
    if (S16(BPW(0x14f)) < 0x0c) {                             /* 35fd cmp [bp+14f],0ch; jge */
        uint16_t v = (uint16_t)(BPW(0x16d) - 1);              /* 3605 dec [bp+16d]; jns */
        BPW_SET(0x16d, v);
        if (v & 0x8000) {
            BPW_SET(0x16d, 0x40);
            uint16_t ax = R.ax, dx = R.dx;                    /* push ax; push dx */
            R.cx = 0;
            R.bx = (uint16_t)(dgw(0x00e8) - R.ax);
            if (!(R.bx & 0x8000)) R.cx = 6;                   /* 361d js */
            if (BPW(0x14f) == R.cx) {                         /* 3622 */
                R.ax = cwd_abs(R.bx);                         /* 3629 */
                if (!(S16(R.ax) > 0x64)) {                    /* 3630 cmp ax,64h; jg */
                    A7_next_anim();                           /* 3636 */
                    R.dx = dx; R.ax = ax;
                    goto leap;                                /* 363b jmp 366c */
                }
            } else {
                R.ax = R.cx;                                  /* 363e */
                A8_set_anim();
            }
            R.dx = dx; R.ax = ax;                             /* 3644 pop dx; pop ax */
        }
        A3_motion_step();                                     /* 3646 */
        A4_map_move();
        if (R.bx & 1) {
            A7_next_anim();                                   /* 3655 walked off: pos not stored */
        } else {
            BPW_SET(0x153, R.ax);                             /* 365b */
            BPW_SET(0x157, R.dx);
        }
        A2_anim_step();                                       /* 3665 */
        TAIL(A1_draw);
    }
leap:
    A3_motion_step();                                         /* 366c */
    ovl_grav(0x2000);                                         /* 3670 call 3f4d */
    A4_map_move();                                            /* 3674 */
    if (R.bx & 1) {                                           /* 3677 jne 365b */
        BPW_SET(0x153, R.ax);
        BPW_SET(0x157, R.dx);
        A2_anim_step();
        TAIL(A1_draw);
    }
    A7_next_anim();                                           /* 367e */
    TAIL(A1_draw);
}

/* Registration of all six story overlays. */
void ovl_story_register(void) {
    static const int lv[6] = { LVL_L1, LVL_L2, LVL_L3, LVL_L4, LVL_L5, LVL_L6 };
    /* A6 (1000:42a9) stores handler 0x000b = the API0 thunk at overlay:000b into a stomped
     * object's slot; the engine loop then CALLFs ovl:000b -> 1000:4636. */
    for (int i = 0; i < 6; i++) farcall_register_overlay(lv[i], 0x000b, A0_dying_fall);
    ovl_L1_register();
    ovl_L2_register();
    ovl_L3_register();
    ovl_L4_register();
    ovl_L5_register();
    ovl_L6_register();
}
