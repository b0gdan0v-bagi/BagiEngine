"""A clone with no todo store (tools/todo/CLAUDE.md, "Without the store"): the scripts of this folder, run as programs, end with one
line and start no sign-in, and nothing of the build names them. No store, no network.
Run: .venv/bin/python -m pytest tools/todo/tests"""

import os
import socket
import subprocess
import sys
from pathlib import Path

import pytest

TODO_DIR = Path(__file__).resolve().parents[1]
ENGINE_DIR = TODO_DIR.parents[1]
COMMANDS = (("todo.py", "ping"), ("worker.py", "list"), ("buildslots.py", "status"))
# Below the loader's own time-out for a request, so a call that waits for one fails here
ANSWER_WITHIN_S = 5


def run_without_store(folder: Path, script: str, command: str, **settings: str) -> subprocess.CompletedProcess:
    """The script in an environment that holds no setting, cached copy or sign-in; `settings` are added to it."""
    browser = folder / "browser.py"
    browser.write_text(f"open({str(folder / 'browser-opened')!r}, 'w').close()\n", encoding="utf-8")
    env = {name: value for name, value in os.environ.items() if name not in ("TODO_API_URL", "TODO_API_TOKEN", "BAGITODO_CLI_PIN")}
    env.update(
        HOME=str(folder),
        USERPROFILE=str(folder),
        BAGITODO_CACHE=str(folder / "cache"),
        TODO_LOGIN_FILE=str(folder / "login.json"),
        BROWSER=f'"{sys.executable}" "{browser}" %s',
        NO_PROXY="127.0.0.1",
        no_proxy="127.0.0.1",
        **settings,
    )
    return subprocess.run([sys.executable, str(TODO_DIR / script), command], cwd=folder, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=ANSWER_WITHIN_S)


def the_error_line(done: subprocess.CompletedProcess, folder: Path) -> str:
    lines = done.stderr.strip().splitlines()
    assert (done.returncode, done.stdout) == (1, "") and len(lines) == 1 and lines[0].startswith("error:"), done.stderr
    assert not (folder / "browser-opened").exists() and not (folder / "login.json").exists()
    return lines[0]


@pytest.mark.parametrize("script, command", COMMANDS)
def test_with_no_settings_a_script_names_what_is_missing_and_starts_no_sign_in(tmp_path, script, command):
    assert "TODO_API_URL" in the_error_line(run_without_store(tmp_path, script, command), tmp_path)


@pytest.mark.parametrize("script, command", COMMANDS)
def test_with_a_service_that_does_not_answer_a_script_ends_with_one_line(tmp_path, script, command):
    with socket.socket() as taken:
        taken.bind(("127.0.0.1", 0))
        port = taken.getsockname()[1]
    done = run_without_store(tmp_path, script, command, TODO_API_URL=f"http://127.0.0.1:{port}", TODO_API_TOKEN="dummy")
    assert "did not hand one out" in the_error_line(done, tmp_path)


def test_no_line_of_the_build_names_the_todo_tools():
    files = [ENGINE_DIR / "CMakeLists.txt", ENGINE_DIR / "CMakePresets.json", ENGINE_DIR / "tests" / "CMakeLists.txt",
             *sorted((ENGINE_DIR / "cmake").glob("*.cmake"))]
    named = [f"{path.relative_to(ENGINE_DIR)}:{number}"
             for path in files
             for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
             if "tools/todo" in line and not line.lstrip().startswith("#")]
    assert len(files) > 3 and named == []
