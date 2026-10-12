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

## Everyday commands

| Command | What it does |
|---|---|
| `scripts/build.zsh` | Build the arm64 app (`build/macos/engine/EGA Trek.app`) |
| `scripts/run.zsh [--screenshot f.bmp]` | Build if needed, then run against `TREK_DATA` |
| `scripts/build-wasm.zsh [--serve]` | Build the wasm port in a container and run its tests under node |
| `scripts/test.zsh` | Python + C++ tests on macOS |
| `scripts/test.zsh --docker` | The same tests in the Linux CI container |
| `scripts/inventory.zsh` | Segment map, strings and the decoded Triplex font into `out/` |
| `scripts/analyze.zsh [--decompile] [--project NAME]` | Headless Ghidra analysis into `out/ghidra/` |
| `scripts/bundle.zsh` / `scripts/notarize.zsh` | Sign, zip, and optionally notarize the app |

Tests that need the real `TREK.EXE` are marked `gamedata` (Python) or return early (C++) when
`TREK_DATA` is absent, as in CI.

## Roadmap

1. ✅ Tooling, containers, CI (Linux x86-64/arm64 + wasm); segment map; Ghidra analysis with
   Pascal string linking; EGA framebuffer and BGI stroke-font rendering from the original font data
2. Name the Turbo Pascal 4 runtime: System (6-byte Real arithmetic, strings, `Random`), Graph (BGI
   calls), Crt, Dos. Implement them in C++ with bit-exact `Real48` and TP's `Random` generator
3. Decompile the game units (galaxy, combat, navigation, console, briefing, boss screen) to C++,
   verified by differential tests against the original x86 code run in an emulator
4. Native macOS: input (function keys, arrows, Shift-F1), PC-speaker sound via SDL audio, `TREK.SAV`
   in Application Support
5. WebAssembly: `TREK.EXE` picked from the user's disk into OPFS, saves kept in the browser
