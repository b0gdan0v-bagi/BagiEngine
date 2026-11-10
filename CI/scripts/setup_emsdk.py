#!/usr/bin/env python3
"""
Install the Emscripten SDK that the `web-*` presets use into `external/emsdk` (the git submodule of this repository).

    python CI/scripts/setup_emsdk.py            # install and activate the pinned SDK; does nothing when it is installed
    python CI/scripts/setup_emsdk.py --check    # exit 0 when it is installed, 1 when not; no network, no change
    python CI/scripts/setup_emsdk.py --force    # install again

Any working directory does: the script finds the engine root from its own place. In a game that carries the engine as the
submodule `engine/`, run it as `python engine/CI/scripts/setup_emsdk.py`.

The SDK version is the constant `EMSDK_VERSION`; it has to equal the tag the submodule `external/emsdk` is on (the script stops
when it does not). To move to another SDK change the constant and check out the same tag in the submodule.
The SDK is about 2 GB (the compiler, node, python, the sysroot) and is installed once per checkout: a second checkout, or a worker
folder, that builds the web installs its own. Nothing is written outside the checkout: `activate` runs without `--permanent` (no
registry, no shell profile, no administrator rights), and the config file `.emscripten` lands in the emsdk folder. If the
submodule is not initialised in this checkout, the script initialises that one submodule (`git submodule update --init
external/emsdk`, run in the engine root). `git status` stays clean: emsdk's own `.gitignore` covers what the install writes.

The install runs `emsdk.py` with the Python that runs this script (not `emsdk.bat` / `emsdk.sh`), so it is the same on Windows,
macOS and Linux. Standard library only. Exit code: 0 done (or installed, with `--check`), 1 not installed (`--check`) or the install
failed, 2 the submodule or the git tag is wrong.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# The SDK version to install. It is also the name of the tag of external/emsdk (emsdk tags are the SDK versions).
EMSDK_VERSION = "4.0.23"

ENGINE_DIR = Path(__file__).resolve().parents[2]
EMSDK_DIR = ENGINE_DIR / "external" / "emsdk"
SUBMODULE_PATH = "external/emsdk"
# What `emsdk install` unpacks and `emsdk activate` writes; the presets use the toolchain file below it
VERSION_FILE = Path("upstream") / "emscripten" / "emscripten-version.txt"
COMPILER_DRIVER = Path("upstream") / "emscripten" / "emcc.py"
CONFIG_FILE = Path(".emscripten")


class SetupError(Exception):
    """Something is wrong with the submodule or the tag; the message says what to do. The exit code is 2."""


def read_installed_version(emsdk_dir: Path) -> str | None:
    """The version the unpacked SDK reports (the file holds it in quotes, "4.0.23"), None when it is not there."""
    try:
        text = (emsdk_dir / VERSION_FILE).read_text(encoding="utf-8")
    except OSError:
        return None
    return text.strip().strip("\"'")


def is_installed(emsdk_dir: Path, version: str) -> bool:
    """The pinned SDK is unpacked and activated: the version file says so, the compiler driver and the config are there."""
    return read_installed_version(emsdk_dir) == version and (emsdk_dir / COMPILER_DRIVER).is_file() and (emsdk_dir / CONFIG_FILE).is_file()


def install_commands(emsdk_dir: Path, version: str, python: str) -> list[list[str]]:
    """The commands to run, in order. The same on every OS: emsdk.py is run by the Python that runs this script."""
    emsdk = str(emsdk_dir / "emsdk.py")
    return [[python, emsdk, "install", version], [python, emsdk, "activate", version]]


def submodule_commands() -> list[str]:
    """The git command that fetches the submodule; it runs in the engine root, never in a superproject of a game."""
    return ["git", "submodule", "update", "--init", SUBMODULE_PATH]


def is_submodule_present(emsdk_dir: Path) -> bool:
    return (emsdk_dir / "emsdk.py").is_file()


def exact_tag(emsdk_dir: Path, runner=subprocess.run) -> str | None:
    """The tag the checked-out commit of the submodule has, None when it has none (or git cannot say)."""
    try:
        result = runner(["git", "-C", str(emsdk_dir), "describe", "--tags", "--exact-match"], capture_output=True, text=True, check=False)
    except OSError:
        return None
    return result.stdout.strip() if result.returncode == 0 and result.stdout.strip() else None


def check_tag(tag: str | None, version: str) -> None:
    if tag != version:
        found = f"is on {tag}" if tag else "is not on a tag"
        raise SetupError(f"external/emsdk {found}, but EMSDK_VERSION in CI/scripts/setup_emsdk.py is {version}: "
                         f"check out the tag {version} in external/emsdk (git -C external/emsdk checkout {version}) "
                         f"or change the constant to the tag the submodule is on")


def run_step(command: list[str], cwd: Path, runner=subprocess.run) -> int:
    """Runs one command with its output on ours; returns its exit code (127 when it cannot be started)."""
    print(f"+ ({cwd}) {subprocess.list2cmdline(command)}", flush=True)
    try:
        return runner(command, cwd=cwd, check=False).returncode
    except OSError as error:
        print(f"setup_emsdk.py: cannot start `{command[0]}`: {error}", file=sys.stderr)
        return 127


def setup(emsdk_dir: Path, engine_dir: Path, version: str, force: bool, runner=subprocess.run, which=shutil.which, python: str = sys.executable) -> int:
    if not is_submodule_present(emsdk_dir):
        if not which("git"):
            raise SetupError("external/emsdk is empty and git is not on PATH: install git, or run `git submodule update --init external/emsdk`")
        print("external/emsdk is empty: initialising the submodule (network)", flush=True)
        if run_step(submodule_commands(), engine_dir, runner) != 0:
            raise SetupError("`git submodule update --init external/emsdk` failed (network? run it by hand in the engine root and see its message)")
        if not is_submodule_present(emsdk_dir):
            raise SetupError("external/emsdk is still empty after `git submodule update --init external/emsdk`")
    elif not which("git"):
        raise SetupError("git is not on PATH: it is needed to check the tag of external/emsdk")
    check_tag(exact_tag(emsdk_dir, runner), version)

    if is_installed(emsdk_dir, version) and not force:
        print(f"Emscripten SDK {version} is installed in {emsdk_dir}", flush=True)
        return 0

    print(f"Installing Emscripten SDK {version} into {emsdk_dir} (about 2 GB, a few minutes; the download shows its own progress)", flush=True)
    for command in install_commands(emsdk_dir, version, python):
        if run_step(command, emsdk_dir, runner) != 0:
            print(f"setup_emsdk.py: `emsdk.py {command[2]} {version}` failed (network? disk space? run this script again: a finished download is kept)",
                  file=sys.stderr)
            return 1
    if not is_installed(emsdk_dir, version):
        print(f"setup_emsdk.py: emsdk finished, but {emsdk_dir / VERSION_FILE} does not say {version} or {CONFIG_FILE} / "
              f"{COMPILER_DRIVER.as_posix()} is missing", file=sys.stderr)
        return 1
    print(f"Emscripten SDK {version} is installed in {emsdk_dir}", flush=True)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=f"Install the Emscripten SDK {EMSDK_VERSION} into external/emsdk.")
    parser.add_argument("--check", action="store_true", help="exit 0 when the SDK is installed, 1 when not; no network, no change")
    parser.add_argument("--force", action="store_true", help="install again, also when the SDK is installed")
    args = parser.parse_args(argv)

    if args.check:
        if is_installed(EMSDK_DIR, EMSDK_VERSION):
            print(f"Emscripten SDK {EMSDK_VERSION} is installed in {EMSDK_DIR}")
            return 0
        print(f"Emscripten SDK {EMSDK_VERSION} is not installed in {EMSDK_DIR}: run `python CI/scripts/setup_emsdk.py` (the engine root)")
        return 1
    try:
        return setup(EMSDK_DIR, ENGINE_DIR, EMSDK_VERSION, args.force)
    except SetupError as error:
        print(f"setup_emsdk.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
