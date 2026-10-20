/* DAT entry loading into conventional memory.  160a:12a0 dat_load / 160a:12e0 dat_read.
 * The original keeps the directory (first 0x178 bytes of YOGHODAT.DAT) at DGROUP:0x2258
 * (read by main at startup) and loads entry idx = [dir[idx], dir[idx+1]) to seg:off, crossing
 * segment boundaries linearly. */
#include "game.h"
#include "dat.h"
#include "log.h"

void dat_read(uint32_t pos, uint32_t size, uint16_t off, uint16_t seg) {   /* 160a:12e0 */
    size_t total;
    const uint8_t *f = dat_raw(&total);
    uint32_t dst = lin(seg, off);
    if (pos > total) return;
    if (size > total - pos) size = (uint32_t)(total - pos);
    if (dst + size > 0xA0000u && dst < 0xA0000u) log_gap("dat_read into VRAM range %05X+%X", (unsigned)dst, (unsigned)size);
    memcpy(&MEM[dst], f + pos, size);
}

uint32_t dat_load(uint16_t idx, uint16_t off, uint16_t seg) {               /* 160a:12a0 */
    uint32_t a = (uint32_t)dgw((uint16_t)(0x2258 + 4 * idx)) | (uint32_t)dgw((uint16_t)(0x225a + 4 * idx)) << 16;
    uint32_t b = (uint32_t)dgw((uint16_t)(0x2258 + 4 * idx + 4)) | (uint32_t)dgw((uint16_t)(0x225a + 4 * idx + 4)) << 16;
    uint32_t size = b - a;
    dat_read(a, size, off, seg);
    R.dx = (uint16_t)(size >> 16);
    return size;
}
