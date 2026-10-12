"""Headless Ghidra analysis of TREK.EXE (run inside the trek-analysis container).

Imports the 16-bit MZ executable, then before auto-analysis:

  * renames each memory block after its role (main, unit, graph, system, ...),
    using the segment map from trekre (relocations give exact unit boundaries);
  * pins DS to the data segment for all code, as Turbo Pascal's startup does,
    so global variables and typed constants resolve in the decompiler;
  * types every Pascal string literal in game segments as PascalString255,
    which keeps the disassembler out of them and shows text in decompiled code.

Exports:

  functions.json  every function: address, name, segment role, size, callers/callees
  strings.json    Pascal string literals with the functions that reference them
  summary.json    counts per segment role
  decomp/         optional C output per game segment (--decompile)

Usage: python analyze.py --binary /data/TREK.EXE --out /work/out/ghidra [--decompile]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "trekre" / "src"))
from trekre import pstrings, segments  # noqa: E402
from trekre.mz import Executable  # noqa: E402

GAME_ROLES = ("main", "unit")


def seg_space(program):
    return program.getAddressFactory().getDefaultAddressSpace()


def seg_addr(space, segment: int, offset: int):
    from jpype import JInt  # SegmentedAddressSpace.getAddress(int, int) is overloaded

    return space.getAddress(JInt(segment), JInt(offset))


def load_base(program) -> int:
    """Ghidra loads MZ images at a segment bias (0x1000 by default)."""
    space = seg_space(program)
    return min(b.getStart().getSegment() for b in program.getMemory().getBlocks()
               if b.getStart().getAddressSpace() == space)


def prepare(flat, exe: Executable, segs):
    from ghidra.program.model.data import PascalString255DataType  # type: ignore
    from ghidra.program.model.symbol import SourceType  # type: ignore

    program = flat.getCurrentProgram()
    space = seg_space(program)
    bias = load_base(program)
    mem, listing = program.getMemory(), program.getListing()
    ctx = program.getProgramContext()
    ds_reg = program.getRegister("DS")
    data = next(s for s in segs if s.role == "data")

    typed = 0
    tx = program.startTransaction("Prepare TREK.EXE")
    try:
        for s in segs:
            start = seg_addr(space, bias + s.base, 0)
            block = mem.getBlock(start)
            if block is None:
                continue
            block.setName(f"{s.role}_{s.base:04x}")
            if s.role == "data":
                continue
            ctx.setValue(ds_reg, block.getStart(), block.getEnd(), _big(bias + data.base))
            if s.role in GAME_ROLES:
                chunk = exe.image[s.base * 16:s.base * 16 + s.size]
                for off, text in pstrings.find(chunk, 4):
                    addr = seg_addr(space, bias + s.base, off)
                    try:
                        listing.createData(addr, PascalString255DataType.dataType)
                        typed += 1
                    except Exception:  # overlaps something the loader already defined
                        pass
        entry = seg_addr(space, bias + exe.header.cs, exe.header.ip)
        program.getSymbolTable().createLabel(entry, "program_entry", SourceType.ANALYSIS)
    finally:
        program.endTransaction(tx, True)
    return typed


# MOV DI, imm16 ; PUSH CS ; PUSH DI -- how Turbo Pascal passes a string literal.
STRING_ARG = re.compile(rb"\xbf(..)\x0e\x57", re.S)


def link_strings(program, exe: Executable, segs) -> int:
    """Add a data reference from each string-literal argument to its string.

    The literal's offset is an immediate, so Ghidra cannot tell it is a pointer.
    Strings shorter than our scan threshold are typed here as well.
    """
    from ghidra.program.model.data import PascalString255DataType  # type: ignore
    from ghidra.program.model.symbol import RefType, SourceType  # type: ignore

    space, bias = seg_space(program), load_base(program)
    listing, refman = program.getListing(), program.getReferenceManager()
    linked = 0
    tx = program.startTransaction("Link string literals")
    try:
        for s in segs:
            if s.role not in GAME_ROLES:
                continue
            chunk = exe.image[s.base * 16:s.base * 16 + s.size]
            for m in STRING_ARG.finditer(chunk):
                target = int.from_bytes(m.group(1), "little")
                if target >= len(chunk) or target + 1 + chunk[target] > len(chunk):
                    continue
                at = seg_addr(space, bias + s.base, m.start())
                if listing.getInstructionAt(at) is None:
                    continue
                to = seg_addr(space, bias + s.base, target)
                if listing.getDefinedDataAt(to) is None:
                    try:
                        listing.createData(to, PascalString255DataType.dataType)
                    except Exception:
                        continue
                refman.addMemoryReference(at, to, RefType.DATA, SourceType.ANALYSIS, 1)
                linked += 1
    finally:
        program.endTransaction(tx, True)
    return linked


def _big(v: int):
    from java.math import BigInteger  # type: ignore

    return BigInteger.valueOf(v)


def name_functions(program, segs):
    """FUN_xxxx_yyyy -> <role>_<seg>_<off> using our own segment numbers (no load bias)."""
    from ghidra.program.model.symbol import SourceType  # type: ignore

    bias = load_base(program)
    role_of = {s.base: s.role for s in segs}
    tx = program.startTransaction("Name functions")
    try:
        for f in program.getFunctionManager().getFunctions(True):
            if not f.getName().startswith("FUN_"):
                continue
            a = f.getEntryPoint()
            seg = a.getSegment() - bias
            f.setName(f"{role_of.get(seg, 'seg')}_{seg:04x}_{a.getSegmentOffset():04x}", SourceType.ANALYSIS)
    finally:
        program.endTransaction(tx, True)


def export(program, segs, monitor):
    bias = load_base(program)
    role_of = {s.base: s.role for s in segs}
    fm, listing, refman = program.getFunctionManager(), program.getListing(), program.getReferenceManager()

    def ours(addr) -> str:
        return f"{addr.getSegment() - bias:04x}:{addr.getSegmentOffset():04x}"

    rows = []
    for f in fm.getFunctions(True):
        a = f.getEntryPoint()
        rows.append({
            "address": ours(a),
            "name": f.getName(),
            "role": role_of.get(a.getSegment() - bias),
            "size": int(f.getBody().getNumAddresses()),
            "callers": len(f.getCallingFunctions(monitor)),
            "callees": len(f.getCalledFunctions(monitor)),
        })

    strings = []
    for d in listing.getDefinedData(True):
        if "PascalString" not in d.getDataType().getName():
            continue
        users = sorted({fm.getFunctionContaining(r.getFromAddress()).getName()
                        for r in refman.getReferencesTo(d.getAddress())
                        if fm.getFunctionContaining(r.getFromAddress()) is not None})
        strings.append({"address": ours(d.getAddress()), "text": str(d.getValue()), "used_by": users})
    return rows, strings


def decompile(program, rows, out_dir: Path, monitor):
    from ghidra.app.decompiler import DecompInterface  # type: ignore

    iface = DecompInterface()
    iface.openProgram(program)
    fm = program.getFunctionManager()
    by_seg: dict[str, list[str]] = defaultdict(list)
    failures = 0
    for f in fm.getFunctions(True):
        name = f.getName()
        role = name.split("_")[0]
        if role not in GAME_ROLES and name != "program_entry":
            continue
        res = iface.decompileFunction(f, 120, monitor)
        if not res.decompileCompleted():
            failures += 1
            by_seg[name.split("_")[1] if "_" in name else "entry"].append(f"/* {name}: decompile failed */\n")
            continue
        seg = f"{f.getEntryPoint().getSegment() - load_base(program):04x}"
        by_seg[seg].append(res.getDecompiledFunction().getC())
    target = out_dir / "decomp"
    target.mkdir(parents=True, exist_ok=True)
    for seg, chunks in by_seg.items():
        (target / f"seg_{seg}.c").write_text("\n".join(chunks))
    iface.dispose()
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--project", default=Path("/work/ghidra-projects"), type=Path)
    ap.add_argument("--project-name", default="trek")
    ap.add_argument("--decompile", action="store_true")
    args = ap.parse_args()

    exe = Executable.parse(args.binary.read_bytes())
    segs = segments.classify(exe)

    import pyghidra

    pyghidra.start()
    from ghidra.util.task import ConsoleTaskMonitor  # type: ignore

    monitor = ConsoleTaskMonitor()
    args.out.mkdir(parents=True, exist_ok=True)
    args.project.mkdir(parents=True, exist_ok=True)

    with pyghidra.open_program(
        str(args.binary), project_location=str(args.project), project_name=args.project_name, analyze=False
    ) as flat:
        program = flat.getCurrentProgram()
        typed = prepare(flat, exe, segs)
        flat.analyzeAll(program)
        linked = link_strings(program, exe, segs)
        name_functions(program, segs)
        rows, strings = export(program, segs, monitor)

        (args.out / "functions.json").write_text(json.dumps(rows, indent=1))
        (args.out / "strings.json").write_text(json.dumps(strings, indent=1))
        summary = {
            "functions": len(rows),
            "functions_by_role": dict(Counter(r["role"] for r in rows).most_common()),
            "pascal_strings_typed": typed,
            "string_arguments_linked": linked,
            "strings_with_users": sum(1 for s in strings if s["used_by"]),
        }
        if args.decompile:
            summary["decompile_failures"] = decompile(program, rows, args.out, monitor)
        (args.out / "summary.json").write_text(json.dumps(summary, indent=1))
        print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
