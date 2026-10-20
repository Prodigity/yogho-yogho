#include "vga.h"
#include <string.h>

Vga vga;

void vga_reset(void) {
    memset(&vga, 0, sizeof vga);
    vga.map_mask = 0x0f;
    vga.line_offset = 0x60;   /* 384-pixel virtual line, set by the game's CRTC table */
    vga.line_compare = 0x3ff;
}

void vga_dac_write(uint8_t index, const uint8_t *rgb6, int count) {
    for (int i = 0; i < count; i++) {
        uint8_t c = (uint8_t)(index + i);
        vga.dac[c][0] = rgb6[i * 3 + 0] & 0x3f;
        vga.dac[c][1] = rgb6[i * 3 + 1] & 0x3f;
        vga.dac[c][2] = rgb6[i * 3 + 2] & 0x3f;
    }
}

static inline uint32_t dac_rgba(uint8_t c) {
    /* 6-bit to 8-bit the way DOSBox does it: (v << 2) | (v >> 4) */
    uint32_t r = (vga.dac[c][0] << 2) | (vga.dac[c][0] >> 4);
    uint32_t g = (vga.dac[c][1] << 2) | (vga.dac[c][1] >> 4);
    uint32_t b = (vga.dac[c][2] << 2) | (vga.dac[c][2] >> 4);
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

void vga_scanout(uint32_t *rgba, int width, int height) {
    /* Mode X: pixel x of a row lives in plane (x & 3) at byte (x >> 2); the row starts at
     * start_addr + row * line_offset; pel panning shifts by whole pixels (value / 2 in 256-colour mode). */
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = dac_rgba((uint8_t)i);
    int pan = (vga.pel_pan & 7) >> 1;
    for (int y = 0; y < height; y++) {
        uint16_t row = (uint16_t)((y >= vga.line_compare) ? (y - vga.line_compare - 1) * vga.line_offset
                                                          : vga.start_addr + y * vga.line_offset);
        uint32_t *out = rgba + y * width;
        for (int x = 0; x < width; x++) {
            int px = x + (y >= vga.line_compare ? 0 : pan);
            uint16_t off = (uint16_t)(row + (px >> 2));
            out[x] = pal[vga.plane[px & 3][off]];
        }
    }
}
