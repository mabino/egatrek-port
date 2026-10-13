# EGA Trek port

A personal-use port of *EGA Trek* (1988, v1.0, Nels Anderson): decompile the original 16-bit DOS
program to portable C++ and run it natively on **macOS (Apple Silicon)** first, then in the browser via
**WebAssembly**.

This repository contains **only original code and tooling**. The original `TREK.EXE`, `TREK.DOC`,
`TREK.REF` and anything derived from them (Ghidra projects, decompiler output) stay outside git: in the
folder above this repo and in the ignored `out/` and `ghidra-projects/` folders. The port reads game
assets (such as the Borland Triplex font linked into `TREK.EXE`) from your copy at run time.

## Layout

```
engine/          C++20 port (SDL3 app shell, EGA framebuffer, BGI stroke fonts, doctest tests)
tools/trekre/    Python RE tools: MZ segment map, Turbo Pascal strings, BGI asset decoding (pytest)
tools/ghidra/    Headless Ghidra analysis (PyGhidra), runs in the trek-analysis container
docker/          analysis.Dockerfile (Ghidra 12, arm64 natives), ci.Dockerfile (Linux), wasm.Dockerfile
scripts/         zsh: build, build-wasm, run, test, analyze, inventory, bundle, notarize
docs/findings.md What is known about the original binary
```

## Setup

```zsh
mise trust && mise install                 # Python, uv, CMake, Ninja
```

`TREK_DATA` defaults to the folder above this repo (see `mise.toml`), which holds `TREK.EXE`.

## Architecture

The port uses a hybrid ahead-of-time (AOT) static recompiler and 1 MB real-mode PC emulation layer:

- **`trekre recomp`**: 8086 decoder and control-flow analyzer (seeds, no-return calls, Graph code relocation). Converts basic blocks into 16 switch-dispatched C++ segment translation units (`build/generated/seg_XXXX.cpp`) derived from the user's `TREK.EXE` (never committed).
- **`trek::dos::Machine`**: 1 MB real-mode 8086 PC runtime with fast block dispatcher, CPU register file with unpacked flags, and an instruction interpreter fallback for dynamic and self-modifying code sites (`0x0AAB:0x0010` and `0x1521:0x0099`).
- **Display**: 640×350 EGA planar memory and register emulation with 16-color palette, blinking text, and 4:3 letterboxed presentation.
- **Audio & Timing**: PIT channel 2 square-wave audio and PC speaker emulation via SDL3. Cooperative `Crt.Delay` yielding maintains 60 FPS presentation, smooth visuals, and synchronized sound effects. Title theme fanfare starts immediately via rest duration tuning.
- **Packaging**: Self-contained native macOS app bundle (`EGA Trek.app`) with custom Tahoe-compatible squircle icon and notarization scripts. Browser build with IDBFS persistence via WebAssembly and Emscripten.

## Everyday commands

| Command | What it does |
|---|---|
| `scripts/build.zsh` | Build the native Apple Silicon app (`build/macos/engine/EGA Trek.app`) |
| `scripts/run.zsh [--screenshot f.bmp]` | Build if needed, then run natively |
| `scripts/bundle.zsh` | Package and codesign `dist/EGA Trek.app` and `dist/EGA Trek.zip` |
| `scripts/build-wasm.zsh [--serve]` | Build the wasm port in Docker and run node tests |
| `scripts/test.zsh` | Run all 27 Python + 5 C++ tests on macOS |
| `scripts/test.zsh --docker` | Run the full test suite in the Linux CI container |
| `scripts/inventory.zsh` | Segment map, strings, and assets into `out/` |
| `scripts/notarize.zsh` | Notarize and staple the macOS app bundle |

## Verification

- **Instruction Corpus (`instruction_corpus`)**: Comprehensive test suite checking CPU instruction decoding and flag semantics against the Unicorn CPU emulator.
- **Lockstep Checker (`game_lockstep`)**: Scripted play session executed in strict lockstep, comparing recompiled code state against the reference interpreter block by block.
- **Headless Runner (`game_headless`)**: Automated multi-thousand-frame gameplay session exercising navigation, weapons, computer commands, boss screen, and game saving.

## Status

1. ✅ Tooling, containers, and Linux CI (x86-64/arm64 + wasm); segment map; Ghidra RE integration
2. ✅ Static 8086 recompiler (`trekre recomp`) and C++ code generator with interpreter fallback
3. ✅ Native Apple Silicon app: SDL3 window, keyboard mapping, PC speaker audio, `TREK.SAV` in Application Support
4. ✅ Sound and timing: Cooperative delay yielding, PIT channel 2 square-wave sound, title fanfare startup
5. ✅ macOS App Bundle: Standalone application with squircle icon, codesigning, and zip packaging
6. ✅ WebAssembly: Browser port with IDBFS save persistence, tested in Node.js

