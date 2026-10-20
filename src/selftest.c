/* Developer self-tests (command line), used to diff the port against reference streams.
 *   yogho --opltrace <dat-entry> <ticks> [--boot] [--sfx TICK:NUM]... [--arg N] [--countdown N] [--base OFF]
 * prints "<tick> <reg> <val>" for every OPL write that reaches the chip, in the format of
 * scripts/music_dump.py --emit. */
#include "selftest.h"
#include "adlib.h"
#include "dat.h"
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *trace_out;
static unsigned trace_tick;
static void trace_sink(uint8_t reg, uint8_t val) { fprintf(trace_out, "%u %02x %02x\n", trace_tick, reg, val); }

int selftest_opltrace(int argc, char **argv) {
    int entry = atoi(argv[0]), ticks = atoi(argv[1]), boot = 0;
    unsigned arg = 0, base = 0; int countdown = -1;
    int sfx_t[64], sfx_n[64], nsfx = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--boot")) boot = 1;
        else if (!strcmp(argv[i], "--arg") && i + 1 < argc) arg = (unsigned)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--base") && i + 1 < argc) base = (unsigned)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--countdown") && i + 1 < argc) countdown = (int)strtol(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--sfx") && i + 1 < argc && nsfx < 64) {
            sscanf(argv[++i], "%d:%d", &sfx_t[nsfx], &sfx_n[nsfx]); nsfx++;
        }
    }
    trace_out = stdout;
    adlib_set_sink(trace_sink);
    size_t n;
    const uint16_t seg = 0x5000;      /* any free segment; offsets are what matter */
    if (boot) {
        AdlibDetect();
        const uint8_t *s = dat_entry(70, &n);
        memcpy(P(0x4000, 0x10bc), s, n);
        SfxLoad(0x4000, 0x10bc);
    }
    const uint8_t *m = dat_entry(entry, &n);
    memcpy(P(seg, (uint16_t)base), m, n);
    MusicPlay(seg, (uint16_t)base, (uint16_t)arg);
    if (countdown >= 0) dgw_set(0x2d7c, (uint16_t)countdown);
    dgw_set(0x2ca6, 0);
    for (int t = 1; t <= ticks; t++) {
        trace_tick = (unsigned)t;
        for (int k = 0; k < nsfx; k++) if (sfx_t[k] == t) dgw_set(0x2ca4, (uint16_t)sfx_n[k]);
        isr_music_hook();
    }
    return 0;
}
