import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent
CONTROL_DIR = TOOL_DIR.parent / "control"

sys.path.insert(0, str(CONTROL_DIR))
sys.path.insert(0, str(TOOL_DIR))

QA_FAKE_GAME = TESTS_DIR / "qa_fake_game.py"


@pytest.fixture
def qa_game_exe(tmp_path):
    """An executable that runs qa_fake_game.py (the knobs are environment variables a test sets before the launch): a script with a shebang, a .bat on Windows."""
    if sys.platform == "win32":
        exe = tmp_path / "QaFakeGame.bat"
        exe.write_text(f'@"{sys.executable}" "{QA_FAKE_GAME}" %*\r\n@exit /b %ERRORLEVEL%\r\n')
        return exe
    exe = tmp_path / "QaFakeGame"
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(QA_FAKE_GAME)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    return exe


@pytest.fixture
def out_dir(tmp_path):
    return tmp_path / "out"
