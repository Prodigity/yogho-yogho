#include "platform.h"
#include "vga.h"
#include "dat.h"
#include "mem.h"
#include "selftest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *datpath = "YOGHODAT.DAT", *exepath = "YOGHO.EXE";
    int show = 11;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) datpath = argv[++i];
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exepath = argv[++i];
        else if (!strcmp(argv[i], "--show") && i + 1 < argc) show = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--opltrace") && i + 2 < argc) {
            if (dat_open(datpath) || mem_load_exe(exepath)) { fprintf(stderr, "data files missing\n"); return 1; }
            return selftest_opltrace(argc - i - 1, argv + i + 1);
        }
    }
    if (dat_open(datpath) != 0) { fprintf(stderr, "cannot open %s\n", datpath); return 1; }
    if (mem_load_exe(exepath) != 0) { fprintf(stderr, "cannot open %s\n", exepath); return 1; }
    if (plat_init("Yogho Yogho") != 0) return 1;
    vga_reset();
    size_t n;
    const uint8_t *pal = dat_entry(10, &n);
    vga_dac_write(0, pal, 256);
    const uint8_t *img = dat_entry(show, &n);
    for (size_t i = 0; i < n && i < 64000; i++) wb(0xA000, (uint16_t)i, img[i]);
    while (!plat_should_quit()) plat_yield();
    return 0;
}
