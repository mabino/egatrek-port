from builders import build_mz
from trekre import cfg
from trekre.mz import Executable

# Segment 0:  0 call 16h / 3 jne 6 / 5 nop / 6 callf 3:0 / B callf 3:5 (may not return) / 10 data / 16 ret
MAIN = bytes.fromhex("e81300" "7501" "90" "9a00000000" "9a05000000") + b"\x02junk\x00" + b"\xc3"
FAR = bytes.fromhex("cb" "90909090" "f4")  # retf / nops / hlt at 5


def program(moved=()):
    raw = build_mz([MAIN, FAR])
    exe = Executable.parse(raw)
    # Point both far calls at the second segment (unrelocated base 3, see builders).
    image = bytearray(exe.image)
    for at in (9, 14):  # segment words of the two far calls
        image[at:at + 2] = (3).to_bytes(2, "little")
    exe.image = bytes(image)
    exe.relocations += [(0, 9), (0, 14)]
    return cfg.load(exe, load_segment=0x1000, moved=moved)


def test_relocation_and_discovery():
    prog = cfg.discover(program())
    base = 0x10000
    assert prog.entry == (0x1000, 0)
    starts = {s - base for s in prog.block_starts}
    # entry, after call, jne fall-through and target, after each far call, call target, far targets
    assert starts == {0x0, 0x3, 0x5, 0x6, 0xB, 0x10, 0x16, 0x30, 0x35}
    assert prog.segments[1][0] == 0x1003
    # Without the no-return hint, the data after the second far call is decoded as code.
    assert set(prog.errors) == {base + 0x13}


def test_noreturn_stops_fallthrough():
    prog = cfg.discover(program(), noreturn={(0x1003, 0x0005)})
    assert 0x10000 + 0x10 not in prog.block_starts  # data after the call is not decoded
    assert 0x10000 + 0x10 not in prog.insns
    assert not prog.errors


def test_blocks_split_at_targets():
    prog = cfg.discover(program())
    blocks = dict(prog.blocks())
    assert [i.op for i in blocks[0x10000]] == ["call"]
    assert [i.op for i in blocks[0x10005]] == ["nop"]  # falls into the next block start
    assert [i.op for i in blocks[0x10006]] == ["callf"]


def test_moved_code_is_decoded_at_its_destination():
    prog = cfg.load(Executable.parse(build_mz([b"\xeb\xfe" + b"\x90" * 14, b"\xc3"])), load_segment=0x1000,
                    moved=[((2, 0), (0, 4), 1)])
    assert prog.image[4] == 0xC3 and prog.original[4] == 0x90
