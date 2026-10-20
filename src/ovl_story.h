/* Story-level overlays L1..L6 (DAT 77..82, level indices 0,2,4,6,8,10).
 * Spec: teardown/60-overlays.md (§4 families, §5..§10 levels), engine side 50-objects-api.md.
 * Ground truth: the recursive-descent listings extracted/ovl_dis/L1..L6.asm — every routine here
 * is a line-by-line transcription of one of them (addresses "L3:2b99" = overlay L3 offset 2b99).
 *
 * All handlers are REG routines (cpu.h): entered from the engine object loop (1000:1650) with
 * AX = S.x, DX = S.y, BP = slot*0x44, ES = overlay, DS = DGROUP, exactly like the original
 * CALLF [slot+0].  Engine API calls ("push cs; call ovl:0x0b+5i") are plain calls of the A*()
 * ports; a tail "jmp stub" is the call followed by return.  All overlay data (records, type /
 * sprite descriptors, templates, lookup tables, the L3 self-modified template byte) stays in MEM
 * at overlay_seg:offset (overlay_seg = DGROUP word 0x2d64) and is read/written there.
 *
 * Register dependencies on the API ports (as in the original, see 50-objects-api.md §3d/§3i):
 * A6 preserves AX/DX, sets BX (0/1/-1) and ZF, and leaves DI = S.sprite (L6:2891 reads ES:[DI+7]);
 * A3 preserves AX/DX and returns BX/CX; A4 returns AX/DX/BX; A2 returns CF and AH, keeps DX;
 * A7/A8 keep DX; A21 keeps AX/DX/BP and returns SI with SF; A22 returns ZF and SI; A23 returns AX
 * and keeps BX/CX/DX.
 */
#ifndef YOGHO_OVL_STORY_H
#define YOGHO_OVL_STORY_H
#include "game.h"
#include "log.h"

/* Registers every story-overlay handler (object handlers, spawn-template handlers, the +8
 * per-frame entry, and the A0 handler pointer 0x000b that A6 stores into a slot) with
 * farcall_register_overlay().  To be called from ovl_register_all(). */
void ovl_story_register(void);

/* ---------------- internal: shared between ovl_shared.c and ovl_L1..L6.c ---------------- */

/* Level indices (DGROUP byte 0x12d) of the story overlays. */
enum { LVL_L1 = 0, LVL_L2 = 2, LVL_L3 = 4, LVL_L4 = 6, LVL_L5 = 8, LVL_L6 = 10 };

/* Overlay segment: CS = ES = DGROUP word 0x2d64 inside every handler. */
static inline uint16_t ovl_seg(void) { return dgw(0x2d64); }
/* cs:/es: overlay memory */
static inline uint16_t CSW(uint16_t o) { return rw(ovl_seg(), o); }
static inline uint8_t  CSB(uint16_t o) { return rb(ovl_seg(), o); }
static inline void CSW_SET(uint16_t o, uint16_t v) { ww(ovl_seg(), o, v); }
static inline void CSB_SET(uint16_t o, uint8_t v) { wb(ovl_seg(), o, v); }
/* ds:[bp+o] slot fields; o is the displacement exactly as written in the listing (0x153 = S.x) */
static inline uint16_t BPW(uint16_t o) { return dgw((uint16_t)(R.bp + o)); }
static inline void BPW_SET(uint16_t o, uint16_t v) { dgw_set((uint16_t)(R.bp + o), v); }
static inline uint8_t BPB(uint16_t o) { return DB((uint16_t)(R.bp + o)); }
static inline void BPB_SET(uint16_t o, uint8_t v) { DB((uint16_t)(R.bp + o)) = v; }

/* x86 helpers */
static inline uint16_t sar16(uint16_t v, int n) {          /* SAR r16,n */
    if (!(v & 0x8000)) return (uint16_t)(v >> n);
    uint16_t t = (uint16_t)~v; t = (uint16_t)(t >> n); return (uint16_t)~t;
}
static inline uint16_t cbw16(uint16_t ax) { return (uint16_t)(int16_t)(int8_t)(uint8_t)ax; }   /* CBW */
static inline uint16_t xchg_ahal(uint16_t ax) { return (uint16_t)((ax << 8) | (ax >> 8)); }    /* XCHG AH,AL */
static inline uint16_t cwd_abs(uint16_t ax) {              /* CWD; XOR AX,DX; SUB AX,DX (sets R.dx) */
    R.dx = (ax & 0x8000) ? 0xffff : 0; return (uint16_t)((ax ^ R.dx) - R.dx);
}
static inline uint16_t cwd_abs1(uint16_t ax) {             /* CWD; XOR AX,DX (ones' complement) */
    R.dx = (ax & 0x8000) ? 0xffff : 0; return (uint16_t)(ax ^ R.dx);
}
#define S16(v) ((int16_t)(uint16_t)(v))

/* Tail-call shorthands */
#define TAIL(fn) do { fn(); return; } while (0)

/* ---------------- shared routines (ovl_shared.c) ---------------- */
int  ovl_keep(void);                      /* standard keep-window prologue; 0 = outside (caller jumps to A10) */
void ovl_grav(uint16_t acc);              /* L4:3f4d = L5:3872 = L6:37a1 (acc 0x2000), inline copies; in DX, out CX, BX kept */
void ovl_hud(uint16_t n, uint16_t desc);                 /* F-HUD   L1:0cad L2:0f2d L5:2639 L6:2745 */
void ovl_hud3(uint16_t n, uint16_t nf, uint16_t base);   /* F-HUD3  L3:2625 L4:32b9 */
void ovl_spring(void);                    /* F-SPRING L1:0ee6 L2:167a L3:26d0 L4:3374 L5:26ae (L6:27ba body) */
enum { DOOR_KEY, DOOR_ICE, DOOR_TOWER, DOOR_L5 };
void ovl_door(int mode);                  /* F-DOOR L1:19f1 L2:1709 L3:275f L4:3403 (L6:2915 body); L3:2831 L5:2835 L5:273d */
enum { Q_SFX_BEFORE = 1, Q_SFX_AFTER = 2, Q_ANIM = 4, Q_NOPUSH = 8, Q_TESTBX = 16 };
void ovl_quest(uint16_t score, uint16_t d, unsigned flags);   /* F-QUEST L1:1861 L2:1baf L3:34bf L4:3f05 L6:3173 */
void ovl_path(void);                      /* F-PATH   L2:1594 = L2:1610 */
void ovl_pathcol(int with_a2);            /* F-PATHCOL L3:3105 = L5:2b93 (1); L5:2d99 (0) */
void ovl_turnwalker(void);                /* F-TURNWALKER L1:1967 = L3:31f7 */
void ovl_roller(void);                    /* F-ROLLER L1:1025 = L3:295f */
void ovl_seeker(void);                    /* F-SEEKER L5:2921 (L6:2a0d body) */
void ovl_charger(void);                   /* F-CHARGER L4:35e1 = L5:30a7 */

/* per-overlay registration (ovl_L1.c .. ovl_L6.c) */
void ovl_L1_register(void);
void ovl_L2_register(void);
void ovl_L3_register(void);
void ovl_L4_register(void);
void ovl_L5_register(void);
void ovl_L6_register(void);

#endif
