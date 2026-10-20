/* ENDSEQ: the end-credits roll, port of DAT entry 89 (a separately linked Borland C++ 1991
 * program image, "ENDSEQ.DAT") and its use of DAT entry 76 (picture, palettes, two fonts).
 * Spec: teardown/90-endseq.md.  Addresses "89:xxxx" are offsets in the program's code segment
 * (= file offsets in 89.bin); its data segment is CS+0x335 (file offset 0x3350).
 *
 * Memory, as in the original (the flow loads both entries at 160a:0c98 / 160a:0cab, before the
 * L6 finale; this module does not load anything):
 *   [DGROUP:2ce2]:0000        DAT 89 raw image (code; CS:1598 = far timer pointer, self-modified)
 *   [2ce2]+0x335 : 0000       its DGROUP (string table at 00c8, font-1 segment variable at 077a)
 *   S = [2ce2]+0x500 : 0000   DAT 76: picture 320x200 (0), palette A (S+0xfa0), palette B
 *                             (S+0xfd0), font 1 (S+0x1000), font 2 (S+0x121c), scratch screen
 *                             buffer (S+0x1438, 64000 bytes, beyond the end of the entry).
 * All script text, the glyph-order table (CS:1175 / CS:1327) and the stored pointers are read
 * from MEM.  VRAM is mode 13h (chain-4) after INT 10h, written through wb()/rb() at A000.
 * The program waits only through its 1-tick wait (89:159c) on the game's ISR countdown
 * DGROUP:2c98; it never reads the keyboard and returns when the script ends ('~'). */
#include "game.h"
#include "log.h"
#include "platform.h"
#include "vga.h"

#define VSEG 0xA000u

static uint16_t cs89;   /* program code segment = [2ce2] */
static uint16_t ds89;   /* program data segment, set by 89:156b */

/* ---- 89:159c  wait one timer tick: *timer = 1; while (*timer) ; ---- */
static void es_wait1(void) {
    uint16_t toff = rw(cs89, 0x1598), tseg = rw(cs89, 0x159a);   /* LES DI,CS:[1598] */
    ww(tseg, toff, 1);
    while (rw(tseg, toff) != 0) plat_yield();
}

/* ---- 89:056e  wait(n): for (si = n; si > 0; si--) wait1(); ---- */
static void es_wait(int16_t n) {
    for (int16_t si = n; si > 0; si--) es_wait1();
}

/* ---- 89:0f86  DAC upload: OUT 3C8,start; then CX bytes from seg:off to 3C9 (LOOP) ---- */
static void es_dac_load(uint16_t seg, uint16_t off, uint16_t start, uint16_t count) {
    io_out(0x3c8, (uint8_t)start);
    uint16_t cx = count;
    do { io_out(0x3c9, rb(seg, off)); off++; } while (--cx);
}

/* ---- 89:1477  DAC = src + (tgt - src) * num / den for `count` colours from `start` ---- */
static void es_dac_lerp(uint16_t sseg, uint16_t soff, uint16_t tseg, uint16_t toff,
                        uint16_t start, uint16_t count, int16_t num, int16_t den) {
    io_out(0x3c8, (uint8_t)start);
    uint16_t cx = count;
    do {
        uint8_t bl = 3;
        do {
            uint8_t al = rb(tseg, toff);                         /* MOV AL,ES:[DI] */
            uint8_t bh = rb(sseg, soff);                         /* MOV BH,[SI] */
            int16_t d = (int8_t)(uint8_t)(al - bh);              /* SUB AL,BH ; CBW */
            int32_t prod = (int32_t)d * num;                     /* IMUL word [bp+10] */
            int16_t q = (int16_t)(prod / den);                   /* IDIV word [bp+12] (trunc) */
            io_out(0x3c9, (uint8_t)((uint8_t)q + bh));           /* ADD AL,BH ; OUT 3C9 */
            soff++; toff++;
        } while (--bl);
    } while (--cx);
}

/* ---- 89:053c  fade(src, tgt, start, count, n): for (s = 0; s <= n; s++) { wait1; lerp(s/n) } ---- */
static void es_fade(uint16_t sseg, uint16_t soff, uint16_t tseg, uint16_t toff,
                    uint16_t start, uint16_t count, int16_t n) {
    for (int16_t s = 0; s <= n; s++) {
        es_wait1();
        es_dac_lerp(sseg, soff, tseg, toff, start, count, s, n);
    }
}

/* ---- 89:0fc8  tint: n colours, dst = min(63, (src >> shift) + add[c]) per component ---- */
static void es_pal_tint(uint16_t seg, uint16_t soff, uint16_t doff, uint16_t n, uint8_t shift,
                        uint8_t a0, uint8_t a1, uint8_t a2) {
    const uint8_t add[3] = {a0, a1, a2};
    uint16_t dx = n;
    do {
        for (int c = 0; c < 3; c++) {
            uint8_t al = (uint8_t)(rb(seg, soff) >> shift); soff++;   /* LODSB ; SHR AL,CL */
            al = (uint8_t)(al + add[c]);
            if (!((int8_t)al < 0x40)) al = 0x3f;                     /* CMP AL,40h ; JL (signed) */
            wb(seg, doff, al); doff++;
        }
    } while (--dx);
}

/* ---- 89:14c3  32 colours at seg:off = (r>>2, g>>2, b>>2) ---- */
static void es_pal_fill32(uint16_t seg, uint16_t off, uint8_t r, uint8_t g, uint8_t b) {
    r >>= 2; g >>= 2; b >>= 2;
    for (int i = 0; i < 0x20; i++) {
        wb(seg, off, r); wb(seg, (uint16_t)(off + 1), g); wb(seg, (uint16_t)(off + 2), b);
        off = (uint16_t)(off + 3);
    }
}

/* ---- 89:14ff  768 bytes at seg:off = 0x3f (white palette) ---- */
static void es_pal_white(uint16_t seg, uint16_t off) {
    for (int i = 0; i < 0x300; i++) { wb(seg, off, 0x3f); off++; }
}

/* ---- 89:1429  REP MOVSB / 89:144f  REP MOVSW (count/2 words) ---- */
static void es_copy_b(uint16_t sseg, uint16_t soff, uint16_t dseg, uint16_t doff, uint16_t n) {
    for (uint16_t cx = n; cx; cx--) { wb(dseg, doff, rb(sseg, soff)); soff++; doff++; }
}
static void es_copy_w(uint16_t sseg, uint16_t soff, uint16_t dseg, uint16_t doff, uint16_t n) {
    for (uint16_t cx = (uint16_t)(n >> 1); cx; cx--) {
        wb(dseg, doff, rb(sseg, soff));
        wb(dseg, (uint16_t)(doff + 1), rb(sseg, (uint16_t)(soff + 1)));
        soff = (uint16_t)(soff + 2); doff = (uint16_t)(doff + 2);
    }
}

/* ---- 89:13a3  slanted band, left edge moving right: row r at off + r*0x141, width w - r;
 *      dst = src | or.  Stops when the width (BX) or the row count (DX) runs out. ---- */
static void es_band_slant(uint16_t dseg, uint16_t doff, uint16_t sseg, uint16_t soff,
                          uint16_t w, uint16_t h, uint8_t orv) {
    uint16_t bx = w, dx = h;
    for (;;) {
        uint16_t cx = bx, di = doff, si = soff;
        do { wb(dseg, di, (uint8_t)(rb(sseg, si) | orv)); di++; si++; } while (--cx);   /* LOOP */
        soff = (uint16_t)(soff + 0x141); doff = (uint16_t)(doff + 0x141);
        if (--bx == 0) break;
        if (--dx == 0) break;
    }
}

/* ---- 89:13e7  growing band: row r at off + r*0x140, width w + r; dst = src | or ---- */
static void es_band_grow(uint16_t dseg, uint16_t doff, uint16_t sseg, uint16_t soff,
                         uint16_t w, uint16_t h, uint8_t orv) {
    uint16_t dx = w, bx = h;
    do {
        uint16_t cx = dx, di = doff, si = soff;
        do { wb(dseg, di, (uint8_t)(rb(sseg, si) | orv)); di++; si++; } while (--cx);   /* LOOP */
        doff = (uint16_t)(doff + 0x140); soff = (uint16_t)(soff + 0x140);
        dx++;
    } while (--bx);
}

/* Glyph index: linear scan of the CS order string (89:1175 or 89:1327), unbounded like the asm. */
static uint16_t es_glyph(uint16_t table, uint8_t ch) {
    uint16_t si = table;
    uint8_t al;
    do { al = rb(cs89, si); si++; } while (al != ch);
    return (uint16_t)(si - (uint16_t)(table + 1));
}

/* ---- 89:1057  text, font 1: 12x15 glyphs from a 576-wide strip, nonzero pixels stored ---- */
static void es_text(uint16_t str, uint16_t dseg, uint16_t di, uint16_t fseg) {
    uint16_t bx = str;
    for (;;) {
        uint8_t al = rb(ds89, bx); bx++;
        if (al == ' ') { di = (uint16_t)(di + 12); continue; }
        if (al == 0) return;
        uint16_t si = (uint16_t)(es_glyph(0x1175, al) * 12);           /* MUL CX (low word) */
        uint16_t cx = 12;
        do {
            for (uint16_t r = 0; r < 15; r++) {
                uint8_t v = rb(fseg, (uint16_t)(si + r * 0x240));
                if (v) wb(dseg, (uint16_t)(di + r * 0x140), v);
            }
            si++; di++;
        } while (--cx);
    }
}

/* ---- 89:11a4  text, font 2: nonzero pixels do dst = (dst & 0x1f) | v (row 0 done twice) ---- */
static void es_or_px(uint16_t seg, uint16_t off, uint8_t v) {
    wb(seg, off, (uint8_t)(rb(seg, off) & 0x1f));
    wb(seg, off, (uint8_t)(rb(seg, off) | v));
}
static void es_text_or(uint16_t str, uint16_t dseg, uint16_t di, uint16_t fseg) {
    uint16_t bx = str;
    for (;;) {
        uint8_t al = rb(ds89, bx); bx++;
        if (al == ' ') { di = (uint16_t)(di + 12); continue; }
        if (al == 0) return;
        uint16_t si = (uint16_t)(es_glyph(0x1327, al) * 12);
        uint16_t cx = 12;
        do {
            uint8_t v = rb(fseg, si);
            if (v) es_or_px(dseg, di, v);                                   /* 89:11ed */
            for (uint16_t r = 0; r < 15; r++) {                             /* 89:11fa.. */
                v = rb(fseg, (uint16_t)(si + r * 0x240));
                if (v) es_or_px(dseg, (uint16_t)(di + r * 0x140), v);
            }
            si++; di++;
        } while (--cx);
    }
}

/* REPNE SCASB length of the string at DS89:off. */
static uint16_t es_strlen(uint16_t off) {
    uint16_t n = 0;
    while (rb(ds89, (uint16_t)(off + n)) != 0 && n < 0xfffe) n++;
    return n;
}

/* ---- 89:0582  draw a font-1 line centred on the 320-wide screen at row y ---- */
static void es_line_centred(uint16_t str, int16_t y) {
    uint16_t fseg = rw(ds89, 0x077a);
    uint16_t row = (uint16_t)(y * 0x140);                                   /* IMUL (low word) */
    uint16_t x = (uint16_t)((uint16_t)(0x140 - es_strlen(str) * 12) >> 1); /* SHR: unsigned */
    es_text(str, VSEG, (uint16_t)(row + x), fseg);
}

static uint16_t es_str(int16_t idx) { return rw(ds89, (uint16_t)(0xc8 + (uint16_t)idx * 2)); }

/* ---- 89:05c3  main(S, mode) ---- */
static void es_main(uint16_t S, uint16_t mode) {
    int16_t blockflag = 2;      /* [bp-0a]  never 1 in this build */
    const int16_t step = 6;     /* [bp-0c] */
    int16_t idx = 0;            /* [bp-0e]  string index */
    const int16_t h1 = 0x1a;    /* [bp-10]  title band height */
    const int16_t h2 = 0x2d;    /* [bp-12]  name band height */
    const int16_t hold_name = 0x64;    /* [bp-16] */
    const int16_t hold_block = 0xb4;   /* [bp-18] */

    if (mode == 1) {            /* 89:05f6 -> 154d: standalone loader/TSR path (DOS file I/O); */
        log_gap("ENDSEQ mode 1 (standalone loader) not ported");   /* never used by the game */
        return;
    }
    if (mode == 0) ds89 = (uint16_t)(cs89 + rw(cs89, 0x1569));          /* 89:156b */

    const uint16_t pA = (uint16_t)(S + 0xfa0);          /* [bp-02] */
    const uint16_t pB = (uint16_t)(pA + 0x30);          /* [bp-04] */
    ww(ds89, 0x077a, (uint16_t)(pB + 0x30));            /* font 1 segment */
    const uint16_t f2 = (uint16_t)(pB + 0x30 + 0x21c);  /* [bp-06] font 2 */
    const uint16_t buf = (uint16_t)(f2 + 0x21c);        /* [bp-08] scratch screen */

    es_pal_tint(pA, 0, 0x60, 0x20, 2, 0x32, 0x1e, 0x1e);   /* colours 32..63: red band */
    es_pal_tint(pA, 0, 0xc0, 0x20, 2, 0x00, 0x30, 0x0c);   /* colours 64..95: green band */
    vga_reset();                                           /* 89:0e38: INT 10h AX=0013h (clears VRAM) */
    es_copy_b(VSEG, 0, buf, 0, 0x300);                     /* black palette from cleared VRAM */
    es_copy_b(VSEG, 0, (uint16_t)(buf + 0xf70), 0, 0x300);
    es_dac_load(VSEG, 0, 0, 0x300);                        /* DAC all black */
    es_copy_w(S, 0, VSEG, 0, 0xfa00);                      /* picture */
    es_fade(buf, 0, pA, 0, 0, 0x100, 0x40);                /* black -> A, 65 ticks */

    while (rb(ds89, es_str(idx)) != '~') {                 /* 89:0cdc */
        es_dac_load(pA, 0, 0, 0x300);                      /* 89:06be */
        uint16_t s = es_str(idx);
        if (rb(ds89, s) == ']') { es_wait(2); continue; }  /* 89:06d7 (idx not advanced) */

        if (rb(ds89, s) == '<' || blockflag == 1) {
            /* ---- 89:06fe text page ---- */
            int16_t y = 0;
            uint16_t slot = (uint16_t)(0xc8 + (uint16_t)idx * 2);   /* [bp-1e] */
            for (;;) {
                uint16_t p = rw(ds89, slot);
                if (rb(ds89, p) == '>') break;
                uint16_t si = p;
                for (;;) {
                    uint8_t c = rb(ds89, si);
                    if (c == '<') { si++; continue; }
                    if (c == '[') {                                 /* 89:071c y = ddd */
                        uint16_t v = (uint16_t)((int8_t)rb(ds89, (uint16_t)(si + 1)) * 100);
                        v = (uint16_t)(v + (uint16_t)((int8_t)rb(ds89, (uint16_t)(si + 2)) * 10));
                        v = (uint16_t)(v + (uint16_t)(int8_t)rb(ds89, (uint16_t)(si + 3)));
                        y = (int16_t)(uint16_t)(v + 0xeb30);
                        si = (uint16_t)(si + 4);
                        continue;
                    }
                    if (c == '`') { y = (int16_t)(y + 16); si++; continue; }
                    break;
                }
                es_line_centred(si, y);
                es_wait(0x14);
                y = (int16_t)(y + 16);
                slot = (uint16_t)(slot + 2);
                idx++;
            }
            blockflag = 2;
            idx++;                                                  /* skip '>' */
            es_wait(hold_block);
            es_copy_w(S, 0, VSEG, 0, 0xfa00);                       /* erase: picture again */
            continue;
        }

        /* ---- 89:0799 title + name(s) ---- */
        blockflag = 2;
        uint16_t title = es_str(idx);                               /* [bp-1a] */
        uint16_t name = es_str((int16_t)(idx + 1));                 /* [bp-1c] */
        idx = (int16_t)(idx + 2);

        {   /* red slanted band over rows 15.., sweeping right to left */
            int16_t si = 0x13f, di = 1;
            const int16_t neg = (int16_t)(-step);                   /* [bp-20] */
            while (si >= 0x30) {
                es_wait1();
                int16_t bx = di; if ((int16_t)(h1 + step) < di) bx = (int16_t)(h1 + step);
                int16_t cx = di; if (di > h1) cx = h1;
                uint16_t o = (uint16_t)(si + 0x12c0);
                es_band_slant(VSEG, o, S, o, (uint16_t)bx, (uint16_t)cx, 0x20);
                di = (int16_t)(di - neg);
                si = (int16_t)(si - step);
            }
        }
        es_copy_b(pA, 0, pB, 0, 0x300);                             /* B = A, then 96..255 = red band */
        for (uint16_t o = 0x120; o <= 0x2a0; o = (uint16_t)(o + 0x60)) es_copy_b(pA, 0x60, pB, o, 0x60);
        es_wait1();
        es_dac_load(pB, 0, 0, 0x300);
        es_text_or(title, VSEG,
                   (uint16_t)(((uint16_t)(0xfc - es_strlen(title) * 12) >> 1) + 0x1a82), f2);
        es_copy_w(VSEG, 0x1a40, buf, 0, 0x1400);                    /* save rows 21..36 */
        es_pal_fill32(pA, 0x120, 0x00, 0x00, 0x00);                 /* font-2 colours in A */
        es_pal_fill32(pA, 0x180, 0x95, 0x69, 0x44);
        es_pal_fill32(pA, 0x1e0, 0xf6, 0xde, 0x89);
        es_pal_fill32(pA, 0x240, 0xff, 0xff, 0xff);
        es_pal_fill32(pA, 0x2a0, 0x38, 0x24, 0x24);

        {   /* green band over rows 137..181, sweeping left to right; title fades B -> A */
            int16_t di = 0, si = (int16_t)0xffdd;
            do {
                es_wait1();
                int16_t bx = 0; if (si < 0) bx = (int16_t)(-si);
                int16_t cx = 0, dx;
                if (si >= 0) { cx = si; dx = step; } else dx = 1;
                uint16_t o = (uint16_t)((uint16_t)(bx * 0x140) + (uint16_t)cx + 0xab40);
                es_band_grow(VSEG, o, S, o, (uint16_t)dx, (uint16_t)(h2 - bx), 0x40);
                di++;
                if (di <= 0x3f) es_dac_lerp(pB, 0, pA, 0, 0, 0x100, di, 0x32);
                si = (int16_t)(si + 5);
            } while (si <= 0xd2);
        }
        es_text(title, VSEG,                                        /* 89:09a7 title again, font 1 */
                (uint16_t)(((uint16_t)(0xfc - es_strlen(title) * 12) >> 1) + 0x1a82),
                rw(ds89, 0x077a));
        es_copy_b(pA, 0, pB, 0, 0x300);                             /* B = A, then 96..255 = green band */
        for (uint16_t o = 0x120; o <= 0x2a0; o = (uint16_t)(o + 0x60)) es_copy_b(pA, 0xc0, pB, o, 0x60);
        es_wait1();
        es_dac_load(pB, 0, 0, 0x300);
        es_copy_w(VSEG, 0xbe00, buf, 0x1400, 0x1400);               /* save rows 152..167 */

        {   /* names: first, then each following string starting with '`' */
            int16_t done = 0;                                       /* [bp-14] */
            uint16_t slot = (uint16_t)(0xc8 + (uint16_t)idx * 2);
            while (!done) {
                es_text_or(name, VSEG,
                           (uint16_t)(((uint16_t)(0xf0 - es_strlen(name) * 12) >> 1) + 0xbe00 - 5), f2);
                es_fade(pB, 0, pA, 0, 0, 0x100, 0x3f);              /* name in, 64 ticks */
                es_wait(hold_name);
                uint16_t p = rw(ds89, slot);
                if (rb(ds89, p) == '`') {
                    es_fade(pA, 0, pB, 0, 0, 0x100, 0x3f);          /* name out, 64 ticks */
                    es_copy_w(buf, 0x1400, VSEG, 0xbe00, 0x1400);
                    name = (uint16_t)(p + 1);
                    slot = (uint16_t)(slot + 2);
                    idx++;
                    es_wait(0x0a);
                } else {
                    done = 1;
                }
            }
        }
        /* 89:0b34: B = base colours everywhere except 32..63 (red band kept) */
        es_copy_b(pA, 0, pB, 0, 0x60);
        for (uint16_t o = 0xc0; o <= 0x2a0; o = (uint16_t)(o + 0x60)) es_copy_b(pA, 0, pB, o, 0x60);
        es_fade(pA, 0, pB, 0, 0, 0x100, 0x15);                      /* 22 ticks */
        es_copy_w(S, 0xab40, VSEG, 0xab40, (uint16_t)(h2 * 0x140)); /* rows 137..181 = picture */
        es_wait1();
        for (uint16_t o = 0; o <= 0x2a0; o = (uint16_t)(o + 0x60)) es_copy_b(pA, 0, pB, o, 0x60);
        es_wait1();
        es_dac_load(pA, 0, 0, 0x300);
        es_copy_w(buf, 0, VSEG, 0x1a40, 0x1400);                    /* title rows back to font-2 state */
        es_fade(pA, 0, pB, 0, 0, 0x100, 0x15);                      /* 22 ticks */
        es_copy_w(S, 0x12c0, VSEG, 0x12c0, (uint16_t)(h1 * 0x140)); /* rows 15..40 = picture */
    }

    /* ---- 89:0ced finale: bands meet at row 77, fade to white, then black ---- */
    es_dac_load(pA, 1, 0, 0x300);                   /* note: source offset 1 (as in the asm) */
    int16_t di = 0;
    es_pal_white(pB, 0);
    int16_t si = 0x13f;
    int16_t w = 1;                                  /* [bp-22] */
    int16_t g = (int16_t)0xffd6;                    /* [bp-24] */
    const int16_t neg = (int16_t)(-step);           /* [bp-2a],[bp-26],[bp-28] */
    while (!((int16_t)(0xa0 - h2 / 2) > si)) {      /* CWD/SUB/SAR: 160 - 22 */
        es_wait1();
        int16_t bx = w; if ((int16_t)(h2 + step) < w) bx = (int16_t)(h2 + step);
        int16_t cx = w; if (w > h2) cx = h2;
        uint16_t o = (uint16_t)(si + 0x6040);
        es_band_slant(VSEG, o, S, o, (uint16_t)bx, (uint16_t)cx, 0x20);
        bx = 0; int16_t dx = g;
        if (g < 0) bx = (int16_t)(-g);
        cx = 0;
        if (dx >= 0) { cx = dx; dx = step; } else dx = 1;
        o = (uint16_t)((uint16_t)(bx * 0x140) + (uint16_t)cx + 0x6040);
        es_band_grow(VSEG, o, S, o, (uint16_t)dx, (uint16_t)(h2 - bx), 0x40);
        di++;
        if (di <= 0x28) es_dac_lerp(pA, 0, pB, 0, 0, 0x100, di, 0x28);
        w = (int16_t)(w - neg);
        g = (int16_t)(g - neg);
        si = (int16_t)(si - step);
    }
    while (di <= 0x28) {
        es_wait1();
        es_dac_lerp(pA, 0, pB, 0, 0, 0x100, di, 0x28);
        di++;
    }
    es_wait(0x32);
    es_fade(pB, 0, (uint16_t)(buf + 0xf70), 0, 0, 0x100, 0x40);  /* white -> black, 65 ticks */
}

/* ---- 89:1577  far entry (timer far ptr, mode, data segment) ---- */
static void es_entry(uint16_t timer_off, uint16_t timer_seg, uint16_t mode, uint16_t dataseg) {
    ww(cs89, 0x1598, timer_off);
    ww(cs89, 0x159a, timer_seg);
    es_main(dataseg, mode);
}

/* 160a:0e3c..0e61: [5155] = [2ce2]; [5153] = 0x1577; PUSHA/ES/DS;
 * CALLF [5153](DS:2c98, 0, [2ce2]+0x500); POP DS/ES; POPA.  The caller (flow) has loaded
 * DAT 89 at [2ce2]:0 and DAT 76 at ([2ce2]+0x500):0 (160a:0c98/0cab). */
void endseq_run(void) {
    dgw_set(0x5155, dgw(0x2ce2));
    dgw_set(0x5153, 0x1577);
    cs89 = dgw(0x5155);
    if (rb(cs89, 0x1577) != 0x55 || rb(cs89, 0x1578) != 0x8b || rb(cs89, 0x1579) != 0xec) {
        log_gap("ENDSEQ: DAT 89 not loaded at [2ce2]=%04x", cs89);   /* port sanity check only */
        return;
    }
    es_entry(0x2c98, SEG_DG, 0, (uint16_t)(dgw(0x2ce2) + 0x500));
}
