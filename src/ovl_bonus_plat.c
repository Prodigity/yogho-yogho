/* Bonus overlays: the B1/B3/B5 platform library, the per-frame entry body shared by all six
 * bonus overlays, and the registration of all bonus handlers.
 * Spec: teardown/60-overlays.md §11 "F-BTIMER" and "B1 / B3 / B5"; API semantics
 * teardown/50-objects-api.md §3f (A2/A7), §3g (A3), §3h (A13), §3i (A22).
 * Addresses are overlay offsets (B1:xxxx = B1 overlay segment:xxxx), quoted from the B1 copy;
 * B3/B5 hold byte-identical copies at the offsets listed in ovl_B3.c / ovl_B5.c.
 *
 * Player-state writes done here (40-player-physics.md Q12): player y [0xec] (rider snap from
 * API22's SI), player x/y [0xe8]/[0xec] (moving-platform carry), on-platform flag [0xd4]
 * (crumble collapse). */
#include "ovl_bonus.h"
#include "log.h"

/* ------------------------------------------------------------------------------------------ */
/* F-BTIMER — per-frame entry (B1:1695; called from the ISR via CALLF [2d66], ES = overlay).   */
/* ------------------------------------------------------------------------------------------ */

/* B1:16ec — draw digit AL at screen (camera + BX, camera + 0x14).  Near helper, keeps AX. */
static void digit_at(uint16_t digits) {
    uint16_t save_ax = R.ax;                                   /* 16ec push ax */
    R.ax = (uint16_t)(int16_t)(int8_t)AL(R);                   /* 16ed cbw */
    R.ax = (uint16_t)(R.ax + R.ax);                            /* 16ee add ax,ax */
    R.ax = (uint16_t)(R.ax + R.ax);                            /* 16f0 add ax,ax */
    R.si = (uint16_t)(R.ax + digits);                          /* 16f2 si = 4*d + table */
    R.di = dgw(0x2cb6);
    R.bx = (uint16_t)(R.bx + dgw(0x2cb4));
    R.di = (uint16_t)(R.di + 0x14);
    draw_cs_world_es();                                        /* 1703 API24 */
    R.ax = save_ax;                                            /* 1707 pop ax */
}

/* 8-bit DIV BL: AL = AX / BL, AH = AX % BL. */
static void div_bl(void) {
    uint16_t a = R.ax;
    uint8_t d = BL(R);
    uint16_t q = (uint16_t)(a / d), r = (uint16_t)(a % d);
    if (q > 0xff)   /* #DE (INT 0) in the original; needs [0x2d9a] >= 15360 s, unreachable */
        log_gap("bonus timer: DIV BL overflow, AX=%04X BL=%02X", a, d);
    R.ax = (uint16_t)((r << 8) | (q & 0xff));
}

static void xchg_ah_al(void) { R.ax = (uint16_t)((R.ax >> 8) | (R.ax << 8)); }

/* B1:1695 (B2:1785 B3:1389 B4:1749 B5:1235 B6:1901, byte-identical except the two table
 * offsets).  Ends with RETF. */
void bonus_timer_entry(uint16_t clock, uint16_t digits) {
    R.si = clock;                                              /* 1695 mov si,1709 */
    R.di = dgw(0x2cb6);
    R.bx = dgw(0x2cb4);
    R.di = (uint16_t)(R.di + 2);
    R.bx = (uint16_t)(R.bx + 8);
    draw_cs_world_es();                                        /* 16a7 API24: clock icon */
    R.ax = dgw(0x2d9a);                                        /* 16aa seconds left */
    R.bx = (uint16_t)((R.bx & 0xff00) | 0x3c);                 /* 16ad mov bl,3c */
    div_bl();                                                  /* 16af AL = min, AH = sec */
    R.bx = 0x0d;
    digit_at(digits);                                          /* 16b4 minutes */
    xchg_ah_al();                                              /* 16b7 AL = sec */
    R.ax = (uint16_t)(int16_t)(int8_t)AL(R);                   /* 16b9 cbw */
    R.bx = (uint16_t)((R.bx & 0xff00) | 0x0a);                 /* 16ba mov bl,0a */
    div_bl();                                                  /* 16bc AL = sec/10, AH = sec%10 */
    R.bx = 0x16;
    digit_at(digits);                                          /* 16c1 tens */
    xchg_ah_al();
    R.bx = 0x1d;
    digit_at(digits);                                          /* 16c9 units */
    uint16_t t = (uint16_t)(dgw(0x5120) - 1);                  /* 16cc dec [5120]; jns ret */
    dgw_set(0x5120, t);
    if ((int16_t)t >= 0) return;
    dgw_set(0x5120, 0x45);                                     /* 16d2 70 ticks = 1 s */
    R.ax = dgw(0x2d9a);
    if (R.ax == 0) return;                                     /* 16db or ax,ax; je (Q7: 0 never ends) */
    t = (uint16_t)(dgw(0x2d9a) - 1);                           /* 16df dec [2d9a]; jne ret */
    dgw_set(0x2d9a, t);
    if (t != 0) return;
    dgw_set(0x00c2, 2);                                        /* 16e5 time up: level done */
}

/* ------------------------------------------------------------------------------------------ */
/* Handlers present in both libraries                                                          */
/* ------------------------------------------------------------------------------------------ */

/* F-LIFE B1:1692 — jmp API16 (tail). */
void bonus_life(void) {
    A16_extra_life();
}

/* F-BPLAT B1:1761 — static rideable platform. */
void bonus_plat(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1761..1777 -> 175e jmp API10 */
    A22_platform();                                            /* 177a */
    if (!R.zf) dgw_set(0x00ec, R.si);                          /* 177d je; 177f [ec] = top-1 */
    A1_draw();                                                 /* 1783 jmp API1 */
}

/* ------------------------------------------------------------------------------------------ */
/* B1/B3/B5 library                                                                            */
/* ------------------------------------------------------------------------------------------ */

/* F-BMOVPLAT B3:1763 (= B1:1a6f) — platform moved by its anim's motion script; carries the
 * rider by the platform's displacement. */
void bonus_movplat(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1763..1779 -> 1760 jmp API10 */
    A22_platform();                                            /* 177c (preserves AX, DX) */
    R.cx = 0;                                                  /* 177f mov cx,0 (flags kept) */
    if (!R.zf) {                                               /* 1782 je */
        dgw_set(0x00ec, R.si);                                 /* 1784 [ec] = top-1 */
        R.cx = 1;
    }
    uint16_t s_ax = R.ax, s_dx = R.dx, s_cx = R.cx;            /* 178b push ax; push dx; push cx */
    A3_motion_step();                                          /* 178e API3 -> BX,CX */
    BPW_SET(0x153, R.bx);                                      /* 1792 S.x = BX */
    BPW_SET(0x157, R.cx);                                      /* 1797 S.y = CX */
    R.si = s_cx; R.dx = s_dx; R.ax = s_ax;                     /* 179c pop si; pop dx; pop ax */
    if (R.si != 0) {                                           /* 179f or si,si; je */
        R.bx = (uint16_t)(R.bx - R.ax);                        /* dx moved */
        R.cx = (uint16_t)(R.cx - R.dx);                        /* dy moved */
        dgw_set(0x00e8, (uint16_t)(dgw(0x00e8) + R.bx));       /* player x += dx */
        dgw_set(0x00ec, (uint16_t)(dgw(0x00ec) + R.cx));       /* player y += dy */
    }
    A1_draw();                                                 /* 17af jmp API1 */
}

/* F-BSTANDPLAT B1:1e27 — platform that shows frame 1 while ridden, frame 0 otherwise. */
void bonus_standplat(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1e27..1e3d -> 1e24 jmp API10 */
    A22_platform();                                            /* 1e40 */
    if (!R.zf) {                                               /* 1e43 je 1e4f */
        dgw_set(0x00ec, R.si);
        R.bx = 1;                                              /* 1e49 */
    } else {
        R.bx = 0;                                              /* 1e4f xor bx,bx */
    }
    BPB(0x13c) = BL(R);                                        /* 1e51 S.frame = BL */
    A1_draw();                                                 /* 1e56 jmp API1 */
}

/* F-BCRUMBLE B1:1fa7 — platform that shakes for param+1 ticks after the first landing, then
 * plays its break anim (next = 0x0c, aflags 0x41) and is killed for the rest of the level. */
void bonus_crumble(void) {
    if (bonus_outside_keep()) { A10_despawn(); return; }       /* 1fa7..1fbd -> 1fa4 jmp API10 */
    if ((int16_t)BPW(0x14f) > 6) {                             /* 1fbf cmp anim,6; jg 2003 */
        A2_anim_step();                                        /* 2004 API2 */
        if (!R.cf) { A1_draw(); return; }                      /* 2007 jae 2000 -> jmp API1 */
        A13_kill_unride();                                     /* 2009 jmp API13 */
        return;
    }
    A22_platform();                                            /* 1fc8 */
    if (!R.zf) {                                               /* 1fcb je 1fd7 */
        dgw_set(0x00ec, R.si);                                 /* 1fcd */
        BPW_SET(0x16f, BPW(0x16f) | 1);                        /* 1fd1 armed */
    }
    if (BPW(0x16f) & 1) {                                      /* 1fd7 test; je 2000 */
        uint16_t p = (uint16_t)(BPW(0x16b) - 1);               /* 1fe0 dec param; jns 1ff4 */
        BPW_SET(0x16b, p);
        if ((int16_t)p < 0) {
            dgw_set(0x00d4, 0);                                /* 1fe7 player no longer riding */
            A7_next_anim();                                    /* 1fee API7 -> break anim */
        } else {
            R.bx = (uint16_t)(dgw(0x00d8) & 3);                /* 1ff4 random shake frame */
            BPB(0x13c) = BL(R);
        }
    }
    A1_draw();                                                 /* 2000 jmp API1 */
}

/* ------------------------------------------------------------------------------------------ */
void ovl_bonus_register(void) {
    ovl_B1_register();
    ovl_B2_register();
    ovl_B3_register();
    ovl_B4_register();
    ovl_B5_register();
    ovl_B6_register();
}
