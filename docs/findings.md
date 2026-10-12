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

- **Reals.** Game logic does arithmetic in Turbo Pascal's 48-bit `Real`. To reproduce the original
  exactly (scores, hit damage, random sequences fed through `Random`), the port needs a bit-exact
  `Real48` type rather than `double`.
- **Strings.** Turbo Pascal passes string literals as `MOV DI,offset; PUSH CS; PUSH DI`. Ghidra treats
  the offset as a plain immediate, so `analyze.py` adds the data references itself (579 linked).
- **Assets.** The only assets are the Triplex font and the BGI driver inside the executable. The port
  reads the font from the user's `TREK.EXE`. The driver is replaced by `EgaSurface`. The BIOS 8×8 text
  font, which BGI's `DefaultFont` uses, is not in the executable.
- **Display.** 640×350 in 16 colours, shown at 4:3 (non-square pixels).
- **Saves.** `TREK.SAV` in the game folder. *Format still to be decoded.*
