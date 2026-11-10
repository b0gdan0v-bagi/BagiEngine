"""preset_executable (where a preset puts a game) and the two ways of naming the game for GameSession.launch."""

import os
import sys
from pathlib import Path

import pytest

import game_process
from bagi_session import GameSession, SessionError
from game_process import preset_executable

FAKE_GAME = Path(__file__).resolve().parent / "fake_game.py"
EXE_SUFFIX = ".exe" if sys.platform == "win32" else ""


def make_file(path: Path, mtime: float | None = None) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("")
    if mtime is not None:
        os.utime(path, (mtime, mtime))
    return path


@pytest.fixture
def session():
    game = GameSession()
    yield game
    game.close()


def test_the_executable_is_in_the_bin_folder_of_the_preset(tmp_path):
    exe = make_file(tmp_path / "build" / "p" / "bin" / f"G{EXE_SUFFIX}")
    assert preset_executable("p", "G", tmp_path) == exe


def test_a_multi_config_preset_gives_the_newest_config_folder(tmp_path):
    make_file(tmp_path / "build" / "p" / "bin" / "Debug" / f"G{EXE_SUFFIX}", mtime=1_000_000)
    release = make_file(tmp_path / "build" / "p" / "bin" / "Release" / f"G{EXE_SUFFIX}", mtime=2_000_000)
    assert preset_executable("p", "G", tmp_path) == release


def test_a_single_config_file_wins_over_a_config_folder(tmp_path):
    exe = make_file(tmp_path / "build" / "p" / "bin" / f"G{EXE_SUFFIX}")
    make_file(tmp_path / "build" / "p" / "bin" / "Debug" / f"G{EXE_SUFFIX}")
    assert preset_executable("p", "G", tmp_path) == exe


def test_a_missing_executable_gives_the_expected_path_without_failing(tmp_path):
    assert preset_executable("p", "G", tmp_path) == tmp_path / "build" / "p" / "bin" / f"G{EXE_SUFFIX}"
    (tmp_path / "build" / "p" / "bin" / "Debug").mkdir(parents=True)
    assert preset_executable("p", "G", tmp_path) == tmp_path / "build" / "p" / "bin" / f"G{EXE_SUFFIX}"


def test_the_default_root_is_the_repository_of_the_tools():
    assert preset_executable("p", "G") == game_process.REPOSITORY_ROOT / "build" / "p" / "bin" / f"G{EXE_SUFFIX}"
    assert (game_process.REPOSITORY_ROOT / "tools" / "control").is_dir()


@pytest.mark.parametrize("arguments", [{}, {"exe": "x", "preset": "p", "game": "G"}, {"exe": "x", "preset": "p"}, {"preset": "p"}, {"game": "G"}])
def test_launch_wants_the_exe_or_the_preset_with_the_game(session, arguments):
    with pytest.raises(SessionError, match="either by `exe`.*or by `preset` and `game`"):
        session.launch(**arguments)


def test_launch_by_preset_names_the_missing_file(session, monkeypatch, tmp_path):
    monkeypatch.setattr(game_process, "REPOSITORY_ROOT", tmp_path)
    with pytest.raises(SessionError, match=r"no game executable at .*build.*bin.*build the preset 'p'"):
        session.launch(preset="p", game="G")


@pytest.mark.skipif(sys.platform == "win32", reason="the fake game is a script with a shebang here")
def test_launch_by_preset_starts_the_game_of_that_folder(session, monkeypatch, tmp_path):
    exe = tmp_path / "build" / "p" / "bin" / "G"
    exe.parent.mkdir(parents=True)
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(FAKE_GAME)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    monkeypatch.setattr(game_process, "REPOSITORY_ROOT", tmp_path)
    assert session.launch(preset="p", game="G")["activeGame"] == "Fake"
