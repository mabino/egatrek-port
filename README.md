# EGA Trek port

A native port of *EGA Trek* (1988, v1.0, Nels Anderson) for **macOS (Apple Silicon)** and modern web browsers via **WebAssembly**.

The port statically decompiles and recompiles the original 16-bit real-mode MS-DOS binary (`TREK.EXE`, Turbo Pascal 4.0) into portable C++20, running inside a lightweight PC environment that emulates the 1 MB real-mode address space, EGA planar graphics, PC speaker audio, PIT timer, and DOS/BIOS interrupts.

This repository contains **only clean-room engine code, tools, and build infrastructure**. No copyrighted game files or derived generated code are committed to the repository. The recompiler processes your legally owned copy of `TREK.EXE` at build time.

> [!NOTE]
> **Disclaimer**: *Star Trek* is a registered trademark of Paramount Global / CBS Studios. This project is an independent, non-commercial educational and digital preservation research project. It is not affiliated with, endorsed by, or sponsored by Paramount, CBS, or Nels Anderson. No copyrighted game binaries, media assets, or decompiled code are distributed in this repository.

---

## Architecture & Design

```
+------------------------------------------------------------------------+
|                          Host Presentation Layer                       |
|   macOS Native: SDL3 + Metal/Cocoa    |    Web: SDL3 + WebGL + IDBFS   |
+------------------------------------------------------------------------+
                                    |
+------------------------------------------------------------------------+
|                             engine (C++20)                             |
|                                                                        |
|  +--------------------+  +--------------------+  +------------------+  |
|  |    dos::Machine    |  |     dos::Ega       |  |   dos::Speaker   |  |
|  | 1 MB real-mode mem |  | 640x350 planar mem |  | PIT ch2 square-  |  |
|  | Register file & CFG|  | 16-color palette   |  | wave audio synth |  |
|  | Fast block dispatch|  | 4:3 letterboxing   |  | Cooperative yield|  |
|  +--------------------+  +--------------------+  +------------------+  |
|            |                                                           |
|  +--------------------+  +--------------------+                        |
|  | recompiled segments|  |    Interpreter     |                        |
|  | (16 C++ switch-    |  |  fallback for self-|                        |
|  |  dispatched units) |  |  modifying code    |                        |
|  +--------------------+  +--------------------+                        |
+------------------------------------------------------------------------+
                                    ^
                                    | static translation
+------------------------------------------------------------------------+
|                        tools/trekre (Python 3.12)                      |
|   8086 decoder  *  CFG discovery  *  no-return & relocation hints       |
+------------------------------------------------------------------------+
                                    ^
                                    | binary analysis
                            Original TREK.EXE
```

### Static Recompiler (`tools/trekre`)
The recompiler parses the 16-bit MZ executable, resolves relocations, recovers Turbo Pascal 4 runtime units, and performs iterative control-flow discovery. It handles:
- **Relocated code segments**: Recognizes Borland `Graph` driver and font memory copies and relocates references accordingly.
- **No-return runtime procedures**: Avoids decoding invalid dead paths following `Halt` or runtime error handlers.
- **Segment translation**: Emits C++ switch-dispatched blocks (`build/generated/seg_XXXX.cpp`) compiled as a static library.

### Emulation Layer (`engine/src/dos`)
- **`trek::dos::Machine`**: Manages the 1 MB real-mode memory map, CPU register state, flags, segment registers, and BIOS/DOS interrupts (`INT 10h`, `INT 16h`, `INT 1Ah`, `INT 21h`).
- **Dynamic & Self-Modifying Code Fallback**: An integrated 8086 interpreter seamlessly handles self-modifying instructions (e.g. Borland runtime self-patching at `0x0AAB:0x0010` and `0x1521:0x0099`).
- **EGA Display (`dos::Ega`)**: Emulates the 640×350 16-color EGA planar layout, CRT controller registers, and text-mode blink counter. Preserves authentic CRT 4:3 proportions via SDL logical presentation letterboxing.
- **Audio & PIT Timing (`dos::Speaker`)**: Synthesizes 44.1 kHz square-wave audio from 8253 PIT channel 2. Cooperative `Crt.Delay` yielding gives authentic sound duration and synchronized visual effects across presentation frames while maintaining smooth 60 FPS presentation.

---

## Prerequisites & Environment Setup

The repository uses [mise](https://mise.jdx.dev/) for pinned development environments and [Docker](https://www.docker.com/) for containerized builds.

### 1. Install mise Tools
```zsh
mise trust
mise install
```
This provisions Python 3.12, `uv`, CMake, and Ninja.

### 2. Container Engine
Docker (or Colima on macOS Apple Silicon) is required for containerized Linux CI runs and WebAssembly compilation:
```zsh
# If using Colima on Apple Silicon:
colima start --cpu 4 --memory 8
```

---

## Usage Guide

### 1. Game Binary Setup
Place your original `TREK.EXE` (along with `TREK.DOC` and `TREK.REF`) in the directory directly above this repository:
```
EGATREK10/
  ├── TREK.EXE
  ├── TREK.DOC
  ├── TREK.REF
  └── egatrek-port/      <-- this repository
```
*(You can also set the `TREK_DATA` environment variable to point to any directory containing `TREK.EXE`.)*

### 2. Static Recompilation
Generate the portable C++ code from `TREK.EXE`:
```zsh
./scripts/recomp.zsh
```
This analyzes the binary and writes the generated translation units to `build/generated/`.

### 3. Native macOS Build & Run
Build the native Apple Silicon app:
```zsh
./scripts/build.zsh
```
Run the game natively:
```zsh
./scripts/run.zsh
```

### 4. macOS Application Bundle Packaging
Package a standalone, codesigned macOS application bundle (`EGA Trek.app`) with custom Tahoe squircle icon:
```zsh
./scripts/bundle.zsh
```
Output:
- `dist/EGA Trek.app`
- `dist/EGA Trek.zip`

To notarize and staple the bundle for distribution (requires Apple Developer credentials):
```zsh
./scripts/notarize.zsh
```

### 5. WebAssembly Browser Build
Build the WebAssembly version in the Emscripten container and serve it locally:
```zsh
./scripts/build-wasm.zsh --serve
```
Open `http://localhost:8080/egatrek.html` in any modern web browser.
- **Responsive Viewport**: Letterboxed 4:3 presentation that automatically adapts to any window resolution or Retina display without edge clipping.
- **Persistent Saves**: Saves written in-game are persisted in the browser via IndexedDB (`IDBFS`).

---

## In-Game Controls & Features

| Key / Command | Action |
|---|---|
| **Arrow Keys / Keypad** | Navigate ship and menus |
| **Space / Enter** | Advance text, accept command |
| **Shift-F1** | **Boss Key**: Instantly flips to a fake DOS prompt screen (`dir/w`) |
| **`SAVE`** | Save current game state to `TREK.SAV` (persisted in macOS Application Support or browser IndexedDB) |
| **`RESTORE`** | Restore saved game |
| **`Q` / Cmd-Q** | Quit game |

---

## Development Guide

### Repository Layout

```
engine/                  C++20 game engine and platform shell
  ├── CMakeLists.txt     Build configuration for native and Emscripten targets
  ├── resources/         macOS Info.plist and AppIcon.icns
  ├── src/
  │   ├── app/           Application shell (main.cpp, host key mapping)
  │   ├── core/          Byte reading, MZ image parsing, asset path resolution
  │   ├── dos/           PC machine, 8086 interpreter, EGA, PIT speaker, ROM font
  │   └── gfx/           EGA surface and Borland BGI stroke font renderer
  ├── web/               HTML/CSS shell page for WebAssembly build
  └── tests/             C++ unit tests (doctest), lockstep, and headless test runners
tools/
  ├── trekre/            Python 3.12 reverse-engineering & recompiler toolsuite
  │   └── trekre/        Decoder, CFG generator, C++ code emitter, BGI decoders
  └── ghidra/            Headless Ghidra analysis scripts (PyGhidra)
docker/
  ├── ci.Dockerfile      Linux CI environment (Clang, SDL3, Unicorn, doctest)
  ├── wasm.Dockerfile    Emscripten WebAssembly build environment
  └── analysis.Dockerfile Ghidra 12 analysis container for Apple Silicon
scripts/                 macOS zsh build, test, and lifecycle scripts
docs/
  └── findings.md        Technical analysis of original 1988 binary structure
```

### Running Tests

#### 1. Native macOS Test Suite
Runs all 27 Python recompiler tests and all 5 C++ test suites on macOS:
```zsh
./scripts/test.zsh
```

Test suites include:
- `unit`: Core byte reader, MZ loader, font rendering, and machine execution tests.
- `instruction_corpus`: Compares instruction decoder and flag calculation against the Unicorn x86 emulator across 7,500+ cases.
- `game_headless`: Multi-thousand-frame gameplay session exercising navigation, weapons, computer commands, boss screen, and game saving.
- `game_lockstep`: Runs recompiled code and interpreted code in lockstep block by block, verifying state parity.
- `app_smoke`: Verification of SDL application startup, frame ticking, and exit.

#### 2. Linux Container CI Test Suite
Tests run inside the containerized Linux environment matching GitHub Actions CI:
```zsh
./scripts/test.zsh --docker
```

#### 3. WebAssembly Node.js Test Suite
Builds the WebAssembly binaries and runs the test suite under Node.js:
```zsh
./scripts/build-wasm.zsh
```

### Reverse Engineering & Analysis Tools

- **Binary Inventory**: Dumps segment maps, extracted strings, and BGI driver/font assets into `out/`:
  ```zsh
  ./scripts/inventory.zsh
  ```
- **Headless Ghidra Analysis**: Runs automated Ghidra decompilation inside the Docker analysis container:
  ```zsh
  ./scripts/analyze.zsh
  ```
- **Generate App Icon**: Regenerates the continuous superellipse Tahoe squircle icon:
  ```zsh
  python3 scripts/generate_icon.py
  ```

---

## Clean-Room & Security Policy

- Original binary files (`*.EXE`, `*.DOC`, `*.REF`, `*.SAV`, `*.CHR`, `*.BGI`) are ignored by git and never committed.
- Recompiled C++ translation units (`build/generated/*.cpp`) are generated locally from the user's binary and never committed.
- All test suites use mock byte sequences or synthetic MZ executables to enable verification in clean CI environments without external game assets.
