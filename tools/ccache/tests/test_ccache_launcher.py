"""cmake/BagiCcacheLauncher.py with a fake ccache and a fake compiler, and its agreement with BagiCcacheLauncher.sh."""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

CMAKE_DIR = Path(__file__).resolve().parents[3] / "cmake"
LAUNCHER = CMAKE_DIR / "BagiCcacheLauncher.py"
LAUNCHER_SH = CMAKE_DIR / "BagiCcacheLauncher.sh"

WATCHED = ("CCACHE_SLOPPINESS", "CCACHE_PCH_EXTSUM", "CCACHE_BASEDIR", "CCACHE_NOHASHDIR")

# What a fake program does: log its arguments and the ccache variables, make the file after -o (a compiler only), exit with the
# code the test set for its role.
FAKE_PROGRAM = """\
import json, os, sys
role = {role!r}
with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(json.dumps({{"role": role, "argv": sys.argv[1:], "env": {{k: os.environ.get(k) for k in {watched!r}}}}}) + "\\n")
code = int(os.environ.get("FAKE_" + role.upper() + "_EXIT", "0"))
if role == "compiler" and code == 0 and "-o" in sys.argv:
    index = len(sys.argv) - 1 - sys.argv[::-1].index("-o")
    with open(sys.argv[index + 1], "w") as output:
        output.write("object")
sys.exit(code)
"""


def make_program(directory: Path, role: str) -> str:
    """A program that runs on every OS and is started by its path alone, like the real ccache and compiler."""
    script = directory / f"fake_{role}.py"
    script.write_text(FAKE_PROGRAM.format(role=role, watched=WATCHED), encoding="utf-8")
    if os.name == "nt":
        wrapper = directory / f"fake_{role}.cmd"
        wrapper.write_text(f'@"{sys.executable}" "{script}" %*\n', encoding="utf-8")
    else:
        wrapper = directory / f"fake_{role}"
        wrapper.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{script}" "$@"\n', encoding="utf-8")
        wrapper.chmod(0o755)
    return str(wrapper)


@pytest.fixture
def setup(tmp_path, monkeypatch):
    for name in WATCHED:
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("FAKE_LOG", str(tmp_path / "log.jsonl"))
    for role in ("CCACHE", "COMPILER"):
        monkeypatch.delenv(f"FAKE_{role}_EXIT", raising=False)

    class Setup:
        directory = tmp_path
        ccache = make_program(tmp_path, "ccache")
        compiler = make_program(tmp_path, "compiler")

        @staticmethod
        def run(base_dir, *arguments):
            return subprocess.run([sys.executable, "-S", "-E", str(LAUNCHER), Setup.ccache, base_dir, Setup.compiler, *arguments], check=False)

        @staticmethod
        def calls():
            log = tmp_path / "log.jsonl"
            return [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()] if log.exists() else []

    return Setup


def test_pch_compile_bypasses_ccache_and_writes_the_sum(setup):
    pch = setup.directory / "cmake_pch.hxx.pch"
    result = setup.run("-", "-Xclang", "-emit-pch", "-x", "c++-header", "pch.hxx", "-o", str(pch))

    assert result.returncode == 0
    assert [call["role"] for call in setup.calls()] == ["compiler"]
    assert pch.exists()
    assert Path(str(pch) + ".sum").read_bytes() == b"bagi-pch\n"


def test_pch_sum_follows_the_last_o(setup):
    first = setup.directory / "first.pch"
    second = setup.directory / "second.pch"
    setup.run("-", "-emit-pch", "-o", str(first), "-o", str(second))

    assert Path(str(second) + ".sum").exists()
    assert not Path(str(first) + ".sum").exists()


def test_pch_compile_without_o_writes_no_sum(setup):
    assert setup.run("-", "-emit-pch", "pch.hxx").returncode == 0
    assert not list(setup.directory.glob("*.sum"))


def test_failing_pch_compile_returns_its_code_and_writes_no_sum(setup, monkeypatch):
    monkeypatch.setenv("FAKE_COMPILER_EXIT", "3")
    pch = setup.directory / "bad.pch"
    result = setup.run("-", "-emit-pch", "-o", str(pch))

    assert result.returncode == 3
    assert not Path(str(pch) + ".sum").exists()


def test_compile_goes_through_ccache_with_the_pch_settings(setup):
    result = setup.run("-", "-c", "a.cpp", "-o", "a.o", "-DX=a;b")

    assert result.returncode == 0
    [call] = setup.calls()
    assert call["role"] == "ccache"
    assert call["argv"] == [setup.compiler, "-c", "a.cpp", "-o", "a.o", "-DX=a;b"]
    assert call["env"] == {
        "CCACHE_SLOPPINESS": "pch_defines,time_macros,include_file_mtime,include_file_ctime",
        "CCACHE_PCH_EXTSUM": "1",
        "CCACHE_BASEDIR": None,
        "CCACHE_NOHASHDIR": None,
    }


def test_base_dir_is_given_to_ccache_with_hash_dir_off(setup):
    setup.run("D:/work", "-c", "a.cpp", "-o", "a.o")

    [call] = setup.calls()
    assert call["env"]["CCACHE_BASEDIR"] == "D:/work"
    assert call["env"]["CCACHE_NOHASHDIR"] == "1"


def test_ccache_exit_code_is_the_launchers(setup, monkeypatch):
    monkeypatch.setenv("FAKE_CCACHE_EXIT", "5")

    assert setup.run("-", "-c", "a.cpp", "-o", "a.o").returncode == 5


def test_launcher_and_shell_launcher_hold_the_same_rule():
    python_text = LAUNCHER.read_text(encoding="utf-8")
    shell_text = LAUNCHER_SH.read_text(encoding="utf-8")

    sloppiness = re.search(r'^SLOPPINESS = "([^"]+)"', python_text, re.MULTILINE).group(1)
    assert f"CCACHE_SLOPPINESS={sloppiness}\n" in shell_text
    assert "bagi-pch" in shell_text and "bagi-pch" in python_text
    for variable in ("CCACHE_PCH_EXTSUM", "CCACHE_BASEDIR", "CCACHE_NOHASHDIR"):
        assert variable in shell_text and variable in python_text
    assert "BagiCcacheLauncher.py" in shell_text
    assert "BagiCcacheLauncher.sh" in python_text
