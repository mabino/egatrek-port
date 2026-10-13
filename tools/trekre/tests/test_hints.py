import pytest

from trekre import cfg, hints, x86
from trekre.mz import Executable


@pytest.mark.gamedata
def test_hints_match_the_binary(real_exe):
    import hashlib

    assert hashlib.sha256(real_exe).hexdigest().startswith(hints.SHA256)
    prog = cfg.load(Executable.parse(real_exe), moved=hints.MOVED)
    # Driver table entries and run-time seeds all decode.
    for seg, off in hints.seeds(prog.image) + list(hints.FOUND_AT_RUN_TIME):
        x86.decode(prog.image, seg * 16 + off, seg * 16)
    # The patched INT in Dos.Intr and the emulate RET are where the hints say.
    assert prog.image[0x1521 * 16 + 0x99] == 0xCD
    assert prog.image[0x0AAB * 16 + 0x10] == 0xC3
    # Graph's moved interface starts with its dispatcher (push ds; push cs; pop ds).
    assert prog.image[0x1C1F0:0x1C1F3] == b"\x1e\x0e\x1f"
