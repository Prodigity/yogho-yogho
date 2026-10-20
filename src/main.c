/* Program entry: command line, data files, platform, then the original's main (160a:0009).
 *   yogho [--data YOGHODAT.DAT] [--exe YOGHO.EXE]
 *   yogho --opltrace <dat-entry> <ticks> [...]      AdLib register trace (selftest.c)
 *   yogho --script <file> [--fast]                deterministic input/dump script (testscript.c)
 *   yogho --render-selftest|--player-selftest|--objects-selftest
 * YOGHOHI.DAT is read/written next to the data file (the original keeps it next to YOGHO.EXE). */
#include "platform.h"
#include "game.h"
#include "audio.h"
#include "vga.h"
#include "dat.h"
#include "mem.h"
#include "selftest.h"
#include "testscript.h"
#include <stdio.h>
#include <string.h>

/* module self-tests: render_selftest / player_selftest / objects_selftest (called after the data
 * files are loaded, before any platform init) */
#include "render_ext.h"
#include "player_ext.h"
#include "objects_ext.h"

int main(int argc, char **argv) {
    const char *datpath = "YOGHODAT.DAT", *exepath = "YOGHO.EXE";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) datpath = argv[++i];
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exepath = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) { if (script_load(argv[++i])) { fprintf(stderr, "cannot read script\n"); return 1; } }
        else if (!strcmp(argv[i], "--fast")) script_fast = 1;
        else if (!strcmp(argv[i], "--opltrace") && i + 2 < argc) {
            if (dat_open(datpath) || mem_load_exe(exepath)) { fprintf(stderr, "data files missing\n"); return 1; }
            return selftest_opltrace(argc - i - 1, argv + i + 1);
        } else if (!strcmp(argv[i], "--render-selftest") || !strcmp(argv[i], "--player-selftest") ||
                   !strcmp(argv[i], "--objects-selftest")) {
            if (dat_open(datpath) || mem_load_exe(exepath)) { fprintf(stderr, "data files missing\n"); return 1; }
            if (argv[i][2] == 'r') return render_selftest();
            if (argv[i][2] == 'p') return player_selftest();
            return objects_selftest();
        }
    }
    if (dat_open(datpath) != 0) { fprintf(stderr, "cannot open %s\n", datpath); return 1; }
    if (mem_load_exe(exepath) != 0) { fprintf(stderr, "cannot open %s\n", exepath); return 1; }
    if (plat_init("Yogho Yogho") != 0) return 1;
    audio_init();
    plat_set_tick_hook(audio_tick);
    vga_reset();
    /* argv[0] for the game = the data file path: set_exe_dir derives YOGHOHI.DAT's folder from it */
    char *gargv[2] = { (char *)datpath, NULL };
    ovl_register_all();
    int r = game_main(1, gargv);
    plat_quit();
    return r;
}
