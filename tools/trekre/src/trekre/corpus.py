"""Instruction test corpus with Unicorn as the reference CPU.

For each instruction form we generate random register, flag and memory
states, run one instruction in Unicorn (x86, 16-bit real mode) and record the
result, masking flags Intel leaves undefined. The output is two C++ files:

  ops_corpus.inc        the cases (input state, expected state, defined flags)
  ops_corpus_recomp.cpp each case's instruction as recompiled code, generated
                        by the same code generator the game goes through

engine/tests/ops_corpus_test.cpp runs every case through the interpreter and
the recompiled function and compares both with Unicorn.
"""

from __future__ import annotations

import random
from dataclasses import dataclass
from pathlib import Path

from . import x86
from .cfg import Program
from .codegen import HEADER, _SegmentGen

CS, DS, SS = 0x1000, 0x2000, 0x3000
IP = 0x0100
DATA = 0x0200        # 32-byte data window at DS:0200 (ES = DS)
STACK_SP = 0x0110    # 32-byte stack window at SS:0100

CF, PF, AF, ZF, SF, OF = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800
ARITH = CF | PF | AF | ZF | SF | OF
LOGIC = CF | PF | ZF | SF | OF
ALL = ARITH | 0x400  # plus DF


@dataclass
class Case:
    code: bytes
    regs: list[int]          # ax cx dx bx sp bp si di
    flags: int
    mem: bytes               # data window
    stack: bytes             # stack window
    out_regs: list[int]
    out_sregs: list[int]     # es cs ss ds
    out_flags: int
    mask: int                # flags to compare
    out_mem: bytes
    out_stack: bytes
    out_ip: int
    text: str


def _modrm_forms(rng: random.Random):
    """(modrm byte, extra displacement bytes) for a register or a memory operand."""
    if rng.random() < 0.5:
        return 0xC0 | rng.randrange(8), b"", "reg"
    # [bx+si], [bx+di], [bp+si] (SS), [si+disp8], [di+disp8], [bx+disp8], [disp16]
    choice = rng.choice(["bx+si", "bx+di", "si+d8", "di+d8", "bx+d8", "d16", "bp+d8"])
    if choice == "bx+si":
        return 0x00, b"", choice
    if choice == "bx+di":
        return 0x01, b"", choice
    if choice == "si+d8":
        return 0x44, bytes([rng.randrange(8)]), choice
    if choice == "di+d8":
        return 0x45, bytes([rng.randrange(8)]), choice
    if choice == "bx+d8":
        return 0x47, bytes([rng.randrange(8)]), choice
    if choice == "bp+d8":
        return 0x46, bytes([rng.randrange(8)]), choice
    return 0x06, (DATA + rng.randrange(16)).to_bytes(2, "little"), choice


def _with_reg(modrm: int, reg: int) -> int:
    return (modrm & 0xC7) | (reg << 3)


def templates(rng: random.Random):
    """Yields (code bytes, defined-flag mask, register constraints)."""
    for _ in range(6):
        for op in range(8):
            for kind in range(4):
                m, disp, _ = _modrm_forms(rng)
                b = (op << 3) | kind
                mask = LOGIC if op in (1, 4, 6) else ARITH
                yield bytes([b, _with_reg(m, rng.randrange(8))]) + disp, mask, {}
            yield bytes([(op << 3) | 4, rng.randrange(256)]), LOGIC if op in (1, 4, 6) else ARITH, {}
            yield bytes([(op << 3) | 5]) + rng.randrange(65536).to_bytes(2, "little"), LOGIC if op in (1, 4, 6) else ARITH, {}
            for opc in (0x80, 0x81, 0x83):
                m, disp, _ = _modrm_forms(rng)
                imm = rng.randrange(256).to_bytes(1, "little") if opc != 0x81 else rng.randrange(65536).to_bytes(2, "little")
                yield bytes([opc, _with_reg(m, op)]) + disp + imm, LOGIC if op in (1, 4, 6) else ARITH, {}
    for _ in range(10):
        for opc in (0xD0, 0xD1, 0xD2, 0xD3, 0xC0, 0xC1):
            for sub in (0, 1, 2, 3, 4, 5, 7):
                m, disp, _ = _modrm_forms(rng)
                count = {0xD0: 1, 0xD1: 1}.get(opc)
                imm = b""
                if opc in (0xC0, 0xC1):
                    count = rng.choice([0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31])
                    imm = bytes([count])
                yield bytes([opc, _with_reg(m, sub)]) + disp + imm, ("shift", sub, opc, count), {}
    for _ in range(25):
        for opc in (0xF6, 0xF7):
            for sub in (0, 2, 3, 4, 5, 6, 7):
                m, disp, _ = _modrm_forms(rng)
                imm = b""
                if sub == 0:
                    imm = bytes([rng.randrange(256)]) if opc == 0xF6 else rng.randrange(65536).to_bytes(2, "little")
                mask = {0: LOGIC, 2: 0, 3: ARITH, 4: CF | OF, 5: CF | OF, 6: 0, 7: 0}[sub]
                yield bytes([opc, _with_reg(m, sub)]) + disp + imm, mask, {"div": sub >= 6}
        for opc, sub in ((0xFE, 0), (0xFE, 1), (0xFF, 0), (0xFF, 1)):
            m, disp, _ = _modrm_forms(rng)
            yield bytes([opc, _with_reg(m, sub)]) + disp, PF | AF | ZF | SF | OF | CF, {}
        for r in range(8):
            if r != 4:
                yield bytes([0x40 + r]), ARITH, {}
                yield bytes([0x48 + r]), ARITH, {}
        for opc in (0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B):
            m, disp, _ = _modrm_forms(rng)
            reg = rng.randrange(8)
            if opc in (0x87, 0x89, 0x8B) and (m & 0xC0) == 0xC0 and (reg == 4 or (m & 7) == 4):
                continue
            if opc in (0x8B,) and reg == 4:
                continue
            yield bytes([opc, _with_reg(m, reg)]) + disp, LOGIC if opc < 0x86 else ALL, {}
        m, disp, _ = _modrm_forms(rng)
        if (m & 0xC0) != 0xC0:
            yield bytes([0x8D, _with_reg(m, rng.choice([0, 1, 2, 3, 6, 7]))]) + disp, ALL, {}
            yield bytes([0xC4, _with_reg(m, rng.choice([0, 1, 2, 3, 6, 7]))]) + disp, ALL, {"lds": True}
        yield bytes([0x69, 0xC0 | rng.randrange(8) << 3 | rng.choice([0, 1, 2, 3, 6, 7])]) + rng.randrange(65536).to_bytes(2, "little"), CF | OF, {}
        yield bytes([0x6B, 0xC0 | rng.choice([0, 1, 2, 3, 6, 7]) << 3 | rng.randrange(8), rng.randrange(256)]), CF | OF, {}
        for opc, mask in ((0x98, ALL), (0x99, ALL), (0x9F, ALL), (0x9E, ALL), (0x27, CF | AF | SF | ZF | PF),
                          (0x2F, CF | AF | SF | ZF | PF), (0x37, CF | AF), (0x3F, CF | AF), (0xF5, ALL),
                          (0xF8, ALL), (0xF9, ALL), (0xFC, ALL), (0xFD, ALL), (0xD7, ALL)):
            yield bytes([opc]), mask, {}
        yield bytes([0xD4, rng.choice([10, 7, 16, 3])]), SF | ZF | PF, {}
        yield bytes([0xD5, rng.choice([10, 7, 16, 3])]), SF | ZF | PF, {}
        for r in (0, 1, 2, 3, 5, 6, 7):
            yield bytes([0x50 + r]), ALL, {}
            yield bytes([0x58 + r]), ALL, {}
        yield bytes([0x91 + rng.randrange(7)]), ALL, {}
        yield bytes([0x8C, 0xC0 | rng.randrange(4) << 3 | rng.choice([0, 1, 2, 3, 6, 7])]), ALL, {}
        for cc in range(16):
            yield bytes([0x70 + cc, rng.randrange(256)]), ALL, {}
        for opc in (0xE0, 0xE1, 0xE2, 0xE3):
            yield bytes([opc, rng.randrange(256)]), ALL, {"cx_small": True}
        for opc in (0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF):
            for prefix in (b"", b"\xf3", b"\xf2"):
                if prefix == b"\xf2" and opc not in (0xA6, 0xA7, 0xAE, 0xAF):
                    continue
                mask = ARITH if opc in (0xA6, 0xA7, 0xAE, 0xAF) else ALL
                yield prefix + bytes([opc]), mask, {"string": True}


def _shift_mask(spec, w: int, count: int) -> int:
    _, sub, _, _ = spec
    count &= 0x1F
    if count == 0:
        return ALL
    if sub in (0, 1, 2, 3):  # rotates: CF, and OF for a count of 1
        return CF | (OF if count == 1 else 0)
    mask = SF | ZF | PF | (OF if count == 1 else 0)
    if count <= w:
        mask |= CF
    return mask


def generate(seed: int = 1988, per_template: int = 2) -> list[Case]:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.unicorn import UcError
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_BX, UC_X86_REG_CS, UC_X86_REG_CX,
                                   UC_X86_REG_DI, UC_X86_REG_DS, UC_X86_REG_DX, UC_X86_REG_EFLAGS, UC_X86_REG_ES,
                                   UC_X86_REG_IP, UC_X86_REG_SI, UC_X86_REG_SP, UC_X86_REG_SS)

    regs16 = [UC_X86_REG_AX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_BX, UC_X86_REG_SP, UC_X86_REG_BP,
              UC_X86_REG_SI, UC_X86_REG_DI]
    sregs = [UC_X86_REG_ES, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS]
    rng = random.Random(seed)
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x40000)
    cases: list[Case] = []
    for code, mask_spec, cons in templates(rng):
        insn = x86.decode(code + b"\0" * 8, 0)
        for _ in range(per_template):
            regs = [rng.randrange(65536) for _ in range(8)]
            regs[4] = STACK_SP
            regs[3] = DATA + rng.randrange(8)        # bx
            regs[5] = 0x0100 + rng.randrange(8)      # bp: into the stack window via SS
            regs[6] = rng.randrange(8) + (DATA if rng.random() < 0.7 else 0)   # si
            regs[7] = rng.randrange(8) + DATA + 8    # di
            if cons.get("string") or cons.get("cx_small"):
                regs[1] = rng.randrange(5)
                regs[6] = DATA + rng.randrange(8)
            if cons.get("div"):
                regs[2] = rng.randrange(4)           # keep quotients small enough, usually
            if code[0] == 0xD7:
                regs[0] = (regs[0] & 0xFF00) | rng.randrange(16)
                regs[3] = DATA
            flags = 0x0002 | (rng.randrange(0x1000) & (ARITH | 0x400))
            if cons.get("string") and rng.random() < 0.5:
                flags &= ~0x400
            mem = bytes(rng.randrange(256) for _ in range(32))
            stack = bytes(rng.randrange(256) for _ in range(32))

            mu.mem_write(0, b"\0" * 0x40000)
            mu.mem_write(CS * 16 + IP, code + b"\x90" * 8)
            # Unicorn keeps translated code across writes; drop it or the
            # previous case's instruction runs again.
            mu.ctl_remove_cache(CS * 16 + IP, CS * 16 + IP + 16)
            mu.mem_write(DS * 16 + DATA, mem)
            mu.mem_write(SS * 16 + 0x100, stack)
            for r, v in zip(regs16, regs):
                mu.reg_write(r, v)
            for r, v in zip(sregs, (DS, CS, SS, DS)):
                mu.reg_write(r, v)
            mu.reg_write(UC_X86_REG_EFLAGS, flags)
            try:
                if insn.op in x86.CONDITIONAL:
                    mu.emu_start(CS * 16 + IP, 0, count=1)
                else:
                    # Run to the next instruction: Unicorn counts each REP iteration separately.
                    mu.emu_start(CS * 16 + IP, CS * 16 + IP + len(code))
            except UcError:
                continue  # divide error and friends: not in the corpus
            out_regs = [mu.reg_read(r) & 0xFFFF for r in regs16]
            out_sregs = [mu.reg_read(r) & 0xFFFF for r in sregs]
            out_flags = mu.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF
            out_ip = mu.reg_read(UC_X86_REG_IP) & 0xFFFF
            if out_sregs[1] != CS or (out_ip == IP and insn.op not in x86.CONDITIONAL):
                continue  # faulted (divide error): Unicorn stops or vectors; not in the corpus
            if isinstance(mask_spec, tuple):
                count = mask_spec[3] if mask_spec[3] is not None else regs[1] & 0xFF
                mask = _shift_mask(mask_spec, 8 if insn.w == 8 else 16, count)
            else:
                mask = mask_spec
            if insn.op in ("lds", "les") and out_sregs[0] != DS and out_sregs[3] != DS:
                pass
            cases.append(Case(code, regs, flags, mem, stack, out_regs, out_sregs, out_flags, mask,
                              bytes(mu.mem_read(DS * 16 + DATA, 32)), bytes(mu.mem_read(SS * 16 + 0x100, 32)),
                              out_ip, str(insn)))
    return cases


def write(cases: list[Case], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for c in cases:
        hexs = lambda bs: ",".join(f"0x{b:02x}" for b in bs)  # noqa: E731
        words = lambda ws: ",".join(f"0x{w:04x}" for w in ws)  # noqa: E731
        rows.append(f"    {{{{{hexs(c.code)}}}, {len(c.code)}, {{{words(c.regs)}}}, 0x{c.flags:04x}, "
                    f"{{{hexs(c.mem)}}}, {{{hexs(c.stack)}}}, {{{words(c.out_regs)}}}, {{{words(c.out_sregs)}}}, "
                    f"0x{c.out_flags:04x}, 0x{c.mask:04x}, {{{hexs(c.out_mem)}}}, {{{hexs(c.out_stack)}}}, "
                    f"0x{c.out_ip:04x}, \"{c.text}\"}},")
    (out_dir / "ops_corpus.inc").write_text(
        "// Generated by `trekre corpus` (Unicorn reference). Synthetic data; regenerate, do not edit.\n"
        + "\n".join(rows) + "\n")

    # One recompiled function per case, through the real code generator.
    lines = [HEADER.replace("namespace trek::recomp {", "namespace trek::corpus {")
             .replace("// Generated by `trekre recomp` from the user's TREK.EXE. Do not edit; do not commit.",
                      "// Generated by `trekre corpus`. Synthetic; regenerate, do not edit.")]
    names = []
    for i, c in enumerate(cases):
        lin = CS * 16 + IP
        image = bytearray(lin + 64)
        image[lin:lin + len(c.code)] = c.code
        prog = Program(image=bytes(image), load_segment=0, segments=[(CS, 0x10000)], entry=(CS, IP),
                       stack=(SS, STACK_SP), min_alloc=0)
        insn = x86.decode(bytes(image), lin, CS * 16)
        if insn.target is not None:
            insn.target_lin = CS * 16 + insn.target
        gen = _SegmentGen(prog, CS, [(lin, [insn])], {lin})
        name = f"case_{i}"
        names.append(name)
        lines.append(gen.function(name))
    lines.append(f"extern void (*const kCorpusFns[{len(names)}])(Machine&);")
    lines.append(f"void (*const kCorpusFns[{len(names)}])(Machine&) = {{" + ",".join(names) + "};")
    lines.append("\n}  // namespace trek::corpus\n")
    (out_dir / "ops_corpus_recomp.cpp").write_text("\n".join(lines))
