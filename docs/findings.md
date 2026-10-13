# Reverse-engineering findings

Facts established about `TREK.EXE` (EGA Trek 1.0, 1988-05-09, shareware, registration #0011).
Regenerate the raw data with `scripts/inventory.zsh` and `scripts/analyze.zsh`; it lands in `out/`.

## Binary

| Property | Value |
|---|---|
| Format | MS-DOS MZ, 147,120 bytes, no overlay, not packed |
| sha256 | `1bc83431eeb9fd01…` (full value in `out/inventory.json`) |
| Load image | 132,096 bytes, 3,747 relocations |
| Entry | `0000:0EB0` (main program body), stack `210E:4000` |
| Compiler | Turbo Pascal 4.0: length-prefixed strings inline in code, BGI 1.00 (Sept 1987), `Runtime error` text, Graph unit error strings |

Turbo Pascal gives each unit its own code segment, and every inter-unit far call is relocated, so the
set of relocated segment words is an exact segment map (`trekre inventory`).

## Segment map

Load order is main program, then units, then System, then the data segment.
Roles in *italics* are inferred from decompiled code and still need confirmation.

| Seg | Bytes | Role |
|---|---|---|
| `0000` | 6,176 | Main program: quadrant setup (Klingons, bases, stars, planets, Romulans), enemy fire |
| `0182` | 30,144 | Game unit: *weapons, navigation, orbit/mining, shields, energy, repair, chart, scans* |
| `08DE` | 7,216 | `TRIP.CHR` Triplex stroke font, linked raw with BINOBJ (95 glyphs from space, top 21, descent 7) |
| `0AA1` | 5,152 | `EGAVGA.BGI` driver, linked raw with BINOBJ |
| `0BE3` | 17,856 | Game unit: *title screen and briefings (mostly text)* |
| `103F` | 20,000 | Game unit: *command console (status panel, messages, alert), end-of-game report and scoring* |
| `1521` | 208 | Dos unit (`MsDos`, `GetIntVec`/`SetIntVec` via INT 21h functions 35h/25h) |
| `152E` | 8,176 | Game unit: *decorative "computer" panels (SUBSPACE COM NET, POWER DISTRIB, …)* |
| `172D` | 8,496 | Game unit, no strings: *likely sound/graphics helpers* (32 calls from game code) |
| `1940` | 352 | Author helper: draws text in a 5×5 bitmap mini-font with `PutPixel` |
| `1956` | 352 | Author helper: number formatting (`Str` with padding) |
| `196C` | 304 | *Text-file device driver hooks* |
| `197F` | 1,632 | *Crt unit* (`Delay`, keyboard) |
| `19E5` | 672 | Game unit: new-game initialisation (8×8 galaxy, `Random(9)` per quadrant) |
| `1A0F` | 14,352 | Graph unit (BGI) |
| `1D90` | 7,856 | System unit |
| `1F7B` | 3,152 | Data segment (typed constants; globals follow in BSS) |

## Static Recompiler Architecture

The port uses a hybrid ahead-of-time (AOT) static recompiler (`trekre recomp`) and an emulated 1 MB real-mode 8086 CPU (`trek::dos::Machine`):

1. **Decoder & CFG Discovery (`trekre.cfg`)**:
   - Starting from `0000:0EB0`, the recompiler decodes 8086 instructions using recursive descent.
   - Code is partitioned into basic blocks bounded by jumps, calls, returns, and interrupt instructions.
   - Blocks are grouped by segment into 16 C++ translation units (`seg_XXXX.cpp`), each compiled as a single switch-dispatch loop.

2. **Control-Flow Hints (`trekre.hints`)**:
   - **No-return procedures (`NORETURN`)**: `1D90:01E9`, `1D90:01F0`, and `1D90:01F3` are Turbo Pascal `RunError` and `Halt` routines. Marking them non-returning prevents recursive-descent disassembly from mistaking trailing data bytes for executable code.
   - **Driver seeds (`seeds()`)**: `EGAVGA.BGI` code begins at `0x0AAB:0x00A0`; the BGI interface table at `0x0AAB:0x0016` contains 29 near pointers which are seeded into the CFG.
   - **Graph driver relocation (`MOVED`)**: During BGI startup, `Graph.InitGraph` copies its internal driver interface from `1A0F:2106` down to `1C1F:0000` (length `0x1703` bytes) and executes it with `CS=1C1F`. The recompiler decodes this code in its relocated position.
   - **Runtime-discovered indirect entries (`FOUND_AT_RUN_TIME`)**: Addresses reached via function pointers computed at run time (e.g. text file driver hooks at `196C:0038`, `196C:0078`, `196C:00B8`, and Graph internal helpers in `1C1F`).

3. **Self-Modifying / Dynamic Code Sites (`DYNAMIC`)**:
   Two instructions in `TREK.EXE` are patched at run time:
   - `0x0AAB:0x0010`: The BGI driver's emulation entry point, initially a `RET` (`0xC3`), is dynamically overwritten by `Graph` with a far jump (`0xEA ...`).
   - `0x1521:0x0099`: Turbo Pascal's `Dos.Intr` dispatcher. It contains an `INT 00` (`CD 00`) opcode whose second byte is dynamically overwritten in memory with the desired interrupt number immediately before execution.
   The recompiler isolates these instruction boundaries and hands them off to `Machine::step()` to interpret in RAM.

4. **Cooperative Delay & Audio Emulation**:
   - **Turbo Pascal `Crt.Delay` Hook**: `197F:02E9` is an uncalibrated CPU busy loop. In native execution, `Machine::run` intercepts `pc == 0x29AD9` (`handle_delay()`), recording `delay_end_ms = now_ms + ms` and yielding back to `SDL_AppIterate`. This lets the host maintain 60 FPS presentation, render in-flight phaser and torpedo visuals, and keep audio streaming across frames.
   - **PC Speaker & PIT Channels**: Channel 2 divisor writes to port `42h` and speaker gate toggles on port `61h` record timestamped square-wave events in `dos::Speaker`. The PIT counter read toggle (`pit_read_hi_`) is separate from divisor writes (`pit_lo_written_`) to avoid state corruption.
   - **Title Music Optimization**: The Star Trek theme table at `1F7B:06A9` contains `(36, 0) (4, 0) (4, 74) ...`. The initial two notes (duration 36 and 4) were rests totaling 80 timer ticks (~4.4 seconds of silence) to mask 1988 EGA vector drawing latency. In `Machine::reset()`, both rests are shortened to 1 tick (~200 ms total), so the theme fanfare begins promptly when the title screen appears.

## Game state (data segment `1F7B`, tentative)

- `0x13B4`: galaxy as `array[1..8,1..8] of Integer`, encoded *Klingons×100 + bases×10 + stars*
  (same encoding the long-range scan prints).
- `0x1470`: current quadrant as `array[0..9,0..9] of Char` with a border; `.` empty, `E` Enterprise,
  `K` Klingon, `C` commander, `S` Klingon starbase, `B` StarBase, `P` planet, `*`/`N` star/nova, `R` Romulan.
- `0x1440`: enemies in quadrant, 6 bytes each (sector row, column, energy); Klingons start with
  `skill*10+250` energy, commanders `skill*25+450`, starbases and Romulans 900.
- `0x0D33`/`0x0D35`: current quadrant; `0x0D37`/`0x0D39`: sector.

## Runtime calls most used by game code

| Address | Calls | Identification |
|---|---|---|
| `1A0F:1E0C` | 319 | Graph `OutTextXY(x, y, s)` |
| `1D90:0720` | 192 | System string assign/concat helper |
| `1A0F:1D35` | 179 | Graph, *`SetColor`* |
| `1D90:0B4D`…`0B7F` | ~490 | System 6-byte `Real` arithmetic (operands in DX:BX:AX, opaque to the decompiler) |
| `1D90:02AD` | 92 | Stack check (`{$S+}` prologue) |
| `1D90:100C` | 47 | `Random(n)`; `1D90:1092` is `Randomize` |
| `1A0F:1ED3` | 15 | Graph `PutPixel` |

## Porting notes

- **Reals.** Game logic does arithmetic in Turbo Pascal's 48-bit `Real`. The recompiled code evaluates
  real operations directly via the original Turbo Pascal runtime in `seg_1d90`.
- **Strings.** Turbo Pascal passes string literals as `MOV DI,offset; PUSH CS; PUSH DI`.
- **Display.** 640×350 in 16 colours, presented at 4:3 with integer or pixel-art scaling.
- **Saves.** `TREK.SAV` in the host save directory (Application Support on macOS, IDBFS in browser).
  Contains the 14,400-byte data segment image preserving full galaxy and vessel state.

