/* Hi-score screen: insertion, name entry, table display, EUPHORIA cheat.
 * Spec: teardown/10-flow-frontend.md §2.1, §3.7.  Addresses are Ghidra (segment 160a). */
#include "game.h"
#include "flow_ext.h"
#include "platform.h"

#define W(o)     dgw(o)
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))

/* Table DGROUP 0x33f4, 10 x 16 bytes: +0 u32 packed-BCD score (lo,hi), +4 name[8], +0xf world. */
#define HS 0x33f4

/* ---- 160a:2a7d: one row at y = 16*i + 16, page 0, sprites from DAT 75 ([0x5161]) ---- */
void highscore_draw_row(uint16_t i) {
    uint16_t y = (uint16_t)((i << 4) + 0x10);               /* [bp-2] */
    uint8_t rec[16];                                        /* local copy [bp-0x16] */
    for (int k = 0; k < 16; k++) rec[k] = DB(HS + (i << 4) + k);
    for (uint16_t k = 0, x = 0x0c; x != 0x6c; k++, x += 0x0c) {   /* name, all 8 glyphs */
        uint8_t g = DB(0x14b7 + (rec[4 + k] & 0x3f));
        blit_planar_vga_t((uint16_t)(0x2600 + g * 0xc0), W(0x5161), (int16_t)x, (int16_t)y, 0x0c, 0x10);
    }
    uint16_t lo = (uint16_t)(rec[0] | rec[1] << 8), hi = (uint16_t)(rec[2] | rec[3] << 8);
    uint32_t v = (uint32_t)hi << 16 | lo;                   /* [bp-4]:[bp-6] */
    for (uint16_t x = 0xbe; x != 0x6a && v != 0; x -= 0x0c) {     /* digits from the right, max 7 */
        blit_planar_vga_t((uint16_t)(0x3d40 + (v & 0xf) * 0xc0), W(0x5161), (int16_t)x, (int16_t)y, 0x0c, 0x10);
        v >>= 4;                                            /* long unsigned shr 1000:4cba */
    }
    if (rec[0xf] < 6)                                       /* world icon */
        blit_planar_vga_t(W(0x26d0 + 2 * rec[0xf]), W(0x5161), 0x128, (int16_t)y, 0x10, 0x10);
    /* rank badge by score (u32 compares against hi:0000): 50k,100k,150k,200k,300k,400k,500k */
    static const uint16_t lim[7] = {0x05, 0x10, 0x15, 0x20, 0x30, 0x40, 0x50};
    uint16_t badge = 0x1c00;
    for (int k = 0; k < 7; k++) if (hi < lim[k]) { badge = (uint16_t)(k * 0x400); break; }
    blit_planar_vga_t(badge, W(0x5161), 0xcc, (int16_t)y, 0x40, 0x10);
}

/* ---- 160a:2a13 ---- */
void highscore_draw_all(void) {
    vga_reglist(0x34e5, SEG_DG);
    set_palette(0, W(0x5167), 0, 0x100);
    blit_linear_vga(0, W(0x5163), 0, 0, 0x140, 0xc8);
    blit_linear_vga(0, W(0x5163), 0, 0xc8, 0x140, 0xc8);
    SW(0x2cde, W(0x2cda));                                  /* quirk, harmless */
    for (uint16_t i = 0; i < 10; i++) highscore_draw_row(i);
}

/* ---- 160a:273c ---- */
int highscore_screen(void) {
    int16_t timeout = 0x118;                                /* [bp-2] */
    int16_t rank = -1;                                      /* [bp-6], SI */
    uint8_t cur = 0, blink = 0, blink_on = 0, cheat = 0;    /* [bp-8], [bp-9], [bp-0xa], [bp-0xb] */
    int ret = 1;                                            /* [bp-0xe] */
    if (W(0xca) | W(0xcc)) {
        int16_t i = 0;
        for (uint16_t r = HS; r != 0x3494; r += 0x10, i++) {
            uint16_t hi = W(r + 2), lo = W(r);
            if (hi > W(0xcc) || (hi == W(0xcc) && lo >= W(0xca))) continue;   /* insert if score > rec */
            rank = i;
            for (int16_t j = 8, dx = 0x80; j >= rank; j--, dx -= 0x10)
                for (int k = 0; k < 16; k++) DB(0x3404 + dx + k) = DB(HS + dx + k);
            SW(0x33f6 + (rank << 4), W(0xcc));
            SW(0x33f4 + (rank << 4), W(0xca));
            DB(0x3403 + (rank << 4)) = (uint8_t)(DB(0x12d) / 2);
            for (int k = 0; k < 8; k++) DB(0x33f8 + (rank << 4) + k) = 0;
            break;
        }
    }
    highscore_draw_all();
    fade_in(8);
    for (int k = 0; k < 0x3e; k++) DB(0x2ce8 + k) = 0;
    DB(0x2d82) = 0;
    if (rank != -1) {
        /* ---- name entry 160a:283c ---- */
        for (;;) {
            if (W(0x2c8a) & 2) break;
            wait_tick();
            menu_palette_tick();
            uint8_t c = (uint8_t)getkey_ascii();
            if (c) {
                if (c == 0x0d) break;                       /* Enter */
                if (c == 0x09 && cur > 0) {                 /* Backspace */
                    cur--;
                    DB(0x33f8 + (rank << 4) + cur) = 0;
                    cursor_row16((uint16_t)((rank << 4) + 0x10));
                    highscore_draw_row((uint16_t)rank);
                }
                if (cur < 8 && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')) {
                    DB(0x33f8 + (rank << 4) + cur) = c;
                    cur++;
                    cursor_row16((uint16_t)((rank << 4) + 0x10));
                    highscore_draw_row((uint16_t)rank);
                }
            }
            if ((int8_t)blink-- < 0) {                      /* tests the old value */
                blink = 0x0f;
                blink_on ^= 1;
                if (cur < 8) {
                    cursor_row16((uint16_t)((rank << 4) + 0x10));
                    highscore_draw_row((uint16_t)rank);
                    if (blink_on)
                        blit_planar_vga_t(0x3b00, W(0x5161), (int16_t)(cur * 0x0c + 0x0c),
                                          (int16_t)((rank << 4) + 0x10), 0x0c, 0x10);
                }
            }
            if (plat_should_quit()) break;                  /* port: window closed */
        }
        cursor_row16((uint16_t)((rank << 4) + 0x10));       /* final redraw without cursor */
        highscore_draw_row((uint16_t)rank);
    }
    /* ---- display 160a:29e9 / 298a ---- */
    while (--timeout > 0 && !(W(0x2c8a) & 2)) {
        wait_tick();
        menu_palette_tick();
        if (DB(0x2ce9)) { DB(0x2ce9) = 0; break; }          /* Esc */
        uint8_t c = (uint8_t)getkey_ascii();
        if (c) {
            if (c == 'S') { ret = 0; break; }               /* start a game directly */
            if (DB(0x224f + cheat) == c) {                  /* "EUPHORIA" */
                if (++cheat == 8) { SW(0x2ca4, 3); DB(0xb0) ^= 1; cheat = 0; }
            } else cheat = 0;
        }
        if (plat_should_quit()) break;                      /* port */
    }
    SW(0xcc, 0); SW(0xca, 0);
    fade_out(4);
    return ret;
}
