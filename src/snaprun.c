/* Differential test against the original (developer tool):
 *   yogho --snaprun <mem.bin> <vga.planes> <level> <schedule> <outdir> [lo hi]
 * Loads a DOSBox-X snapshot of conventional memory (640 KB, taken at a timer-ISR entry during
 * gameplay) and the 4 VGA planes, then for every frame i of <schedule> ("60x00,120x01,...",
 * DGROUP:3510 input per frame, as tools/trace.py uses): sets the input, writes DGROUP[lo:hi] to
 * <outdir>/fNNNNN.bin (the same moment trace.py dumps), and runs the port's timer ISR once.
 * The main-thread part of the original (level loop key polling) is not run. */
#include "game.h"
#include "vga.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load(const char *p, void *dst, size_t n) {
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    size_t r = fread(dst, 1, n, f); fclose(f);
    return r == n ? 0 : -1;
}

/* yogho --snapplay <mem.bin> <vga.planes> <level> <schedule> <outdir> [every]
 * Like --snaprun, but the whole program continues from the snapshot: the main thread is resumed
 * inside level_run's per-tick loop (where the original's main thread waits in wait_tick() when
 * the ISR breakpoint hits), so deaths, level ends, extra lives and music restarts run too.
 * Inputs/dumps are applied per tick by the test-script engine; DGROUP is dumped every <every>
 * frames (default 1). */
#include "platform.h"
#include "testscript.h"
#include "flow_ext.h"
int snapplay(int argc, char **argv) {
    if (argc < 5) return 2;
    if (load(argv[0], MEM, 0xA0000)) { fprintf(stderr, "bad mem\n"); return 1; }
    if (load(argv[1], vga.plane, sizeof vga.plane)) { fprintf(stderr, "bad vga\n"); return 1; }
    int level = atoi(argv[2]);
    const char *s = argv[3], *out = argv[4];
    int every = argc > 5 ? atoi(argv[5]) : 1;
    if (every < 1) every = 1;
    vga_reglist(0x3495, SEG_DG);
    vga.ac_index = 0x20;
    for (unsigned i = 0; i < 256; i++) {
        vga.dac[i][0] = DB(0x23d0 + 3 * i) & 63; vga.dac[i][1] = DB(0x23d1 + 3 * i) & 63; vga.dac[i][2] = DB(0x23d2 + 3 * i) & 63;
    }
    int frame = 0;
    if (s[0] == '@') {
        /* events file from tools/trace2.py: "<frame> bits|poke|dump <arg>" in frame order */
        FILE *f = fopen(s + 1, "r");
        if (!f) { fprintf(stderr, "no events file\n"); return 1; }
        char line[300];
        while (fgets(line, sizeof line, f)) {
            int fr; char c[16], a[260] = "";
            if (sscanf(line, "%d %15s %259[^\r\n]", &fr, c, a) < 2) continue;
            if (!strcmp(c, "dump")) {
                char path[512]; snprintf(path, sizeof path, "%s/f%05d.bin", out, fr);
                script_add((uint64_t)fr, "dumpdg", path);
            } else script_add((uint64_t)fr, c, a);
            if (fr > frame) frame = fr;
        }
        fclose(f);
        frame++;
    } else while (*s) {
        int n = atoi(s); const char *x = strchr(s, 'x'); if (!x) break;
        char v[8]; snprintf(v, sizeof v, "%02x", (unsigned)strtoul(x + 1, 0, 16));
        for (int k = 0; k < n; k++, frame++) {
            script_add((uint64_t)frame, "bits", v);
            if (frame % every == 0) {
                char path[512]; snprintf(path, sizeof path, "%s/f%05d.bin", out, frame);
                script_add((uint64_t)frame, "dumpdg", path);
            }
        }
        const char *c = strchr(s, ','); if (!c) break; s = c + 1;
    }
    script_add((uint64_t)frame, "quit", "");
    script_fast = 1;
    ovl_register_all();
    farcall_set_overlay(level);
    plat_set_isr(timer_isr);
    kbd_install();
    game_loop(1);
    fprintf(stderr, "snapplay: %d frames\n", frame);
    return 0;
}

int snaprun(int argc, char **argv) {
    if (argc < 5) return 2;
    if (load(argv[0], MEM, 0xA0000)) { fprintf(stderr, "bad mem\n"); return 1; }
    if (load(argv[1], vga.plane, sizeof vga.plane)) { fprintf(stderr, "bad vga\n"); return 1; }
    int level = atoi(argv[2]);
    const char *sched = argv[3], *out = argv[4];
    unsigned lo = argc > 5 ? (unsigned)strtoul(argv[5], 0, 16) : 0, hi = argc > 6 ? (unsigned)strtoul(argv[6], 0, 16) : 0x5200;
    /* game-mode VGA state (script DG:3495 + the ISR's own writes rebuild the rest) */
    vga_reglist(0x3495, SEG_DG);
    vga.ac_index = 0x20;
    for (unsigned i = 0; i < 256; i++) {              /* DAC = working palette */
        vga.dac[i][0] = DB(0x23d0 + 3 * i) & 63; vga.dac[i][1] = DB(0x23d1 + 3 * i) & 63; vga.dac[i][2] = DB(0x23d2 + 3 * i) & 63;
    }
    ovl_register_all();
    farcall_set_overlay(level);
    int frame = 0;
    const char *s = sched;
    while (*s) {
        int n = atoi(s); const char *x = strchr(s, 'x'); if (!x) break;
        unsigned v = (unsigned)strtoul(x + 1, 0, 16);
        for (int k = 0; k < n; k++, frame++) {
            DB(0x3510) = (uint8_t)v;
            char path[512]; snprintf(path, sizeof path, "%s/f%05d.bin", out, frame);
            FILE *f = fopen(path, "wb"); if (f) { fwrite(P(SEG_DG, (uint16_t)lo), 1, hi - lo, f); fclose(f); }
            timer_isr();
        }
        const char *c = strchr(s, ','); if (!c) break; s = c + 1;
    }
    fprintf(stderr, "snaprun: %d frames\n", frame);
    return 0;
}
