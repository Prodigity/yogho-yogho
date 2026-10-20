/* Minimal VGA model used by the ported renderer.
 *
 * The original draws in Mode X (unchained 256-colour, 4 planes) on a 384-pixel virtual line
 * (0x60 bytes per plane row), flips pages through the CRTC start address and scrolls with
 * pel panning (see teardown 30-render / 20-timing).  We keep the same memory model so the
 * ported blitters write exactly what the original writes, and the frame shown is produced the
 * way the hardware scans it out.  Nothing here is game logic.
 */
#ifndef YOGHO_VGA_H
#define YOGHO_VGA_H
#include <stdint.h>

#define VGA_PLANE_SIZE 0x10000

typedef struct {
    uint8_t plane[4][VGA_PLANE_SIZE];
    uint8_t map_mask;        /* sequencer index 2 (planes enabled for CPU writes) */
    uint8_t read_map;        /* graphics controller index 4 */
    uint8_t latch[4];
    uint16_t start_addr;     /* CRTC 0x0c/0x0d */
    uint8_t pel_pan;         /* attribute controller 0x13 (0..7, in 256-colour mode pixel = value/2) */
    uint16_t line_offset;    /* bytes per plane row (CRTC 0x13 * 2) */
    uint16_t line_compare;   /* CRTC line compare (split screen); 0x3ff = off */
    uint8_t dac[256][3];     /* 6-bit */
    uint8_t dac_write_index;
    uint8_t dac_component;
} Vga;

extern Vga vga;

void vga_reset(void);
/* CPU write through the map mask (write mode 0, no logical ops — what the game uses for drawing). */
static inline void vga_write(uint16_t off, uint8_t v) {
    if (vga.map_mask & 1) vga.plane[0][off] = v;
    if (vga.map_mask & 2) vga.plane[1][off] = v;
    if (vga.map_mask & 4) vga.plane[2][off] = v;
    if (vga.map_mask & 8) vga.plane[3][off] = v;
}
static inline uint8_t vga_read(uint16_t off) { return vga.plane[vga.read_map & 3][off]; }
/* Write mode 1 latch copy (used for VRAM-to-VRAM tile copies): read fills latches, write stores them. */
static inline void vga_latch_copy(uint16_t dst, uint16_t src) {
    for (int p = 0; p < 4; p++) vga.latch[p] = vga.plane[p][src];
    for (int p = 0; p < 4; p++) if (vga.map_mask & (1 << p)) vga.plane[p][dst] = vga.latch[p];
}
void vga_dac_write(uint8_t index, const uint8_t *rgb6, int count);
/* Scan out the visible 320x200 frame to RGBA8888. */
void vga_scanout(uint32_t *rgba, int width, int height);

#endif
