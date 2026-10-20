/* VGA hardware model used by the ported code.
 *
 * The original programs the VGA directly (Mode X: unchained 256 colours, 4 planes, 96-byte
 * rows, page flipping through the CRTC start address, a line-compare split for the HUD, pel
 * panning, DAC uploads, write mode 1 latch copies).  The port keeps those exact semantics so
 * ported routines can do what the original does, port for port:
 *   - io_out()/io_in() on 3C0/3C4/3C5/3C8/3C9/3CE/3CF/3D4/3D5/3DA,
 *   - CPU reads/writes to A000:0000-AFFF:FFFF go through the planar logic (mem.h routes them),
 *   - the shown frame is scanned out from the CRTC/AC registers like the hardware does.
 * Retrace polling (3DA) always reports "in retrace": the timing the original got from
 * spinning on it is provided by the ISR scheduling (platform.h) instead.
 */
#ifndef YOGHO_VGA_H
#define YOGHO_VGA_H
#include <stdint.h>

#define VGA_PLANE_SIZE 0x10000

typedef struct {
    uint8_t plane[4][VGA_PLANE_SIZE];
    uint8_t latch[4];
    uint8_t seq_index, seq[8];       /* seq[2] = map mask, seq[4] = memory mode */
    uint8_t gc_index, gc[9];         /* gc[4] = read map, gc[5] = mode (write mode bits 0-1), gc[8] = bit mask */
    uint8_t crtc_index, crtc[0x19];
    uint8_t ac_index, ac_flipflop, ac[0x15];
    uint8_t dac[256][3];             /* 6-bit */
    uint8_t dac_windex, dac_wcomp, dac_rindex, dac_rcomp;
    uint8_t misc;
} Vga;

extern Vga vga;

void vga_reset(void);        /* registers as after INT 10h mode 13h */
void vga_out(uint16_t port, uint8_t val);
uint8_t vga_in(uint16_t port);
uint8_t vga_mem_read(uint16_t off);       /* CPU read at A000:off */
void vga_mem_write(uint16_t off, uint8_t v);
/* Scan out the visible 320x200 frame to RGBA8888 (R in the low byte). */
void vga_scanout(uint32_t *rgba);
/* Convenience for tests/tools. */
void vga_dac_write(uint8_t index, const uint8_t *rgb6, int count);

/* x86-style port I/O used by ported code: io_out = OUT DX,AL; io_outw = OUT DX,AX (index, data). */
static inline void io_out(uint16_t port, uint8_t v) { vga_out(port, v); }
static inline void io_outw(uint16_t port, uint16_t v) { vga_out(port, (uint8_t)v); vga_out((uint16_t)(port + 1), (uint8_t)(v >> 8)); }
static inline uint8_t io_in(uint16_t port) { return vga_in(port); }

#endif
