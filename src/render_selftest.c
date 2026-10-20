/* Renderer self-test: level 1 background at camera (4800, 80) + level-start HUD, drawn by the
 * ported routines into the VGA model and scanned out to build/render_L1.raw (320x200 RGBA8888).
 * Reference: DOSBox screenshot runs/shot_L1_idle.png (camera (4800,80), 30-render.md intro).
 *
 * The level state is built the way the original builds it (160a:0333 directory, 160a:062d
 * level load, 160a:06e0..072f palettes) at the segments of the L1-start RAM snapshot
 * (mem_L1_start.bin: [13fc]=3488 map, [2ce2]=35b4, [2cde]=36b4, [13fe]=326b, [1402]=70d4).
 * Only the DAC upload is done inline (it is palette_tick's job in the running game). */
#include "game.h"
#include "render_ext.h"
#include "dat.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>

static uint16_t load_at(uint16_t idx, uint16_t seg) {      /* returns the next free paragraph */
    uint32_t size = dat_load(idx, 0, seg);
    return (uint16_t)(seg + ((size + 15) >> 4));
}

int render_selftest(void) {
    size_t n;
    const uint8_t *raw = dat_raw(&n);
    if (!raw || n < 0x178) { log_info("render_selftest: DAT not open"); return 1; }
    memcpy(P(SEG_DG, 0x2258), raw, 0x178);                  /* 160a:0333: directory */

    const uint16_t seg_free = 0x3488, seg74 = 0x326b, seg_buf = 0x70d4;
    dgw_set(0x13fa, 0); dgw_set(0x13fc, seg_free); dgw_set(0x13fe, seg74); dgw_set(0x1402, seg_buf);
    DB(0x12d) = 0;                                          /* level index: L1 */
    dat_load(74, 0, seg74);                                 /* 160a:034e: HUD patches */

    video_init();                                           /* row tables, mode 13h */
    vga_reglist(0x3495, SEG_DG);                            /* game script (split HUD) */

    /* HUD at level start: score 0, 5 lives, 0 items, full energy; caches as 160a:0a87 */
    dgw_set(0xca, 0); dgw_set(0xcc, 0);
    dgw_set(0xbe, 5); dgw_set(0xc0, 0);
    DB(0xb2) = 5; DB(0xb1) = 0;
    dgw_set(0xc8, 0xffff); dgw_set(0xd0, 0xffff); dgw_set(0xce, 0xffff); dgw_set(0xc6, 0xffff);
    hud_init();                                             /* uses [13fc] as scratch */

    /* palettes: menu DAT10 (160a:208d), DAT3 -> 0x40.., level palette -> 0x00.. */
    uint16_t desc = dgw((uint16_t)(0x1b07 + 2 * DB(0x12d)));
    dat_load(10, 0, seg_free); set_palette(0, seg_free, 0, 0x100);
    dat_load(3, 0, seg_free);  set_palette(0, seg_free, 0x40, 0xc0);
    dat_load(dgw((uint16_t)(desc + 5)), 0, seg_free); set_palette(0, seg_free, 0, 0x40);

    /* level data, consecutive paragraphs from [13fc] (160a:0732..) */
    uint16_t s = seg_free;
    dgw_set(0x2cda, s); s = load_at(dgw((uint16_t)(desc + 0x0b)), s);   /* map */
    dgw_set(0x2ce2, s); s = load_at(dgw((uint16_t)(desc + 0x07)), s);   /* metatiles */
    dgw_set(0x2cde, s); s = load_at(dgw((uint16_t)(desc + 0x09)), s);   /* tiles */
    uint16_t mw = dgw((uint16_t)(desc + 1)), mh = dgw((uint16_t)(desc + 3));
    dgw_set(0x2da6, (uint16_t)(mw * 2));                    /* 160a:06a5 */
    for (uint16_t r = 0, o = 0; r < mh; r++, o = (uint16_t)(o + mw * 2))   /* 160a:06ad */
        dgw_set((uint16_t)(0x32f6 + 2 * r), o);
    patch_render_segs();

    /* DAC upload of the master palette (as palette_tick does for dac_schedule(0x4e20,0,0x100)) */
    io_out(0x3c8, 0);
    for (unsigned i = 0; i < 0x300; i++) io_out(0x3c9, DB(0x4e20 + i));

    /* camera and initial fill (160a:1174 + 1000:0e1a) */
    const uint16_t sx = 4800, sy = 80;
    dgw_set(0x2cb4, sx); dgw_set(0x2cb6, sy);
    dgw_set(0x2cb8, sx & 0xfff0); dgw_set(0x2cba, sy & 0xfff0);
    R.ds = SEG_DG;
    bg_fill();

    /* one draw iteration into page A, then display it as the next ISR does */
    dgw_set(0x2d9c, 0);
    edge_strip_update();                                    /* no change: nothing streamed */
    set_draw_page_vars();
    bg_to_page();                                           /* AX = page start (0xA210) */
    io_outw(0x3d4, 0x210c);                                 /* CRTC start 0x2100 + ((sx>>2)&3) */
    io_outw(0x3d4, (uint16_t)(((uint8_t)(0x00 + ((sx >> 2) & 3)) << 8) | 0x0d));
    (void)io_in(0x3da);
    io_out(0x3c0, 0x33); io_out(0x3c0, (uint8_t)((sx & 3) * 2));   /* pel panning, PAS on */

    uint32_t *rgba = malloc(320 * 200 * sizeof *rgba);
    if (!rgba) return 1;
    vga_scanout(rgba);
    FILE *f = fopen("build/render_L1.raw", "wb");
    if (!f) { free(rgba); log_info("render_selftest: cannot write build/render_L1.raw"); return 1; }
    fwrite(rgba, sizeof *rgba, 320 * 200, f);
    fclose(f);
    free(rgba);
    log_info("render_selftest: wrote build/render_L1.raw (320x200 RGBA)");
    return 0;
}
