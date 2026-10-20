/* Deterministic test scripts (developer tool, desktop):  yogho --script file [--fast]
 *
 * One command per line, applied right BEFORE the ISR call of the given tick (tick = number of
 * timer ISR calls so far, plat_ticks()):
 *   <tick> key <scancode-hex> <1=down|0=up>   keyboard make/break through the game's kbd handler
 *   <tick> mouse <x> <y> <buttons>            mouse state as plat_mouse() reports it
 *   <tick> dumpdg <path>                      DGROUP (64 KB)
 *   <tick> dumpmem <path>                     conventional memory 0..A0000
 *   <tick> dumpvga <path>                     4 VGA planes (4 x 64 KB)
 *   <tick> shot <path>                        320x200 frame as binary PPM
 *   <tick> watch <dg-hex> [...]               from now on print these DGROUP words every tick
 *   <tick> quit
 * --fast runs ticks back to back without waiting for real time. */
#include "testscript.h"
#include "mem.h"
#include "vga.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t tick; char cmd[16]; char arg[260]; } Ev;
static Ev *ev; static int nev, pos;
static uint16_t watch[16]; static int nwatch;
int script_fast, script_mouse_override, script_mx, script_my, script_mb;

int script_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[400];
    while (fgets(line, sizeof line, f)) {
        unsigned long long t; char c[16]; char a[260] = "";
        if (line[0] == '#' || sscanf(line, "%llu %15s %259[^\r\n]", &t, c, a) < 2) continue;
        ev = realloc(ev, sizeof(Ev) * (size_t)(nev + 1));
        ev[nev].tick = t; strcpy(ev[nev].cmd, c); strcpy(ev[nev].arg, a); nev++;
    }
    fclose(f);
    return 0;
}

static void write_file(const char *p, const void *d, size_t n) {
    FILE *f = fopen(p, "wb"); if (!f) return; fwrite(d, 1, n, f); fclose(f);
}

int script_tick(uint64_t tick, key_fn keyh) {
    int quit = 0;
    while (pos < nev && ev[pos].tick <= tick) {
        Ev *e = &ev[pos++];
        if (!strcmp(e->cmd, "key") && keyh) {
            unsigned sc, down; sscanf(e->arg, "%x %u", &sc, &down);
            int ext = sc >= 0x47 && sc <= 0x53 && sc != 0x4a && sc != 0x4e;   /* grey cursor block */
            keyh((uint8_t)sc, (int)down, ext);
        } else if (!strcmp(e->cmd, "mouse")) {
            sscanf(e->arg, "%d %d %d", &script_mx, &script_my, &script_mb); script_mouse_override = 1;
        } else if (!strcmp(e->cmd, "dumpdg")) write_file(e->arg, P(SEG_DG, 0), 0x10000);
        else if (!strcmp(e->cmd, "dumpmem")) write_file(e->arg, MEM, 0xA0000);
        else if (!strcmp(e->cmd, "dumpvga")) write_file(e->arg, vga.plane, sizeof vga.plane);
        else if (!strcmp(e->cmd, "shot")) {
            static uint32_t px[320 * 200]; static uint8_t rgb[320 * 200 * 3];
            vga_scanout(px);
            for (int i = 0; i < 320 * 200; i++) { rgb[3*i] = (uint8_t)px[i]; rgb[3*i+1] = (uint8_t)(px[i] >> 8); rgb[3*i+2] = (uint8_t)(px[i] >> 16); }
            FILE *f = fopen(e->arg, "wb");
            if (f) { fprintf(f, "P6 320 200 255\n"); fwrite(rgb, 1, sizeof rgb, f); fclose(f); }
        } else if (!strcmp(e->cmd, "watch")) {
            char *s = e->arg; nwatch = 0;
            while (*s && nwatch < 16) { watch[nwatch++] = (uint16_t)strtoul(s, &s, 16); while (*s == ' ') s++; }
        } else if (!strcmp(e->cmd, "quit")) quit = 1;
    }
    if (nwatch) {
        printf("%llu", (unsigned long long)tick);
        for (int i = 0; i < nwatch; i++) printf(" %04x", dgw(watch[i]));
        printf("\n");
    }
    return quit;
}
