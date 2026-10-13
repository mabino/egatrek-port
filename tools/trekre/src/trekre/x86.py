"""A decoder for 16-bit x86 (8086 plus the few 186 additions) aimed at recompilation.

Unlike a disassembler it keeps exact operand semantics: register numbers,
effective-address components with their default segment, immediates already
sign-extended where the encoding says so, and branch targets as linear
addresses.
"""

from __future__ import annotations

from dataclasses import dataclass, field

REG16 = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")
REG8 = ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh")
SREG = ("es", "cs", "ss", "ds")
ES, CS, SS, DS = range(4)
AX, CX, DX, BX, SP, BP, SI, DI = range(8)

ALU = ("add", "or", "adc", "sbb", "and", "sub", "xor", "cmp")
SHIFT = ("rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar")
GRP3 = ("test", "test", "not", "neg", "mul", "imul", "div", "idiv")
JCC = ("jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja", "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg")

# Effective-address base/index per r/m field, and whether SS is the default segment.
EA = ((BX, SI), (BX, DI), (BP, SI), (BP, DI), (SI, None), (DI, None), (BP, None), (BX, None))


class DecodeError(ValueError):
    pass


@dataclass(frozen=True)
class Reg:
    n: int
    w: int  # 8 or 16

    def __str__(self) -> str:
        return (REG8 if self.w == 8 else REG16)[self.n]


@dataclass(frozen=True)
class SReg:
    n: int
    w: int = 16

    def __str__(self) -> str:
        return SREG[self.n]


@dataclass(frozen=True)
class Mem:
    w: int                 # access width: 8, 16, or 32 for far pointers
    base: int | None
    index: int | None
    disp: int              # 0..0xFFFF
    seg: int               # effective segment register after any override

    def __str__(self) -> str:
        parts = [REG16[r] for r in (self.base, self.index) if r is not None]
        if self.disp or not parts:
            parts.append(f"{self.disp:#x}")
        return f"{SREG[self.seg]}:[{'+'.join(parts)}]"


@dataclass(frozen=True)
class Imm:
    v: int
    w: int

    def __str__(self) -> str:
        return f"{self.v:#x}"


Operand = Reg | SReg | Mem | Imm


@dataclass
class Insn:
    lin: int                        # linear address
    size: int
    op: str
    w: int = 16                     # operand width for width-generic ops
    ops: list[Operand] = field(default_factory=list)
    rep: str | None = None          # "rep"/"repe" (F3) or "repne" (F2)
    seg_override: int | None = None
    target: int | None = None       # near branch target as an offset within CS
    target_lin: int | None = None   # the same target as a linear address (set by discovery)
    far: tuple[int, int] | None = None  # direct far branch (segment, offset)

    @property
    def next(self) -> int:
        return self.lin + self.size

    def __str__(self) -> str:
        pre = f"{self.rep} " if self.rep else ""
        if self.far:
            return f"{pre}{self.op} {self.far[0]:04x}:{self.far[1]:04x}"
        if self.target is not None:
            return f"{pre}{self.op} {self.target:#06x}"
        return f"{pre}{self.op} {', '.join(map(str, self.ops))}".strip()


class _Stream:
    def __init__(self, data: bytes, pos: int):
        self.data, self.pos, self.start = data, pos, pos

    def u8(self) -> int:
        if self.pos >= len(self.data):
            raise DecodeError(f"truncated instruction at {self.start:#x}")
        v = self.data[self.pos]
        self.pos += 1
        return v

    def s8(self) -> int:
        v = self.u8()
        return v - 0x100 if v & 0x80 else v

    def u16(self) -> int:
        return self.u8() | (self.u8() << 8)


def _modrm(s: _Stream, w: int, seg: int | None) -> tuple[int, Operand]:
    """Returns (reg field, r/m operand)."""
    b = s.u8()
    mod, reg, rm = b >> 6, (b >> 3) & 7, b & 7
    if mod == 3:
        return reg, Reg(rm, 8 if w == 8 else 16)
    if mod == 0 and rm == 6:
        base, index, disp, default = None, None, s.u16(), DS
    else:
        base, index = EA[rm]
        default = SS if base == BP else DS
        disp = 0 if mod == 0 else (s.s8() & 0xFFFF) if mod == 1 else s.u16()
    return reg, Mem(w, base, index, disp, default if seg is None else seg)


def decode(data: bytes, pos: int, cs_base: int = 0) -> Insn:
    """Decode one instruction at data[pos]. Linear address = pos; cs_base is CS*16,
    used to express near branch targets as CS offsets."""
    s = _Stream(data, pos)
    seg = rep = None
    while True:
        b = s.u8()
        if b in (0x26, 0x2E, 0x36, 0x3E):
            seg = (b >> 3) & 3
        elif b == 0xF2:
            rep = "repne"
        elif b == 0xF3:
            rep = "rep"
        elif b == 0xF0:
            pass  # LOCK: meaningless here
        else:
            break
        if s.pos - s.start > 14:
            raise DecodeError(f"too many prefixes at {pos:#x}")

    def done(op: str, ops=(), w: int = 16, **kw) -> Insn:
        return Insn(pos, s.pos - pos, op, w, list(ops), rep, seg, **kw)

    def rel(n: int) -> int:
        return (s.pos - cs_base + n) & 0xFFFF

    if b < 0x40 and (b & 7) < 6:
        op = ALU[b >> 3]
        kind = b & 7
        if kind < 4:
            w = 8 if kind in (0, 2) else 16
            reg, rm = _modrm(s, w, seg)
            r = Reg(reg, w)
            return done(op, (rm, r) if kind < 2 else (r, rm), w)
        if kind == 4:
            return done(op, (Reg(AX, 8), Imm(s.u8(), 8)), 8)
        return done(op, (Reg(AX, 16), Imm(s.u16(), 16)), 16)
    if b in (0x06, 0x0E, 0x16, 0x1E):
        return done("push", (SReg(b >> 3),))
    if b in (0x07, 0x17, 0x1F):
        return done("pop", (SReg(b >> 3),))
    if b in (0x27, 0x2F, 0x37, 0x3F):
        return done({0x27: "daa", 0x2F: "das", 0x37: "aaa", 0x3F: "aas"}[b], w=8)
    if 0x40 <= b < 0x60:
        return done(("inc", "dec", "push", "pop")[(b - 0x40) >> 3], (Reg(b & 7, 16),))
    if b == 0x60:
        return done("pusha")
    if b == 0x61:
        return done("popa")
    if b == 0x68:
        return done("push", (Imm(s.u16(), 16),))
    if b == 0x6A:
        return done("push", (Imm(s.s8() & 0xFFFF, 16),))
    if b in (0x69, 0x6B):
        reg, rm = _modrm(s, 16, seg)
        imm = s.u16() if b == 0x69 else s.s8() & 0xFFFF
        return done("imul3", (Reg(reg, 16), rm, Imm(imm, 16)))
    if 0x70 <= b < 0x80:
        d = s.s8()
        return done(JCC[b - 0x70], target=rel(d))
    if 0x80 <= b <= 0x83:
        w = 8 if b in (0x80, 0x82) else 16
        reg, rm = _modrm(s, w, seg)
        imm = s.u8() if w == 8 else s.u16() if b == 0x81 else s.s8() & 0xFFFF
        return done(ALU[reg], (rm, Imm(imm, w)), w)
    if b in (0x84, 0x85, 0x86, 0x87):
        w = 8 if b in (0x84, 0x86) else 16
        reg, rm = _modrm(s, w, seg)
        return done("test" if b < 0x86 else "xchg", (rm, Reg(reg, w)), w)
    if 0x88 <= b <= 0x8B:
        w = 8 if b in (0x88, 0x8A) else 16
        reg, rm = _modrm(s, w, seg)
        return done("mov", (rm, Reg(reg, w)) if b < 0x8A else (Reg(reg, w), rm), w)
    if b == 0x8C:
        reg, rm = _modrm(s, 16, seg)
        return done("mov", (rm, SReg(reg & 3)))
    if b == 0x8D:
        reg, rm = _modrm(s, 16, seg)
        if not isinstance(rm, Mem):
            raise DecodeError(f"lea with register operand at {pos:#x}")
        return done("lea", (Reg(reg, 16), rm))
    if b == 0x8E:
        reg, rm = _modrm(s, 16, seg)
        return done("mov", (SReg(reg & 3), rm))
    if b == 0x8F:
        _, rm = _modrm(s, 16, seg)
        return done("pop", (rm,))
    if b == 0x90:
        return done("nop")
    if 0x91 <= b <= 0x97:
        return done("xchg", (Reg(AX, 16), Reg(b & 7, 16)))
    simple = {0x98: "cbw", 0x99: "cwd", 0x9B: "wait", 0x9C: "pushf", 0x9D: "popf", 0x9E: "sahf",
              0x9F: "lahf", 0xC9: "leave", 0xCC: "int3", 0xCE: "into", 0xCF: "iret", 0xD7: "xlat",
              0xF4: "hlt", 0xF5: "cmc", 0xF8: "clc", 0xF9: "stc", 0xFA: "cli", 0xFB: "sti",
              0xFC: "cld", 0xFD: "std", 0xD6: "salc"}
    if b in simple:
        return done(simple[b], w=8 if b == 0xD7 else 16)
    if b in (0x9A, 0xEA):
        off = s.u16()
        sg = s.u16()
        return done("callf" if b == 0x9A else "jmpf", far=(sg, off))
    if 0xA0 <= b <= 0xA3:
        w = 8 if b in (0xA0, 0xA2) else 16
        m = Mem(w, None, None, s.u16(), DS if seg is None else seg)
        return done("mov", (Reg(AX, w), m) if b < 0xA2 else (m, Reg(AX, w)), w)
    if 0xA4 <= b <= 0xAF and b not in (0xA8, 0xA9):
        name = {0xA4: "movs", 0xA6: "cmps", 0xAA: "stos", 0xAC: "lods", 0xAE: "scas"}[b & 0xFE]
        return done(name, w=8 if b % 2 == 0 else 16)
    if b == 0xA8:
        return done("test", (Reg(AX, 8), Imm(s.u8(), 8)), 8)
    if b == 0xA9:
        return done("test", (Reg(AX, 16), Imm(s.u16(), 16)))
    if 0xB0 <= b < 0xB8:
        return done("mov", (Reg(b & 7, 8), Imm(s.u8(), 8)), 8)
    if 0xB8 <= b < 0xC0:
        return done("mov", (Reg(b & 7, 16), Imm(s.u16(), 16)))
    if b in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
        w = 8 if b % 2 == 0 else 16
        reg, rm = _modrm(s, w, seg)
        if b in (0xC0, 0xC1):
            count: Operand = Imm(s.u8(), 8)
        elif b in (0xD0, 0xD1):
            count = Imm(1, 8)
        else:
            count = Reg(CX, 8)
        return done(SHIFT[reg] if reg != 6 else "shl", (rm, count), w)
    if b in (0xC2, 0xCA):
        return done("ret" if b == 0xC2 else "retf", (Imm(s.u16(), 16),))
    if b in (0xC3, 0xCB):
        return done("ret" if b == 0xC3 else "retf", (Imm(0, 16),))
    if b in (0xC4, 0xC5):
        reg, rm = _modrm(s, 32, seg)
        if not isinstance(rm, Mem):
            raise DecodeError(f"les/lds with register operand at {pos:#x}")
        return done("les" if b == 0xC4 else "lds", (Reg(reg, 16), rm))
    if b in (0xC6, 0xC7):
        w = 8 if b == 0xC6 else 16
        _, rm = _modrm(s, w, seg)
        return done("mov", (rm, Imm(s.u8() if w == 8 else s.u16(), w)), w)
    if b == 0xC8:
        size = s.u16()
        return done("enter", (Imm(size, 16), Imm(s.u8(), 8)))
    if b == 0xCD:
        return done("int", (Imm(s.u8(), 8),))
    if b in (0xD4, 0xD5):
        return done("aam" if b == 0xD4 else "aad", (Imm(s.u8(), 8),), 8)
    if 0xD8 <= b <= 0xDF:
        _modrm(s, 16, seg)
        return done("esc")
    if 0xE0 <= b <= 0xE3:
        d = s.s8()
        return done(("loopne", "loope", "loop", "jcxz")[b - 0xE0], target=rel(d))
    if b in (0xE4, 0xE5, 0xEC, 0xED):
        w = 8 if b in (0xE4, 0xEC) else 16
        port: Operand = Imm(s.u8(), 8) if b < 0xEC else Reg(DX, 16)
        return done("in", (Reg(AX, w), port), w)
    if b in (0xE6, 0xE7, 0xEE, 0xEF):
        w = 8 if b in (0xE6, 0xEE) else 16
        port = Imm(s.u8(), 8) if b < 0xEE else Reg(DX, 16)
        return done("out", (port, Reg(AX, w)), w)
    if b == 0xE8:
        d = s.u16()
        return done("call", target=rel(d))
    if b == 0xE9:
        d = s.u16()
        return done("jmp", target=rel(d))
    if b == 0xEB:
        d = s.s8()
        return done("jmp", target=rel(d))
    if b in (0xF6, 0xF7):
        w = 8 if b == 0xF6 else 16
        reg, rm = _modrm(s, w, seg)
        if reg < 2:
            return done("test", (rm, Imm(s.u8() if w == 8 else s.u16(), w)), w)
        return done(GRP3[reg], (rm,), w)
    if b == 0xFE:
        reg, rm = _modrm(s, 8, seg)
        if reg > 1:
            raise DecodeError(f"invalid FE /{reg} at {pos:#x}")
        return done(("inc", "dec")[reg], (rm,), 8)
    if b == 0xFF:
        reg, rm = _modrm(s, 16, seg)
        if reg == 7:
            raise DecodeError(f"invalid FF /7 at {pos:#x}")
        if reg in (3, 5):
            if not isinstance(rm, Mem):
                raise DecodeError(f"indirect far branch through register at {pos:#x}")
            rm = Mem(32, rm.base, rm.index, rm.disp, rm.seg)
        names = ("inc", "dec", "calli", "callfi", "jmpi", "jmpfi", "push")
        return done(names[reg], (rm,))
    raise DecodeError(f"unsupported opcode {b:#04x} at {pos:#x}")


# Control-flow classification used by the recompiler.
CONDITIONAL = set(JCC) | {"loop", "loope", "loopne", "jcxz"}
ENDS_BLOCK = CONDITIONAL | {"jmp", "jmpf", "jmpi", "jmpfi", "ret", "retf", "iret", "call", "callf",
                            "calli", "callfi", "int", "int3", "into", "hlt"}
