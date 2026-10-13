from builders import build_mz
from trekre import cfg, codegen
from trekre.mz import Executable

CODE = bytes.fromhex(
    "b80500"    # mov ax, 5
    "e80300"    # call +3
    "cd21"      # int 21h
    "c3"        # ret (unused)
    "48"        # dec ax       <- call target
    "75fd"      # jne -3
    "c3"        # ret
)


def generated(tmp_path):
    prog = cfg.discover(cfg.load(Executable.parse(build_mz([CODE])), load_segment=0x1000))
    paths = codegen.generate(prog, tmp_path, source_sha256="test")
    return prog, {p.name: p.read_text() for p in paths}


def test_files_and_tables(tmp_path):
    prog, files = generated(tmp_path)
    assert set(files) == {"program.cpp", "seg_0000.cpp"}
    assert "kCodeImage" in files["program.cpp"] and "&seg_0000" in files["program.cpp"]
    for start in prog.block_starts:
        assert f"0x{start:05x}" in files["program.cpp"]


def test_control_flow(tmp_path):
    _, files = generated(tmp_path)
    seg = files["seg_0000.cpp"]
    assert "goto L_10009;" in seg                      # near call to a local block
    assert "m.push(static_cast<std::uint16_t>(0x10006u - c.s[1] * 16u));" in seg
    assert "if (op::cond(c, 5)) goto L_10009;" in seg  # jne back to the loop
    assert "m.interrupt(0x21, 0x10008); goto dispatch;" in seg
    assert "m.near_return(0x0); goto dispatch;" in seg
    assert seg.count("if (--m.budget < 0)") == seg.count("\nL_")


def test_unchanged_output_keeps_timestamps(tmp_path):
    generated(tmp_path)
    before = (tmp_path / "seg_0000.cpp").stat().st_mtime_ns
    generated(tmp_path)
    assert (tmp_path / "seg_0000.cpp").stat().st_mtime_ns == before
