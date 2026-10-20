/* Extra exports of the flow module (flow.c, menu.c, hiscore.c, intro.c) beyond game.h.
 * Addresses are Ghidra addresses; spec: teardown/10-flow-frontend.md. */
#ifndef YOGHO_FLOW_EXT_H
#define YOGHO_FLOW_EXT_H
#include <stdint.h>

/* ---- flow.c ---- */
int  hw_init(void);                         /* 160a:316d  returns AL (0xff = ok, 0 = failed) */
void hw_shutdown(void);                     /* 160a:31b0 */

/* ---- menu.c ---- */
int  menu(void);                            /* 160a:1fad  0 = new game, -1 = quit */
void menu_load_assets(void);                /* 160a:2068 */
int  title_screen(void);                    /* 160a:2192  0..4 (port: -2 = host window closed) */
int  quit_confirm(void);                    /* 160a:2642  -1 = quit, 1 = back */
void draw_text_list(uint16_t list);         /* 160a:26ab  DGROUP near ptr to {x,y,str} records */
void joystick_calibration(void);            /* 160a:24a8 */
void menu_palette_tick(void);               /* 160a:2cae */
uint16_t pal_rotate(uint16_t t, int16_t from, uint16_t to, uint16_t period);   /* 160a:2d49 */
void mouse_show(void);                      /* 160a:2ff2 */
void mouse_hide(void);                      /* 160a:3005 */
void wait_anykey(void);                     /* 160a:30e9 */
void mouse_restore(void);                   /* 1000:0c9b */
/* Joystick (port 201h; the port's game port is a stub that reports "no joystick"). */
uint16_t joy_absent(void);                  /* 1000:4a96  0 present / 0xffff absent */
void joy_read(void);                        /* 1000:4ab3  REG: AX = X count, DX = Y count */
uint16_t joy_button(void);                  /* 1000:4ae5  1 = button 1 pressed */
uint16_t joy_cal_valid(void);               /* 2a33:0058 */
uint16_t joy_to_dirbits(void);              /* 2a33:007d  direction bits 8 up,2 left,1 right,4 down,0x80 fire */

/* ---- hiscore.c ---- */
int  highscore_screen(void);                /* 160a:273c  0 = start game, 1 = back to title */
void highscore_draw_all(void);              /* 160a:2a13 */
void highscore_draw_row(uint16_t i);        /* 160a:2a7d */

/* ---- intro.c ---- */
void intro(void);                           /* 160a:2ea6 */
uint16_t intro_scene(uint16_t dat, uint16_t wait_in, uint16_t wait_out,
                     uint16_t fade_in_step, uint16_t fade_out_step, uint16_t delay);   /* 160a:2f4f */

#endif
