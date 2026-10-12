"""Command-line entry point: `trekre {inventory,strings,font}`."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

from . import bgi, pstrings, segments
from .mz import Executable


def _exe_path(arg: str | None) -> Path:
    value = arg or os.environ.get("TREK_DATA")
    if not value:
        sys.exit("error: pass --data or set TREK_DATA to the folder holding TREK.EXE")
    folder = Path(value).expanduser().resolve()
    for child in folder.iterdir() if folder.is_dir() else ():
        if child.name.upper() == "TREK.EXE":
            return child
    sys.exit(f"error: {folder} does not contain TREK.EXE")


def _write(path: str, obj) -> None:
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(obj, indent=1))
    print(f"Wrote {out}")


def inventory(raw: bytes) -> dict:
    exe = Executable.parse(raw)
    h = exe.header
    segs = segments.classify(exe)
    return {
        "sha256": hashlib.sha256(raw).hexdigest(),
        "file_size": len(raw),
        "image_size": len(exe.image),
        "entry": f"{h.cs:04x}:{h.ip:04x}",
        "stack": f"{h.ss:04x}:{h.sp:04x}",
        "relocations": len(exe.relocations),
        "segments": [s.to_json() for s in segs],
        "bgi": [
            {"kind": a.kind, "segment": f"{a.offset // 16:04x}", "offset": a.offset,
             "name": a.name, "description": a.description, "size": a.size}
            for a in bgi.scan(exe.image)
        ],
    }


def _cmd_inventory(args) -> int:
    result = inventory(_exe_path(args.data).read_bytes())
    print(f"TREK.EXE  sha256 {result['sha256'][:16]}…  entry {result['entry']}  "
          f"relocations {result['relocations']}")
    for s in result["segments"]:
        print(f"  {s['base']}  {s['size']:6d}  {s['role']:10}  {s['strings'] or ''}")
    for a in result["bgi"]:
        print(f"  {a['kind']:6} {a['name']:8} @ {a['segment']}  {a['description']}")
    _write(args.out, result)
    return 0


def _cmd_strings(args) -> int:
    exe = Executable.parse(_exe_path(args.data).read_bytes())
    rows = []
    for seg in segments.classify(exe):
        if seg.role not in ("main", "unit", "data"):
            continue
        chunk = exe.image[seg.base * 16:seg.base * 16 + seg.size]
        rows += [{"segment": f"{seg.base:04x}", "offset": f"{o:04x}", "text": t}
                 for o, t in pstrings.find(chunk, args.min)]
    print(f"{len(rows)} strings")
    _write(args.out, rows)
    return 0


def _cmd_font(args) -> int:
    exe = Executable.parse(_exe_path(args.data).read_bytes())
    fonts = [a for a in bgi.scan(exe.image) if a.kind == "font"]
    if not fonts:
        sys.exit("error: no BGI stroke font found")
    f = bgi.parse_font(exe.image, fonts[0].offset)
    print(f"{f.name}: {len(f.strokes)} glyphs from {chr(f.first_char)!r}, top {f.top} bottom {f.bottom}")
    _write(args.out, {"name": f.name, "first_char": f.first_char, "top": f.top, "baseline": f.baseline,
                      "bottom": f.bottom, "widths": f.widths, "strokes": f.strokes})
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="trekre")
    sub = parser.add_subparsers(dest="command", required=True)
    for name, func, default, help_ in (
        ("inventory", _cmd_inventory, "out/inventory.json", "segment map and embedded BGI assets"),
        ("strings", _cmd_strings, "out/strings.json", "Pascal string constants by segment"),
        ("font", _cmd_font, "out/trip-font.json", "decode the embedded Triplex stroke font"),
    ):
        p = sub.add_parser(name, help=help_)
        p.add_argument("--data", help="folder holding TREK.EXE (default: $TREK_DATA)")
        p.add_argument("--out", default=default)
        if name == "strings":
            p.add_argument("--min", type=int, default=4, help="minimum string length")
        p.set_defaults(func=func)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
