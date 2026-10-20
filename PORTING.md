# Porting conventions (read before writing any code)

This is a **from-scratch rewrite in C** of the DOS game Yogho Yogho, faithful to the original in
gameplay. The specification is the teardown in `..\extracted\teardown\` (00-BRIEFING first).
Every ported routine must be traceable to the original: cite the Ghidra address in a comment at
the top of the function and at non-obvious lines. **No tuning by eye, no guessing**: if the spec
is unclear, read the instructions (`..\extracted\ghidra\disasm_fixed.txt`, overlay listings in
`..\extracted\ovl_dis\`, capstone via `..\scripts\dis16.py` / `exe_dis.py` / `ovl_dis.py`), and if
still unclear, implement the most literal reading and list it under "open questions" in your report.

## Memory model (`src/mem.h`)
- All game state lives in `MEM` (1 MB real-mode memory) at the **original addresses**: the EXE
  image (with relocations) is loaded at runtime segment `0x0824` (Ghidra `1000`), DGROUP is at
  `0x226A` (Ghidra `2a46`). Runtime segment = Ghidra segment − 0x7DC; use `SEG_G(0x160a)` etc.
- DGROUP access: `DB(off)` byte lvalue, `dgw(off)` / `dgw_set(off,v)` words, `dgs(off)` signed.
  Other memory: `rb/rw/wb/ww(seg, off)` (these route A000–AFFF to the VGA model). `P(seg,off)` gives a
  raw pointer — never for VRAM.
- Initialised tables (strings, level descriptors, frame tables, collision shapes, CS-resident
  tables…) are **read from MEM** (they come from the user's YOGHO.EXE), never retyped.
- Self-modified CS variables (e.g. CS:1011, CS:101f, CS:3b7a) are kept as words in MEM at
  `SEG_EXE:offset` exactly like the original, so dumps line up.
- Loaded data (DAT entries) goes where the original puts it (segments held in DGROUP like
  `[0x13fe]`, `[0x1402]`, `[0x2cda]`…), via `dat_load(idx, off, seg)`.
- Integer semantics: use `uint16_t`/`int16_t`/`uint8_t`/`int8_t` exactly as the asm does (wrap,
  signed vs unsigned compares, CBW, carries via explicit 32-bit adds). Byte vs word stores matter.

## Calling conventions (`src/cpu.h`, `src/game.h`)
- **REG** routines (asm with register args — engine helpers, all 26 API slots, every overlay
  handler): `void name(void)`, arguments and results in the global `Regs R` (`R.ax`, `R.bx`, …,
  `R.es`, `R.ds`, flags `R.cf/R.zf/R.sf` where the caller tests them). Preserve/clobber registers
  as the original does only where a caller depends on it (document it).
- **C** routines (Borland cdecl, mostly segment 160a; blitters with stack args): normal C params
  in original order; far pointers as `(off, seg)`; return AX (32-bit results also put DX in `R.dx`).
- `game.h` is the contract: it declares every cross-module routine with its owner. **Do not edit
  `game.h`.** If you need to export more, declare it in your own header `src/<module>_ext.h` and
  mention it in your report; the lead merges.
- Dynamic far calls through data (object handlers `CALLF [slot+0]`, overlay entry `CALLF [2d66]`,
  compiled sprites) go through `farcall(seg, off)` (`src/farcall.c`). Overlay modules register
  handlers per level with `farcall_register_overlay(level_index, offset, fn)` inside
  `ovl_register_all()`-reachable init functions; engine routines reachable through data pointers
  register with `farcall_register_exe(ghidra_seg, off, fn)`.
- Near calls/jumps between ported routines are plain C calls; a tail `jmp stub` to an API slot is a
  call followed by `return`.

## Hardware
- VGA: port I/O with `io_out(port, v)` / `io_outw(port, ax)` (= `OUT DX,AX`) / `io_in(port)`;
  VRAM by `wb/rb(0xA000+.., off)`. The model (`src/vga.c`) implements map mask, read map,
  write modes 0/1 + latches, CRTC start/offset/line compare, pel panning, DAC. Port the asm's
  writes literally (plane order, map-mask words) — that keeps output pixel-identical.
- Retrace polling (`IN 3DAh`) always reports "in retrace". Any original busy-wait on a counter
  that the ISR changes becomes a loop with `plat_yield()` (see `wait_ticks`, `spin_while_*` in
  `src/isr.c`). Never busy-wait without yielding.
- AdLib: done (`src/adlib.c`). Timer ISR / keyboard / waits: done (`src/isr.c`).
- Mouse (int 33h) and joystick: the flow module owns them (platform gives `plat_mouse()`).

## Build / check
- `./check.sh src/yourfile.c ...` compiles your files only (MSVC, C11) — use it constantly; it
  must be warning-clean for your files. `./b.sh` builds the whole program (will only link once
  all modules exist).
- Style: C11, 4-space indent, match `src/adlib.c` / `src/isr.c`. Comment density like those files.
- Do not run the original game or DOSBox. Do not touch files outside your module list.
