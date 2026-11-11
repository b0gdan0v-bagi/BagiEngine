import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent
FAKE_GAME = TESTS_DIR / "fake_game.py"

sys.path.insert(0, str(TOOL_DIR))


@pytest.fixture
def fake_game_exe(tmp_path):
    """An executable that runs fake_game.py with the flags the session gives it: a script with a shebang, a .bat on Windows."""
    if sys.platform == "win32":
        exe = tmp_path / "FakeGame.bat"
        exe.write_text(f'@"{sys.executable}" "{FAKE_GAME}" %*\r\n@exit /b %ERRORLEVEL%\r\n')
        return exe
    exe = tmp_path / "FakeGame"
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(FAKE_GAME)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    return exe
