/* Objects module — exports beyond game.h (objects.c, api.c, objects_selftest.c).
 * Spec: teardown/50-objects-api.md.  Addresses are Ghidra (seg 1000 = runtime 0824).
 *
 * Data layouts (all kept in MEM at the original addresses):
 *  - active-object slot i: DGROUP 0x131 + BP, BP = 0x44*i (i = 0..31)       (§2d)
 *  - pickup slot:          DGROUP 0x9b1 + BP, BP = 0xd*j  (j = 0..191)       (§2e)
 *  - score popup:          DGROUP 0x218f + BP, BP = 0xc*k (k = 0..15)        (§2f)
 *  - object record:        overlay segment ([2d64]) : offset, 20 bytes        (§2a)
 */
#ifndef YOGHO_OBJECTS_EXT_H
#define YOGHO_OBJECTS_EXT_H
#include <stdint.h>
#include "mem.h"

/* ---- active-object slot fields (offset from slot start; code addresses DS:[BP+0x131+f]) ---- */
#define SLOT_BASE   0x131
#define S_CODE      0x00   /* u16 handler offset (0 = free) */
#define S_CSEG      0x02   /* u16 handler segment (= overlay) */
#define S_ANIMS     0x04   /* u16 type+2 */
#define S_SPRITE    0x06   /* u16 sprite descriptor */
#define S_REC       0x08   /* u16 record / spawn template */
#define S_LAST      0x0a   /* u8  last frame */
#define S_FRAME     0x0b   /* u8  frame */
#define S_DELAY     0x0c   /* u8  delay reload */
#define S_DCNT      0x0d   /* u8  delay counter */
#define S_SHAPE     0x0e   /* u16 map-shape cache */
#define S_TX        0x10   /* u16 cached tile x */
#define S_TY        0x12   /* u16 cached tile y */
#define S_SCRIPT    0x14   /* u16 motion script ptr */
#define S_CNT       0x16   /* i16 script counter */
#define S_OP        0x18   /* u16 current op word */
#define S_AFLAGS    0x1a   /* u16 anim flags */
#define S_FRAMESET  0x1c   /* u16 frameset offset */
#define S_ANIMOFS   0x1e   /* u16 anim entry offset */
#define S_XF        0x20
#define S_X         0x22
#define S_YF        0x24
#define S_Y         0x26
#define S_VXF       0x28
#define S_VX        0x2a
#define S_VYF       0x2c
#define S_VY        0x2e
#define S_AXF       0x30
#define S_AX        0x32
#define S_AYF       0x34
#define S_AY        0x36
#define S_LATCH     0x38   /* u16 touch latch (A6) */
#define S_PARAM     0x3a   /* u16 record param / A0 motion flags */
#define S_W3C       0x3c   /* u16 bit0 riding (A22) / running (A17) */
#define S_T3E       0x3e   /* u8  A17 step timer */
#define S_T3F       0x3f   /* u8  A17 step index */
#define S_FREE40    0x40   /* u16 overlay use */
#define S_HITS      0x42   /* u16 hits */

/* ---- pickup slot fields (DS:[BP+0x9b1+f]) ---- */
#define PICK_BASE   0x9b1
#define P_CODE      0x00   /* stamp routine offset in seg 238e; 0 = free */
#define P_X         0x02
#define P_Y         0x04
#define P_SIDX      0x06
#define P_REC       0x08
#define P_VIS       0x0a   /* bit 0x8000 = stamped */
#define P_PFL       0x0c   /* u8 */

/* ---- score popup fields (DS:[BP+0x218f+f]) ---- */
#define POP_BASE    0x218f
#define Q_TYPE      0x00
#define Q_X         0x02
#define Q_YF        0x04
#define Q_Y         0x06
#define Q_SPF       0x08
#define Q_SP        0x0a

/* ---- object record fields (overlay seg) ---- */
#define REC_X       0x00
#define REC_Y       0x02
#define REC_FLAGS   0x04
#define REC_FLAGS0  0x06
#define REC_STATE   0x08
#define REC_KIND    0x0a   /* u8 */
#define REC_ANIM0   0x0b   /* u8  (big) */
#define REC_FRAME0  0x0c   /* u8  (big) */
#define REC_DELAY0  0x0d   /* u8  (big) */
#define REC_HANDLER 0x0e   /* u16 (big) */
#define REC_TYPE    0x10   /* u16 (big) */
#define REC_PARAM   0x12   /* u16 (big) */
#define REC_SIDX    0x0b   /* u16 (pickup) */
#define REC_STAMP   0x0d   /* u16 (pickup) */
#define REC_PFL     0x0f   /* u8  (pickup) */
#define REC_SIZE    0x14

static inline uint16_t slot_w(uint16_t bp, uint16_t f) { return dgw((uint16_t)(SLOT_BASE + bp + f)); }
static inline void slot_set(uint16_t bp, uint16_t f, uint16_t v) { dgw_set((uint16_t)(SLOT_BASE + bp + f), v); }
static inline uint8_t slot_b(uint16_t bp, uint16_t f) { return DB((uint16_t)(SLOT_BASE + bp + f)); }
static inline void slot_bset(uint16_t bp, uint16_t f, uint8_t v) { DB((uint16_t)(SLOT_BASE + bp + f)) = v; }
static inline uint16_t pick_w(uint16_t bp, uint16_t f) { return dgw((uint16_t)(PICK_BASE + bp + f)); }
static inline void pick_set(uint16_t bp, uint16_t f, uint16_t v) { dgw_set((uint16_t)(PICK_BASE + bp + f), v); }
static inline uint16_t pop_w(uint16_t bp, uint16_t f) { return dgw((uint16_t)(POP_BASE + bp + f)); }
static inline void pop_set(uint16_t bp, uint16_t f, uint16_t v) { dgw_set((uint16_t)(POP_BASE + bp + f), v); }
static inline uint16_t ovl_seg(void) { return dgw(0x2d64); }
static inline uint16_t rec_w(uint16_t rec, uint16_t f) { return rw(ovl_seg(), (uint16_t)(rec + f)); }
static inline uint8_t rec_b(uint16_t rec, uint16_t f) { return rb(ovl_seg(), (uint16_t)(rec + f)); }
static inline void rec_set(uint16_t rec, uint16_t f, uint16_t v) { ww(ovl_seg(), (uint16_t)(rec + f), v); }

/* 1000:04d2  overlay link: fill the 26 API far-JMP stubs at ovl:0x0b+5i from CS:050f and apply
 * the overlay's relocation table (ovl:[2]) for segment [2d64].  Called by level_load (160a:08b9). */
void overlay_link(void);
/* Register the API slots with farcall: ovl:0x000b+5i (every level) and the exe targets of the
 * CS:050f table both resolve to API slot i (handler 0x000b written by A6 = A0). */
void objects_register(void);
/* Developer self-test: loads L1 (DAT 77) at 4fce, links it, prints the object list and the
 * slots filled by a full activation scan at the level start position. */
int objects_selftest(void);

#endif
