/* Renderer exports beyond game.h (render agent).  To be merged into game.h by the lead. */
#ifndef YOGHO_RENDER_EXT_H
#define YOGHO_RENDER_EXT_H
#include <stdint.h>

/* Developer self-test (render_selftest.c): needs MEM loaded by mem_load_exe() and the DAT opened
 * by dat_open().  Builds the level-1 state, renders the background at camera (4800,80) plus the
 * HUD through the real routines, and writes build/render_L1.raw (320x200 RGBA8888, vga_scanout).
 * Returns 0 on success. */
int render_selftest(void);

#endif
