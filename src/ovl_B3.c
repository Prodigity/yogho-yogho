/* Bonus overlay B3 (DAT 85, level index 5).  Spec: teardown/60-overlays.md §11.
 * Per-frame entry (+8: JMP 1389) = F-BTIMER with this overlay's clock icon far pointer at
 * B3:13fd and digit far pointers 0..9 at B3:1401; B3:13e0 digit helper.
 * Record handlers (object list +0x8d, kind 0; census from scripts/ovl_types.py) are the shared
 * library routines in ovl_bonus_plat.c.
 * Dead (unreferenced) copies, not registered: 1455, 17dd F-BPLAT; 1a9b F-BMOVPLAT; 1b1b, 1be3 F-BSTANDPLAT; 1c9b, 1e6f F-BCRUMBLE. */
#include "ovl_bonus.h"

#define LEVEL_B3 5

/* B3:0008 JMP B3:1389 — F-BTIMER (RETF). */
static void B3_entry(void) {
    bonus_timer_entry(0x13fd, 0x1401);
}

void ovl_B3_register(void) {
    farcall_register_overlay(LEVEL_B3, 0x0008, B3_entry);   /* +8 entry (CALLF [2d66]) */
    farcall_register_overlay(LEVEL_B3, 0x1389, B3_entry);   /* its JMP target */
    farcall_register_overlay(LEVEL_B3, 0x1386, bonus_life);        /* F-LIFE (JMP API16) x1 */
    farcall_register_overlay(LEVEL_B3, 0x14a5, bonus_plat);        /* F-BPLAT x10 */
    farcall_register_overlay(LEVEL_B3, 0x1763, bonus_movplat);     /* F-BMOVPLAT, anims 0 / 0x18 x2 */
    farcall_register_overlay(LEVEL_B3, 0x1b7f, bonus_standplat);   /* F-BSTANDPLAT x12 */
    farcall_register_overlay(LEVEL_B3, 0x1d85, bonus_crumble);     /* F-BCRUMBLE, param 0x1e x44 */
}
