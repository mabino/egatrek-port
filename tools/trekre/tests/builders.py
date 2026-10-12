"""Build tiny synthetic MZ executables and BGI stroke fonts for tests (no game data required)."""

from __future__ import annotations

import struct


def build_mz(segments: list[bytes], *, far_refs: list[tuple[int, int]] = (), cs: int = 0, ip: int = 0,
             ss_size: int = 16) -> bytes:
    """Lay out paragraph-aligned segments followed by a stack segment.

    far_refs: (from segment index, to segment index). Each segment gets one
    trailing paragraph; a far ref writes the target's relocated segment word
    at the start of the "from" segment's trailing paragraph, like a far call.
    """
    bases, tails, image = [], [], b""
    for s in segments:
        bases.append(len(image) // 16)
        image += s.ljust((len(s) + 15) // 16 * 16, b"\x00")
        tails.append(len(image) - bases[-1] * 16)
        image += b"\x00" * 16
    ss = len(image) // 16
    image += b"\x00" * ss_size
    image = bytearray(image)
    relocs = []
    for frm, to in far_refs:
        struct.pack_into("<H", image, bases[frm] * 16 + tails[frm], bases[to])
        relocs.append((bases[frm], tails[frm]))
    header_size = (28 + 4 * len(relocs) + 15) // 16 * 16
    total = header_size + len(image)
    pages, last = (total + 511) // 512, total % 512
    head = b"MZ" + struct.pack("<13H", last, pages, len(relocs), header_size // 16, 0, 0xFFFF,
                               ss, ss_size, 0, ip, bases[cs], 28, 0)
    head += b"".join(struct.pack("<HH", off, seg) for seg, off in relocs)
    return head.ljust(header_size, b"\x00") + bytes(image)


def _s7(v: int, flag: int) -> int:
    return (v & 0x7F) | (flag << 7)


def build_font(glyphs: dict[str, list[tuple[int, int, int]]], *, name: bytes = b"TEST",
               top: int = 10, bottom: int = -3) -> bytes:
    """A BGI .CHR stroke font. glyphs maps char -> [(op, x, y)] with op 2=move, 3=draw."""
    first = min(map(ord, glyphs))
    last = max(map(ord, glyphs))
    n = last - first + 1
    strokes, offsets, widths = b"", [], []
    for code in range(first, last + 1):
        offsets.append(len(strokes))
        pts = glyphs.get(chr(code), [])
        widths.append(max((x for _, x, _ in pts), default=0) + 1)
        for op, x, y in pts:
            strokes += bytes([_s7(x, op >> 1), _s7(y, op & 1)])
        strokes += b"\x00\x00"
    defs = 16 + 3 * n
    body = (b"+" + struct.pack("<HBBHB", n, 0, first, defs, 0) + struct.pack("<bbb", top, 0, bottom)
            + b"\x00" * 5 + struct.pack(f"<{n}H", *offsets) + bytes(widths) + strokes)
    desc = b"PK\x08\x08BGI Test font\r\n\x00\x1a"
    head = desc + struct.pack("<H", 0x80) + name + struct.pack("<H", len(body)) + b"\x01\x00"
    return head.ljust(0x80, b"\x00") + body
