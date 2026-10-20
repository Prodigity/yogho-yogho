#include "cpu.h"
#include "mem.h"
#include "log.h"

Regs R;

#define MAX_EXE 512
#define MAX_OVL 256
static struct { uint32_t lin; code_fn f; } exe_tab[MAX_EXE];
static int exe_n;
static struct { uint16_t off; code_fn f; } ovl_tab[12][MAX_OVL];
static int ovl_n[12];
static int ovl_live = -1;

void farcall_register_exe(uint16_t ghidra_seg, uint16_t off, code_fn f) {
    if (exe_n < MAX_EXE) { exe_tab[exe_n].lin = lin(SEG_G(ghidra_seg), off); exe_tab[exe_n].f = f; exe_n++; }
}
void farcall_register_overlay(int tag, uint16_t off, code_fn f) {
    if (tag >= 0 && tag < 12 && ovl_n[tag] < MAX_OVL) { ovl_tab[tag][ovl_n[tag]].off = off; ovl_tab[tag][ovl_n[tag]].f = f; ovl_n[tag]++; }
}
void farcall_set_overlay(int level_index) { ovl_live = level_index; }

void farcall(uint16_t seg, uint16_t off) {
    uint32_t a = lin(seg, off);
    uint16_t ovl = dgw(0x2d64);
    if (ovl_live >= 0 && seg == ovl) {
        for (int i = 0; i < ovl_n[ovl_live]; i++)
            if (ovl_tab[ovl_live][i].off == off) { ovl_tab[ovl_live][i].f(); return; }
    }
    for (int i = 0; i < exe_n; i++) if (exe_tab[i].lin == a) { exe_tab[i].f(); return; }
    if (cspr_is_sprite(seg, off)) { cspr_exec(seg, off); return; }
    log_gap("farcall to unported code %04X:%04X (lin %05X, overlay seg %04X)", seg, off, (unsigned)a, ovl);
}
