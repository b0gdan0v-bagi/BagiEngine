"""One command from the sources to the Railway service: configure and build the game for the web, package the output, deploy it.

    python tools/web/build_and_deploy.py --game BagiMatch3 --dry-run             # print the commands, run nothing
    python tools/web/build_and_deploy.py --game BagiMatch3                       # build and package into dist/
    python tools/web/build_and_deploy.py --game BagiMatch3 --deploy --service bagi-web

Run it from the root of the project that has the web presets (the engine, or a game that carries it); `--source-dir` names another.
The steps, in order:
  1. cmake --preset <preset>                           (needs the Emscripten SDK: the environment variable EMSDK)
  2. cmake --build --preset <preset> --target <game>
  3. package.py <build dir> <dist> --game <game>
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
# Where the web build writes its files: build/<Compiler>Web (cmake/BagiEngine.cmake, "Output Directories"; Emscripten's compiler is Clang)
OUTPUT_FOLDER = Path("build") / "ClangWeb"
STAGE_FOLDER = Path("build") / "web-deploy"
DIST_FOLDER = Path("dist")


class DeployError(Exception):
    """A precondition is missing; the message says what to do."""


class Step(NamedTuple):
    command: list[str]
    cwd: Path
    # The deploy step: the files it needs are staged just before it
    is_deploy: bool = False


def plan_commands(args: argparse.Namespace, source_dir: Path, dist: Path, stage_dir: Path) -> list[Step]:
    """The steps to run, in order. The staging between step 3 and the deploy is done by stage()."""
    build_dir = args.build_dir or source_dir / OUTPUT_FOLDER
    steps = [
        Step(["cmake", "--preset", args.preset], source_dir),
        Step(["cmake", "--build", "--preset", args.preset, "--target", args.game], source_dir),
        Step([sys.executable, str(PACKAGE_SCRIPT), str(build_dir), str(dist), "--game", args.game], source_dir),
    ]
    if args.deploy:
        steps.append(Step(["railway", "up", str(stage_dir), "--path-as-root", "--service", args.service, "--detach"], stage_dir, True))
    return steps


def format_command(command: list[str]) -> str:
    """The command as a shell of this OS would take it (only for printing)."""
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def check_environment(args: argparse.Namespace, environ: dict, which) -> None:
    if not environ.get("EMSDK"):
        raise DeployError("EMSDK is not set: install the Emscripten SDK (emsdk install latest; emsdk activate latest) and run emsdk_env "
                          "in this shell, then try again")
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


def run(argv: list[str] | None = None, environ: dict | None = None, which=shutil.which, runner=subprocess.run) -> int:
    parser = argparse.ArgumentParser(description="Build the game for the web, package it and deploy it to Railway.")
    parser.add_argument("--game", default=DEFAULT_GAME, help=f"the game target (default: {DEFAULT_GAME})")
    parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"the web preset (default: {DEFAULT_PRESET})")
    parser.add_argument("--source-dir", type=Path, help="the project with CMakePresets.json (default: the current folder)")
    parser.add_argument("--build-dir", type=Path, help=f"the folder the build writes the page to (default: <source>/{OUTPUT_FOLDER.as_posix()})")
    parser.add_argument("--dist", type=Path, help=f"the packaged folder (default: <source>/{DIST_FOLDER.as_posix()})")
    parser.add_argument("--stage-dir", type=Path, help=f"the folder `railway up` runs in (default: <source>/{STAGE_FOLDER.as_posix()})")
    parser.add_argument("--deploy", action="store_true", help="after packaging, run `railway up`")
    parser.add_argument("--service", default=DEFAULT_SERVICE, help=f"the Railway service for --deploy (default: {DEFAULT_SERVICE})")
    parser.add_argument("--dry-run", action="store_true", help="print the commands and run nothing")
    args = parser.parse_args(argv)
    environ = os.environ if environ is None else environ
    source_dir = (args.source_dir or Path.cwd()).resolve()

    try:
        check_environment(args, environ, which)
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
