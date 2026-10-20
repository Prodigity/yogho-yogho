/* Bonus overlay B2 (DAT 84, level index 3).  Spec: teardown/60-overlays.md §11.
 * Per-frame entry (+8: JMP 1785) = F-BTIMER with this overlay's clock icon far pointer at
 * B2:17f9 and digit far pointers 0..9 at B2:17fd; B2:17dc digit helper.
 * Record handlers (object list +0x8d, kind 0; census from scripts/ovl_types.py) are the shared
 * library routines in ovl_bonus_spring.c / ovl_bonus_plat.c.
 * Dead (unreferenced) copies, not registered: 194e F-BBUMPH; 1aa7 F-BBUMPC; 1b97 F-BWINDUP; 1c75 F-BWINDR; 1d41 F-BWINDL. */
#include "ovl_bonus.h"

#define LEVEL_B2 3

/* B2:0008 JMP B2:1785 — F-BTIMER (RETF). */
static void B2_entry(void) {
    bonus_timer_entry(0x17f9, 0x17fd);
}

void ovl_B2_register(void) {
    farcall_register_overlay(LEVEL_B2, 0x0008, B2_entry);   /* +8 entry (CALLF [2d66]) */
    farcall_register_overlay(LEVEL_B2, 0x1785, B2_entry);   /* its JMP target */
    farcall_register_overlay(LEVEL_B2, 0x1782, bonus_life);        /* F-LIFE (JMP API16) x1 */
    farcall_register_overlay(LEVEL_B2, 0x1863, bonus_plat);        /* F-BPLAT x14 */
    farcall_register_overlay(LEVEL_B2, 0x18b6, bonus_spring);      /* F-BSPRING, param 0x8000/0xa000/0xc000 x6 */
    farcall_register_overlay(LEVEL_B2, 0x19fc, bonus_bump_d);      /* F-BBUMPD, param 0x8000 x9 */
}
