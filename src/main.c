#include "platform.h"
#include "vga.h"
#include "dat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Temporary pipeline test: show DAT entry <n> as a linear 320x200 picture with palette 10. */
int main(int argc, char **argv) {
    const char *datpath = "YOGHODAT.DAT";
    int show = 11;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) datpath = argv[++i];
        else if (!strcmp(argv[i], "--show") && i + 1 < argc) show = atoi(argv[++i]);
    }
    if (dat_open(datpath) != 0) { fprintf(stderr, "cannot open %s\n", datpath); return 1; }
    if (plat_init("Yogho Yogho") != 0) return 1;
    vga_reset();
    vga.line_offset = 80;
    size_t n;
    const uint8_t *pal = dat_entry(10, &n);
    vga_dac_write(0, pal, 256);
    const uint8_t *img = dat_entry(show, &n);
    for (size_t i = 0; i < n && i < 64000; i++) {
        int x = (int)(i % 320), y = (int)(i / 320);
        vga.plane[x & 3][y * 80 + (x >> 2)] = img[i];
    }
    while (!plat_should_quit()) plat_yield();
    return 0;
}
