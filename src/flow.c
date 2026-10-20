/* Game flow: main (160a:0009), new game, level load, level run (incl. the L6 ending and the B1
 * descent transition), story screen, hardware init/shutdown, YOGHOHI.DAT.
 * Spec: teardown/10-flow-frontend.md §3.0-3.6, 20-timing-input.md §3.5/3.7, 70-sound.md §3.5.
 * Addresses are Ghidra (segment 160a unless noted); DGROUP offsets are bare hex.
 *
 * The original's busy-waits on ISR-driven variables become loops that call plat_yield() (the
 * platform runs the timer ISR from inside it).  Where the original would spin forever waiting
 * for input, the port additionally gives up when the host window is closed (marked "port:"). */
#include "game.h"
#include "flow_ext.h"
#include "objects_ext.h"   /* overlay_link 1000:04d2 */
#include "adlib.h"
#include "platform.h"
#include "dat.h"
#include "log.h"
#include <stdio.h>
#include <string.h>


#define W(o)     dgw(o)
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))

/* ---- memory layout (DOS allocation reproduced) ----
 * main allocates allocmem(0x5869 - [0x5124]) paragraphs into [0x13fe].  Under DOSBox (the
 * reference for all dumps) DOS returns segment 0x326B (L1 RAM snapshot mem_L1_start.bin:
 * [0x13fe] = 0x326B, [0x13fc] = 0x3488, [0x1402] = 0x70D4 = 0x326B + 0x5869 - 0x1A00, video
 * state buffer [0x2cae] = 0x8AD5 = first paragraph after the block).  The port hard-codes that
 * segment so every derived segment ([0x13fc], [0x1402], level / menu / intro buffers, overlay
 * segment 0x4FCE for L1) equals the original's. */
#define DOS_ALLOC_SEG 0x326Bu

/* ---- C runtime file handles (plumbing) ----
 * Borland's _open/_creat hand out DOS handles from 5 upwards (0-4 = std streams); the game never
 * closes YOGHOHI.DAT, so the values end up as in the snapshot ([0x1373] = 5, [0x1371] = 6). */
static uint16_t next_handle = 5;

/* Host path of the data file: the port keeps YOGHOHI.DAT next to YOGHODAT.DAT (the original
 * keeps both next to YOGHO.EXE), so game_main receives that path as argv[0]. */
static const char *exe_path;

static int host_quit(void) { return plat_should_quit(); }   /* port: window closed */

static void spin_until_equal_w(uint16_t a, uint16_t b) {     /* while ([a] != [b]) ; */
    while (W(a) != W(b)) plat_yield();
}

/* 160a:050a: copy argv[0] up to and including the last ':' or '\' to DS:4778. */
static void set_exe_dir(const char *argv0) {
    int16_t i = (int16_t)((int16_t)strlen(argv0) - 1);       /* NOT CX; DEC; DEC = len-1 */
    for (; i >= 0; i--) {
        char c = argv0[i];
        /* port: '/' accepted too (host paths); prefixes longer than the 1 KB buffer are dropped */
        if (c == ':' || c == '\\' || c == '/') {
            if (i + 2 > 0x400) return;
            memcpy(&DB(0x4778), argv0, (size_t)i + 1);       /* strncpy(0x4778, argv0, i+1) */
            DB(0x4779 + i) = 0;
            return;
        }
    }
}

/* strcat(DS:4778, DS:name) */
static void path_append(uint16_t name) {
    uint16_t d = 0x4778;
    while (DB(d) && d < 0x4778 + 0x3ff) d++;
    while (DB(name) && d < 0x4778 + 0x3ff) DB(d++) = DB(name++);
    DB(d) = 0;
}
static const char *path_buf(void) { return (const char *)&DB(0x4778); }

/* ---- hi-score file (160a:01b1..02aa load, 160a:047f..04db save) ---- */
static void hiscore_load(void) {
    set_exe_dir(exe_path); path_append(0x1411);              /* "YOGHOHI.DAT" */
    FILE *f = fopen(path_buf(), "rb");                       /* _open(.., O_RDONLY, &[0x1373]) */
    if (!f) return;
    SW(0x1373, next_handle++);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);                                     /* filelength(): DX:AX == 0:0xa0 */
    fseek(f, 0, SEEK_SET);
    if (len == 0xa0) {
        size_t got = fread(&DB(0x33f4), 1, 0xa0, f);         /* _read(.., DS:33f4, 0xa0, &[0x5149]) */
        SW(0x5149, got);
        for (SW(0x5149, 0); W(0x5149) < 10; SW(0x5149, W(0x5149) + 1)) {   /* i in global 0x5149 */
            uint16_t r = (uint16_t)(W(0x5149) << 4);
            uint16_t hi = W(0x33f6 + r), lo = W(0x33f4 + r);
            if (hi > 0x999 || (hi == 0x999 && lo > 0x9999)) {   /* clamp to 9 999 999 (BCD) */
                SW(0x33f6 + r, 0x999); SW(0x33f4 + r, 0x9999);
            }
            if (DB(0x3403 + r) > 5) DB(0x3403 + r) = 5;           /* world */
            uint16_t p = (uint16_t)(0x33f8 + r);
            for (SW(0x5147, 0); W(0x5147) < 0x0b; SW(0x5147, W(0x5147) + 1)) DB(p++) &= 0x1f;
        }
    }
    fclose(f);   /* port: the original never closes it */
}

static void hiscore_save(void) {
    set_exe_dir(exe_path); path_append(0x1411);
    FILE *f = fopen(path_buf(), "wb");                       /* _creat(.., 0, &[0x1373]) */
    if (!f) return;
    SW(0x1373, next_handle++);
    size_t put = fwrite(&DB(0x33f4), 1, 0xa0, f);            /* _write(.., DS:33f4, 0xa0, &[0x5149]) */
    SW(0x5149, put);
    fclose(f);
}

/* ---- hardware (160a:316d / 31b0) ---- */
static int mouse_driver_on;
void mouse_restore(void) { mouse_driver_on = 0; }           /* 1000:0c9b: handler swapped back */

/* Platform tick: the INT 33h driver runs on its own IRQ; the port delivers mouse events once per
 * retrace, just before the timer ISR (1000:05f0). */
static void flow_tick(void) {
    if (mouse_driver_on) mouse_poll();
    timer_isr();
}

int hw_init(void) {                                         /* 160a:316d */
    DB(0xaa) = 1;                       /* detect_vga 160a:30f6: INT 10h 1A00 -> VGA (BL=8) */
    video_init();                       /* 1000:02c8 */
    DB(0xab) = 0xff;                    /* video_init succeeded (snapshot: 0xff) */
    mouse_init();                       /* 1000:0c6c, sets [0xac] */
    mouse_driver_on = DB(0xac) == 0xff;
    timer_install();                    /* 1000:0405 */
    plat_set_isr(flow_tick);            /* port: mouse events + timer ISR per retrace */
    return 0xff;
}

void hw_shutdown(void) {                                    /* 160a:31b0 */
    timer_remove();                     /* 1000:05ba */
    if (DB(0xab)) { /* video_restore 1000:03a2: BIOS mode/state restore — nothing on the host */ }
    if (DB(0xac)) mouse_restore();
}

/* ---- 160a:058c ---- */
void new_game(void) {
    display_off();
    vram_clear();
    MusicStop();
    SW(0x2ca6, W(0x2ca6) | 1);
    if (W(0x5124) != 0) {               /* embedded debug level (dead in retail): tag "L1".."B6" */
        uint16_t tag = rw(SEG_DG, 0);
        uint8_t ah = (uint8_t)(((tag >> 8) - 0x31) * 2);
        if ((uint8_t)tag == 0x42) ah++;
        DB(0x12d) = ah;
    } else DB(0x12d) = 0;
    SW(0xcc, 0); SW(0xca, 0);           /* score */
    SW(0xbe, 5);                        /* lives */
    SW(0xc6, 0xffff);
    /* player physics constants, u32 lo/hi (16.16) */
    SW(0x10c, 0); SW(0x10a, 0x2000);
    SW(0x110, 0xffff); SW(0x10e, 0xe000);
    SW(0x11c, 0); SW(0x11a, 0x2000);
    SW(0x114, 2); SW(0x112, 0);
    SW(0x118, 0xfffe); SW(0x116, 0);
    SW(0x120, 3); SW(0x11e, 0);
    SW(0x13f8, 0);                      /* extra-life threshold index */
}

/* acc>>4 as the original computes it (long shr 1000:4cba, low word used) */
static uint16_t para(uint32_t acc) { return (uint16_t)(acc >> 4); }
static uint32_t round16(uint32_t n) { return (n + 0xf) & 0xfffffff0u; }

/* ---- 160a:062d ---- */
void level_load(void) {
    uint32_t acc = 0;                                       /* [bp-6]:[bp-8] */
    uint16_t D = W(0x1b07 + 2 * DB(0x12d));
    SW(0x5138, D);
    SW(0x2ca6, W(0x2ca6) | 1);
    SW(0xc4, 0); SW(0xc2, 0); SW(0xc0, 0); SW(0x1475, 0); SW(0x2d84, 0);
    if (!(DB(0x12d) & 1)) SW(0x2d9a, 0x1e);                 /* main levels only */
    SW(0x5141, 0); SW(0x513f, 0); SW(0x513d, 0); SW(0x513b, 0);
    SW(0x2da6, W(D + 1) * 2);                               /* map row stride */
    {
        uint16_t off = 0, di = 0x32f6;
        for (uint16_t r = 0; r < W(D + 3); r++) { SW(di, off); off = (uint16_t)(off + W(0x2da6)); di += 2; }
    }
    vga_reglist(0x3495, SEG_DG);                            /* game mode, split screen */
    hud_init();
    dat_load(3, W(0x13fa), W(0x13fc));                      /* common palette, colours 0x40..0xfe */
    set_palette(W(0x13fa), W(0x13fc), 0x40, 0xc0);
    dat_load(W(D + 5), W(0x13fa), W(0x13fc));               /* level palette, colours 0x00..0x3e */
    set_palette(W(0x13fa), W(0x13fc), 0, 0x40);

    SW(0x2cda, W(0x13fc) + para(acc));                      /* map */
    acc += round16(dat_load(W(D + 0xb), 0, W(0x2cda)));
    SW(0x2ce2, W(0x13fc) + para(acc));                      /* 4096-byte block table */
    acc += round16(dat_load(W(D + 7), 0, W(0x2ce2)));
    SW(0x2cde, W(0x13fc) + para(acc));                      /* tileset */
    acc += round16(dat_load(W(D + 9), 0, W(0x2cde)));
    SW(0x2d7a, W(0x13fc) + para(acc));                      /* level music — size NOT rounded */
    acc += dat_load(W(D + 0x19), W(0x2d78), W(0x2d7a));
    if (W(0x5124) != 0) SW(0x2d64, SEG_DG);                 /* embedded overlay (dead) */
    else {
        SW(0x2d64, W(0x13fc) + para(acc));                  /* overlay (overwrites <= 15 B of the song tail) */
        acc += round16(dat_load(DB(D), 0, W(0x2d64)));
    }
    SW(0x5169, W(0x13fc) + para(acc));
    {
        uint16_t e = (uint16_t)(W(D + 0xd) << 2);
        uint32_t pos = (uint32_t)W(0x2258 + e) | (uint32_t)W(0x225a + e) << 16;
        dat_read(pos, 0x400, 0x4778, SEG_DG);               /* 1 KB tile attributes (always 1024 B) */
    }
    SW(0x5151, (uint16_t)(W(D + 1) << 5) - 0x140);
    SW(0x514f, (uint16_t)(W(D + 3) << 5) - 0xa0);
    SW(0x514d, W(0x5151) + 0x140);
    SW(0x514b, W(0x514f) + 0xa0);
    R.ds = SEG_DG;
    overlay_link();                                         /* 1000:04d2 */
    farcall_set_overlay(DB(0x12d));                         /* port: which overlay's handlers are live */
    {
        uint16_t es = W(0x2d64), o = rw(es, 4);             /* object list (20-byte records) */
        while (rw(es, o) != 0) {
            ww(es, (uint16_t)(o + 4), rw(es, (uint16_t)(o + 6)));
            ww(es, (uint16_t)(o + 8), 0);
            o = (uint16_t)(o + 0x14);
        }
    }
    SW(0xe2, W(D + 0xf));                                   /* respawn point */
    SW(0xe4, W(D + 0x11));
    SW(0x2d68, W(0x2d64));                                  /* overlay per-frame entry ovl:0008 */
    SW(0x2d66, 8);
}

/* ---- 160a:0fa0 ---- */
void story_screen(void) {
    uint16_t D = W(0x5138);
    uint16_t txt = W(D + 0x17);                             /* [bp-2] */
    uint16_t x = 0, y = 0, idx = 0;                         /* [bp-8], [bp-0xa], [bp-0xc] */
    SW(0x2cd6, 0);
    display_off();
    if (txt == 0) return;                                   /* bonus levels */
    set_player_pos_and_camera(0, W(D + 0x13), 0, W(D + 0x15));
    vga_reglist(0x3495, SEG_DG);
    R.ds = SEG_DG;
    bg_fill();                                              /* 1000:0e1a */
    pal_black();
    hud_update();
    dat_load(0x0d, 0, W(0x2cde));                           /* 320x150 frame into the tileset buffer */
    blit_planar_buf_t(0, W(0x2cde), 0, 0, 0x140, 0x96);
    dat_load(0x0f, 0, W(0x2cde));                           /* story font replaces it */
    wait_tick();
    DB(0x2c91) = 0x7f;
    SW(0x2c9e, 0); SW(0x2ca2, 0);
    SW(0x2c9c, 0x3c00);                                     /* ISR renders, no game logic */
    uint16_t save_seg = (uint16_t)(W(0x2cde) + 0x400);      /* [bp-4]; offset [bp-6] = 0 */
    for (uint16_t i = 0; i < 0x300; i++) wb(save_seg, i, DB(0x4e20 + i));
    for (uint16_t i = 0; i < 0x2d0; i++) DB(0x4e20 + i) >>= 1;   /* colours 0..0xef at half brightness */
    fade_in(4);
    DB(0x2c91) = 0;
    DB(0x2d82) = 0;
    uint16_t si = (uint16_t)(txt + idx);
    while (DB(si) != 0 && DB(0x2d82) == 0) {
        uint8_t g = DB(0x1477 + (DB(si) & 0x3f));
        si++;
        if (g == 0x3d) {                                    /* '=' pause up to 70 ticks */
            for (uint16_t i = 0; ; ) { wait_tick(); if (DB(0x2d82)) break; if (++i >= 0x46) break; }
        } else if (g == 0x3e) {                             /* '>' newline */
            y += 9; x = 0;
        } else {
            blit_planar_buf_t((uint16_t)(g * 0x3f), W(0x2cde), (int16_t)(x + 0x2a), (int16_t)(y + 0x16), 7, 9);
            x += 7;
            wait_ticks(2);
        }
    }
    while (DB(0x2d82) == 0 && !host_quit()) plat_yield();  /* any key */
    DB(0x2c91) = 0x7f;
    fade_out(4);
    for (uint16_t i = 0; i < 0x300; i++) DB(0x4e20 + i) = rb(save_seg, i);
    SW(0x2c9c, 0x8000);
    dat_load(W(D + 9), 0, W(0x2cde));                       /* restore the tileset */
}

/* B1 descent -> bonus transition, 160a:0bce..0c53 (level index 1 only) */
static void b1_transition(void) {
    SW(0x2db4, 0x2c); SW(0x2db2, 0x3c);
    spin_until_equal_w(0x2db2, 0x2db4);                     /* ISR flash ramp */
    DB(0x2c91) = 0x7f;
    for (uint16_t s = 0; s != 0x880; s += 0x44) {
        if (W(0x131 + s) != 0) {
            SW(0x131 + s, 0);
            uint16_t rec = W(0x139 + s), ovl = W(0x2d64);
            ww(ovl, (uint16_t)(rec + 4), rw(ovl, (uint16_t)(rec + 6)));
        }
    }
    DB(0x2c91) = 0x7f;
    SW(0x2c9c, W(0x2c9c) | 0x1000);
    R.ds = SEG_DG;
    activate_full();                                        /* 1000:19f7 */
    SW(0x2c9c, W(0x2c9c) & 0xe63f);
    SW(0x5120, 0x45);
    SW(0x2db4, 0);
    spin_until_equal_w(0x2db2, 0x2db4);                     /* flash decay */
    DB(0x2c91) = 7;
}

/* L6 ending, 160a:0c6c..0e6f (level index 10, overlay sets [0x5143] = 1) */
static void ending(void) {
    uint16_t x = 0x70, y = 0x40, idx = 0;                   /* [bp-6], [bp-8], [bp-0xa] */
    const uint16_t text = 0x2b25;                           /* [bp-0xc] */
    MusicStop();
    wait_ticks(2);
    dat_load(0x45, 0, W(0x2cda) + 0x200);                   /* ending music = title music DAT 69 */
    dat_load(0x59, 0, W(0x2ce2));                           /* DAT 89 ENDSEQ program */
    dat_load(0x4c, 0, W(0x2ce2) + 0x500);                   /* DAT 76 credits assets */
    dat_load(0x02, 0, W(0x1402) - 0xd00);                   /* end pictures */
    uint16_t font = (uint16_t)(W(0x1402) - 0xe00);          /* DI */
    dat_load(0x0f, 0, font);                                /* story font */
    SW(0x2d7a, W(0x2cda) + 0x200);
    SW(0x2d78, 0);
    MusicPlay(W(0x2d7a), W(0x2d78), 0);
    SW(0x5143, 2);
    while (W(0xc2) == 0 && !host_quit()) plat_yield();     /* overlay finale runs in the ISR */
    SW(0x2c9c, 0x8000);
    fade_out(8);
    vga_reglist(0x34e5, SEG_DG);
    SW(0x2cd6, 0); SW(0x2cb4, 0);
    rle_decode(0, W(0x1402) - 0xd00);                       /* picture A -> [1402]:0 */
    set_palette(0x92b8, W(0x1402) - 0xd00, 0, 0x100);
    blit_linear_vga(W(0x1400), W(0x1402), 0, 0, 0x140, 0xc8);
    rle_decode(0x95b8, W(0x1402) - 0xd00);                  /* picture B (216x141) */
    fade_in(4);
    wait_ticks(0x69);
    blit_linear_vga(W(0x1400), W(0x1402), 0x66, 0x36, 0xd8, 0x8d);
    wait_ticks(0x23);
    for (uint16_t p = (uint16_t)(text + idx); DB(p) != 0; ) {
        uint8_t g = DB(0x1477 + (DB(p) & 0x3f));
        p++; idx++;
        if (g == 0x3e) { y += 9; x = 0x70; wait_ticks(0x14); }
        else { blit_planar_vga_t((uint16_t)(g * 0x3f), font, (int16_t)x, (int16_t)y, 7, 9); x += 8; }
        wait_ticks(3);                                      /* also after a newline */
    }
    wait_anykey();
    fade_out(2);
    SW(0x5155, W(0x2ce2)); SW(0x5153, 0x1577);              /* far ENDSEQ entry [2ce2]:1577 */
    endseq_run();                                           /* CALLF [5153](&DS:2c98, 0, [2ce2]+0x500) */
    SW(0x2ca6, W(0x2ca6) | 1);
    MusicStop();
}

/* ---- 160a:090d ---- */
void level_run(void) {
    patch_render_segs();                                    /* 1000:04aa */
    for (uint16_t s = 0; s != 0x880; s += 0x44) { SW(0x131 + s, 0); SW(0x133 + s, W(0x2d64)); }
    for (uint16_t p = 0x218f; p != 0x224f; p += 0x0c) SW(p, 0);
    for (uint16_t p = 0x09b1; p != 0x1371; p += 0x0d) SW(p, 0);
    {   /* per-life object reset 160a:0985 */
        uint16_t es = W(0x2d64), o = rw(es, 4);
        while (rw(es, o) != 0) {
            uint16_t f = rw(es, (uint16_t)(o + 4));
            if (f & 0x1000) ww(es, (uint16_t)(o + 4), rw(es, (uint16_t)(o + 6)));
            else {
                if (f & 0x0800) { f &= 0x77ff; ww(es, (uint16_t)(o + 4), f); }
                if (rw(es, (uint16_t)(o + 4)) & 0x4000) ww(es, (uint16_t)(o + 4), rw(es, (uint16_t)(o + 4)) & 0x7fff);
            }
            o = (uint16_t)(o + 0x14);
        }
    }
    DB(0xb2) = 5; DB(0xb1) = 0; DB(0xb3) = 0; DB(0xb4) = 0; DB(0xb6) = 0;
    for (uint16_t p = 0xf2; p < 0x10a; p += 2) SW(p, 0);    /* 8 x u32 player vars */
    SW(0x2dbe, 0); SW(0x2dc0, 0); SW(0x2db2, 0); SW(0x2db4, 0);
    SW(0xde, 0); SW(0xe0, 0);
    SW(0x2cb2, 0);                                          /* unpause */
    SW(0xb7, 0);
    DB(0xbd) = 0; DB(0xb5) = 1;
    SW(0xc2, 0); SW(0xd4, 0);
    SW(0x5120, 0x45);
    SW(0xc8, 0xffff); SW(0xd0, 0xffff); SW(0xce, 0xffff); SW(0xc6, 0xffff);   /* HUD caches */
    if (W(0xc4) == 0) { SW(0xc4, W(0xc4) + 1); story_screen(); }   /* first life of this load */
    vga_reglist(0x3495, SEG_DG);
    pal_black();
    set_player_pos_and_camera(0, W(0xe2), 0, W(0xe4));
    R.ds = SEG_DG;
    activate_full();                                        /* 1000:19f7 */
    R.ds = SEG_DG;
    bg_fill();                                              /* 1000:0e1a */
    SW(0x2c9e, 0);
    for (uint16_t i = 0; i < 0x20; i++) SW(0x1435 + 2 * i, 0);   /* REP STOSW CX=0x20 */
    SW(0x5149, 0); SW(0x5147, 0); SW(0x5145, 0); SW(0x5143, 0);
    MusicPlay(W(0x2d76), W(0x2d74), 0);                     /* level-start jingle (DAT 71 sub 0) */
    SW(0x2d7c, 0x140);                                      /* level music after 320 ticks */
    SW(0x2ca6, W(0x2ca6) & 0xfffe);
    for (uint16_t i = 0; i < 0x3e; i++) DB(0x2ce8 + i) = 0;  /* key array, 0x1f words */
    DB(0x2c91) = 0x7f;
    wait_tick();
    if (DB(0x12d) == 1) { SW(0x2c9c, 0x9c0); SW(0x5120, 0x3e8); }   /* B1 descent phase */
    else SW(0x2c9c, 0x80);
    SW(0x2c9e, 0); SW(0x2ca2, 0);
    SW(0x12a, 1);
    SW(0x2c9c, W(0x2c9c) & 0x7fff);
    fade_in(8);                                             /* ISR already runs the game with 0x80 */
    DB(0x2c91) = 0;
    wait_ticks(0x32);
    SW(0x2c9c, W(0x2c9c) & 0xff7f);                         /* play */

    while (W(0xc2) == 0) {                                  /* 160a:0f56 test first */
        wait_tick();
        if (W(0x2d7c) == 0) MusicPlay(W(0x2d7a), W(0x2d78), 0);   /* jingle over / music back on */
        if (DB(0x12d) == 1 && (W(0x2c9c) & 0x40) && W(0xec) == 0x43f) b1_transition();
        if (DB(0x12d) == 0x0a && W(0x5143) == 1) { ending(); return; }   /* returns without the exit fade */
        {   /* extra life: threshold (u32 BCD) <= score, unsigned */
            uint16_t b = (uint16_t)(W(0x13f8) << 2);
            uint16_t thi = W(0x13e6 + b), tlo = W(0x13e4 + b);
            if (thi < W(0xcc) || (thi == W(0xcc) && tlo <= W(0xca))) {
                SW(0x13f8, W(0x13f8) + 1);
                SW(0xbe, W(0xbe) + 1);
                SW(0x2ca4, 5);
            }
        }
        if (DB(0x2d01)) {                                   /* P: pause toggle with half-brightness fade */
            DB(0x2d01) = 0;
            if (W(0x2cb2)) {
                SW(0x515b, 0x80); SW(0x5159, 0x100); SW(0x5157, 0x10);
                spin_while_nonzero_w(0x5157);
                wait_tick();
                SW(0x2cb2, 0);
            } else {
                SW(0x2cb2, 1);
                SW(0x515b, 0x100); SW(0x5159, 0x80); SW(0x5157, 0xfff0);
                wait_tick();
                spin_while_nonzero_w(0x5157);
            }
        }
        if (DB(0x2ce9)) { SW(0xc2, 4); DB(0x2ce9) = 0; }    /* Esc: quit */
        if (DB(0x2d15)) { SW(0xc2, 1); DB(0x2d15) = 0; }    /* X: lose a life */
        if (DB(0xb0) && DB(0x2d07)) SW(0xc2, 2);            /* cheat + S: level done (key not cleared) */
        if (DB(0x2d0a)) {                                   /* G: music toggle */
            SW(0x2ca6, W(0x2ca6) ^ 0x80);
            if (W(0x2ca6) != 0) MusicStop(); else SW(0x2d7c, 0);   /* 0 -> loop restarts level music */
            DB(0x2d0a) = 0;
        }
        if (host_quit()) SW(0xc2, 4);                       /* port: window closed = Esc */
    }
    /* exit 160a:0f60 */
    SW(0x2c9c, 0x8000);
    if (W(0x2cb2)) {                                        /* paused: 0x80 -> 0, no screen_off */
        SW(0x515b, 0x80); SW(0x5159, 0); SW(0x5157, 0xfff8);
        spin_while_nonzero_w(0x5157);
    } else fade_out(8);
    SW(0x2ca6, W(0x2ca6) | 1);
    MusicStop();
}

/* ---- 160a:0009 ---- */
int game_main(int argc, char **argv) {
    exe_path = argc > 0 && argv[0] ? argv[0] : "";
    SW(0x5124, (uint16_t)argc);                             /* overwritten below */
    /* Embedded-level size: both linker segment symbols are DGROUP in retail -> 0 */
    SW(0xcc, 0); SW(0xca, 0);
    SW(0x5124, 0);                                          /* SEG_X - DGROUP */
    SW(0x2d68, SEG_DG); SW(0x2d66, 0);
    /* (sz != 0: embedded overlay written to LEVELxx.DAT — dead code, not ported) */
    /* windows_running() 1000:4877: cannot be true for the port */
    SW(0xdc, 2);                                            /* cpu_detect 1000:481a (snapshot: 2) */
    SW(0x5122, 0x5869 - W(0x5124));
    SW(0x13fe, DOS_ALLOC_SEG);                              /* allocmem(0x5869, &[0x13fe]) */

    hiscore_load();

    set_exe_dir(exe_path); path_append(0x1404);             /* "YOGHODAT.DAT" */
    SW(0x1402, W(0x13fe) + (uint16_t)(W(0x5122) - 0x1a00)); /* background buffer = top 0x1a00 paras */
    SW(0x1400, 0);
    if (dat_count() <= 0) {                                 /* _open(.., &[0x1371]) failed */
        fputs((const char *)&DB(0x2ad1), stderr);
        return 0;
    }
    SW(0x1371, next_handle++);                              /* main has opened the file already */
    AdlibDetect();                                          /* 1000:47f4 */
    if (hw_init()) {
        kbd_install();
        dat_read(0, 0x178, 0x2258, SEG_DG);                 /* directory: 94 dwords */
        SW(0xd2, dat_load(0x4a, 0, W(0x13fe)));             /* HUD sprites at 13fe:0 */
        SW(0x2d72, W(0x13fe)); SW(0x2d70, W(0xd2));
        SW(0xd2, W(0xd2) + (uint16_t)dat_load(0x46, W(0x2d70), W(0x2d72)));   /* DAT 70 */
        SW(0x2d76, W(0x13fe)); SW(0x2d74, W(0xd2));
        SW(0xd2, W(0xd2) + (uint16_t)dat_load(0x47, W(0x2d74), W(0x2d76)));   /* DAT 71 jingles */
        SW(0x13fc, W(0x13fe) + (uint16_t)((int16_t)(W(0xd2) + 0xf) / 16));   /* CWD; IDIV 16 */
        SW(0x13fa, 0);
        SfxLoad(W(0x2d72), W(0x2d70));                      /* 1000:47df */
        SW(0xcc, 0); SW(0xca, 0);
        int first = 1;
        for (;;) {
            SW(0x2c9c, 0x8000);                             /* 160a:03d7 */
            if (first) {
                SW(0x126, 2);   /* IRQs that hit during the original's startup (DOSBox: 2) */
                first = 0;
            }
            if (menu() == -1) break;
            new_game();
            while (dgs(0xbe) > 0) {
                level_load();
                while (dgs(0xbe) > 0 && W(0xc2) == 0) {
                    level_run();
                    switch (W(0xc2)) {
                    case 1: SW(0xbe, W(0xbe) - 1); SW(0xc2, 0); break;          /* died: same level */
                    case 2:
                        if (DB(0x12d) == 0x0b) SW(0xbe, 0);                       /* after B6: game over */
                        else if (W(0x5124) == 0) DB(0x12d)++;                     /* next level */
                        break;
                    case 4: SW(0xbe, 0); break;                                   /* Esc */
                    default: break;
                    }
                    for (uint8_t k = 0; k < 0x3e; k++) DB(0x2ce8 + k) = 0;
                }
            }
        }
        MusicStop();
        kbd_remove();
    }
    hw_shutdown();
    hiscore_save();
    if (W(0x1371)) { /* _close(YOGHODAT.DAT): the data file stays loaded for the port */ }
    if (W(0x13fe)) { /* freemem(0x13fe) */ }
    return 0;
}
