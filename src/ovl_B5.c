/* Bonus overlay B5 (DAT 87, level index 9).  Spec: teardown/60-overlays.md §11.
 * Per-frame entry (+8: JMP 1235) = F-BTIMER with this overlay's clock icon far pointer at
 * B5:12a9 and digit far pointers 0..9 at B5:12ad; B5:128c digit helper.
 * Record handlers (object list +0x8d, kind 0; census from scripts/ovl_types.py) are the shared
 * library routines in ovl_bonus_plat.c.
 * Dead (unreferenced) copies, not registered: 1301, 1351 F-BPLAT; 160f F-BMOVPLAT; 19c7, 1a2b F-BSTANDPLAT; 1b47, 1c31 F-BCRUMBLE. */
#include "ovl_bonus.h"

#define LEVEL_B5 9

/* B5:0008 JMP B5:1235 — F-BTIMER (RETF). */
static void B5_entry(void) {
    bonus_timer_entry(0x12a9, 0x12ad);
}

void ovl_B5_register(void) {
    farcall_register_overlay(LEVEL_B5, 0x0008, B5_entry);   /* +8 entry (CALLF [2d66]) */
    farcall_register_overlay(LEVEL_B5, 0x1235, B5_entry);   /* its JMP target */
    farcall_register_overlay(LEVEL_B5, 0x1232, bonus_life);        /* F-LIFE (JMP API16) x2 */
    farcall_register_overlay(LEVEL_B5, 0x1689, bonus_plat);        /* F-BPLAT x9 */
    farcall_register_overlay(LEVEL_B5, 0x1947, bonus_movplat);     /* F-BMOVPLAT, anims 0x12 / 0x1e x2 */
    farcall_register_overlay(LEVEL_B5, 0x1a8f, bonus_standplat);   /* F-BSTANDPLAT x12 */
    farcall_register_overlay(LEVEL_B5, 0x1d1b, bonus_crumble);     /* F-BCRUMBLE, param 0x19 x32 */
}
