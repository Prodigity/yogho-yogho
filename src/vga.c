#include "vga.h"
#include <string.h>

Vga vga;

void vga_reset(void) {
    memset(&vga, 0, sizeof vga);
    /* mode 13h defaults that matter here; the game then applies its register scripts (1000:0349) */
    vga.seq[2] = 0x0f; vga.seq[4] = 0x0e;               /* chain-4 on in 13h */
    vga.gc[5] = 0x40; vga.gc[6] = 0x05; vga.gc[8] = 0xff;
    vga.crtc[0x07] = 0x1f; vga.crtc[0x09] = 0x41; vga.crtc[0x11] = 0x8e;
    vga.crtc[0x13] = 0x28; vga.crtc[0x14] = 0x40; vga.crtc[0x17] = 0xa3; vga.crtc[0x18] = 0xff;
    vga.ac[0x10] = 0x41; vga.ac[0x13] = 0;
    vga.ac_index = 0x20;
}

void vga_out(uint16_t port, uint8_t v) {
    switch (port) {
    case 0x3c0:
        if (!vga.ac_flipflop) vga.ac_index = v;
        else if ((vga.ac_index & 0x1f) < 0x15) vga.ac[vga.ac_index & 0x1f] = v;
        vga.ac_flipflop ^= 1;
        break;
    case 0x3c2: vga.misc = v; break;
    case 0x3c4: vga.seq_index = v & 7; break;
    case 0x3c5: vga.seq[vga.seq_index] = v; break;
    case 0x3c7: vga.dac_rindex = v; vga.dac_rcomp = 0; break;
    case 0x3c8: vga.dac_windex = v; vga.dac_wcomp = 0; break;
    case 0x3c9:
        vga.dac[vga.dac_windex][vga.dac_wcomp] = v & 0x3f;
        if (++vga.dac_wcomp == 3) { vga.dac_wcomp = 0; vga.dac_windex++; }
        break;
    case 0x3ce: vga.gc_index = v & 0x0f; break;
    case 0x3cf: if (vga.gc_index < 9) vga.gc[vga.gc_index] = v; break;
    case 0x3d4: vga.crtc_index = v & 0x1f; break;
    case 0x3d5: {
        uint8_t i = vga.crtc_index;
        if (i >= 0x19) break;
        /* CR11 bit 7 write-protects CR00-CR07 except CR07 bit 4 (line compare bit 8) */
        if ((vga.crtc[0x11] & 0x80) && i <= 7) {
            if (i == 7) vga.crtc[7] = (uint8_t)((vga.crtc[7] & ~0x10) | (v & 0x10));
            break;
        }
        vga.crtc[i] = v;
        break;
    }
    default: break;   /* PIT, PIC EOI, AdLib (handled by the driver), joystick... */
    }
}

uint8_t vga_in(uint16_t port) {
    switch (port) {
    case 0x3c0: return vga.ac_index;
    case 0x3c1: return vga.ac[vga.ac_index & 0x1f];
    case 0x3c5: return vga.seq[vga.seq_index];
    case 0x3c9: {
        uint8_t r = vga.dac[vga.dac_rindex][vga.dac_rcomp];
        if (++vga.dac_rcomp == 3) { vga.dac_rcomp = 0; vga.dac_rindex++; }
        return r;
    }
    case 0x3cf: return vga.gc_index < 9 ? vga.gc[vga.gc_index] : 0;
    case 0x3d5: return vga.crtc_index < 0x19 ? vga.crtc[vga.crtc_index] : 0;
    case 0x3da: case 0x3ba:
        vga.ac_flipflop = 0;          /* reading input status resets the AC flip-flop */
        return 0x09;                  /* always "in vertical retrace" (see vga.h) */
    default: return 0xff;
    }
}

uint8_t vga_mem_read(uint16_t off) {
    if (vga.seq[4] & 0x08) return vga.plane[off & 3][off >> 2];       /* chain-4 (mode 13h) */
    for (int p = 0; p < 4; p++) vga.latch[p] = vga.plane[p][off];
    return vga.plane[vga.gc[4] & 3][off];
}

void vga_mem_write(uint16_t off, uint8_t v) {
    if (vga.seq[4] & 0x08) { if (vga.seq[2] & (1 << (off & 3))) vga.plane[off & 3][off >> 2] = v; return; }
    uint8_t mask = vga.seq[2];
    switch (vga.gc[5] & 3) {
    case 1:   /* write mode 1: store the latches */
        for (int p = 0; p < 4; p++) if (mask & (1 << p)) vga.plane[p][off] = vga.latch[p];
        break;
    default:  /* write mode 0 without rotate/logical ops/set-reset (all the game uses) */
        for (int p = 0; p < 4; p++) if (mask & (1 << p)) {
            uint8_t bm = vga.gc[8];
            vga.plane[p][off] = (uint8_t)((v & bm) | (vga.latch[p] & ~bm));
        }
        break;
    }
}

void vga_dac_write(uint8_t index, const uint8_t *rgb6, int count) {
    for (int i = 0; i < count; i++) {
        uint8_t c = (uint8_t)(index + i);
        vga.dac[c][0] = rgb6[i * 3] & 0x3f;
        vga.dac[c][1] = rgb6[i * 3 + 1] & 0x3f;
        vga.dac[c][2] = rgb6[i * 3 + 2] & 0x3f;
    }
}

void vga_scanout(uint32_t *rgba) {
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = (vga.dac[i][0] << 2) | (vga.dac[i][0] >> 4);
        uint32_t g = (vga.dac[i][1] << 2) | (vga.dac[i][1] >> 4);
        uint32_t b = (vga.dac[i][2] << 2) | (vga.dac[i][2] >> 4);
        pal[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
    if (!(vga.ac_index & 0x20)) {     /* palette address source clear: display blanked */
        for (int i = 0; i < 320 * 200; i++) rgba[i] = 0xff000000u;
        return;
    }
    uint16_t start = (uint16_t)(vga.crtc[0x0c] << 8 | vga.crtc[0x0d]);
    uint16_t pitch = (uint16_t)(vga.crtc[0x13] * 2);     /* byte mode */
    unsigned lc = vga.crtc[0x18] | ((vga.crtc[0x07] & 0x10) << 4) | ((vga.crtc[0x09] & 0x40) << 3);
    int dscan = (vga.crtc[0x09] & 0x80) ? 2 : ((vga.crtc[0x09] & 0x1f) + 1);
    int pan = (vga.ac[0x13] & 7) >> 1;
    int chain4 = (vga.seq[4] & 0x08) != 0;
    uint16_t addr = start;
    int after_lc = 0;
    for (int y = 0; y < 200; y++) {
        uint32_t *out = rgba + y * 320;
        int ppan = (after_lc && (vga.ac[0x10] & 0x20)) ? 0 : pan;
        if (chain4) {
            for (int x = 0; x < 320; x++) {
                uint32_t a = (uint32_t)(y * 320 + x);
                out[x] = pal[vga.plane[a & 3][(a >> 2) & 0xffff]];
            }
        } else {
            for (int x = 0; x < 320; x++) {
                int px = x + ppan;
                out[x] = pal[vga.plane[px & 3][(uint16_t)(addr + (px >> 2))]];
            }
        }
        /* advance; the line compare is checked per scan line (each row = dscan scan lines) */
        int split = 0;
        for (int s = 0; s < dscan; s++) {
            unsigned scan = (unsigned)(y * dscan + s);
            if (scan == lc) split = 1;
        }
        if (split) { addr = 0; after_lc = 1; }
        else addr = (uint16_t)(addr + pitch);
    }
}
