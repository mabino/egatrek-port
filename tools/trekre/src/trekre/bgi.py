"""Borland BGI assets linked into the executable with BINOBJ.

Stroke fonts (.CHR) start with "PK\\x08\\x08BGI " and drivers (.BGI) with
"pk\\x08\\x08BGI ". Both carry a text description ending in 0x1A, then a
header size word, a four-letter name, the data size and a version.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass

FONT_MAGIC = b"PK\x08\x08BGI "
DRIVER_MAGIC = b"pk\x08\x08BGI "

END, SCAN, MOVE, DRAW = 0, 1, 2, 3


class BGIError(ValueError):
    pass


@dataclass(frozen=True)
class Asset:
    kind: str          # "font" or "driver"
    offset: int        # within the searched buffer
    name: str          # e.g. "TRIP", "EGAVGA"
    description: str
    size: int          # header + data


@dataclass
class StrokeFont:
    name: str
    first_char: int
    top: int           # origin to ascender (positive, y up)
    baseline: int
    bottom: int        # origin to descender (negative)
    widths: list[int]
    # Per character: list of (op, x, y) with op MOVE or DRAW, y up.
    strokes: list[list[tuple[int, int, int]]]

    def glyph(self, ch: str) -> list[tuple[int, int, int]]:
        i = ord(ch) - self.first_char
        return self.strokes[i] if 0 <= i < len(self.strokes) else []

    def width(self, ch: str) -> int:
        i = ord(ch) - self.first_char
        return self.widths[i] if 0 <= i < len(self.widths) else 0


def _header(data: bytes, offset: int, kind: str = "font") -> tuple[str, str, int, int]:
    """(description, name, header size, data size) of the asset at offset.

    After the 0x1A a font has: header size, 4-char name, data size, version.
    A driver has: header size, driver number, data size, version; its name is
    only in the description, e.g. "(EGA/VGA)" for EGAVGA.BGI.
    """
    end = data.find(b"\x1a", offset)
    if end < 0 or end - offset > 0x80:
        raise BGIError("no 0x1A after BGI description")
    text = data[offset + 4:end].split(b"\x00")[0].decode("latin1")
    desc = text.splitlines()[0].strip() if text.strip() else ""
    if end + 9 > len(data):
        raise BGIError("truncated BGI header")
    header_size, = struct.unpack_from("<H", data, end + 1)
    if kind == "font":
        name = data[end + 3:end + 7].split(b"\x00")[0].decode("latin1").strip()
    else:
        m = re.search(r"\(([^)]*)\)", desc)
        name = m.group(1).replace("/", "") if m else ""
    size, = struct.unpack_from("<H", data, end + 5 if kind == "driver" else end + 7)
    return desc, name, header_size, size


def scan(data: bytes) -> list[Asset]:
    """Every BGI font and driver embedded in data."""
    found = []
    for kind, magic in (("font", FONT_MAGIC), ("driver", DRIVER_MAGIC)):
        pos = data.find(magic)
        while pos >= 0:
            try:
                desc, name, hs, size = _header(data, pos, kind)
            except BGIError:
                pos = data.find(magic, pos + 1)
                continue
            found.append(Asset(kind, pos, name, desc, hs + size))
            pos = data.find(magic, pos + 1)
    return sorted(found, key=lambda a: a.offset)


def _s7(v: int) -> int:
    v &= 0x7F
    return v - 0x80 if v & 0x40 else v


def parse_font(data: bytes, offset: int = 0) -> StrokeFont:
    if data[offset:offset + len(FONT_MAGIC)] != FONT_MAGIC:
        raise BGIError("not a BGI stroke font")
    _, name, hs, _ = _header(data, offset)
    h = offset + hs
    if data[h:h + 1] != b"+":
        raise BGIError("stroke font header signature missing")
    nchars, = struct.unpack_from("<H", data, h + 1)
    first = data[h + 4]
    defs, = struct.unpack_from("<H", data, h + 5)
    top, base, bottom = struct.unpack_from("<bbb", data, h + 8)
    offs = struct.unpack_from(f"<{nchars}H", data, h + 16)
    widths = list(data[h + 16 + 2 * nchars:h + 16 + 3 * nchars])
    strokes = []
    for o in offs:
        p = h + defs + o
        glyph = []
        while p + 2 <= len(data):
            b1, b2 = data[p], data[p + 1]
            p += 2
            op = ((b1 >> 7) << 1) | (b2 >> 7)
            if op == END:
                break
            if op in (MOVE, DRAW):
                glyph.append((op, _s7(b1), _s7(b2)))
        strokes.append(glyph)
    return StrokeFont(name, first, top, base, bottom, widths, strokes)
