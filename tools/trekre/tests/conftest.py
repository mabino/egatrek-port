import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))


@pytest.fixture
def real_exe() -> bytes:
    folder = os.environ.get("TREK_DATA")
    path = next((p for p in Path(folder).iterdir() if p.name.upper() == "TREK.EXE"), None) if folder else None
    if path is None:
        pytest.skip("TREK_DATA not set or TREK.EXE missing")
    return path.read_bytes()
