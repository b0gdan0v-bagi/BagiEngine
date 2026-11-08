"""worker.py on small repositories made in a temp folder: a game with the submodule `engine`, which has a
nested submodule of its own, as the real game has `engine/external/*`."""

import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import worker  # noqa: E402

GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}


def run(*args: str, cwd: Path) -> str:
    env = {**os.environ, **GIT_ENV}
    return subprocess.run(["git", "-c", "protocol.file.allow=always", *args], cwd=cwd, env=env, check=True, capture_output=True, text=True).stdout


@pytest.fixture
def game(tmp_path: Path, monkeypatch) -> Path:
    for key, value in GIT_ENV.items():
        monkeypatch.setenv(key, value)
    ext = tmp_path / "ext"
    ext.mkdir()
    run("init", "-q", "-b", "main", cwd=ext)
    run("commit", "-q", "--allow-empty", "-m", "ext", cwd=ext)
    eng = tmp_path / "eng"
    eng.mkdir()
    run("init", "-q", "-b", "games/chess", cwd=eng)
    (eng / ".gitignore").write_text(".venv/\n")
    run("add", ".", cwd=eng)
    run("submodule", "add", "-q", "../ext", "external/ext", cwd=eng)
    run("commit", "-q", "-m", "eng", cwd=eng)
    main = tmp_path / "game"
    main.mkdir()
    run("init", "-q", "-b", "main", cwd=main)
    run("submodule", "add", "-q", "-b", "games/chess", "../eng", "engine", cwd=main)
    run("submodule", "update", "-q", "--init", "--recursive", cwd=main)
    (main / ".gitignore").write_text(".env\n.claude/settings.local.json\n")
    run("add", ".", cwd=main)
    run("commit", "-q", "-m", "game", cwd=main)
    (main / ".env").write_text("TODO_API_URL=x\n")
    (main / ".claude").mkdir()
    (main / ".claude" / "settings.local.json").write_text("{}\n")
    (main / "engine" / ".venv" / "bin").mkdir(parents=True)
    monkeypatch.chdir(main)
    return main


def engine_config(main: Path) -> str:
    return (main / ".git" / "modules" / "engine" / "config").read_text()


def test_create_makes_clean_workers_that_share_branches(game: Path):
    before = engine_config(game)
    assert worker.main(["create", "2", "--no-configure"]) == 0
    w1 = game.parent / "game.workers" / "w1"
    assert run("status", "--porcelain", cwd=w1) == ""
    assert run("submodule", "status", "engine", cwd=w1).startswith(" ")
    assert (w1 / "engine" / "external" / "ext" / ".git").exists()
    for rel in (".env", ".claude/settings.local.json", "engine/.venv"):
        assert (w1 / rel).is_symlink(), rel
    run("switch", "-q", "-c", "todo/probe", cwd=w1 / "engine")
    assert "todo/probe" in run("branch", "--list", "todo/*", cwd=game / "engine")
    run("switch", "-q", "-c", "todo/game-probe", cwd=w1)
    assert "todo/game-probe" in run("branch", "--list", "todo/*", cwd=game)
    assert engine_config(game) == before


def test_a_submodule_update_in_a_worker_leaves_the_main_engine_alone(game: Path):
    before = engine_config(game)
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    run("submodule", "update", "--init", "--recursive", cwd=w1)
    assert engine_config(game) == before
    assert run("status", "--porcelain", cwd=game / "engine") == ""
    assert worker.main(["check"]) == 0


def test_check_finds_and_fixes_a_broken_core_worktree(game: Path):
    worker.main(["create", "1", "--no-configure"])
    run("config", "-f", str(game / ".git" / "modules" / "engine" / "config"), "core.worktree", "../../../../elsewhere", cwd=game)
    with pytest.raises(SystemExit):
        worker.main(["check"])
    assert worker.main(["check", "--fix"]) == 0
    assert worker.main(["check"]) == 0
    assert run("status", "--porcelain", cwd=game / "engine") == ""


def test_remove_refuses_a_dirty_worker_and_keeps_its_branches(game: Path):
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    run("switch", "-q", "-c", "todo/kept", cwd=w1 / "engine")
    (w1 / "engine" / "junk").write_text("x")
    assert worker.main(["remove", "1"]) == 1
    (w1 / "engine" / "junk").unlink()
    assert worker.main(["remove", "1"]) == 0
    assert not w1.exists()
    assert "todo/kept" in run("branch", "--list", "todo/*", cwd=game / "engine")
    assert run("worktree", "list", cwd=game).count("\n") == 1


def test_run_print_only_prints(game: Path, capsys):
    worker.main(["create", "1", "--no-configure"])
    assert worker.main(["run", "1", "--print", "--", "--model", "sonnet"]) == 0
    assert "claude --model sonnet" in capsys.readouterr().out
