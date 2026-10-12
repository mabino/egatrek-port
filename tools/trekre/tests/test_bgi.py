import pytest

from builders import build_font
from trekre import bgi


def test_parse_font_strokes_and_metrics():
    raw = b"\x00" * 7 + build_font({"I": [(2, 1, 0), (3, 1, 10)], "L": [(2, 0, 10), (3, 0, 0), (3, 5, 0)]},
                                   top=10, bottom=-3)
    [asset] = bgi.scan(raw)
    assert (asset.kind, asset.offset, asset.name) == ("font", 7, "TEST")
    f = bgi.parse_font(raw, asset.offset)
    assert (f.first_char, f.top, f.bottom) == (ord("I"), 10, -3)
    assert f.glyph("I") == [(bgi.MOVE, 1, 0), (bgi.DRAW, 1, 10)]
    assert f.glyph("L")[-1] == (bgi.DRAW, 5, 0)
    assert f.glyph("J") == [] and f.width("L") == 6
    assert f.glyph("z") == [] and f.width("z") == 0


def test_negative_coordinates():
    f = bgi.parse_font(build_font({"g": [(2, 0, 0), (3, 3, -3)]}))
    assert f.glyph("g") == [(bgi.MOVE, 0, 0), (bgi.DRAW, 3, -3)]


def test_not_a_font():
    with pytest.raises(bgi.BGIError):
        bgi.parse_font(b"PK\x08\x08XYZ")


@pytest.mark.gamedata
def test_real_assets(real_exe):
    from trekre.mz import Executable
    exe = Executable.parse(real_exe)
    assets = bgi.scan(exe.image)
    assert [(a.kind, a.name, a.offset // 16) for a in assets] == [("font", "TRIP", 0x8DE), ("driver", "EGAVGA", 0xAA1)]
    f = bgi.parse_font(exe.image, assets[0].offset)
    assert (len(f.strokes), f.first_char, f.top, f.bottom) == (95, 32, 21, -7)
    assert f.glyph("A")[0] == (bgi.MOVE, 9, 21)
