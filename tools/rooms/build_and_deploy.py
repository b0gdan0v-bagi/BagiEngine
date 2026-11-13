"""Builds the room server for Linux on this machine and deploys the binary to the Railway service, so Railway compiles nothing.

    python3 tools/rooms/build_and_deploy.py --dry-run                                    # print the commands, run nothing
    python3 tools/rooms/build_and_deploy.py                                              # build and stage into build/rooms-deploy/
    python3 tools/rooms/build_and_deploy.py --deploy --service bagi-rooms                # ... and `railway up`
    python3 engine/tools/rooms/build_and_deploy.py --server-target BagiMahjongServer --deploy --service bagi-mahjong-server   # in a game

Run it from the root of the project that holds the server target (the engine, or a game that carries it); `--source-dir` names another.
The steps, in order:
  1. docker build --platform linux/amd64 --target build -f tools/rooms/Dockerfile --build-arg SERVER_TARGET=.. --build-arg PRESET=.. <source>
     (the build stage of the image: the compiler runs in a container, in emulation on an arm machine; --platform is the architecture of Railway)
  2. docker create, docker cp <container>:/out/. <stage>/app, docker rm: the executable `room-server` and its shared libraries are taken out
  3. stage Dockerfile.prebuilt as <stage>/Dockerfile (debian-slim, libstdc++6, COPY, USER nobody, the settings of the real Dockerfile)
  4. with --deploy: `railway up <stage> --path-as-root --service <name> --detach` (the Railway CLI, logged in and the project linked with
     `railway link`, or RAILWAY_TOKEN of the project). The upload is the stage folder only, a few MB.
The service must exist and use the Dockerfile builder; its settings (health check /healthz, variables) are not part of the upload.
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
BUILD_DOCKERFILE = TOOLS_DIR / "Dockerfile"
RUNTIME_DOCKERFILE = TOOLS_DIR / "Dockerfile.prebuilt"
DEFAULT_SERVER_TARGET = "BagiRoomServer"
DEFAULT_PRESET = "linux-server-release"
DEFAULT_SERVICE = "bagi-rooms"
# The architecture Railway runs; an arm machine builds it in emulation
PLATFORM = "linux/amd64"
# The name of the stage of tools/rooms/Dockerfile that compiles and collects /out
BUILD_STAGE = "build"
OUT_FOLDER = "/out"
BINARY_NAME = "room-server"
STAGE_FOLDER = Path("build") / "rooms-deploy"
# The folder of the stage that the runtime Dockerfile copies (Dockerfile.prebuilt: COPY app/ /app/)
APP_FOLDER = "app"
BYTES_PER_MEGABYTE = 1024 * 1024


class DeployError(Exception):
    """A precondition is missing; the message says what to do."""


class Step(NamedTuple):
    command: list[str]
    cwd: Path


def image_tag(server_target: str) -> str:
    return f"bagi-room-build-{server_target.lower()}"


def plan_commands(args: argparse.Namespace, source_dir: Path, stage_dir: Path, container: str) -> list[Step]:
    """The steps to run, in order: build, create, cp, rm, then `railway up` with --deploy. Emptying stage/app before the cp and staging
    the Dockerfile after the rm are done by run()."""
    tag = image_tag(args.server_target)
    steps = [
        Step(["docker", "build", "--platform", PLATFORM, "--target", BUILD_STAGE, "-f", str(BUILD_DOCKERFILE),
              "--build-arg", f"SERVER_TARGET={args.server_target}", "--build-arg", f"PRESET={args.preset}", "-t", tag, str(source_dir)], source_dir),
        Step(["docker", "create", "--platform", PLATFORM, "--name", container, tag], source_dir),
        Step(["docker", "cp", f"{container}:{OUT_FOLDER}/.", str(stage_dir / APP_FOLDER)], source_dir),
        Step(["docker", "rm", "-f", container], source_dir),
    ]
    if args.deploy:
        steps.append(Step(["railway", "up", str(stage_dir), "--path-as-root", "--service", args.service, "--detach"], stage_dir))
    return steps


def format_command(command: list[str]) -> str:
    """The command as a shell of this OS would take it (only for printing)."""
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def check_environment(args: argparse.Namespace, which) -> None:
    if not which("docker"):
        raise DeployError("the build needs Docker (`docker`) on PATH, with a running daemon: https://docs.docker.com/get-docker/")
    if args.deploy and not which("railway"):
        raise DeployError("--deploy needs the Railway CLI (`railway`) on PATH: https://docs.railway.com/guides/cli")


def empty_stage(stage_dir: Path, source_dir: Path) -> None:
    """Starts the staging from an empty folder, so a file of an earlier run does not travel with this one.

    Only a folder inside <source>/build is emptied; any other folder must be absent or empty already, so a wrong --stage-dir
    (the source root, a parent of it, the home folder) never loses a file."""
    stage_dir, build_dir = stage_dir.resolve(), (source_dir / "build").resolve()
    if stage_dir.is_relative_to(build_dir) and stage_dir != build_dir:
        if stage_dir.exists():
            shutil.rmtree(stage_dir)
    elif stage_dir.exists() and (not stage_dir.is_dir() or any(stage_dir.iterdir())):
        raise DeployError(f"will not empty {stage_dir}: it is not a folder inside {build_dir} (use an empty or new folder for --stage-dir)")
    stage_dir.mkdir(parents=True, exist_ok=True)


def prepare_stage(stage_dir: Path, source_dir: Path) -> None:
    """An empty stage with an empty app/, so a file of an older run does not travel with this one."""
    empty_stage(stage_dir, source_dir)
    (stage_dir / APP_FOLDER).mkdir()


def stage_runtime(stage_dir: Path) -> None:
    """Puts the runtime Dockerfile next to app/ after checking that the build produced the executable."""
    if not (stage_dir / APP_FOLDER / BINARY_NAME).is_file():
        raise DeployError(f"the build left no {BINARY_NAME} in {stage_dir / APP_FOLDER}")
    shutil.copyfile(RUNTIME_DOCKERFILE, stage_dir / "Dockerfile")


def stage_size(stage_dir: Path) -> int:
    return sum(path.stat().st_size for path in stage_dir.rglob("*") if path.is_file())


def run_step(step: Step, dry_run: bool, which, runner) -> int:
    """Prints the step and runs it; the exit code (2: it could not be started)."""
    print(f"+ ({step.cwd}) {format_command(step.command)}", flush=True)
    if dry_run:
        return 0
    program = step.command[0]
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
    return completed.returncode


def run(argv: list[str] | None = None, which=shutil.which, runner=subprocess.run) -> int:
    parser = argparse.ArgumentParser(description="Build the room server for Linux in Docker and deploy the binary to Railway.")
    parser.add_argument("--server-target", default=DEFAULT_SERVER_TARGET, help=f"the server target (default: {DEFAULT_SERVER_TARGET})")
    parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"the Linux preset of the build stage (default: {DEFAULT_PRESET}; a game names one that includes the engine's)")
    parser.add_argument("--source-dir", type=Path, help="the build context, the project with CMakePresets.json (default: the current folder)")
    parser.add_argument("--stage-dir", type=Path, help=f"the folder `railway up` uploads (default: <source>/{STAGE_FOLDER.as_posix()})")
    parser.add_argument("--deploy", action="store_true", help="after staging, run `railway up`")
    parser.add_argument("--service", default=DEFAULT_SERVICE, help=f"the Railway service for --deploy (default: {DEFAULT_SERVICE})")
    parser.add_argument("--dry-run", action="store_true", help="print the commands and run nothing")
    args = parser.parse_args(argv)
    source_dir = (args.source_dir or Path.cwd()).resolve()
    stage_dir = (args.stage_dir or source_dir / STAGE_FOLDER).resolve()

    try:
        check_environment(args, which)
    except DeployError as error:
        print(f"build_and_deploy.py: {error}", file=sys.stderr)
        return 2

    build, create, copy, remove, *upload = plan_commands(args, source_dir, stage_dir, f"bagi-room-extract-{os.getpid()}")
    for step in (build, create):
        if code := run_step(step, args.dry_run, which, runner):
            return code
    print(f"empty {stage_dir}, then {stage_dir / APP_FOLDER}", flush=True)
    code = 0
    try:
        if not args.dry_run:
            prepare_stage(stage_dir, source_dir)
        code = run_step(copy, args.dry_run, which, runner)
    except (DeployError, OSError) as error:
        print(f"build_and_deploy.py: cannot prepare {stage_dir}: {error}", file=sys.stderr)
        code = 2
    # The container exists once `docker create` succeeded: it is removed whatever the copy did, and a failure of the removal is only reported
    run_step(remove, args.dry_run, which, runner)
    if code:
        return code

    print(f"stage {RUNTIME_DOCKERFILE.name} as {stage_dir / 'Dockerfile'}", flush=True)
    if not args.dry_run:
        try:
            stage_runtime(stage_dir)
        except (DeployError, OSError) as error:
            print(f"build_and_deploy.py: {error}", file=sys.stderr)
            return 2
        print(f"staged {stage_size(stage_dir) / BYTES_PER_MEGABYTE:.1f} MB in {stage_dir}", flush=True)
    for step in upload:
        if code := run_step(step, args.dry_run, which, runner):
            return code
    return 0


if __name__ == "__main__":
    sys.exit(run())
