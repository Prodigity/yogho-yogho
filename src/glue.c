/* Registration glue: every module that is reached through far pointers in data registers its
 * C ports here once at startup (see farcall.c).  (ovl_story.h and objects_ext.h both define the
 * inline helper ovl_seg(), so the register functions are declared here directly.) */
#include "game.h"

void objects_register(void);      /* objects_ext.h */
void ovl_story_register(void);    /* ovl_story.h */
void ovl_bonus_register(void);    /* ovl_bonus.h */

void ovl_register_all(void) {
    objects_register();      /* API stubs ovl:0x0b+5i -> API slot i, all levels */
    ovl_story_register();    /* L1..L6 handlers + +8 entries */
    ovl_bonus_register();    /* B1..B6 handlers + +8 entries */
}

void ovl_frame_entry(void) { farcall(dgw(0x2d68), dgw(0x2d66)); }
