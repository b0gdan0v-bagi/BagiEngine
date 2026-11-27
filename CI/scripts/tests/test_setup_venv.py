"""setup_venv.sh: the steps and the sign-in offer, run on a copy of the script in a temp tree with fake uv, python and todo.py.

The fakes log their arguments as JSON lines into FAKE_LOG; the real uv, pip and venv are never started.
"""

import json
import os
import pty
import subprocess
import sys
from pathlib import Path

import pytest

ENGINE = Path(__file__).resolve().parents[3]
SCRIPT = ENGINE / "CI" / "setup_venv.sh"

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="the shell script is for macOS and Linux")

SECRET = "secret-token-value"

# A fake program is a Python script: it logs its argv and acts by the FAKE_* variables. "venv" makes a venv whose python is
# the same kind of fake.
FAKE = """#!{python}
import json, os, sys
from pathlib import Path

args = sys.argv[1:]
with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(json.dumps([Path(sys.argv[0]).name] + args) + "\\n")

def make_venv(folder):
    python = Path(folder) / "bin" / "python3"
    python.parent.mkdir(parents=True)
    python.write_text(Path(sys.argv[0]).parent.joinpath("fake_venv_python").read_text())
    python.chmod(0o755)

name = Path(sys.argv[0]).name
if name == "uv" and args[:1] == ["venv"]:
    make_venv(args[1])
elif name == "uv" and args[:2] == ["pip", "install"]:
    sys.exit(1 if os.environ.get("FAKE_PIP_FAIL") else 0)
elif name == "python3" and args[:2] == ["-m", "venv"]:
    make_venv(args[2])
"""

FAKE_VENV_PYTHON = """#!{python}
import json, os, sys

with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(json.dumps(["venv-python"] + sys.argv[1:]) + "\\n")
if sys.argv[1:2] == ["-c"]:
    if os.environ.get("FAKE_JINJA_FAIL"):
        sys.exit(1)
    print("3.1.4")
elif sys.argv[1:3] == ["-m", "pip"] and os.environ.get("FAKE_PIP_FAIL"):
    sys.exit(1)
"""

FAKE_TODO = """import json, os, sys
with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(json.dumps(["todo.py"] + sys.argv[1:]) + "\\n")
"""


class Tree:
    """A temp copy of the script's surroundings and a PATH that holds only the fakes."""

    def __init__(self, root: Path, with_uv: bool):
        self.root = root / "repo"
        self.bin = root / "bin"
        self.log = root / "fake.log"
        self.env_file = self.root / ".env"
        (self.root / "CI").mkdir(parents=True)
        (self.root / "tools" / "control").mkdir(parents=True)
        (self.root / "tools" / "todo").mkdir(parents=True)
        self.bin.mkdir()
        (self.root / "CI" / "setup_venv.sh").write_bytes(SCRIPT.read_bytes())
        (self.root / "requirements.txt").write_text("jinja2\n")
        (self.root / "tools" / "control" / "requirements.txt").write_text("mcp\n")
        (self.root / "tools" / "todo" / "todo.py").write_text(FAKE_TODO)
        self._fake("python3")
        if with_uv:
            self._fake("uv")
        (self.bin / "fake_venv_python").write_text(FAKE_VENV_PYTHON.format(python=sys.executable))
        # Only these real tools are reachable, so a uv of the machine can not leak into the no-uv cases.
        for tool in ("bash", "dirname", "grep", "cat"):
            (self.bin / tool).symlink_to(self._real(tool))

    def _fake(self, name: str):
        path = self.bin / name
        path.write_text(FAKE.format(python=sys.executable))
        path.chmod(0o755)

    @staticmethod
    def _real(tool: str) -> str:
        for folder in ("/bin", "/usr/bin"):
            if os.path.exists(f"{folder}/{tool}"):
                return f"{folder}/{tool}"
        raise FileNotFoundError(tool)

    def env(self, **extra: str) -> dict:
        env = {"PATH": str(self.bin), "HOME": str(self.root / "home"), "FAKE_LOG": str(self.log),
               "TODO_LOGIN_FILE": str(self.root / "home" / "login.json")}
        env.update(extra)
        return env

    def run(self, *args: str, **extra: str) -> subprocess.CompletedProcess:
        return subprocess.run(["bash", str(self.root / "CI" / "setup_venv.sh"), *args], env=self.env(**extra),
                              stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60, cwd=self.root.parent)

    def calls(self) -> list:
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def calls_of(self, name: str, *prefix: str) -> list:
        return [c for c in self.calls() if c[0] == name and c[1:1 + len(prefix)] == list(prefix)]


@pytest.fixture
def tree(tmp_path):
    return Tree(tmp_path, with_uv=True)


@pytest.fixture
def tree_without_uv(tmp_path):
    return Tree(tmp_path, with_uv=False)


def test_a_new_venv_with_uv_is_made_once_and_both_requirements_go_in_one_call(tree):
    result = tree.run("--no-login")
    assert result.returncode == 0, result.stdout + result.stderr
    assert len(tree.calls_of("uv", "venv")) == 1
    installs = tree.calls_of("uv", "pip", "install")
    assert len(installs) == 1
    args = installs[0]
    assert args[3] == "--python" and args[4].endswith("repo/.venv/bin/python3")
    assert args[5::2] == ["-r", "-r"]
    assert [Path(a).relative_to(tree.root).as_posix() for a in args[6::2]] == ["requirements.txt", "tools/control/requirements.txt"]
    assert "[OK] reflection generator: jinja2 3.1.4" in result.stdout


def test_a_second_run_keeps_the_venv_and_installs_again(tree):
    assert tree.run("--no-login").returncode == 0
    assert tree.run("--no-login").returncode == 0
    assert len(tree.calls_of("uv", "venv")) == 1
    assert len(tree.calls_of("uv", "pip", "install")) == 2


def test_without_uv_python_makes_the_venv_and_pip_installs(tree_without_uv):
    tree = tree_without_uv
    result = tree.run("--no-login")
    assert result.returncode == 0, result.stdout + result.stderr
    assert len(tree.calls_of("python3", "-m", "venv")) == 1
    assert len(tree.calls_of("venv-python", "-m", "pip", "install")) == 1


def test_without_a_terminal_the_sign_in_command_is_printed_and_not_run(tree):
    result = tree.run()
    assert result.returncode == 0, result.stdout + result.stderr
    assert "login https://<todo address>" in result.stdout
    assert tree.calls_of("todo.py") == []
    assert tree.calls_of("venv-python", str(tree.root / "tools" / "todo" / "todo.py")) == []


def test_the_sign_in_is_skipped_when_it_exists_or_is_not_wanted(tree):
    (tree.root / "home").mkdir()
    (tree.root / "home" / "login.json").write_text("{}")
    assert "[OK] todo: signed in" in tree.run().stdout
    (tree.root / "home" / "login.json").unlink()
    assert "login https://" in tree.run().stdout

    tree.env_file.write_text("TODO_API_URL=x\n")
    assert "[OK] todo: signed in" in tree.run().stdout
    tree.env_file.write_text("TODO_API_URL=\n")
    assert "login https://" in tree.run().stdout
    tree.env_file.unlink()

    assert "[OK] todo: signed in" in tree.run(TODO_API_URL="http://todo.invalid").stdout
    assert "todo:" not in tree.run("--no-login").stdout


def test_a_token_is_never_read_printed_or_passed_on(tree):
    env_bytes = f"TODO_API_URL=\nTODO_API_TOKEN={SECRET}\n".encode()
    tree.env_file.write_bytes(env_bytes)
    result = tree.run(TODO_API_TOKEN=SECRET)
    assert result.returncode == 0, result.stdout + result.stderr
    assert SECRET not in result.stdout + result.stderr
    assert SECRET not in json.dumps(tree.calls())
    assert tree.env_file.read_bytes() == env_bytes


def test_a_failing_install_fails_the_setup(tree):
    result = tree.run("--no-login", FAKE_PIP_FAIL="1")
    assert result.returncode == 1
    assert "[ERROR]" in result.stdout


def test_a_venv_without_jinja2_fails_the_setup(tree):
    result = tree.run("--no-login", FAKE_JINJA_FAIL="1")
    assert result.returncode == 1
    assert "[ERROR]" in result.stdout


def test_the_address_typed_on_a_terminal_goes_to_the_login_command(tree):
    master, slave = pty.openpty()
    process = subprocess.Popen(["bash", str(tree.root / "CI" / "setup_venv.sh")], env=tree.env(), stdin=slave,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=tree.root.parent)
    os.close(slave)
    os.write(master, b"https://todo.example\n")
    try:
        output, _ = process.communicate(timeout=60)
    finally:
        os.close(master)
    assert process.returncode == 0, output
    assert tree.calls_of("venv-python", str(tree.root / "tools" / "todo" / "todo.py"), "login", "https://todo.example")
