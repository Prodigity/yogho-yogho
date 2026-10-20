/* Timer ISR / frame scheduler, keyboard ISR and wait helpers.
 * Spec: teardown/20-timing-input.md §3.1-3.7.  Addresses are Ghidra (seg 1000).
 *
 * The platform calls timer_isr() once per emulated vertical retrace (70.086 Hz).  The frame body
 * (logic steps + drawing) runs synchronously inside it, so it always "finishes before the next
 * IRQ" exactly like on a fast PC / DOSBox: the busy path is only taken while 0x2ca2 is still set
 * from the EXE image (boot warm-up), which is reproduced because 0x2ca2 starts at 1 in MEM. */
#include "game.h"
#include "adlib.h"
#include "platform.h"
#include "vga.h"

#define W(o)     dgw(o)
#define SW(o, v) dgw_set((o), (uint16_t)(v))
#define S16(o)   dgs(o)

static void crtc_write_start(uint16_t v, uint8_t fine) {
    /* OUT 3D4h,(hi<<8)|0Ch ; OUT 3D4h,(lo<<8)|0Dh with the fine byte added to the low byte only */
    io_outw(0x3d4, (uint16_t)((v & 0xff00) | 0x0c));
    io_outw(0x3d4, (uint16_t)(((uint16_t)(uint8_t)((uint8_t)v + fine) << 8) | 0x0d));
}

uint16_t rng_step(void) {                                   /* 1000:05da / ISR 05fd */
    uint32_t t = (uint32_t)W(0xd8) + W(0xd6);
    unsigned c1 = t > 0xffff; SW(0xd8, t);
    t = (uint32_t)W(0xda) + W(0xd8) + c1;
    unsigned c2 = t > 0xffff; SW(0xda, t);
    SW(0xd6, W(0xd6) + W(0xda) + c2);
    return W(0xda);
}

void palette_tick(void) {                                   /* 1000:0935 */
    if (W(0x2d6a)) {
        uint16_t start = W(0x2d6c);
        io_out(0x3c8, (uint8_t)start);
        uint16_t p = (uint16_t)(W(0x2d6a) + 3 * start);
        for (uint16_t n = W(0x2d6e); n; n--) {
            io_out(0x3c9, DB(p)); io_out(0x3c9, DB(p + 1)); io_out(0x3c9, DB(p + 2));
            p = (uint16_t)(p + 3);
        }
        SW(0x2d6a, 0);
    }
    int16_t d = S16(0x5157);
    if (d == 0) return;
    int16_t lv = (int16_t)(S16(0x515b) + d);
    int16_t tgt = S16(0x5159);
    if (d > 0 ? lv >= tgt : lv <= tgt) { SW(0x5157, 0); lv = tgt; }
    SW(0x515b, lv);
    for (unsigned n = 0; n < 768; n++)
        DB(0x23d0 + n) = (uint8_t)(((uint32_t)DB(0x4e20 + n) * (uint16_t)lv) >> 8);
    SW(0x2d6c, 0); SW(0x2d6e, 0x100); SW(0x2d6a, 0x23d0);
}

static void flash_step(void) {                              /* 1000:0873..08b8 */
    static const uint8_t tab[15] = {0, 2, 8, 0x0c, 0x10, 0x14, 0x18, 0x20, 0x30, 0x40, 0x40, 0x40, 0x40, 0x40, 0x30};
    if (W(0x2db2) == W(0x2db4)) return;
    uint16_t i = (uint16_t)(W(0x2db2) >> 2);
    SW(0x2db2, W(0x2db2) - 1);
    if (i < 15) {
        uint8_t add = tab[i];   /* CS:08ec low bytes */
        for (unsigned n = 0; n < 768; n++) {
            unsigned v = DB(0x4e20 + n) + add;
            DB(0x23d0 + n) = (uint8_t)(v >= 0x3f ? 0x3f : v);
        }
        SW(0x2d6c, 0); SW(0x2d6e, 0x100); SW(0x2d6a, 0x23d0);
    }
}

/* The retrace waits (IN AL,3DAh loops at 065e / 0713) are not needed for timing in the port, but
 * reading 3DAh resets the attribute controller's index/data flip-flop, which the following AC
 * writes (and the main thread's display_on/off) depend on — so the read is kept. */
static void retrace_wait(void) { (void)io_in(0x3da); }

static void plain_tick_tail(void) {                         /* 065e..068d */
    retrace_wait();
    palette_tick();
    isr_music_hook();
    if (W(0x2c98)) SW(0x2c98, W(0x2c98) - 1);
}

void timer_isr(void) {                                      /* 1000:05f0 */
    SW(0x2c9a, W(0x2c9a) + 1);
    rng_step();
    if (S16(0x2c9c) < 0) {                                  /* display-only mode */
        crtc_write_start(W(0x2cd6), (uint8_t)((W(0x2cb4) >> 2) & 3));
        plain_tick_tail();
        return;
    }
    SW(0x12a, W(0x12a) - 1);
    if (W(0x2ca2) != 0) {                                   /* previous frame body still running */
        SW(0x126, W(0x126) + 1);
        if (S16(0x2c9e) >= 0) SW(0x2c9e, W(0x2c9e) - 1);
        plain_tick_tail();
        return;
    }
    if (S16(0x2c9e) >= 0) {
        SW(0x2c9e, W(0x2c9e) - 1);
        if (S16(0x2c9e) >= 0) { plain_tick_tail(); return; }
    }
    /* ---- render: start a new frame (0690) ---- */
    int16_t b = (int16_t)(W(0x126) * 2);
    SW(0x126, 0);
    if (b >= 0x20) b = 0x1e;
    SW(0x1435 + b, W(0x1435 + b) + 1);
    if (S16(0x12a) <= 0) {
        SW(0x12a, 0x20);
        int i = 15;
        while (W(0x1435 + 2 * i) == 0 && i != 0) i--;
        for (int j = i; j >= 0; j--) SW(0x1435 + 2 * j, 0);
        SW(0x124, i);
    }
    SW(0x2c9e, W(0x124));
    SW(0x2cd6, (W(0x2d9c) & 0x80) ? 0x6f00 : 0x2100);
    crtc_write_start(W(0x2cd6), (uint8_t)((W(0x2cb4) >> 2) & 3));
    io_out(0x3c0, 0x33);
    io_out(0x3c0, (uint8_t)((W(0x2cb4) & 3) * 2));
    retrace_wait();                                         /* 0713 */
    int8_t div = (int8_t)(DB(0x2c91) - 1);
    DB(0x2c91) = (uint8_t)div;
    if (div < 0) {                                          /* palette running light */
        DB(0x2c91) = 6;
        uint8_t old = DB(0x2c90);
        DB(0x2c90) = (uint8_t)(old + 1);
        if (DB(0x2c90) >= 15) DB(0x2c90) = 0;
        io_out(0x3c8, (uint8_t)(0xe0 + old)); io_out(0x3c9, 0); io_out(0x3c9, 0); io_out(0x3c9, 0);
        io_out(0x3c8, (uint8_t)(0xe0 + DB(0x2c90))); io_out(0x3c9, 0x3d); io_out(0x3c9, 0x34); io_out(0x3c9, 0x09);
    }
    plain_tick_tail();
    SW(0x2ca2, W(0x2ca2) + 1);
    if (W(0x2cb2) == 0) {                                   /* not paused */
        SW(0x2ca0, W(0x124));
        SW(0x2d9c, (uint16_t)~W(0x2d9c));
        do {                                                /* one logic step (0795..08b8) */
            if (!(W(0x2c9c) & 0x0400)) player_tick();
            edge_strip_update();
            set_draw_page_vars();
            R.ax = (uint16_t)(((W(0x2d9c) & 0x80) ? 0xa5d0 : 0xa0f0) + 0x120);
            R.ds = SEG_DG;
            engine_step();
            R.es = W(0x2d64); R.ds = SEG_DG;
            farcall(W(0x2d68), W(0x2d66));                  /* overlay per-frame entry (ovl:0008) */
            flash_step();
            SW(0x2ca0, W(0x2ca0) - 1);
        } while (S16(0x2ca0) >= 0);
    }
    DB(0xbd) ^= 0x80;
    hud_update();
    SW(0x2ca2, 0);
}

/* ---- keyboard (1000:09c3) ---- */
static uint8_t e0_pending;                                  /* CS:0a51 */
void kbd_event(uint8_t b) {
    if (!e0_pending && b == 0xe0) { e0_pending = 1; return; }
    e0_pending = 0;
    uint8_t rel = b >> 7, sc = b & 0x7f;
    if (!rel) { DB(0x2d82) = 1; DB(0x2d83) = sc; }
    if (sc < 0x3e) DB(0x2ce8 + sc) = rel ? 0x00 : 0xff;
    if (sc >= 0x39 && (uint8_t)(sc - 0x39) <= 0x17) {
        uint8_t m = rb(SEG_EXE, (uint16_t)(0x0a39 + sc - 0x39));
        if (rel) DB(0x3510) &= (uint8_t)~m; else DB(0x3510) |= m;
    }
}

/* ---- waits: the original spins on counters the ISR changes; we yield to the platform ---- */
void wait_ticks(uint16_t n) { SW(0x2c98, n); while (W(0x2c98) != 0) plat_yield(); }   /* 1000:0577 */
void wait_tick(void) { wait_ticks(1); }                                                 /* 1000:0581 */
void spin_while_nonzero_w(uint16_t o) { while (W(o) != 0) plat_yield(); }
void spin_while_zero_b(uint16_t o) { while (DB(o) == 0) plat_yield(); }

void display_on(void)  { (void)io_in(0x3da); (void)io_in(0x3ba); io_out(0x3c0, 0x20); }   /* 1000:0598 */
void display_off(void) { (void)io_in(0x3da); (void)io_in(0x3ba); io_out(0x3c0, 0x00); }   /* 1000:05a9 */

static int timer_installed;
void timer_install(void) {                                  /* 1000:0405 */
    timer_installed = 1;
    plat_set_isr(timer_isr);
    wait_tick();
    vga_reglist(0x34e5, SEG_DG);
    for (unsigned i = 0; i < 0xd0; i++) ww(SEG_EXE, (uint16_t)(0x2bb8 + 2 * i), (uint16_t)(6 * i));
    for (unsigned i = 0; i < 0x180; i++) ww(SEG_EXE, (uint16_t)(0x3158 + 2 * i), (uint16_t)(4 + i / 4));
    for (unsigned i = 0; i < 0x180; i++) {
        uint8_t m = (uint8_t)(0x11 << (i & 3) | 0x11 >> (8 - (i & 3)));
        ww(SEG_EXE, (uint16_t)(0x3838 + 2 * i), (uint16_t)(0x0002 | (m << 8)));
    }
}
void timer_remove(void) { timer_installed = 0; plat_set_isr(0); }   /* 1000:05ba */

static void key_from_platform(uint8_t sc, int down, int ext) {
    if (ext) kbd_event(0xe0);
    kbd_event((uint8_t)(sc | (down ? 0 : 0x80)));
}
void kbd_install(void) { plat_set_key_handler(key_from_platform); }        /* 1000:03cb */
void kbd_remove(void) { plat_set_key_handler(0); }                         /* 1000:03eb */
