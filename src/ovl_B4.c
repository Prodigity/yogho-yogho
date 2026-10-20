/* Bonus overlay B4 (DAT 86, level index 7).  Spec: teardown/60-overlays.md §11.
 * Per-frame entry (+8: JMP 1749) = F-BTIMER with this overlay's clock icon far pointer at
 * B4:17bd and digit far pointers 0..9 at B4:17c1; B4:17a0 digit helper.
 * Record handlers (object list +0x8d, kind 0; census from scripts/ovl_types.py) are the shared
 * library routines in ovl_bonus_spring.c / ovl_bonus_plat.c.
 * Dead (unreferenced) copies, not registered: 19c0 F-BBUMPD; 1b5b F-BWINDUP; 1c39 F-BWINDR; 1d05 F-BWINDL. */
#include "ovl_bonus.h"

#define LEVEL_B4 7

/* B4:0008 JMP B4:1749 — F-BTIMER (RETF). */
static void B4_entry(void) {
    bonus_timer_entry(0x17bd, 0x17c1);
}

void ovl_B4_register(void) {
    farcall_register_overlay(LEVEL_B4, 0x0008, B4_entry);   /* +8 entry (CALLF [2d66]) */
    farcall_register_overlay(LEVEL_B4, 0x1749, B4_entry);   /* its JMP target */
    farcall_register_overlay(LEVEL_B4, 0x1746, bonus_life);        /* F-LIFE (JMP API16) x1 */
    farcall_register_overlay(LEVEL_B4, 0x1827, bonus_plat);        /* F-BPLAT x7 */
    farcall_register_overlay(LEVEL_B4, 0x187a, bonus_spring);      /* F-BSPRING, param 0xa000 x9 */
    farcall_register_overlay(LEVEL_B4, 0x1912, bonus_bump_h);      /* F-BBUMPH, param 0x8000 x21 */
    farcall_register_overlay(LEVEL_B4, 0x1a6b, bonus_bump_c);      /* F-BBUMPC, param 0x8000 x11 */
}
