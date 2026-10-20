/* HUD and player drawing (Ghidra segment 160a, C code).  Spec: teardown/30-render.md §5.1, §7;
 * 80-data.md §2.8.  The HUD lives in VGA rows 0..39 (split screen, screen y = 160 + VGA y) and is
 * drawn directly (single-buffered); hud_update redraws only the fields whose cache changed.
 * Tables are read from DGROUP (exe image): 26dc/26e8/26f4 level icons, 2720 digit sprites,
 * 2724 "Veld" digits, 2798 energy patches, 27ae/27ba item icon positions, 1f7f/1f4f/1fdd/203b
 * player part lists. */
#include "game.h"

#define W(o)   dgw(o)
#define SW(o, v) dgw_set((o), (uint16_t)(v))

#define HUD_SPR_SEG SEG_G(0x2818)   /* PUSH 2818 immediates (relocated) at 160a:1e64 / 1ebc */

void player_draw(uint16_t frame) {                          /* 160a:1bcc (AX = frame) */
    uint16_t ssi = R.si, sdi = R.di;
    uint16_t si = W((uint16_t)(0x1f7f + frame * 2));
    if (DB(0xb4) != 0 && (DB(0xbd) & 0x80)) return;         /* 1bdf: invulnerability blink */
    if (dgs(0xec) > 0) {                                    /* 1bed JLE: py <= 0 -> no parts */
        while (W(si) | W((uint16_t)(si + 2))) {             /* part list ends with far 0:0 */
            R.bx = (uint16_t)(W(0xe8) + W((uint16_t)(si + 4)));
            R.di = (uint16_t)(W(0xec) + W((uint16_t)(si + 6)));
            R.si = si; R.ds = SEG_DG;
            draw_cs_world_ds();                             /* CALLF 1000:17b5 */
            si = (uint16_t)(si + 8);
        }
    }
    if (DB(0xbd) & 1) {                                     /* 1c1c: shield overlay */
        uint16_t di = W((uint16_t)(0x1fdd + frame * 2));
        uint16_t bx = W((uint16_t)(0x203b + frame * 2));
        si = (uint16_t)((W(0xbb) << 3) + 0x1f4f);
        R.bx = (uint16_t)(bx + W(0xe8) + W((uint16_t)(si + 4)));
        R.di = (uint16_t)(di + W(0xec) + W((uint16_t)(si + 6)));
        R.si = si; R.ds = SEG_DG;
        draw_cs_world_ds();
    }
    R.si = ssi; R.di = sdi; R.ds = SEG_DG;
}

void hud_init(void) {                                       /* 160a:1cf8 */
    dat_load(0, W(0x13fa), W(0x13fc));                      /* HUD panel 320x40 */
    blit_linear_vga(W(0x13fa), W(0x13fc), 0, 0, 0x140, 0x28);
    dat_load(1, W(0x13fa), W(0x13fc));                      /* level icons */
    uint16_t i = (uint16_t)((DB(0x12d) >> 1) * 2);          /* CWD / SUB / SAR 1 / SHL 1 */
    blit_planar_vga_t(W((uint16_t)(0x26dc + i)), W(0x13fc), 0xe9, 0x14,
                      (int16_t)W((uint16_t)(0x26e8 + i)), (int16_t)W((uint16_t)(0x26f4 + i)));
    uint16_t j = (uint16_t)((DB(0x12d) >> 1) * 4);
    draw_cs_abs(W((uint16_t)(0x2724 + j)), W((uint16_t)(0x2726 + j)), 0xdc, 0x1b);   /* "Veld" digit */
    hud_update();
}

void hud_update(void) {                                     /* 160a:1da0 */
    uint16_t seg74 = W(0x13fe);                             /* DAT 74 HUD patches */
    /* score [cc:ca], 32-bit BCD, cache [d0:ce] */
    uint16_t si = 0x7a;
    if (W(0xd0) != W(0xcc) || W(0xce) != W(0xca)) {
        uint16_t hi = W(0xcc), lo = W(0xca);
        SW(0xd0, hi); SW(0xce, lo);
        while (lo | hi) {                                   /* score 0 draws nothing */
            blit_planar_vga(0x8ab, seg74, (int16_t)si, 4, 0x0b, 0x0a, 0);
            uint16_t g = (uint16_t)((lo & 0x0f) << 2);
            draw_cs_abs(W((uint16_t)(0x2720 + g)), W((uint16_t)(0x2722 + g)), (int16_t)si, 4);
            lo = (uint16_t)((lo >> 4) | (hi << 12));        /* 1000:4cba: unsigned 32-bit >> 4 */
            hi = (uint16_t)(hi >> 4);
            si = (uint16_t)(si - 0x0c);
        }
    }
    /* lives [be], cache [c6].  The loop is DEC DI / JNZ entered at the DEC: n-1 icons are drawn
     * (n = min(lives, 6)); n = 0 would wrap (as in the original). */
    if (W(0xc6) != W(0xbe)) {
        SW(0xc6, W(0xbe));
        uint16_t n = (dgs(0xbe) >= 6) ? 6 : W(0xbe);
        si = 0x5b;
        blit_planar_vga(0x3eb, seg74, 0x5b, 0x15, 0x4c, 0x10, 0);
        while (--n) {
            draw_cs_abs(0x0c82, HUD_SPR_SEG, (int16_t)si, 0x15);
            si = (uint16_t)(si + 0x0f);
        }
    }
    /* items [c0], cache [c8] */
    if (W(0xc8) != W(0xc0)) {
        SW(0xc8, W(0xc0));
        uint16_t n = (dgs(0xc0) >= 6) ? 6 : W(0xc0);
        blit_planar_vga(0x000, seg74, 0xaa, 4, 0x3b, 0x11, 0);
        for (si = (uint16_t)(n << 1); si; si = (uint16_t)(si - 2))
            draw_cs_abs(0x0b0a, HUD_SPR_SEG, (int16_t)W((uint16_t)(0x27ae + si)), (int16_t)W((uint16_t)(0x27ba + si)));
    }
    /* energy [b2] (signed byte), cache [b1] — glass at (263,4), 23x32 */
    int8_t b2 = (int8_t)DB(0xb2), b1 = (int8_t)DB(0xb1);
    if (b2 == b1) return;
    if (b1 < b2 && b2 != 5)                                 /* 1edd JGE / 1ee6 */
        blit_planar_vga(0x919, seg74, 0x107, 4, 0x17, 0x20, 0);     /* full glass first */
    if (b2 < 0) {
        blit_planar_vga(0xddc, seg74, 0x107, 4, 0x17, 0x20, 0);     /* empty glass */
    } else {
        uint16_t k = (uint16_t)((int16_t)b2 * 4);           /* CBW / SHL 2 */
        uint16_t s = W((uint16_t)(0x279a + k));             /* surface row */
        if (s) {
            blit_planar_vga(0xddc, seg74, 0x107, 4, 0x17, (int16_t)s, (int16_t)(0x20 - s));  /* empty rows 0..s-1 */
            blit_planar_vga(W((uint16_t)(0x2798 + k)), seg74, 0x107, (int16_t)(s + 4), 0x17, 7, 0);   /* surface */
        } else {
            blit_planar_vga(W((uint16_t)(0x2798 + k)), seg74, 0x107, 4, 0x17, 0x20, 0);
        }
    }
    DB(0xb1) = DB(0xb2);
}
