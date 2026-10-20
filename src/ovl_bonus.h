/* Bonus-level overlays B1..B6 (DAT 83..88, level indices 1,3,5,7,9,11).
 * Spec: teardown/60-overlays.md §11 (+ §2-§3), engine API semantics teardown/50-objects-api.md
 * (§3d calling convention, §3h-§3j A6/A10/A13/A20/A22).  Listings: extracted/ovl_dis/B1..B6.asm.
 *
 * All six bonus overlays are built from two byte-identical routine libraries (ovl_share.py groups
 * them with ratio 1.00; only in-overlay data addresses differ) plus one shared per-frame entry:
 *   ovl_bonus_plat.c   B1/B3/B5 platform library (F-LIFE, F-BPLAT, F-BMOVPLAT, F-BSTANDPLAT,
 *                      F-BCRUMBLE) + the F-BTIMER entry body shared by all six + registration
 *   ovl_bonus_spring.c B2/B4/B6 spring / bumper / wind library
 *   ovl_B1.c..ovl_B6.c per-overlay +8 entry (F-BTIMER with that overlay's sprite-table offsets)
 *                      and the handler-offset registration of that overlay.
 *
 * Every handler is a REG routine (50-objects-api.md §3d): entry AX = S.x, DX = S.y,
 * BP = slot*0x44, ES = overlay segment, DS = DGROUP.  They operate on R exactly like the asm;
 * an API reached by `push cs; call stub` is a plain call, a tail `jmp stub` is call + return.
 */
#ifndef YOGHO_OVL_BONUS_H
#define YOGHO_OVL_BONUS_H
#include "game.h"

/* Registers every bonus-overlay handler offset (record +0xe handlers and the +8 entry). */
void ovl_bonus_register(void);
void ovl_B1_register(void);
void ovl_B2_register(void);
void ovl_B3_register(void);
void ovl_B4_register(void);
void ovl_B5_register(void);
void ovl_B6_register(void);

/* ---- shared by all six (ovl_bonus_plat.c) ---- */
/* F-BTIMER (B1:1695 B2:1785 B3:1389 B4:1749 B5:1235 B6:1901): clock icon, mm:ss of [0x2d9a],
 * 70-tick second divider [0x5120], [0xc2]=2 when the time runs out.  `clock` = overlay offset of
 * the clock icon far pointer (B1 1709), `digits` = offset of the 10 digit far pointers (B1 170d). */
void bonus_timer_entry(uint16_t clock, uint16_t digits);
void bonus_life(void);          /* F-LIFE      B1:1692 = JMP API16 (B2:1782 B3:1386 B4:1746 B5:1232) */
void bonus_plat(void);          /* F-BPLAT     B1:1761 (identical in all six: B2:1863 ... B6:19df) */

/* ---- B1/B3/B5 library (ovl_bonus_plat.c) ---- */
void bonus_movplat(void);       /* F-BMOVPLAT  B3:1763 / B5:1947 (B1:1a6f, 1da7 dead) */
void bonus_standplat(void);     /* F-BSTANDPLAT B1:1e27 */
void bonus_crumble(void);       /* F-BCRUMBLE  B1:1fa7 */

/* ---- B2/B4/B6 library (ovl_bonus_spring.c) ---- */
void bonus_spring(void);        /* F-BSPRING   B2:18b6 */
void bonus_bump_h(void);        /* F-BBUMPH    B4:1912 (B2:194e dead) */
void bonus_bump_d(void);        /* F-BBUMPD    B2:19fc */
void bonus_bump_c(void);        /* F-BBUMPC    B4:1a6b (B2:1aa7 dead) */
void bonus_wind_up(void);       /* F-BWINDUP   B6:1d13 (B2:1b97 dead) */
void bonus_wind_right(void);    /* F-BWINDR    B6:1df1 (B2:1c75 dead) — multiplies by x (bug) */
void bonus_wind_left(void);     /* F-BWINDL    B6:1ebd (B2:1d41 dead) */

/* ---- helpers shared by the two library files ---- */
/* Slot fields addressed like the asm: DS:[BP+d] with d the listing's displacement (0x131+n). */
#define BPW(d)       dgw((uint16_t)(R.bp + (d)))
#define BPW_SET(d, v) dgw_set((uint16_t)(R.bp + (d)), (uint16_t)(v))
#define BPB(d)       DB((uint16_t)(R.bp + (d)))
/* Overlay data: ES:[off]. */
static inline uint16_t bonus_esw(uint16_t off) { return rw(R.es, off); }

/* Standard prologue (every bonus handler, e.g. B1:1761..1777):
 *   mov bx,ax; sub bx,[2cbc]; cmp bx,[2cc0]; jae despawn
 *   mov bx,dx; sub bx,[2cbe]; cmp bx,[2cc2]; jae despawn
 * Returns 1 when the object is outside the keep window (caller then tail-jumps to API10). */
static inline int bonus_outside_keep(void) {
    R.bx = (uint16_t)(R.ax - dgw(0x2cbc));
    if (R.bx >= dgw(0x2cc0)) return 1;
    R.bx = (uint16_t)(R.dx - dgw(0x2cbe));
    return R.bx >= dgw(0x2cc2);
}

#endif
