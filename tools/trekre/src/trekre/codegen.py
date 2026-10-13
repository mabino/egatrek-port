"""Generate C++ from discovered x86-16 code.

Each code segment becomes one function holding all of its basic blocks as
labels. Control transfers with a known target inside the same function are
plain `goto`s; everything else (far calls, returns, indirect branches,
interrupts) sets cpu.pc and re-enters the function's dispatch `switch`, which
returns to the machine's loop when the target lies elsewhere. Calls and
returns go through the emulated stack exactly as on the 8086, so code may
stop between any two blocks and resume later.

The output targets engine/src/dos (Machine, op::, exec::).
"""

from __future__ import annotations

import hashlib
from pathlib import Path

from . import x86
from .cfg import Program
from .x86 import Imm, Insn, Mem, Reg, SReg

ALU_FN = {"add": "add", "or": "or_", "adc": "adc", "sbb": "sbb", "and": "and_", "sub": "sub", "xor": "xor_",
          "cmp": "sub"}
SHIFT_KIND = {name: i for i, name in enumerate(x86.SHIFT)}


class CodegenError(ValueError):
    pass


def _off(m: Mem) -> str:
    parts = [f"c.r[{r}]" for r in (m.base, m.index) if r is not None]
    if m.disp or not parts:
        parts.append(f"0x{m.disp:x}")
    return f"static_cast<std::uint16_t>({' + '.join(parts)})"


class _Insn:
    """Emits one instruction. Memory operands get their offset computed once."""

    def __init__(self, gen: "_SegmentGen", insn: Insn):
        self.g, self.i = gen, insn
        self.lines: list[str] = []
        self.mem_names: dict[int, str] = {}

    def addr(self, m: Mem) -> tuple[str, str]:
        key = id(m)
        if key not in self.mem_names:
            name = f"o{len(self.mem_names)}"
            self.mem_names[key] = name
            self.lines.append(f"const std::uint16_t {name} = {_off(m)};")
        return f"c.s[{m.seg}]", self.mem_names[key]

    def read(self, o, w: int | None = None) -> str:
        w = w or getattr(o, "w", 16)
        if isinstance(o, Reg):
            return f"c.r[{o.n}]" if o.w == 16 else f"c.get8({o.n})"
        if isinstance(o, SReg):
            return f"c.s[{o.n}]"
        if isinstance(o, Imm):
            return f"0x{o.v:x}"
        seg, off = self.addr(o)
        return f"m.rd{16 if w != 8 else 8}({seg}, {off})"

    def write(self, o, value: str, w: int | None = None) -> str:
        w = w or getattr(o, "w", 16)
        if isinstance(o, Reg):
            if o.w == 16:
                return f"c.r[{o.n}] = static_cast<std::uint16_t>({value});"
            return f"c.set8({o.n}, static_cast<std::uint8_t>({value}));"
        if isinstance(o, SReg):
            return f"c.s[{o.n}] = static_cast<std::uint16_t>({value});"
        if isinstance(o, Mem):
            seg, off = self.addr(o)
            if w == 8:
                return f"m.wr8({seg}, {off}, static_cast<std::uint8_t>({value}));"
            return f"m.wr16({seg}, {off}, static_cast<std::uint16_t>({value}));"
        raise CodegenError(f"cannot write to {o} at {self.i.lin:#x}")

    def emit(self, s: str) -> None:
        self.lines.append(s)


class _SegmentGen:
    def __init__(self, prog: Program, seg: int, blocks: list[tuple[int, list[Insn]]], all_blocks: set[int]):
        self.prog, self.seg, self.blocks = prog, seg, blocks
        self.local = {start for start, _ in blocks}
        self.all_blocks = all_blocks

    def goto(self, lin: int) -> str:
        if lin in self.local:
            return f"goto L_{lin:05x};"
        return f"{{ c.pc = 0x{lin:05x}; return; }}"

    def ret_ip(self, insn: Insn) -> str:
        return f"static_cast<std::uint16_t>(0x{insn.next:05x}u - c.s[1] * 16u)"

    def near_target(self, insn: Insn) -> int:
        # Discovery resolved the CS-relative target with the CS the code runs under.
        return insn.target_lin

    def insn(self, insn: Insn, last: bool) -> list[str]:
        e = _Insn(self, insn)
        op, w, ops = insn.op, insn.w, insn.ops
        W = 8 if w == 8 else 16
        nxt = insn.next

        if op == "mov":
            e.emit(e.write(ops[0], e.read(ops[1], W), W))
        elif op in ALU_FN:
            a, b = e.read(ops[0], W), e.read(ops[1], W)
            r = f"op::{ALU_FN[op]}<{W}>(c, {a}, {b})"
            e.emit(f"{r};" if op == "cmp" else e.write(ops[0], r, W))
        elif op == "test":
            e.emit(f"op::and_<{W}>(c, {e.read(ops[0], W)}, {e.read(ops[1], W)});")
        elif op in ("inc", "dec", "neg"):
            e.emit(e.write(ops[0], f"op::{op}<{W}>(c, {e.read(ops[0], W)})", W))
        elif op == "not":
            e.emit(e.write(ops[0], f"~{e.read(ops[0], W)}", W))
        elif op in SHIFT_KIND:
            count = e.read(ops[1], 8)
            e.emit(e.write(ops[0], f"op::shift<{W}>(c, {SHIFT_KIND[op]}, {e.read(ops[0], W)}, {count})", W))
        elif op in ("mul", "imul"):
            e.emit(f"op::{op}{W}(c, {e.read(ops[0], W)});")
        elif op in ("div", "idiv"):
            e.emit(f"if (!op::{op}{W}(c, {e.read(ops[0], W)})) {{ m.interrupt(0, 0x{nxt:05x}); goto dispatch; }}")
        elif op == "imul3":
            e.emit(e.write(ops[0], f"op::imul3(c, {e.read(ops[1])}, {e.read(ops[2])})"))
        elif op == "push":
            e.emit(f"m.push({e.read(ops[0])});")
        elif op == "pop":
            if isinstance(ops[0], Mem):
                e.emit("{ const std::uint16_t v = m.pop();")
                e.emit(e.write(ops[0], "v") + " }")
            else:
                e.emit(e.write(ops[0], "m.pop()"))
        elif op == "lea":
            e.emit(e.write(ops[0], _off(ops[1])))
        elif op in ("les", "lds"):
            seg, off = e.addr(ops[1])
            e.emit(f"{{ const std::uint16_t v = m.rd16({seg}, {off}); "
                   f"c.s[{0 if op == 'les' else 3}] = m.rd16({seg}, static_cast<std::uint16_t>({off} + 2)); "
                   f"c.r[{ops[0].n}] = v; }}")
        elif op == "xchg":
            a, b = e.read(ops[0], W), e.read(ops[1], W)
            e.emit(f"{{ const std::uint16_t t = {a}; {e.write(ops[0], b, W)} {e.write(ops[1], 't', W)} }}")
        elif op in ("cbw", "cwd", "lahf", "sahf", "daa", "das", "aaa", "aas"):
            e.emit(f"op::{op}(c);")
        elif op == "aam":
            e.emit(f"if (!op::aam(c, 0x{ops[0].v:x})) {{ m.interrupt(0, 0x{nxt:05x}); goto dispatch; }}")
        elif op == "aad":
            e.emit(f"op::aad(c, 0x{ops[0].v:x});")
        elif op == "pushf":
            e.emit("m.push(c.pack());")
        elif op == "popf":
            e.emit("c.unpack(m.pop());")
        elif op in ("clc", "stc", "cmc", "cli", "sti", "cld", "std"):
            e.emit({"clc": "c.cf = false;", "stc": "c.cf = true;", "cmc": "c.cf = !c.cf;",
                    "cli": "c.ifl = false;", "sti": "c.ifl = true;", "cld": "c.df = false;",
                    "std": "c.df = true;"}[op])
        elif op in ("nop", "wait", "esc"):
            pass
        elif op == "salc":
            e.emit("c.set8(0, c.cf ? 0xFF : 0);")
        elif op == "xlat":
            seg = insn.seg_override if insn.seg_override is not None else x86.DS
            e.emit(f"c.set8(0, m.rd8(c.s[{seg}], static_cast<std::uint16_t>(c.r[3] + c.get8(0))));")
        elif op in ("movs", "lods", "cmps", "scas", "stos"):
            seg = insn.seg_override if insn.seg_override is not None else x86.DS
            rep = {None: "exec::kNoRep", "rep": "exec::kRepE", "repne": "exec::kRepNE"}[insn.rep]
            args = {"movs": f"m, {seg}, {rep}", "lods": f"m, {seg}, {rep}", "cmps": f"m, {seg}, {rep}",
                    "scas": f"m, {rep}", "stos": f"m, {rep}"}[op]
            e.emit(f"exec::{op}<{W}>({args});")
        elif op == "in":
            port = e.read(ops[1])
            e.emit(e.write(ops[0], f"m.in{W}({port})", W))
        elif op == "out":
            e.emit(f"m.out{W}({e.read(ops[0])}, {e.read(ops[1], W)});")
        elif op == "pusha":
            e.emit("{ const std::uint16_t sp = c.r[4]; for (int r = 0; r < 8; ++r) m.push(r == 4 ? sp : c.r[r]); }")
        elif op == "popa":
            e.emit("for (int r = 7; r >= 0; --r) { const std::uint16_t v = m.pop(); if (r != 4) c.r[r] = v; }")
        elif op == "leave":
            e.emit("c.r[4] = c.r[5]; c.r[5] = m.pop();")
        elif op == "enter":
            if ops[1].v != 0:
                raise CodegenError(f"ENTER with nesting level at {insn.lin:#x}")
            e.emit(f"m.push(c.r[5]); c.r[5] = c.r[4]; c.r[4] = static_cast<std::uint16_t>(c.r[4] - 0x{ops[0].v:x});")
        # ---- control transfers ----
        elif op in x86.JCC:
            cc = x86.JCC.index(op)
            e.emit(f"if (op::cond(c, {cc})) {self.goto(self.near_target(insn))}")
            e.emit(self.goto(nxt))
        elif op in ("loop", "loope", "loopne"):
            extra = {"loop": "", "loope": " && c.zf", "loopne": " && !c.zf"}[op]
            e.emit(f"if (--c.r[1] != 0{extra}) {self.goto(self.near_target(insn))}")
            e.emit(self.goto(nxt))
        elif op == "jcxz":
            e.emit(f"if (c.r[1] == 0) {self.goto(self.near_target(insn))}")
            e.emit(self.goto(nxt))
        elif op == "jmp":
            e.emit(self.goto(self.near_target(insn)))
        elif op == "call":
            e.emit(f"m.push({self.ret_ip(insn)});")
            e.emit(self.goto(self.near_target(insn)))
        elif op == "jmpf":
            sg, off = insn.far
            e.emit(f"c.s[1] = 0x{sg:x}; c.pc = 0x{sg * 16 + off:05x}; goto dispatch;")
        elif op == "callf":
            sg, off = insn.far
            e.emit(f"m.push(c.s[1]); m.push({self.ret_ip(insn)});")
            e.emit(f"c.s[1] = 0x{sg:x}; c.pc = 0x{sg * 16 + off:05x}; goto dispatch;")
        elif op in ("jmpi", "calli"):
            target = e.read(ops[0])
            e.emit(f"{{ const std::uint16_t t = {target};")
            if op == "calli":
                e.emit(f"m.push({self.ret_ip(insn)});")
            e.emit("m.near_jump(t); } goto dispatch;")
        elif op in ("jmpfi", "callfi"):
            seg, off = e.addr(ops[0])
            e.emit(f"{{ const std::uint16_t t_off = m.rd16({seg}, {off}); "
                   f"const std::uint16_t t_seg = m.rd16({seg}, static_cast<std::uint16_t>({off} + 2));")
            if op == "callfi":
                e.emit(f"m.push(c.s[1]); m.push({self.ret_ip(insn)});")
            e.emit("m.far_jump(t_seg, t_off); } goto dispatch;")
        elif op == "ret":
            e.emit(f"m.near_return(0x{ops[0].v:x}); goto dispatch;")
        elif op == "retf":
            e.emit(f"m.far_return(0x{ops[0].v:x}); goto dispatch;")
        elif op == "iret":
            e.emit("m.iret(); goto dispatch;")
        elif op in ("int", "int3"):
            n = ops[0].v if op == "int" else 3
            e.emit(f"m.interrupt(0x{n:x}, 0x{nxt:05x}); goto dispatch;")
        elif op == "into":
            e.emit(f"if (c.of) {{ m.interrupt(4, 0x{nxt:05x}); goto dispatch; }}")
            e.emit(self.goto(nxt))
        elif op == "hlt":
            e.emit(f"c.pc = 0x{nxt:05x}; m.halt(); return;")
        else:
            raise CodegenError(f"no code generator for {op} at {insn.lin:#x}")

        if last and op not in x86.ENDS_BLOCK:
            e.emit(self.goto(nxt))  # falls into the next block
        # Braces keep each instruction's temporaries out of the labels' scope.
        return [f"    {{  // {insn.lin:05x}  {insn}"] + [f"        {line}" for line in e.lines] + ["    }"]

    def function(self, name: str) -> str:
        out = [f"void {name}(Machine& m) {{", "    Cpu& c = m.cpu;", "dispatch:", "    switch (c.pc) {"]
        out += [f"        case 0x{start:05x}: goto L_{start:05x};" for start, _ in self.blocks]
        out += ["        default: return;", "    }"]
        for start, body in self.blocks:
            out.append(f"L_{start:05x}:")
            out.append(f"    if (--m.budget < 0) {{ c.pc = 0x{start:05x}; return; }}")
            out.append(f"    if (m.tracing && !m.trace(0x{start:05x})) {{ c.pc = 0x{start:05x}; return; }}")
            for k, insn in enumerate(body):
                out += self.insn(insn, k == len(body) - 1)
        out.append("}")
        return "\n".join(out)


HEADER = """// Generated by `trekre recomp` from the user's TREK.EXE. Do not edit; do not commit.
#include "dos/exec.h"
#include "dos/machine.h"
#include "dos/ops.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-label"
#endif

namespace trek::recomp {
using namespace trek::dos;
"""


def generate(prog: Program, out_dir: Path, *, exclude: set[int] = frozenset(), source_sha256: str = "") -> list[Path]:
    """Writes program.cpp and one seg_XXXX.cpp per code segment; returns the paths."""
    out_dir.mkdir(parents=True, exist_ok=True)
    blocks = [(s, b) for s, b in prog.blocks() if s not in exclude]
    starts = {s for s, _ in blocks}
    by_seg: dict[int, list[tuple[int, list[Insn]]]] = {}
    for start, body in blocks:
        by_seg.setdefault(prog.segment_of(start), []).append((start, body))

    written = []
    seg_entries = []
    for seg, seg_blocks in sorted(by_seg.items()):
        name = f"seg_{seg - prog.load_segment:04x}"
        gen = _SegmentGen(prog, seg, seg_blocks, starts)
        text = HEADER + "\n" + gen.function(name) + "\n\n}  // namespace trek::recomp\n"
        path = out_dir / f"{name}.cpp"
        _write_if_changed(path, text)
        written.append(path)
        size = next(sz for sg, sz in prog.segments if sg == seg)
        seg_entries.append((name, seg * 16, seg * 16 + size))

    image = ",".join(str(b) for b in (prog.original or prog.image))
    code_image = ",".join(str(b) for b in prog.image)
    code_bits = bytearray((len(prog.image) + 7) // 8)
    for start, body in blocks:
        for insn in body:
            for a in range(insn.lin, insn.next):
                i = a - prog.base
                code_bits[i >> 3] |= 1 << (i & 7)
    lines = [HEADER]
    lines += [f"void {name}(Machine& m);" for name, _, _ in seg_entries]
    lines.append(f"\n// sha256 of the source executable: {source_sha256}")
    lines.append(f"static const std::uint8_t kImage[{len(prog.image)}] = {{{image}}};")
    lines.append(f"static const std::uint8_t kCodeImage[{len(prog.image)}] = {{{code_image}}};")
    lines.append(f"static const std::uint8_t kCodeBits[{len(code_bits)}] = {{"
                 + ",".join(str(b) for b in code_bits) + "};")
    lines.append(f"static const std::uint32_t kBlocks[{len(starts)}] = {{"
                 + ",".join(f"0x{s:05x}" for s in sorted(starts)) + "};")
    lines.append("static const Program::Segment kSegments[] = {")
    lines += [f"    {{0x{b:05x}, 0x{e:05x}, &{name}}}," for name, b, e in seg_entries]
    lines.append("};")
    cs, ip = prog.entry
    ss, sp = prog.stack
    lines.append(f"""
const Program& program() {{
    static const Program p{{
        kImage, 0x{prog.load_segment:x}, 0x{cs:x}, 0x{ip:x}, 0x{ss:x}, 0x{sp:x}, 0x{prog.min_alloc:x},
        kBlocks, kSegments, kCodeBits, kCodeImage}};
    return p;
}}

}}  // namespace trek::recomp
""")
    path = out_dir / "program.cpp"
    _write_if_changed(path, "\n".join(lines))
    written.append(path)
    return written


def _write_if_changed(path: Path, text: str) -> None:
    """Keeps timestamps stable so the build does not recompile unchanged segments."""
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == hashlib.sha256(text.encode()).hexdigest():
        return
    path.write_text(text)
