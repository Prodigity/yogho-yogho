/* Objects self-test: load the L1 overlay (DAT 77) at runtime segment 4fce like 160a:062d,
 * link it with overlay_link() (1000:04d2), print the object list through the record accessors,
 * then run activate_full() for the level start position and print the filled slots. */
#include "game.h"
#include "objects_ext.h"
#include "dat.h"
#include <stdio.h>
#include <string.h>

static const char *api_name(uint16_t h) {                   /* handler that is an API stub itself */
    static char b[8];
    if (h >= 0x0b && h < 0x0b + 26 * 5 && (h - 0x0b) % 5 == 0) { snprintf(b, sizeof b, "A%d", (h - 0x0b) / 5); return b; }
    return "";
}

int objects_selftest(void) {
    const uint16_t seg = 0x4fce;                            /* L1 runtime segment */
    size_t n;
    const uint8_t *d = dat_entry(77, &n);
    if (!d) { fprintf(stderr, "DAT 77 missing\n"); return 1; }
    memcpy(P(seg, 0), d, n);
    dgw_set(0x2d64, seg);
    farcall_set_overlay(0);
    overlay_link();

    printf("overlay %c%c at %04X: %u bytes, reloc@%04X list@%04X hdr6=%04X entry jmp=%02X\n",
           rb(seg, 0), rb(seg, 1), seg, (unsigned)n, rw(seg, 2), rw(seg, 4), rw(seg, 6), rb(seg, 8));
    for (int i = 0; i < 26; i += 25)
        printf("  stub %2d @%04X: %02X %04X:%04X\n", i, 0x0b + 5 * i, rb(seg, (uint16_t)(0x0b + 5 * i)),
               rw(seg, (uint16_t)(0x0e + 5 * i)), rw(seg, (uint16_t)(0x0c + 5 * i)));
    {
        uint16_t si = rw(seg, 2); int k = 0;
        printf("  relocs:");
        for (; rw(seg, si); si = (uint16_t)(si + 4), k++)
            printf(" %04X=%04X", rw(seg, si), rw(seg, rw(seg, si)));
        printf("  (%d)\n", k);
    }

    /* 160a:08cf: flags = flags0, state = 0 for every record at load */
    for (uint16_t r = rw(seg, 4); rec_w(r, REC_X); r = (uint16_t)(r + REC_SIZE)) {
        rec_set(r, REC_FLAGS, rec_w(r, REC_FLAGS0));
        rec_set(r, REC_STATE, 0);
    }
    printf("rec  off     x     y  flags init state kind details\n");
    int idx = 0;
    for (uint16_t r = rw(seg, 4); rec_w(r, REC_X); r = (uint16_t)(r + REC_SIZE), idx++) {
        printf("%3d  %04X %5u %5u  %04X  %04X %04X  ", idx, r, rec_w(r, REC_X), rec_w(r, REC_Y),
               rec_w(r, REC_FLAGS), rec_w(r, REC_FLAGS0), rec_w(r, REC_STATE));
        if (rec_b(r, REC_KIND) == 0)
            printf("OBJ  anim=%02X fr=%u dl=%u handler=%04X%s type=%04X param=%04X\n",
                   rec_b(r, REC_ANIM0), rec_b(r, REC_FRAME0), rec_b(r, REC_DELAY0), rec_w(r, REC_HANDLER),
                   api_name(rec_w(r, REC_HANDLER)), rec_w(r, REC_TYPE), rec_w(r, REC_PARAM));
        else
            printf("PICK kind=%u sidx=%u (type %u) stamp=238e:%04X pfl=%02X\n", rec_b(r, REC_KIND),
                   rec_w(r, REC_SIDX), dgw((uint16_t)(0x2dc6 + rec_w(r, REC_SIDX))), rec_w(r, REC_STAMP),
                   rec_b(r, REC_PFL));
    }

    /* Level start: descriptor (DGROUP 0x1b07[0]) map size -> 0x5151/514f/514d/514b (160a:088f),
     * start position +0x0f/+0x11, activation bounds as 160a:1174 (test-only replica). */
    uint16_t desc = dgw(0x1b07);
    uint16_t mx = (uint16_t)((dgw((uint16_t)(desc + 1)) << 5) - 0x140), my = (uint16_t)((dgw((uint16_t)(desc + 3)) << 5) - 0xa0);
    dgw_set(0x5151, mx); dgw_set(0x514f, my);
    dgw_set(0x514d, (uint16_t)(mx + 0x140)); dgw_set(0x514b, (uint16_t)(my + 0xa0));
    uint16_t px = dgw((uint16_t)(desc + 0xf)), py = dgw((uint16_t)(desc + 0x11));
    int cx = (int16_t)px - 0xa0, cy = (int16_t)py - 0x64;
    uint16_t camx = (uint16_t)(cx <= 0 ? 0 : cx >= (int16_t)mx ? mx : cx);
    uint16_t camy = (uint16_t)(cy <= 0 ? 0 : cy >= (int16_t)my ? my : cy);
    uint16_t t;
    dgw_set(0x5134, (uint16_t)(camx - 0xa0));
    t = (uint16_t)(camx + 0x1e0); dgw_set(0x5132, t >= dgw(0x514d) ? dgw(0x514d) : t);
    dgw_set(0x5130, (uint16_t)(camy - 0xa0));
    t = (uint16_t)(camy + 0x140); dgw_set(0x512e, t >= dgw(0x514b) ? dgw(0x514b) : t);
    printf("start (%u,%u) cam (%u,%u) bounds x[%d..%d] y[%d..%d]\n", px, py, camx, camy,
           (int16_t)dgw(0x5134), (int16_t)dgw(0x5132), (int16_t)dgw(0x5130), (int16_t)dgw(0x512e));

    for (uint16_t bp = 0; bp <= 0x83c; bp = (uint16_t)(bp + 0x44)) { slot_set(bp, S_CODE, 0); slot_set(bp, S_CSEG, seg); }
    for (uint16_t bp = 0; bp <= 0x9b3; bp = (uint16_t)(bp + 0xd)) pick_set(bp, P_CODE, 0);
    R.ds = SEG_DG;
    activate_full();
    printf("cursor [5136]=%04X\n", dgw(0x5136));
    for (uint16_t bp = 0x83c; !(bp & 0x8000); bp = (uint16_t)(bp - 0x44)) {
        if (!slot_w(bp, S_CODE)) continue;
        printf("slot %2u rec=%04X (%u,%u) code=%04X spr=%04X anim=%04X fs=%04X fr=%u/%u dl=%u/%u aflags=%04X"
               " script=%04X cnt=%d v=(%d,%d) hits=%02X\n",
               bp / 0x44, slot_w(bp, S_REC), slot_w(bp, S_X), slot_w(bp, S_Y), slot_w(bp, S_CODE),
               slot_w(bp, S_SPRITE), slot_w(bp, S_ANIMOFS), slot_w(bp, S_FRAMESET), slot_b(bp, S_FRAME),
               slot_b(bp, S_LAST), slot_b(bp, S_DCNT), slot_b(bp, S_DELAY), slot_w(bp, S_AFLAGS),
               slot_w(bp, S_SCRIPT), (int16_t)slot_w(bp, S_CNT), (int16_t)slot_w(bp, S_VX),
               (int16_t)slot_w(bp, S_VY), slot_w(bp, S_HITS));
    }
    for (uint16_t bp = 0x9b3; !(bp & 0x8000); bp = (uint16_t)(bp - 0xd)) {
        if (!pick_w(bp, P_CODE)) continue;
        printf("pick %3u rec=%04X (%u,%u) stamp=%04X sidx=%u\n", bp / 0xd, pick_w(bp, P_REC),
               pick_w(bp, P_X), pick_w(bp, P_Y), pick_w(bp, P_CODE), pick_w(bp, P_SIDX));
    }
    return 0;
}
