# Yogho Yogho — rewritten engine

**Play in the browser: https://prodigity.github.io/yogho-yogho/**

The DOS game *Yogho Yogho* (1995, Euphoria) running on an engine rewritten from scratch in C,
for desktop (SDL2) and the web (WebAssembly). Unofficial fan preservation project, not
affiliated with the original makers or brand owners.

## How it was made

- The original program (`YOGHO.EXE`, 16-bit real-mode Borland C++ with hand-written assembly) and
  its data file were reverse-engineered statically (Ghidra + capstone), including the 12 level
  overlays (code that ships inside `YOGHODAT.DAT`), the AdLib S3M music driver and the end-credits
  program.
- Every routine was ported to C, keeping the original's data layout (a 1 MB real-mode memory model
  with the exe image and DGROUP at the original segments), its timer-interrupt frame scheduler and
  a model of the VGA hardware (Mode X planes, CRTC/attribute/DAC registers), so the original's
  quirks are kept.
- Faithfulness is checked against the original running under the DOSBox-X debugger: snapshots of
  the original's memory are replayed through the port with the same input, and the game state is
  compared byte for byte (`--snapplay`), plus the music driver state (`--opltrace`).

The engine reads the original game files at runtime: `YOGHO.EXE` (data tables only — it is never
executed) and `YOGHODAT.DAT`. They are not part of the source tree.

## Build

Desktop (Windows, MSVC Build Tools 2022 + SDL2 VC dev package): `build.bat` → `build\yogho.exe`;
run it next to `YOGHO.EXE`, `YOGHODAT.DAT`, `YOGHOHI.DAT` (or pass `--exe`, `--data`).
Web (emsdk 3.1.64): `build-web.bat <folder with the game files>` → `build-web\`.

Developer tools: `--script file [--fast]` (scripted input, dumps), `--snapplay` / `--snaprun`
(replay a DOSBox snapshot), `--opltrace` (AdLib register stream).

Conventions for the code: `PORTING.md`. Third-party: Nuked-OPL3 (LGPL-2.1, `third_party/`),
SDL2 (zlib).
