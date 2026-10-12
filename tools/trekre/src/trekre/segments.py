"""Assign each segment of a Turbo Pascal 4 program to a role.

Load order is: main program, then units (most recently used first), then the
System unit, then the data segment (DS) and the stack. Units Borland supplied
are recognised by signature strings; the rest are the author's code.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import bgi, pstrings
from .mz import Executable

SIGNATURES = (
    (b"Runtime error ", "system"),
    (b"BGI Error: Graphics not initialized", "graph"),
    (bgi.FONT_MAGIC, "bgi-font"),
    (bgi.DRIVER_MAGIC, "bgi-driver"),
)


@dataclass(frozen=True)
class Segment:
    base: int
    size: int
    role: str
    strings: int

    def to_json(self) -> dict:
        return {"base": f"{self.base:04x}", "size": self.size, "role": self.role, "strings": self.strings}


def classify(exe: Executable, ds: int | None = None) -> list[Segment]:
    """ds defaults to the segment just below the stack segment."""
    segs = exe.segments()
    if ds is None:
        below = [b for b, _ in segs if b < exe.header.ss]
        ds = below[-1] if below else None
    out = []
    for base, size in segs:
        chunk = exe.image[base * 16:base * 16 + size]
        if base == exe.header.ss:
            role = "stack"
        elif base == ds:
            role = "data"
        elif base == exe.header.cs:
            role = "main"
        else:
            role = next((r for sig, r in SIGNATURES if sig in chunk), "unit")
        n = len(pstrings.find(chunk, 6)) if role in ("main", "unit") else 0
        out.append(Segment(base, size, role, n))
    return out
