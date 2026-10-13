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


def _cmd_dis(args) -> int:
    from . import cfg, x86

    prog = cfg.load(Executable.parse(_exe_path(args.data).read_bytes()))
    seg, off = (int(x, 16) for x in args.address.split(":"))
    at = seg * 16 + off
    for _ in range(args.count):
        try:
            insn = x86.decode(prog.image, at, seg * 16)
        except x86.DecodeError as e:
            print(f"{seg:04x}:{at - seg * 16:04x}  {prog.image[at]:02x}             ?? {e}")
            at += 1
            continue
        print(f"{seg:04x}:{at - seg * 16:04x}  {prog.image[at:at + insn.size].hex():14} {insn}")
        at += insn.size
    return 0


def recompile(raw: bytes, out_dir: Path, extra_seeds: list[tuple[int, int]] = ()) -> dict:
    """extra_seeds: (segment, offset) pairs relative to the load segment, as
    trek_headless --dump-interpreted writes them."""
    from . import cfg, codegen, hints

    sha = hashlib.sha256(raw).hexdigest()
    if not sha.startswith(hints.SHA256):
        print(f"warning: TREK.EXE sha256 {sha[:16]}… differs from the version the hints were made for",
              file=sys.stderr)
    prog = cfg.load(Executable.parse(raw), moved=hints.MOVED)
    L = prog.load_segment
    absolute = lambda pairs: {(L + s, o) for s, o in pairs}  # noqa: E731
    exclude = {(L + s) * 16 + o for s, o in hints.DYNAMIC}
    found = {(L + sg, off) for sg, off in list(extra_seeds) + list(hints.FOUND_AT_RUN_TIME)}
    seed_list = sorted(absolute(hints.seeds(prog.image)) | {s for s in found if (s[0] * 16 + s[1]) not in exclude})
    cfg.discover(prog, seeds=seed_list, noreturn=absolute(hints.NORETURN))
    prog.block_starts |= {lin for lin in exclude if lin in prog.insns}  # split blocks there
    paths = codegen.generate(prog, out_dir, exclude=exclude, source_sha256=sha)
    return {"instructions": len(prog.insns), "blocks": len(prog.block_starts - exclude),
            "indirect_branches": len(prog.indirect), "decode_errors": len(prog.errors),
            "files": [p.name for p in paths]}


def _cmd_recomp(args) -> int:
    extra = []
    if args.extra_seeds:
        extra = [tuple(int(x, 16) for x in line.split(":"))
                 for line in Path(args.extra_seeds).read_text().split() if ":" in line]
    stats = recompile(_exe_path(args.data).read_bytes(), Path(args.out), extra)
    print(f"{stats['instructions']} instructions in {stats['blocks']} blocks -> {len(stats['files'])} files "
          f"in {args.out} ({stats['decode_errors']} decode errors)")
    return 0


def _cmd_corpus(args) -> int:
    from . import corpus

    cases = corpus.generate(seed=args.seed)
    corpus.write(cases, Path(args.out))
    print(f"{len(cases)} cases -> {args.out}")
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
    d = sub.add_parser("dis", help="linear disassembly at SEG:OFF (segments as in the notes, unrelocated)")
    d.add_argument("address")
    d.add_argument("count", type=int, nargs="?", default=40)
    d.add_argument("--data", help="folder holding TREK.EXE (default: $TREK_DATA)")
    d.set_defaults(func=_cmd_dis)
    rc = sub.add_parser("recomp", help="recompile TREK.EXE to C++ for the engine")
    rc.add_argument("--data", help="folder holding TREK.EXE (default: $TREK_DATA)")
    rc.add_argument("--out", default="build/generated")
    rc.add_argument("--extra-seeds", help="file of SEG:OFF entry points found at run time")
    rc.set_defaults(func=_cmd_recomp)
    co = sub.add_parser("corpus", help="instruction test corpus from Unicorn (engine/tests/generated)")
    co.add_argument("--out", default="../../engine/tests/generated")
    co.add_argument("--seed", type=int, default=1988)
    co.set_defaults(func=_cmd_corpus)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
