/* Bonus overlay B6 (DAT 88, level index 11).  Spec: teardown/60-overlays.md §11.
 * Per-frame entry (+8: JMP 1901) = F-BTIMER with this overlay's clock icon far pointer at
 * B6:1975 and digit far pointers 0..9 at B6:1979; B6:1958 digit helper.
 * Record handlers (object list +0x8d, kind 0; census from scripts/ovl_types.py) are the shared
 * library routines in ovl_bonus_spring.c / ovl_bonus_plat.c.
 * Dead (unreferenced) copies, not registered: 1aca F-BBUMPH; 1b78 F-BBUMPD; 1c23 F-BBUMPC (B6 has no F-LIFE). */
#include "ovl_bonus.h"

#define LEVEL_B6 11

/* B6:0008 JMP B6:1901 — F-BTIMER (RETF). */
static void B6_entry(void) {
    bonus_timer_entry(0x1975, 0x1979);
}

void ovl_B6_register(void) {
    farcall_register_overlay(LEVEL_B6, 0x0008, B6_entry);   /* +8 entry (CALLF [2d66]) */
    farcall_register_overlay(LEVEL_B6, 0x1901, B6_entry);   /* its JMP target */
    farcall_register_overlay(LEVEL_B6, 0x19df, bonus_plat);        /* F-BPLAT x27 */
    farcall_register_overlay(LEVEL_B6, 0x1a32, bonus_spring);      /* F-BSPRING, param 0xc000/0xd000 x9 */
    farcall_register_overlay(LEVEL_B6, 0x1d13, bonus_wind_up);     /* F-BWINDUP, param 0x100 x1 */
    farcall_register_overlay(LEVEL_B6, 0x1df1, bonus_wind_right);  /* F-BWINDR, param 0x60 (x-multiply quirk) x5 */
    farcall_register_overlay(LEVEL_B6, 0x1ebd, bonus_wind_left);   /* F-BWINDL, param 0x60 x4 */
}
