/* Cross-module routine declarations — the contract between the port's modules.
 *
 * Naming: descriptive name + the Ghidra address of the original routine in the comment.
 * Conventions (see cpu.h):
 *   REG  = register convention: void f(void), arguments/results in the global R (and flags).
 *   C    = Borland cdecl: C parameters in the original stack order; far pointers are passed as
 *          (off, seg); 16-bit return value = AX (a 32-bit result also sets R.dx).
 * Every module defines only its own section.  A module that needs a routine owned by another
 * module calls the name declared here; if it must be added, add it to the owner's section with
 * the address and convention, and say so in the module's header comment.
 */
#ifndef YOGHO_GAME_H
#define YOGHO_GAME_H
#include <stdint.h>
#include "cpu.h"
#include "mem.h"

/* ===================== core (isr.c, dat_load.c — lead) ===================== */
void timer_isr(void);                       /* 1000:05f0  REG-free: the per-retrace scheduler */
void palette_tick(void);                    /* 1000:0935 */
void kbd_event(uint8_t scancode_byte);      /* 1000:09c3  byte as read from port 60h (bit7 = break) */
void wait_ticks(uint16_t n);                /* 1000:0577  C */
void wait_tick(void);                       /* 1000:0581  C */
void display_on(void);                      /* 1000:0598 */
void display_off(void);                     /* 1000:05a9 */
uint16_t rng_step(void);                    /* 1000:05da  (API 23) returns AX = new [da]; also REG via api_23 */
void timer_install(void);                   /* 1000:0405 */
void timer_remove(void);                    /* 1000:05ba */
void kbd_install(void);                     /* 1000:03cb */
void kbd_remove(void);                      /* 1000:03eb */
uint32_t dat_load(uint16_t idx, uint16_t off, uint16_t seg);   /* 160a:12a0  C: returns size (DX:AX) */
void dat_read(uint32_t pos, uint32_t size, uint16_t off, uint16_t seg); /* 160a:12e0  C */
void spin_while_nonzero_w(uint16_t dg_off);   /* helper: while (DGROUP word != 0) yield */
void spin_while_zero_b(uint16_t dg_off);      /* helper: while (DGROUP byte == 0) yield */

/* ===================== render (render.c, cspr.c, hud.c, pal.c — render agent) ============ */
void video_init(void);                      /* 1000:02c8 */
void vga_reglist(uint16_t off, uint16_t seg);   /* 1000:0349  C (far ptr) */
void vram_clear(void);                      /* 1000:0490 */
void patch_render_segs(void);               /* 1000:04aa */
void blit_linear_vga(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h);        /* 1000:0a98 C */
void blit_planar_vga(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h, int16_t skip); /* 1000:0b04 C */
void blit_planar_vga_t(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h);      /* 1000:0b63 C */
void blit_planar_buf_t(uint16_t soff, uint16_t sseg, int16_t x, int16_t y, int16_t w, int16_t h);      /* 1000:0bc6 C */
void dac_schedule(uint16_t ptr, uint16_t start, uint16_t count);   /* 1000:0c52 C */
void cursor_restore(void);                  /* 1000:0d0f */
void cursor_row16(uint16_t y);              /* 1000:0d84 C */
void cursor_draw(void);                     /* 1000:0ddf */
void bg_fill(void);                         /* 1000:0e1a */
void bg_tile_row(void);                     /* 1000:0e7c  REG */
void bg_tile_col(void);                     /* 1000:0ef6  REG */
void bg_tile_one(void);                     /* 1000:0f9c  REG (AX=x, BX=y) */
void bg_to_page(void);                      /* 1000:12e3  REG (AX = page segment) */
void draw_cs_abs(uint16_t off, uint16_t seg, int16_t x, int16_t y);  /* 1000:174a C */
void draw_cs_world_es(void);                /* 1000:177d  REG (API 24): far ptr at ES:SI, BX=x, DI=y */
void draw_cs_world_ds(void);                /* 1000:17b5  REG: far ptr at DS:SI, BX=x, DI=y */
void draw_cs_screen_glyph(void);            /* 1000:17e9  REG (API 25) */
void edge_strip_update(void);               /* 1000:07a2..0816 + 08d8 (ISR part, renderer owns it) */
void set_draw_page_vars(void);              /* 1000:0819..0862 (ISR part) */
void player_draw(uint16_t frame);           /* 160a:1bcc C */
void hud_init(void);                        /* 160a:1cf8 C */
void hud_update(void);                      /* 160a:1da0 C */
void set_palette(uint16_t off, uint16_t seg, uint16_t first, uint16_t count);  /* 160a:2e6d C */
void fade_out(uint16_t step);               /* 160a:3018 C */
void fade_in(uint16_t step);                /* 160a:3042 C */
void pal_scale(uint16_t scale, uint16_t first, uint16_t end);  /* 160a:3074 C */
void pal_black(void);                       /* 160a:30c5 C */

/* ===================== player (player.c, map.c — player agent) =========================== */
void player_tick(void);                     /* 160a:1c5b C (player_update + camera) */
void player_update(void);                   /* 160a:1388 */
void read_input(void);                      /* 160a:1cb4 */
void set_player_pos_and_camera(uint16_t xf, uint16_t x, uint16_t yf, uint16_t y);   /* 160a:1174 C */
void player_move_collide(void);             /* 1000:1dd1 REG */
void probe_point(void);                     /* 1000:1b03 REG */
void probe_extent(void);                    /* 1000:1c16 REG */
void probe_step(void);                      /* 1000:1cbe REG */
void map_bit(void);                         /* 1000:20f7 REG (object shape cache, used by A4) */
void map_probe(void);                       /* 1000:21a3 REG */
void map_probe2(void);                      /* 1000:2274 REG */
void map_step(void);                        /* 1000:232a REG */

/* ===================== objects (objects.c, api.c — objects agent) ======================== */
void engine_step(void);                     /* 1000:1411 REG (AX = page segment) */
void activate_ring(void);                   /* 1000:1805 */
void activate_full(void);                   /* 1000:19f7 */
void slot_init_big(void);                   /* 1000:1922 REG */
void slot_init_pickup(void);                /* 1000:18f0 REG */
void popup_tick(void);                      /* 1000:413c */
/* API slots 0..25 (overlay stub ovl:0x0b+5*i): all REG.  api_call(i) dispatches by number. */
void api_call(int slot);
void A0_dying_fall(void);   void A1_draw(void);        void A2_anim_step(void);   void A3_motion_step(void);
void A4_map_move(void);     void A5_overlap(void);     void A6_touch(void);       void A7_next_anim(void);
void A8_set_anim(void);     void A9_restart_motion(void); void A10_despawn(void); void A11_kill(void);
void A12_despawn_unride(void); void A13_kill_unride(void); void A14_checkpoint(void); void A15_energy(void);
void A16_extra_life(void);  void A17_big_bonus(void);  void A18_time_bonus(void); void A19_sparkle(void);
void A20_add_score(void);   void A21_spawn(void);      void A22_platform(void);   void A23_random(void);
/* A24 = draw_cs_world_es, A25 = draw_cs_screen_glyph (render) */

/* ===================== overlays (ovl_*.c — overlay agents) =============================== */
/* Each overlay module registers its handlers with farcall_register_overlay(level_index, off, fn)
 * from one init function, and provides its per-frame entry (overlay +8). */
void ovl_register_all(void);
void ovl_frame_entry(void);                 /* CALLF [2d66]: dispatches to the live overlay's +8 entry */

/* ===================== flow (flow.c, menu.c, hiscore.c, intro.c — flow agent) ============ */
int  game_main(int argc, char **argv);      /* 160a:0009 */
void new_game(void);                        /* 160a:058c */
void level_load(void);                      /* 160a:062d */
void level_run(void);                       /* 160a:090d */
void story_screen(void);                    /* 160a:0fa0 */
uint16_t getkey_ascii(void);                /* 160a:31dd */
void rle_decode(uint16_t off, uint16_t seg);    /* 1000:478c C (ending picture, DAT 2) */
void anim_play(uint16_t a, uint16_t b, uint16_t c, uint16_t d);  /* 2343:0003 C (intro scenes, DAT 4-9) */
void mouse_init(void);                      /* 1000:0c6c  (+ callback 1000:0cbd semantics fed from plat_mouse) */
void mouse_poll(void);                      /* port glue: feeds plat_mouse() into the 1000:0cbd callback logic */
void endseq_run(void);                      /* DAT 89 ENDSEQ credits (endseq.c — endseq agent) */

#endif
