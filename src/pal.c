/* Palette helpers (Ghidra segment 160a, C code).  Spec: teardown/30-render.md §8, 80-data §2.1.
 * DG:4e20 = master palette (768 bytes, 6-bit), DG:23d0 = working copy uploaded by the ISR
 * (1000:0935, isr.c palette_tick).  The fades only set the ISR fade variables [5157] (step),
 * [5159] (target), [515b] (level) and spin until the ISR clears the step: the spin yields to the
 * platform (spin_while_nonzero_w), which keeps running the ISR. */
#include "game.h"

void set_palette(uint16_t off, uint16_t seg, uint16_t first, uint16_t count) {   /* 160a:2e6d */
    uint16_t si = (uint16_t)(first * 3u), di = off;
    uint16_t end = (uint16_t)((uint16_t)(first + count) * 3u - 3u);   /* count-1 colours (quirk) */
    while (end > si) {                                      /* CMP DX,SI / JA (unsigned) */
        uint8_t v = rb(seg, di);
        DB(0x23d0 + si) = v;
        DB(0x4e20 + si) = v;
        di++; si++;
    }
}

void fade_out(uint16_t step) {                              /* 160a:3018 */
    dgw_set(0x515b, 0x100);
    dgw_set(0x5159, 0);
    dgw_set(0x5157, (uint16_t)-step);
    spin_while_nonzero_w(0x5157);
    wait_tick();
    display_off();
}

void fade_in(uint16_t step) {                               /* 160a:3042 */
    pal_black();
    wait_tick();
    display_on();
    dgw_set(0x515b, 0);
    dgw_set(0x5159, 0x100);
    dgw_set(0x5157, step);
    spin_while_nonzero_w(0x5157);
    wait_tick();
}

void pal_scale(uint16_t scale, uint16_t first, uint16_t end) {   /* 160a:3074 */
    uint16_t lim = (uint16_t)(end * 3u);
    for (uint16_t si = (uint16_t)(first * 3u); lim > si; si++) {
        /* MOV AL,[4e20+SI]; MOV AH,0; IMUL CX; SAR AX,8 (low word of the product only) */
        int16_t p = (int16_t)(uint16_t)((uint32_t)DB(0x4e20 + si) * scale);
        DB(0x23d0 + si) = (uint8_t)(p >> 8);
    }
    wait_tick();
    dac_schedule(0x23d0, first, (uint16_t)(end - first));
}

void pal_black(void) {                                      /* 160a:30c5 */
    for (uint16_t i = 0; i < 0x300; i++) DB(0x23d0 + i) = 0;
    dac_schedule(0x23d0, 0, 0x100);
    wait_tick();
}
