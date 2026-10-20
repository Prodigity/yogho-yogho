/* Intro ("Introductie", menu key I): six delta-compressed animation scenes DAT 4..9, the
 * animation player 2343:0003 with its engine helpers 1000:4896/48b5/48cd/49a3, and the RLE
 * unpacker 1000:478c (ending pictures, DAT 2).
 * Spec: teardown/10-flow-frontend.md §3.14, 80-data.md §2.4 + §2.10; the player itself was
 * reverse-engineered from a capstone listing of 2343:0003..01ff (see comments).
 *
 * Animation file (at animseg:0): u16 nframes, u8 pal[768], then frame records from +0x302:
 *   u16 x0, y0, x1, y1; s16 t;   t = -1: unchanged; t = -2: copy the rect from the other buffer;
 *   t >= 0: t bytes of per-row run codes follow (s8 c: c > 0 run of c x next byte, c < 0 -c literals,
 *   c = 0 skip next-byte pixels).  Buffers are linear 320x200; pages are 320-wide Mode X (80 B/row). */
#include "game.h"
#include "flow_ext.h"
#include "platform.h"

#define W(o)     dgw(o)
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))

/* ---- 1000:478c: escape-byte RLE -> [0x1402]:0 ---- */
void rle_decode(uint16_t off, uint16_t seg) {
    uint16_t es = W(0x1402), si = off, di = 0;
    uint8_t esc = rb(seg, si++);
    for (;;) {
        uint8_t b = rb(seg, si++);
        if (b != esc) { wb(es, di++, b); continue; }
        uint8_t n = rb(seg, si++);
        if (n == 0) return;
        uint8_t v = rb(seg, si++);
        for (uint16_t c = n; c; c--) wb(es, di++, v);
    }
}

/* ---- 1000:4896 / 48b5: 64000-byte copy / clear ---- */
static void buf_copy(uint16_t src, uint16_t dst) { memmove(P(dst, 0), P(src, 0), 64000); }
static void buf_clear(uint16_t seg) { memset(P(seg, 0), 0, 64000); }

/* ---- 1000:48cd: decode one frame record at seg:off into buffer dst (other = the other buffer).
 * Returns the record length (10 + t), 10 for t = -2, 0xffff for t = -1. ---- */
static uint16_t anim_frame(uint16_t seg, uint16_t off, uint16_t dst, uint16_t other) {
    uint16_t x0 = rw(seg, off), y0 = rw(seg, (uint16_t)(off + 2));
    uint16_t x1 = rw(seg, (uint16_t)(off + 4)), y1 = rw(seg, (uint16_t)(off + 6));
    uint16_t t = rw(seg, (uint16_t)(off + 8));
    uint16_t pos = (uint16_t)(y0 * 0x140 + x0);             /* MUL, low word */
    uint16_t rows = (uint16_t)(y1 - y0 + 1);                /* CS:499d */
    uint16_t skip = (uint16_t)(0x13f - x1 + x0);            /* CS:49a1 = 320 - w */
    uint16_t w = (uint16_t)(x1 - x0 + 1);                   /* CS:499f */
    if (t == 0xffff) return 0xffff;                         /* 4917 */
    if (t == 0xfffe) {                                      /* 4921: copy rect from the other buffer */
        uint16_t si = pos, di = pos;
        do {
            for (uint16_t n = w; n; n--) wb(dst, di++, rb(other, si++));
            si = (uint16_t)(si + skip); di = (uint16_t)(di + skip);
        } while (--rows);
        return 10;
    }
    uint16_t si = (uint16_t)(off + 10), di = pos;           /* 4945 */
    do {
        uint16_t cx = w;
        while (cx) {                                        /* JCXZ */
            uint8_t al = rb(seg, si++);
            if ((int8_t)al < 0) {                           /* literals */
                uint8_t n = (uint8_t)-al;
                cx = (uint16_t)(cx - n);
                do wb(dst, di++, rb(seg, si++)); while (--n);
            } else if (al == 0) {                           /* skip */
                uint8_t n = rb(seg, si++);
                cx = (uint16_t)(cx - n);
                di = (uint16_t)(di + n);
            } else {                                        /* run */
                cx = (uint16_t)(cx - al);
                uint8_t n = al, v = rb(seg, si++);
                do wb(dst, di++, v); while (--n);
            }
        }
        di = (uint16_t)(di + skip);
    } while (--rows);
    return (uint16_t)(t + 10);
}

/* ---- 1000:49a3: copy the record's rect from linear buffer buf to the Mode-X page at VRAM
 * offset page (80 bytes per row), plane by plane, column by column. ---- */
static void anim_blit(uint16_t rseg, uint16_t roff, uint16_t buf, uint16_t page) {
    uint16_t x0 = rw(rseg, roff), y0 = rw(rseg, (uint16_t)(roff + 2));
    uint16_t x1 = rw(rseg, (uint16_t)(roff + 4)), y1 = rw(rseg, (uint16_t)(roff + 6));
    uint16_t xcur = x0;                                     /* CS:4a91 */
    uint8_t rows = (uint8_t)(y1 - y0 + 1);                  /* CS:4a8b (byte used) */
    uint16_t ax = (uint16_t)(y0 * 0x50);
    uint16_t di = (uint16_t)(page + ax);
    uint16_t si = (uint16_t)((uint16_t)(ax << 2) + x0);     /* y0*320 + x0 */
    for (int pass = 4; pass; pass--) {                      /* CS:4a93 */
        uint16_t sdi = di, ssi = si;
        uint16_t cx = xcur;
        di = (uint16_t)(di + (cx >> 2));
        uint8_t m = (uint8_t)(0x11 << (cx & 3) | 0x11 >> (8 - (cx & 3)));   /* ROL AH,CL */
        io_outw(0x3c4, (uint16_t)(m << 8 | 0x02));
        int16_t dx = (int16_t)(x1 - xcur);
        if (dx >= 0) {
            uint16_t cols = (uint16_t)(((uint16_t)dx >> 2) + 1);
            xcur++;
            uint16_t bx = di, bp = si;
            do {
                uint8_t cl = rows;
                do {
                    wb(0xa000, di, rb(buf, si));
                    si = (uint16_t)(si + 0x140);
                    di = (uint16_t)(di + 0x50);
                } while (--cl);
                bp = (uint16_t)(bp + 4); si = bp;
                bx++; di = bx;
            } while (--cols);
        }
        si = (uint16_t)(ssi + 1);
        di = sdi;
    }
}

/* ---- 2343:0003 anim_play(animseg, bufA, bufB, delay) ----
 * Frame 0 is decoded into A, copied to B and shown on both pages; frames 1..n-1 alternate
 * between the buffers (index [0x4b78]) — each a delta against its own buffer's content from two
 * frames back — and are blitted to page [0x4b7a + 2*idx] which is then displayed.  Each frame
 * lasts `delay` ticks; a left mouse press during a frame ([0x2c8c] & 2) stops the scene. */
void anim_play(uint16_t animseg, uint16_t bufA, uint16_t bufB, uint16_t delay) {
    uint16_t page[3] = { W(0x4b7a), W(0x4b7c), W(0x4b7e) }; /* [bp-0xe..-0xa] */
    uint16_t buf[3] = { bufA, bufB, bufA };                 /* [bp-8..-4] */
    const uint16_t skip_len = 10;                           /* [bp-0x14] */
    uint32_t ptr = ((uint32_t)animseg << 4) + 0x302;        /* [bp-0x10]:[bp-0x12], linear */
    int16_t nframes = (int16_t)rw(animseg, 0);              /* [bp-2] */
#define PSEG ((uint16_t)(ptr >> 4))
#define POFF ((uint16_t)(ptr & 0xf))
    buf_clear(bufA);
    uint16_t len = anim_frame(PSEG, POFF, bufA, bufB);
    buf_copy(bufA, bufB);
    SW(0x2cd6, page[W(0x4b78)]);
    wait_ticks(1);                                          /* [2c98] = 1; spin */
    anim_blit(PSEG, POFF, bufA, W(0x4b78) == 0 ? 0x3e80 : 0);
    SW(0x2cd6, page[W(0x4b78) + 1]);
    wait_ticks(1);
    anim_blit(PSEG, POFF, bufA, W(0x4b78) == 0 ? 0 : 0x3e80);
    ptr += len;
    for (int16_t f = 2; f <= nframes; f++) {
        SW(0x2c8c, 0);
        SW(0x2c98, delay);
        uint16_t idx = W(0x4b78);
        len = anim_frame(PSEG, POFF, buf[idx], buf[idx + 1]);
        if (len < 0xffff) anim_blit(PSEG, POFF, buf[idx], page[idx]);
        else len = skip_len;
        ptr += len;
        SW(0x2cd6, page[idx]);
        SW(0x4b78, W(0x4b78) ^ 1);
        spin_while_nonzero_w(0x2c98);
        if (W(0x2c8c) & 2) break;
        if (plat_should_quit()) break;                      /* port */
    }
    SW(0x4b78, W(0x4b78) ^ 1);
#undef PSEG
#undef POFF
}

/* ---- 160a:2f4f ---- */
uint16_t intro_scene(uint16_t dat, uint16_t wait_in, uint16_t wait_out,
                     uint16_t fade_in_step, uint16_t fade_out_step, uint16_t delay) {
    dat_load(dat, (uint16_t)(W(0x13fa) + 0xfa00), W(0x13fc));
    if (fade_in_step) {
        vram_clear();
        set_palette((uint16_t)(W(0x13fa) + 0xfa02), W(0x13fc), 0, 0x100);
        dac_schedule(0x4e20, 0, 0x100);
        wait_ticks(wait_in);
        fade_in(fade_in_step);
    }
    anim_play((uint16_t)(W(0x13fc) + 0xfa0), W(0x13fc), (uint16_t)(W(0x1402) + 0x800), delay);
    if (fade_out_step) {
        wait_ticks(wait_out);
        fade_out(fade_out_step);
    }
    return W(0x2c8c) & 2;                                   /* left click: stop the intro */
}

/* ---- 160a:2ea6 ---- */
void intro(void) {
    static const uint16_t sc[6][6] = {     /* dat, waitIn, waitOut, fadeIn, fadeOut, frame delay */
        {4, 0x14, 0x14, 0x10, 0x10, 0x0c}, {5, 0x14, 0x14, 0x10, 0x10, 0x0c},
        {6, 0x14, 0x00, 0x10, 0x00, 0x08}, {7, 0x14, 0x23, 0x00, 0x10, 0x08},
        {8, 0x14, 0x14, 0x10, 0x10, 0x08}, {9, 0x14, 0x46, 0x10, 0x02, 0x08},
    };
    vga_reglist(0x34ba, SEG_DG);                            /* 320-wide pages, no split */
    for (int i = 0; i < 6; i++)
        if (intro_scene(sc[i][0], sc[i][1], sc[i][2], sc[i][3], sc[i][4], sc[i][5]) || plat_should_quit()) break;
    SW(0x2cd6, 0);
}
