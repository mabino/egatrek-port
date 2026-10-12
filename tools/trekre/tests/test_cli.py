import json

import pytest

from builders import build_font, build_mz
from trekre import segments
from trekre.cli import inventory, main
from trekre.mz import Executable

RUNTIME = b"\x0eRuntime error " + b"\x90" * 8


@pytest.fixture
def fake_exe() -> bytes:
    main_seg = b"\x00\x00\x0dCommand: test" + b"\x90" * 4
    return build_mz([main_seg, build_font({"A": [(2, 0, 0), (3, 1, 1)]}), RUNTIME, b"data"],
                    far_refs=[(0, 1), (1, 2), (2, 3)])


def test_classify(fake_exe):
    roles = [s.role for s in segments.classify(Executable.parse(fake_exe))]
    assert roles == ["main", "bgi-font", "system", "data", "stack"]


def test_inventory_and_cli(fake_exe, tmp_path):
    result = inventory(fake_exe)
    assert result["entry"] == "0000:0000"
    assert [a["name"] for a in result["bgi"]] == ["TEST"]
    (tmp_path / "trek.exe").write_bytes(fake_exe)          # lower case on disk is fine
    out = tmp_path / "o" / "strings.json"
    assert main(["strings", "--data", str(tmp_path), "--out", str(out)]) == 0
    assert {"segment": "0000", "offset": "0002", "text": "Command: test"} in json.loads(out.read_text())
    assert main(["font", "--data", str(tmp_path), "--out", str(tmp_path / "f.json")]) == 0


def test_missing_exe(tmp_path):
    with pytest.raises(SystemExit):
        main(["inventory", "--data", str(tmp_path)])


@pytest.mark.gamedata
def test_real_segment_roles(real_exe):
    roles = {s.base: s.role for s in segments.classify(Executable.parse(real_exe))}
    assert roles[0x0000] == "main" and roles[0x1A0F] == "graph" and roles[0x1D90] == "system"
    assert roles[0x1F7B] == "data" and roles[0x08DE] == "bgi-font" and roles[0x0AA1] == "bgi-driver"
