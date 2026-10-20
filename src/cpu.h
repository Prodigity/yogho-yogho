/* Register context for routines that the original passes arguments to in registers.
 *
 * Hand-written asm routines of the engine (segment 1000), the 26 overlay API slots and every
 * overlay handler use register calling conventions (AX=x, DX=y, BP=slot, ES=overlay, results
 * in BX/CX/SI and flags).  Their C ports take no parameters and read/write the global R exactly
 * like the registers, so a ported handler can be checked line by line against the listing.
 * C-convention routines (Borland cdecl: most of segment 160a and the blitters with stack args)
 * take ordinary C parameters instead.
 */
#ifndef YOGHO_CPU_H
#define YOGHO_CPU_H
#include <stdint.h>

typedef struct {
    uint16_t ax, bx, cx, dx, si, di, bp;
    uint16_t ds, es;
    uint8_t cf, zf, sf;        /* flags a routine's result is tested by (set explicitly) */
} Regs;

extern Regs R;

#define AL(r) ((uint8_t)(r).ax)
#define AH(r) ((uint8_t)((r).ax >> 8))
#define SET_AL(r, v) ((r).ax = (uint16_t)(((r).ax & 0xff00) | (uint8_t)(v)))
#define SET_AH(r, v) ((r).ax = (uint16_t)(((r).ax & 0x00ff) | ((uint16_t)(uint8_t)(v) << 8)))
#define BL(r) ((uint8_t)(r).bx)
#define BH(r) ((uint8_t)((r).bx >> 8))
#define CL(r) ((uint8_t)(r).cx)
#define DL(r) ((uint8_t)(r).dx)

/* Set ZF/SF from a 16-bit result, like OR reg,reg / TEST. */
static inline void flags16(uint16_t v) { R.zf = v == 0; R.sf = (v & 0x8000) != 0; }

/* ---- dynamic far calls ----
 * The original jumps to code through far pointers held in data: object-slot handlers
 * (CALLF [slot+0]), the overlay per-frame entry (CALLF [2d66]), compiled sprites (CALLF to
 * sprite code in the exe or an overlay bank).  farcall() resolves seg:off to the C port:
 *  - code inside the loaded overlay's code segment -> the overlay's registered handler table;
 *  - a compiled-sprite routine (recognised by its prologue in MEM) -> the sprite interpreter;
 *  - code in the exe image -> the engine's registered routines.
 * An unknown target is a porting gap: it is logged with its address and the call is skipped. */
typedef void (*code_fn)(void);
void farcall(uint16_t seg, uint16_t off);
/* Registration.  Overlay handlers register by offset in the overlay code segment; they are
 * looked up relative to the currently loaded overlay (DGROUP 0x2d64). */
void farcall_register_exe(uint16_t ghidra_seg, uint16_t off, code_fn f);
void farcall_register_overlay(int level_tag, uint16_t off, code_fn f);   /* level_tag: 0..11 = L1,B1,L2,B2,... (level index 0x12d) */
void farcall_set_overlay(int level_index);                              /* which overlay table is live */
/* Compiled-sprite drawer (implemented by the renderer): called by farcall when the target is
 * a compiled sprite.  Uses R.es, R.di, R.ax (map-mask word), R.dx exactly like the original. */
void cspr_exec(uint16_t seg, uint16_t off);
int  cspr_is_sprite(uint16_t seg, uint16_t off);

#endif
