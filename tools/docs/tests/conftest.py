import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pytest

FAKE_GAME = Path(__file__).resolve().parents[2] / "control" / "tests" / "fake_game.py"


@pytest.fixture
def fake_bin(tmp_path):
    """A folder with an executable `FakeGame` that runs the control tests' fake game (a script with a shebang)."""
    folder = tmp_path / "bin"
    folder.mkdir()
    if sys.platform == "win32":
        pytest.skip("the fake game is started through a shebang script")
    exe = folder / "FakeGame"
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(FAKE_GAME)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    return folder
