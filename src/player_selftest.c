/* Player physics trace for comparison against DOSBox (developer self-test).
 *
 * Preconditions: MEM holds the exe (mem_load_exe) and YOGHODAT.DAT is open (dat_open).
 * Set-up mirrors what the original has done by the first logic tick of L1:
 *  - DAT directory at DGROUP 0x2258 (main reads the first 0x178 bytes of the file there);
 *  - new_game (160a:058c) physics constants 0x10a..0x120;
 *  - level_load (160a:062d) for level index 0: 0x2da6, row table 0x32f6, map / 4k entries at the
 *    segments the L1 snapshot shows (0x3488 / 0x35b4, into [2cda] / [2ce2]), the 1k entry to
 *    DS:0x4778, pixel bounds 0x514b..0x5151, respawn 0xe2/0xe4 = descriptor +0xf/+0x11;
 *  - level_run (160a:090d) per-life player reset and set_player_pos_and_camera(0,e2,0,e4);
 *  - 0x2c9c = 0 (normal play).
 * Only player_tick() runs — no engine_step (timers 0xb4/0xb7, objects, pickups) and no overlay —
 * so the trace is meaningful while no object touches the player and no hurt timer runs.
 * Input script (byte 0x3510 before each tick): idle 30, right 120, right+jump 60, idle 90. */
#include "game.h"
#include "dat.h"
#include "player_ext.h"
#include <stdio.h>
#include <string.h>

#define W(o)     dgw((uint16_t)(o))
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))

static uint32_t L(uint16_t o) { return (uint32_t)W(o) | (uint32_t)W(o + 2) << 16; }

int player_selftest(void) {
    size_t n;
    const uint8_t *raw = dat_raw(&n);
    if (!raw || n < 0x178) { fprintf(stderr, "player_selftest: DAT not open\n"); return 1; }
    memcpy(P(SEG_DG, 0x2258), raw, 0x178);                  /* directory, as main does */

    /* new_game (160a:058c) */
    SW(0xbe, 5);
    SW(0x10a, 0x2000); SW(0x10c, 0x0000);                   /* ACC_R  +0.125 */
    SW(0x10e, 0xe000); SW(0x110, 0xffff);                   /* ACC_L  -0.125 */
    SW(0x11a, 0x2000); SW(0x11c, 0x0000);                   /* GRAV   +0.125 */
    SW(0x112, 0x0000); SW(0x114, 0x0002);                   /* VMAX_R +2.0 */
    SW(0x116, 0x0000); SW(0x118, 0xfffe);                   /* VMAX_L -2.0 */
    SW(0x11e, 0x0000); SW(0x120, 0x0003);                   /* VFALL  +3.0 */

    /* level_load (160a:062d), level index 0 = L1 */
    DB(0x12d) = 0;
    uint16_t d = W(0x1b07);                                 /* descriptor */
    uint16_t w = W(d + 1), h = W(d + 3);
    SW(0x2da6, w * 2);
    for (uint16_t r = 0; r < h; r++) SW(0x32f6 + 2 * r, r * W(0x2da6));
    const uint16_t map_seg = 0x3488, blk_seg = 0x35b4;      /* L1 snapshot values */
    SW(0x2cda, map_seg); dat_load(W(d + 0xb), 0, map_seg);
    SW(0x2ce2, blk_seg); dat_load(W(d + 7), 0, blk_seg);
    {
        uint16_t e = W(d + 0xd);
        uint32_t pos = (uint32_t)W(0x2258 + 4 * e) | (uint32_t)W(0x225a + 4 * e) << 16;
        dat_read(pos, 0x400, 0x4778, SEG_DG);               /* always 1024 bytes */
    }
    SW(0x5151, w * 0x20 - 0x140); SW(0x514f, h * 0x20 - 0xa0);
    SW(0x514d, w * 0x20);         SW(0x514b, h * 0x20);
    SW(0xe2, W(d + 0xf)); SW(0xe4, W(d + 0x11));

    /* level_run (160a:090d) life start */
    DB(0xb2) = 5; DB(0xb1) = 0; DB(0xb3) = 0; DB(0xb4) = 0; DB(0xb6) = 0;
    for (uint16_t o = 0xf2; o < 0x10a; o += 2) SW(o, 0);
    SW(0xde, 0); SW(0xe0, 0); SW(0xb7, 0); DB(0xbd) = 0; DB(0xb5) = 1; SW(0xc2, 0); SW(0xd4, 0);
    SW(0x2c9c, 0);
    set_player_pos_and_camera(0, W(0xe2), 0, W(0xe4));

    printf("# L1 desc %04x w=%u h=%u start=%u,%u W=%u H=%u cam=%u,%u\n", d, w, h, W(0xe2), W(0xe4),
           W(0x514d), W(0x514b), W(0x2cb4), W(0x2cb6));
    printf("# tick in | x xf y yf | vx vy ax ay | 122 137d 137f 2db0 | b3 b5 b6 bd b7 12e 1377 | ae | cam\n");
    for (int t = 1; t <= 300; t++) {
        uint8_t in = t <= 30 ? 0x00 : t <= 150 ? 0x01 : t <= 210 ? 0x81 : 0x00;
        DB(0x3510) = in;
        player_tick();
        printf("%3d %02x | %5d %04x %5d %04x | %08x %08x %08x %08x | %04x %02x %02x %02x | %02x %02x %02x %02x %04x %02x %04x | %02x | %d,%d\n",
               t, in, (int16_t)W(0xe8), W(0xe6), (int16_t)W(0xec), W(0xea),
               (unsigned)L(0xf2), (unsigned)L(0xfe), (unsigned)L(0xf6), (unsigned)L(0x102),
               W(0x122), DB(0x137d), DB(0x137f), DB(0x2db0),
               DB(0xb3), DB(0xb5), DB(0xb6), DB(0xbd), W(0xb7), DB(0x12e), W(0x1377),
               W(0xae), (int16_t)W(0x2cb4), (int16_t)W(0x2cb6));
    }
    return 0;
}
