import random

import pytest

from trekre import x86
from trekre.x86 import Imm, Mem, Reg, SReg


def dec(hexstr: str, cs_base: int = 0):
    return x86.decode(bytes.fromhex(hexstr) + b"\0" * 8, 0, cs_base)


def test_alu_and_operands():
    i = dec("01c3")
    assert (i.op, i.size, i.ops) == ("add", 2, [Reg(3, 16), Reg(0, 16)])
    i = dec("2e8b4702")  # mov ax, cs:[bx+2]
    assert i.ops[1] == Mem(16, x86.BX, None, 2, x86.CS)
    i = dec("8a46fe")  # mov al, [bp-2] -> SS default, disp wraps to 16 bits
    assert i.ops[1] == Mem(8, x86.BP, None, 0xFFFE, x86.SS)
    assert dec("83c0ff").ops[1] == Imm(0xFFFF, 16)  # sign-extended imm8
    assert dec("8ed8").ops == [SReg(x86.DS), Reg(0, 16)]


def test_branches_are_cs_relative():
    assert dec("ebfe").target == 0x0000
    assert dec("e80100").target == 0x0004
    assert dec("9a0c1e0f1a").far == (0x1A0F, 0x1E0C)
    i = dec("ff1e3400")
    assert i.op == "callfi" and i.ops[0].w == 32


def test_prefixes():
    i = dec("f3a4")
    assert (i.op, i.rep, i.w) == ("movs", "rep", 8)
    assert dec("f2ae").rep == "repne"
    assert dec("26c47d0c").ops[1].seg == x86.ES


def test_errors():
    with pytest.raises(x86.DecodeError):
        x86.decode(b"\x0f\x05", 0)
    with pytest.raises(x86.DecodeError):
        x86.decode(b"\x81", 0)  # truncated


def test_lengths_match_capstone_on_random_code():
    capstone = pytest.importorskip("capstone")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    rng = random.Random(7)
    checked = 0
    for _ in range(20000):
        code = bytes(rng.randrange(256) for _ in range(8))
        try:
            ours = x86.decode(code, 0)
        except x86.DecodeError:
            continue
        theirs = next(md.disasm(code, 0), None)
        if theirs is None or ours.op in ("esc", "salc", "pop"):
            continue
        assert ours.size == theirs.size, (code.hex(), str(ours), theirs.mnemonic, theirs.op_str)
        checked += 1
    assert checked > 5000


@pytest.mark.gamedata
def test_real_program_decodes(real_exe):
    from trekre import cfg, hints
    from trekre.mz import Executable

    prog = cfg.load(Executable.parse(real_exe), moved=hints.MOVED)
    cfg.discover(prog, noreturn={(prog.load_segment + s, o) for s, o in hints.NORETURN})
    assert not prog.errors
    assert len(prog.insns) > 30000
    assert not any(i.op == "esc" for i in prog.insns.values())  # no 8087 code
