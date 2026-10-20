/* Bonus overlay B1 (DAT 83, level index 1).  Spec: teardown/60-overlays.md §11.
 * Per-frame entry (+8: JMP 1695) = F-BTIMER with this overlay's clock icon far pointer at
 * B1:1709 and digit far pointers 0..9 at B1:170d; B1:16ec digit helper (near, ported as digit_at).
 * Record handlers (object list +0x8d, kind 0; census from scripts/ovl_types.py) are the shared
 * library routines in ovl_bonus_plat.c.
 * Dead (unreferenced) copies, not registered: 17b1, 1ae9 F-BPLAT; 1a6f, 1da7 F-BMOVPLAT; 1e8b, 1eef F-BSTANDPLAT; 2091, 217b F-BCRUMBLE. */
#include "ovl_bonus.h"

#define LEVEL_B1 1

/* B1:0008 JMP B1:1695 — F-BTIMER (RETF). */
static void B1_entry(void) {
    bonus_timer_entry(0x1709, 0x170d);
}

void ovl_B1_register(void) {
    farcall_register_overlay(LEVEL_B1, 0x0008, B1_entry);   /* +8 entry (CALLF [2d66]) */
    farcall_register_overlay(LEVEL_B1, 0x1695, B1_entry);   /* its JMP target */
    farcall_register_overlay(LEVEL_B1, 0x1692, bonus_life);        /* F-LIFE (JMP API16) x1 */
    farcall_register_overlay(LEVEL_B1, 0x1761, bonus_plat);        /* F-BPLAT x8 */
    farcall_register_overlay(LEVEL_B1, 0x1e27, bonus_standplat);   /* F-BSTANDPLAT x10 */
    farcall_register_overlay(LEVEL_B1, 0x1fa7, bonus_crumble);     /* F-BCRUMBLE, param 0x28 x46 */
}
