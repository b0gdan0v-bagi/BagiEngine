#!/usr/bin/env python3
"""
Exports a desktop game: configures and builds it in a build folder of the export's own and packages the result
(package_desktop.py). The editor's Export panel starts this script and reads its output; a person can run it too.
The command line, the output lines and the cancel contract: tools/export/CLAUDE.md. Standard library only.

    python3 tools/export/export.py build --project . --target BagiStress --config release
    python3 tools/export/export.py build --exe build/macos-debug/bin/BagiStress --config debug
    python3 tools/export/export.py run --dir dist/BagiStress-release
"""

from __future__ import annotations

import argparse
import os
import platform
import signal
import subprocess
import sys
import threading
import time
import traceback
from collections.abc import Callable
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "dev"))

import dev  # noqa: E402
import package_desktop  # noqa: E402

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_CANCELLED = 3  # 2 is argparse's own: a wrong command line

# The editor reads these: a step is `== step <k>/<n>: <name>`, the last line is one of the three results
RESULT_OK = "== export ok: "
RESULT_FAILED = "== export failed: "
RESULT_CANCELLED = "== export cancelled"

STEPS = ("configure", "build", "package")
CANCEL_WORD = b"cancel"
POLL_SECONDS = 0.05
# From the polite signal to the kill. Whoever cancels this script waits longer than this before killing it, or the build outlives it
KILL_GRACE_SECONDS = 5.0
STDIN_CHUNK = 4096
PRESET_PREFIXES = {"Darwin": "macos-", "Windows": "windows-clang-"}
CACHE_NAME = "CMakeCache.txt"
CACHE_SOURCE_KEY = "CMAKE_HOME_DIRECTORY:INTERNAL="


class ExportError(Exception):
    """The export cannot go on; the message says why."""


class Cancelled(Exception):
    pass


class Cancel:
    """A request to give the export up. A signal handler and the stdin thread only set the flag; the main thread acts on it."""

    def __init__(self):
        self.requested = False

    def request(self, *_signal) -> None:
        self.requested = True

    def check(self) -> None:
        if self.requested:
            raise Cancelled


# Runs a command and returns its exit code; raises Cancelled
Runner = Callable[[list[str], Path, dict, Cancel], int]


def watch_input(cancel: Cancel, descriptor: int) -> None:
    """Requests a cancel on a `cancel` line or at the end of the input. Reads the descriptor itself: a thread blocked in
    `sys.stdin` would hold its lock while the interpreter shuts down."""
    pending = b""
    while True:
        try:
            chunk = os.read(descriptor, STDIN_CHUNK)
        except OSError:
            chunk = b""
        if not chunk:
            break
        *lines, pending = (pending + chunk).split(b"\n")
        if any(line.strip() == CANCEL_WORD for line in lines):
            break
    cancel.request()


# --- processes ---------------------------------------------------------------------------------------------


def _descendants(roots: set[int]) -> set[int]:
    """The processes below `roots` (POSIX)."""
    listing = subprocess.run(["ps", "-A", "-o", "pid=,ppid="], capture_output=True, text=True)
    children: dict[int, list[int]] = {}
    for line in listing.stdout.splitlines():
        fields = line.split()
        if len(fields) == 2:
            children.setdefault(int(fields[1]), []).append(int(fields[0]))
    found: set[int] = set()
    pending = list(roots)
    while pending:
        for child in children.get(pending.pop(), []):
            if child not in found:
                found.add(child)
                pending.append(child)
    return found


def _signal(send: Callable[[int, int], None], target: int, number: int) -> bool:
    """`os.kill` or `os.killpg`; False when nothing is left to take the signal (macOS says EPERM for a group of zombies)."""
    try:
        send(target, number)
    except (ProcessLookupError, PermissionError):
        return False
    return True


def stop_tree(child: subprocess.Popen) -> None:
    """Ends `child` and everything it started, and returns when they are gone or have been killed."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(child.pid)], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        child.wait()
        return
    # ninja gives each compiler a group of its own and ends them when it is told to. Every descendant is listed before the signal,
    # because an orphan can no longer be found from the child, so one that does not obey can still be killed
    tree = _descendants({child.pid})
    _signal(os.killpg, child.pid, signal.SIGTERM)
    deadline = time.monotonic() + KILL_GRACE_SECONDS
    while time.monotonic() < deadline:
        child.poll()  # reaps it: a zombie is still a member of the group
        if not _signal(os.killpg, child.pid, 0) and not any(_signal(os.kill, pid, 0) for pid in tree):
            return
        time.sleep(POLL_SECONDS)
    tree |= _descendants(tree | {child.pid})
    _signal(os.killpg, child.pid, signal.SIGKILL)
    for pid in tree:
        _signal(os.kill, pid, signal.SIGKILL)
    child.wait()


def run_child(command: list[str], cwd: Path, env: dict, cancel: Cancel) -> int:
    """Runs a command with this script's output, in a process group of its own, and stops its whole tree on a cancel."""
    cancel.check()
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {"start_new_session": True}
    sys.stdout.flush()  # the child writes to the same place: what is said before it must be there first
    try:
        # No input: the script's own carries the cancel
        child = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL, **options)
    except FileNotFoundError as error:
        raise ExportError(f"{command[0]} is not on PATH") from error
    try:
        while child.poll() is None:
            cancel.check()
            time.sleep(POLL_SECONDS)
    except BaseException:
        stop_tree(child)
        raise
    cancel.check()
    return child.returncode


# --- the export --------------------------------------------------------------------------------------------


def say(line: str) -> None:
    print(line, flush=True)


def resolve_executable(executable: Path) -> tuple[Path, str]:
    """(project, target) of a game's executable in a build folder: the editor knows only the file it runs from."""
    executable = executable.resolve()
    if not executable.is_file():
        raise ExportError(f"no executable {executable}")
    if (executable.parent / package_desktop.MARKER_NAME).is_file():
        raise ExportError(f"{executable} belongs to an exported package, which cannot export: start the editor from a build of the game")
    for folder in executable.parents:
        cache = folder / CACHE_NAME
        if not cache.is_file():
            continue
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(CACHE_SOURCE_KEY):
                return Path(line.removeprefix(CACHE_SOURCE_KEY)), executable.name.removesuffix(".exe")
        raise ExportError(f"{cache} names no source folder")
    raise ExportError(f"no {CACHE_NAME} above {executable}: start the editor from a build of the game (a package cannot export)")


def preset_for(config: str, system: str) -> str:
    if system not in PRESET_PREFIXES:
        raise ExportError(f"no export preset for {system}: a desktop export is made on macOS or Windows (--preset names one)")
    return PRESET_PREFIXES[system] + config


def build_commands(project: Path, build_dir: Path, preset: str, target: str, system: str) -> list[list[str]]:
    """The commands of the steps `configure` and `build`."""
    # Tests off: nothing of them is exported. -B: never the preset's own folder, which holds the program that may be running this export
    commands = [
        ["cmake", "--preset", preset, "-B", str(build_dir), "-DENGINE_BUILD_TESTS=OFF"],
        ["cmake", "--build", str(build_dir), "--target", target],
    ]
    if system == "Windows":
        # The Ninja presets leave the Visual Studio environment to the caller
        prefix = [sys.executable, str(ENGINE_DIR / "CI" / "scripts" / "vs_env.py")]
        commands = [prefix + command for command in commands]
    return commands


def export(args: argparse.Namespace, cancel: Cancel, runner: Runner, system: str) -> Path:
    """Runs the steps of `build` and returns the package folder."""
    if args.exe:
        project, target = resolve_executable(args.exe)
    else:
        project, target = args.project.resolve(), args.target
    if not project.is_dir():
        raise ExportError(f"no project folder {project}")
    preset = args.preset or preset_for(args.config, system)
    build_dir = project / "build" / "export" / preset
    out = (args.out or project / "dist" / f"{target}-{args.config}").resolve()
    env = dev.tool_env(project)

    def run(command: list[str], cwd: Path) -> int:
        say(f"$ {subprocess.list2cmdline(command)}")
        return 0 if args.dry_run else runner(command, cwd, env, cancel)

    for number, name in enumerate(STEPS, start=1):
        cancel.check()
        say(f"== step {number}/{len(STEPS)}: {name}")
        if name != "package":
            code = run(build_commands(project, build_dir, preset, target, system)[number - 1], project)
            if code != 0:
                raise ExportError(f"{name} failed with exit code {code}")
        elif args.dry_run:
            say(f"would package {target} into {out}")
        else:
            package_desktop.package(build_dir / "export" / f"{target}.json", out, release=args.config == "release", project_dir=project,
                                    runner=run, checkpoint=cancel.check)
    return out


# --- run ---------------------------------------------------------------------------------------------------


def launch(package_dir: Path) -> Path:
    """Starts the game of a package, in its folder and on its own: it lives on after this script."""
    folder = package_dir.resolve()
    marker = folder / package_desktop.MARKER_NAME
    if not marker.is_file():
        raise ExportError(f"{folder} is no exported package (no {package_desktop.MARKER_NAME})")
    lines = marker.read_text(encoding="utf-8").splitlines()
    name = lines[0].strip() if lines else ""
    if not name or Path(name).name != name or not (folder / name).is_file():
        raise ExportError(f"{marker} names no executable of the package: {name!r}")
    options = ({"creationflags": subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt"
               else {"start_new_session": True})
    try:
        subprocess.Popen([str(folder / name), "--game"], cwd=folder, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, **options)
    except OSError as error:
        raise ExportError(f"cannot start {folder / name}: {error}") from error
    return folder


# --- command line ------------------------------------------------------------------------------------------


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export a desktop game into a folder that runs by itself.")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build the game and package it")
    source = build.add_mutually_exclusive_group(required=True)
    source.add_argument("--exe", type=Path, help="the game's executable in a build folder: names the project and the target")
    source.add_argument("--project", type=Path, help="the CMake project of the game (with --target)")
    build.add_argument("--target", help="the game executable's target (with --project)")
    build.add_argument("--config", required=True, choices=("debug", "release"))
    build.add_argument("--out", type=Path, help="the package folder (default <project>/dist/<target>-<config>)")
    build.add_argument("--preset", help="the configure preset (default: the host's Ninja preset of --config)")
    build.add_argument("--cancel-on-stdin-close", action="store_true", help="give up on a `cancel` line or the end of stdin")
    build.add_argument("--dry-run", action="store_true", help="print the commands and the folder; run and write nothing")
    run = commands.add_parser("run", help="start an exported game")
    run.add_argument("--dir", type=Path, required=True, help="the package folder")
    args = parser.parse_args(argv)
    if args.command == "build" and bool(args.target) != bool(args.project):
        build.error("--target goes with --project, and --project needs it")
    return args


def main(argv: list[str] | None = None, *, runner: Runner = run_child, system: str | None = None) -> int:
    args = parse_arguments(argv)
    cancel = Cancel()
    previous = {}
    if args.command == "build":
        previous = {number: signal.signal(number, cancel.request) for number in (signal.SIGINT, signal.SIGTERM)}
        if args.cancel_on_stdin_close:
            threading.Thread(target=watch_input, args=(cancel, 0), daemon=True).start()
    try:
        if args.command == "run":
            folder = launch(args.dir)
        else:
            folder = export(args, cancel, runner, system or platform.system())
    except Cancelled:
        say(RESULT_CANCELLED)
        return EXIT_CANCELLED
    except Exception as error:
        if not isinstance(error, (ExportError, package_desktop.PackageError)):
            traceback.print_exc()
            sys.stderr.flush()
        say(RESULT_FAILED + " ".join(str(error).split()))  # one line, whatever the message holds
        return EXIT_FAILED
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)
    say(RESULT_OK + str(folder))
    return EXIT_OK


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")  # the editor reads UTF-8, whatever the system's code page
    sys.exit(main())
