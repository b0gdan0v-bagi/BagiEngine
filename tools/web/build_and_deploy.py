"""One command from the sources to the Railway service: configure and build the game for the web, package the output, deploy it.

    python tools/web/build_and_deploy.py --game BagiMatch3 --dry-run             # print the commands, run nothing
    python tools/web/build_and_deploy.py --game BagiMatch3                       # build and package into dist/
    python tools/web/build_and_deploy.py --game BagiMatch3 --single-thread       # the page without threads (no COOP / COEP needed)
    python tools/web/build_and_deploy.py --game BagiMatch3 --deploy --service bagi-web

Run it from the root of the project that has the web presets (the engine, or a game that carries it); `--source-dir` names another.
The steps, in order:
  1. cmake --preset <preset>                           (needs the Emscripten SDK of the engine: python CI/scripts/setup_emsdk.py)
  2. cmake --build --preset <preset> --target <game>
  3. package.py build/<preset>/bin <dist> --game <game> (with --single-thread when the page is the single-thread one, and with
     --app <source>/web/app.json when the game has that file: its web app manifest and icons; --app FILE names another)
  4. with --deploy: stage Dockerfile, railway.json, serve.py and dist/ in one folder and run
     `railway up <folder> --path-as-root --service <name> --detach` in it (the Railway CLI, logged in and the project linked with
     `railway link`, or RAILWAY_TOKEN of the project; the service must exist, tools/web/CLAUDE.md).
Standard library only. Nothing here reads or writes a secret.
"""

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

TOOLS_DIR = Path(__file__).resolve().parent
PACKAGE_SCRIPT = TOOLS_DIR / "package.py"
STAGED_FILES = ("Dockerfile", "railway.json", "serve.py")
DEFAULT_GAME = "BagiMatch3"
DEFAULT_PRESET = "web-release"
DEFAULT_SERVICE = "bagi-web"
# The Emscripten SDK is the submodule external/emsdk of the engine (CI/scripts/setup_emsdk.py installs it). The presets read the toolchain
# file, and emcc reads .emscripten; setup_emsdk.py --check looks at nearly the same files (this script stays stand-alone and does not
# import it; the version is that script's business)
ENGINE_DIR = TOOLS_DIR.parents[1]
EMSDK_DIR = ENGINE_DIR / "external" / "emsdk"
SDK_FILES = (Path("upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"), Path("upstream/emscripten/emcc.py"), Path(".emscripten"))
SDK_MISSING_MESSAGE = "the Emscripten SDK is not installed: run: python CI/scripts/setup_emsdk.py (in a game: python engine/CI/scripts/setup_emsdk.py)"
# The suffix of the presets of the single-thread page (CMakePresets.json: web-release-single-thread, web-debug-single-thread)
SINGLE_THREAD_SUFFIX = "-single-thread"
# The web build writes the page into build/<preset>/bin (cmake/BagiEngine.cmake, "Output Directories"); the single-thread presets have a
# binary dir of their own, because their libraries differ
BIN_FOLDER = "bin"
STAGE_FOLDER = Path("build") / "web-deploy"
DIST_FOLDER = Path("dist")
# The game's own web app settings (package.py --app); the file is optional
APP_FILE = Path("web") / "app.json"


class DeployError(Exception):
    """A precondition is missing; the message says what to do."""


class Step(NamedTuple):
    command: list[str]
    cwd: Path
    # The deploy step: the files it needs are staged just before it
    is_deploy: bool = False


def resolve_preset(args: argparse.Namespace) -> str:
    """The preset to run: --preset, with the single-thread suffix when --single-thread says so (a preset that has it already stays)."""
    if args.single_thread and not args.preset.endswith(SINGLE_THREAD_SUFFIX):
        return args.preset + SINGLE_THREAD_SUFFIX
    return args.preset


def output_folder(preset: str) -> Path:
    """The folder (relative to the source) the build of `preset` writes its page to."""
    return Path("build") / preset / BIN_FOLDER


def plan_commands(args: argparse.Namespace, source_dir: Path, dist: Path, stage_dir: Path) -> list[Step]:
    """The steps to run, in order. The staging between step 3 and the deploy is done by stage()."""
    preset = resolve_preset(args)
    build_dir = args.build_dir or source_dir / output_folder(preset)
    package_command = [sys.executable, str(PACKAGE_SCRIPT), str(build_dir), str(dist), "--game", args.game]
    app_file = args.app or (source_dir / APP_FILE if (source_dir / APP_FILE).is_file() else None)
    if app_file:
        package_command += ["--app", str(app_file)]
    if preset.endswith(SINGLE_THREAD_SUFFIX):
        # The manifest says threads: false, so serve.py (and the image) send no COOP / COEP for this page
        package_command.append("--single-thread")
    steps = [
        Step(["cmake", "--preset", preset], source_dir),
        Step(["cmake", "--build", "--preset", preset, "--target", args.game], source_dir),
        Step(package_command, source_dir),
    ]
    if args.deploy:
        steps.append(Step(["railway", "up", str(stage_dir), "--path-as-root", "--service", args.service, "--detach"], stage_dir, True))
    return steps


def format_command(command: list[str]) -> str:
    """The command as a shell of this OS would take it (only for printing)."""
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def is_sdk_installed(emsdk_dir: Path) -> bool:
    return all((emsdk_dir / name).is_file() for name in SDK_FILES)


def check_environment(args: argparse.Namespace, emsdk_dir: Path, which) -> None:
    if not is_sdk_installed(emsdk_dir):
        raise DeployError(SDK_MISSING_MESSAGE)
    if args.deploy and not which("railway"):
        raise DeployError("--deploy needs the Railway CLI (`railway`) on PATH: https://docs.railway.com/guides/cli")


def stage(dist: Path, stage_dir: Path) -> None:
    """Puts what the Dockerfile copies into one folder: the files of tools/web and a fresh copy of dist/."""
    stage_dir.mkdir(parents=True, exist_ok=True)
    for name in STAGED_FILES:
        shutil.copyfile(TOOLS_DIR / name, stage_dir / name)
    staged_dist = stage_dir / "dist"
    if staged_dist.exists():
        shutil.rmtree(staged_dist)
    shutil.copytree(dist, staged_dist)


def run(argv: list[str] | None = None, emsdk_dir: Path = EMSDK_DIR, which=shutil.which, runner=subprocess.run) -> int:
    parser = argparse.ArgumentParser(description="Build the game for the web, package it and deploy it to Railway.")
    parser.add_argument("--game", default=DEFAULT_GAME, help=f"the game target (default: {DEFAULT_GAME})")
    parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"the web preset (default: {DEFAULT_PRESET})")
    parser.add_argument("--source-dir", type=Path, help="the project with CMakePresets.json (default: the current folder)")
    parser.add_argument("--single-thread", action="store_true", help=f"build the page without threads: the preset gets the suffix {SINGLE_THREAD_SUFFIX}, the page is built into build/<preset>/bin")
    parser.add_argument("--build-dir", type=Path, help="the folder the build writes the page to (default: <source>/build/<preset>/bin, e.g. build/web-release/bin)")
    parser.add_argument("--app", type=Path, help=f"the game's web app settings for package.py (default: <source>/{APP_FILE.as_posix()} when it exists)")
    parser.add_argument("--dist", type=Path, help=f"the packaged folder (default: <source>/{DIST_FOLDER.as_posix()})")
    parser.add_argument("--stage-dir", type=Path, help=f"the folder `railway up` runs in (default: <source>/{STAGE_FOLDER.as_posix()})")
    parser.add_argument("--deploy", action="store_true", help="after packaging, run `railway up`")
    parser.add_argument("--service", default=DEFAULT_SERVICE, help=f"the Railway service for --deploy (default: {DEFAULT_SERVICE})")
    parser.add_argument("--dry-run", action="store_true", help="print the commands and run nothing")
    args = parser.parse_args(argv)
    source_dir = (args.source_dir or Path.cwd()).resolve()

    try:
        check_environment(args, emsdk_dir, which)
    except DeployError as error:
        print(f"build_and_deploy.py: {error}", file=sys.stderr)
        return 2

    dist = args.dist or source_dir / DIST_FOLDER
    stage_dir = args.stage_dir or source_dir / STAGE_FOLDER
    for step in plan_commands(args, source_dir, dist, stage_dir):
        program = step.command[0]
        if step.is_deploy:
            print(f"stage {dist} and {', '.join(STAGED_FILES)} into {stage_dir}", flush=True)
            if not args.dry_run:
                try:
                    stage(dist, stage_dir)
                except OSError as error:
                    print(f"build_and_deploy.py: cannot stage {dist} into {stage_dir}: {error}", file=sys.stderr)
                    return 2
        print(f"+ ({step.cwd}) {format_command(step.command)}", flush=True)
        if args.dry_run:
            continue
        # The program the PATH search found, not its bare name: CreateProcess does not apply PATHEXT, so `railway` finds only
        # railway.exe and not the railway.cmd that npm installs
        command = [which(program) or program, *step.command[1:]]
        try:
            completed = runner(command, cwd=step.cwd, check=False)
        except OSError as error:
            print(f"build_and_deploy.py: cannot start `{command[0]}`: {error}", file=sys.stderr)
            return 2
        if completed.returncode != 0:
            print(f"build_and_deploy.py: `{program}` failed with exit code {completed.returncode}", file=sys.stderr)
            return completed.returncode or 1
    return 0


if __name__ == "__main__":
    sys.exit(run())
