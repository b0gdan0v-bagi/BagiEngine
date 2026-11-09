#!/usr/bin/env python3
"""
Run a command inside the x64 Visual Studio developer environment (Windows only).

    python engine/CI/scripts/vs_env.py cmake --preset windows-clang-debug
    python engine/CI/scripts/vs_env.py cmake --build --preset windows-clang-debug

The Ninja presets on Windows (`windows-clang-*`, `windows-msvc-*`) leave the Visual Studio environment to the
caller (`vs-env` in CMakePresets.json, strategy `external`): CMake Tools loads it, a plain shell does not have it.
Without it a configure finds no `rc` (the resource compiler of the Windows SDK) and a build no `lib` (the archiver).
This script finds Visual Studio with vswhere, runs `vcvarsall.bat x64` in cmd, takes the variables it sets and
runs the command with them. PATH is extended, not replaced: the caller's entries stay first (Git Bash, the venv,
the clang and cmake the caller picked), the entries of Visual Studio go after them.

When the environment is already there (a developer prompt: `VSCMD_VER` is set), the command runs as it is.
Standard library only. Exit code: the command's, or 2 when Visual Studio or vcvarsall is not found.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
VC_TOOLS_COMPONENT = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
ARCH = "x64"
# Marks the start of `set` output, so a banner or a warning of vcvarsall is not read as a variable.
MARKER = "__VS_ENV_BEGIN__"


def find_vcvarsall() -> Path | None:
    if not VSWHERE.is_file():
        return None
    result = subprocess.run(
        [str(VSWHERE), "-latest", "-products", "*", "-requires", VC_TOOLS_COMPONENT, "-property", "installationPath"],
        capture_output=True,
        text=True,
    )
    install = result.stdout.strip().splitlines()
    if result.returncode != 0 or not install:
        return None
    vcvarsall = Path(install[0]) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    return vcvarsall if vcvarsall.is_file() else None


def read_vs_environment(vcvarsall: Path) -> dict[str, str] | None:
    # One string for cmd, so the quotes around the path survive (subprocess would escape them in a list).
    command = f'cmd /d /s /c ""{vcvarsall}" {ARCH} >nul && echo {MARKER} && set"'
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    if result.returncode != 0 or MARKER not in result.stdout:
        return None
    env: dict[str, str] = {}
    for line in result.stdout.split(MARKER, 1)[1].splitlines():
        name, sep, value = line.partition("=")
        if sep and name:
            env[name] = value
    return env


def merge_path(caller: str, vs: str) -> str:
    """The caller's PATH first, then the entries vcvarsall added (compared without case and trailing slashes)."""
    seen = {entry.rstrip("\\/").lower() for entry in caller.split(os.pathsep) if entry}
    added = []
    for entry in vs.split(os.pathsep):
        key = entry.rstrip("\\/").lower()
        if entry and key not in seen:
            seen.add(key)
            added.append(entry)
    return os.pathsep.join([caller, *added]) if added else caller


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    command = sys.argv[1:]
    env = dict(os.environ)
    if sys.platform != "win32" or env.get("VSCMD_VER"):
        return subprocess.run(command).returncode

    vcvarsall = find_vcvarsall()
    if vcvarsall is None:
        print(f"[vs_env] Visual Studio with the C++ tools ({VC_TOOLS_COMPONENT}) not found by {VSWHERE}", file=sys.stderr)
        return 2
    vs = read_vs_environment(vcvarsall)
    if vs is None:
        print(f"[vs_env] {vcvarsall} {ARCH} failed", file=sys.stderr)
        return 2

    caller_path = env.get("PATH", "")
    # Windows variable names do not depend on case; drop the caller's spelling so the merged one is the only one.
    by_upper = {name.upper(): name for name in env}
    for name, value in vs.items():
        env.pop(by_upper.get(name.upper(), name), None)
        env[name] = value
    path_name = next((name for name in vs if name.upper() == "PATH"), "PATH")
    env[path_name] = merge_path(caller_path, vs.get(path_name, ""))

    executable = shutil.which(command[0], path=env[path_name]) or command[0]
    return subprocess.run([executable, *command[1:]], env=env).returncode


if __name__ == "__main__":
    sys.exit(main())
