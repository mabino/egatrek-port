import pytest

from builders import build_mz
from trekre.mz import Executable, MZError


def test_segments_from_relocations():
    raw = build_mz([b"A" * 40, b"B" * 20, b"C" * 5], far_refs=[(0, 1), (1, 2)])
    exe = Executable.parse(raw)
    assert exe.header.relocations == 2
    assert exe.segment_bases() == [0, 4, 7, 9]          # main, unit, unit, stack
    assert exe.segments() == [(0, 64), (4, 48), (7, 32), (9, 16)]
    assert exe.segment_bytes(4).startswith(b"B" * 20)
    assert exe.word(4 * 16 + 32) == 7


def test_rejects_garbage():
    with pytest.raises(MZError):
        Executable.parse(b"not an exe")
    raw = build_mz([b"x" * 16])
    with pytest.raises(MZError):
        Executable.parse(raw[:-8])


@pytest.mark.gamedata
def test_real_exe(real_exe):
    exe = Executable.parse(real_exe)
    assert (exe.header.cs, exe.header.ip) == (0, 0x0EB0)
    assert exe.header.relocations == 3747
    assert len(exe.segments()) == 17   # the stack lies beyond the load image
