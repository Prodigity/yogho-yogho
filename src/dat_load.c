/* DAT entry loading into conventional memory.  160a:12a0 dat_load / 160a:12e0 dat_read.
 * The original keeps the directory (first 0x178 bytes of YOGHODAT.DAT) at DGROUP:0x2258
 * (read by main at startup) and loads entry idx = [dir[idx], dir[idx+1]) to seg:off, crossing
 * segment boundaries linearly. */
#include "game.h"
#include "dat.h"
#include "log.h"

/* The original seeks relative to its tracked file position (DGROUP 0x1379, u32) and reads in
 * chunks of at most 0xfff0 bytes, leaving the last chunk's byte count in 0x1375 (u16) and
 * advancing the position by what was read (10-flow §3.16).  The bookkeeping is kept so DGROUP
 * matches the original; the data itself is copied from the loaded file. */
void dat_read(uint32_t pos, uint32_t size, uint16_t off, uint16_t seg) {   /* 160a:12e0 */
    size_t total;
    const uint8_t *f = dat_raw(&total);
    uint32_t filepos = pos;                                 /* lseek(h, pos - filepos, SEEK_CUR) */
    dgw_set(0x1379, (uint16_t)filepos); dgw_set(0x137b, (uint16_t)(filepos >> 16));
    int32_t left = (int32_t)size;
    uint32_t dst = lin(seg, off);
    do {
        uint32_t n = (left > 0 && left < 0xfff0) ? (uint32_t)left : 0xfff0;
        uint32_t got = filepos >= total ? 0 : (n < total - filepos ? n : (uint32_t)(total - filepos));
        if (dst + got > 0xA0000u && dst < 0xA0000u) log_gap("dat_read into VRAM range %05X+%X", (unsigned)dst, (unsigned)got);
        if (got) memcpy(&MEM[dst], f + filepos, got);
        dgw_set(0x1375, (uint16_t)got);
        filepos += got;
        dgw_set(0x1379, (uint16_t)filepos); dgw_set(0x137b, (uint16_t)(filepos >> 16));
        dst += 0xfff0;                                      /* seg += 0xfff */
        left -= 0xfff0;
    } while (left > 0);
}

uint32_t dat_load(uint16_t idx, uint16_t off, uint16_t seg) {               /* 160a:12a0 */
    uint32_t a = (uint32_t)dgw((uint16_t)(0x2258 + 4 * idx)) | (uint32_t)dgw((uint16_t)(0x225a + 4 * idx)) << 16;
    uint32_t b = (uint32_t)dgw((uint16_t)(0x2258 + 4 * idx + 4)) | (uint32_t)dgw((uint16_t)(0x225a + 4 * idx + 4)) << 16;
    uint32_t size = b - a;
    dat_read(a, size, off, seg);
    R.dx = (uint16_t)(size >> 16);
    return size;
}
