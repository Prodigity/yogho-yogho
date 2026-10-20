/* Real-mode memory model.
 *
 * The rewrite keeps the original's data layout: a 1 MB conventional-memory array addressed by
 * segment:offset, the EXE image at segment 0x0824 and DGROUP at 0x226A (the values observed in
 * DOSBox-X), so that
 *  - the original's initialised data tables (level descriptors, strings, collision shapes,
 *    driver tables, ...) are read from the user's YOGHO.EXE instead of being retyped, and
 *  - any memory dump taken from the original under the DOSBox-X debugger can be diffed byte for
 *    byte against the port's state at the same tick.
 * Only data lives here; all code is rewritten C.
 *
 * Address notation in comments: Ghidra "SSSS:OOOO" (exe loaded at 1000) as in the teardown docs;
 * runtime segment = Ghidra segment - 0x07DC.
 */
#ifndef YOGHO_MEM_H
#define YOGHO_MEM_H
#include <stdint.h>
#include <string.h>

#define MEM_SIZE 0x110000   /* 1 MB + 64 KB so seg:off never needs wrapping */
extern uint8_t MEM[MEM_SIZE];

#define SEG_EXE    0x0824u               /* Ghidra 1000 */
#define SEG_G(s)   ((uint16_t)((s) - 0x07DCu))   /* Ghidra segment -> runtime segment */
#define SEG_DG     0x226Au               /* DGROUP (Ghidra 2a46) */
#define SEG_ADLIB  SEG_G(0x2f6e)         /* 0x2792 */

#include "vga.h"

static inline uint32_t lin(uint16_t seg, uint16_t off) { return ((uint32_t)seg << 4) + off; }
/* Raw pointer into conventional memory.  Never use it for VRAM (A000-AFFF): use rb/wb. */
static inline uint8_t *P(uint16_t seg, uint16_t off) { return &MEM[lin(seg, off)]; }
/* Byte/word access with VRAM routing: linear A0000-AFFFF goes through the VGA planar logic. */
static inline uint8_t rbl(uint32_t a) {
    if (a - 0xA0000u < 0x10000u) return vga_mem_read((uint16_t)(a - 0xA0000u));
    return MEM[a];
}
static inline void wbl(uint32_t a, uint8_t v) {
    if (a - 0xA0000u < 0x10000u) { vga_mem_write((uint16_t)(a - 0xA0000u), v); return; }
    MEM[a] = v;
}
static inline uint8_t rb(uint16_t seg, uint16_t off) { return rbl(lin(seg, off)); }
static inline uint16_t rw(uint16_t seg, uint16_t off) {
    return (uint16_t)(rbl(lin(seg, off)) | rbl(lin(seg, (uint16_t)(off + 1))) << 8);
}
static inline void wb(uint16_t seg, uint16_t off, uint8_t v) { wbl(lin(seg, off), v); }
static inline void ww(uint16_t seg, uint16_t off, uint16_t v) {
    wbl(lin(seg, off), (uint8_t)v);
    wbl(lin(seg, (uint16_t)(off + 1)), (uint8_t)(v >> 8));
}

/* DGROUP shorthands: DB(0x12d) is the byte at DGROUP:012d, DW(0x2c9a) the word. */
#define DB(o) MEM[lin(SEG_DG, (uint16_t)(o))]
static inline uint16_t dgw(uint16_t o) { return rw(SEG_DG, o); }
static inline void dgw_set(uint16_t o, uint16_t v) { ww(SEG_DG, o, v); }
static inline int16_t dgs(uint16_t o) { return (int16_t)rw(SEG_DG, o); }

/* Load YOGHO.EXE's image at SEG_EXE with its segment relocations applied (as DOS would). */
int mem_load_exe(const char *path);

#endif
