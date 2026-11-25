"""One command from the sources to the Railway service: configure and build the game for the web, package the output, deploy it.

    python tools/web/build_and_deploy.py --game BagiStress --dry-run             # print the commands, run nothing
    python tools/web/build_and_deploy.py --game BagiStress                       # build and package into dist/
    python tools/web/build_and_deploy.py --game BagiStress --single-thread       # the page without threads (no COOP / COEP needed)
    python tools/web/build_and_deploy.py --game BagiStress --deploy --service bagi-web
    python tools/web/build_and_deploy.py --target mahjong-editor --dry-run       # game, preset and service of web/targets.json

Run it from the root of the project that has the web presets (the engine, or a game that carries it); `--source-dir` names another.
The steps, in order:
  1. cmake --preset <preset>                           (needs the Emscripten SDK of the engine: python CI/scripts/setup_emsdk.py)
  2. cmake --build --preset <preset> --target <game>
  3. package.py build/<preset>/bin <dist> --game <game> (with --single-thread when the page is the single-thread one, and with
     --app <source>/web/app.json when the game has that file: its web app manifest and icons; --app FILE names another)
  4. with --deploy: the deploy gate first (a green full test run of HEAD of the source and no uncommitted change besides dist/ and
     the staging, tools/alltests; --skip-alltests passes it, --dry-run only reports it), then stage Dockerfile, serve.py and
     dist/ in one folder and run `railway up <folder> --path-as-root --service <name> --detach` in it (the Railway CLI, logged in and the project linked with
     `railway link`, or RAILWAY_TOKEN of the project; the service must exist and its settings (builder Dockerfile, health check
     /healthz) come from the game's Railway IaC file, not from the upload: tools/web/CLAUDE.md).
--target NAME takes the game, preset and service from <source>/web/targets.json (deploy_targets.py) and refuses every free option of
the command line (REFUSED_WITH_TARGET); only --deploy, --dry-run, --source-dir, --status-file and --checkout-dir go with it.
Standard library only. Nothing here reads or writes a secret.
"""

import argparse
import contextlib
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR.parent / "alltests"))

import alltests_results  # noqa: E402
import deploy_checkout  # noqa: E402
import deploy_status  # noqa: E402
import deploy_targets  # noqa: E402

PACKAGE_SCRIPT = "package.py"
STAGED_FILES = ("Dockerfile", "serve.py")
DEFAULT_GAME = "BagiStress"
DEFAULT_PRESET = "web-release"
DEFAULT_SERVICE = "bagi-web"
# The Emscripten SDK is the submodule external/emsdk of the engine (CI/scripts/setup_emsdk.py installs it). The presets read the toolchain
# file, and emcc reads .emscripten; setup_emsdk.py --check looks at nearly the same files (this script stays stand-alone and does not
# import it; the version is that script's business)
ENGINE_DIR = TOOLS_DIR.parents[1]
EMSDK_RELATIVE = Path("external") / "emsdk"
EMSDK_DIR = ENGINE_DIR / EMSDK_RELATIVE
SDK_FILES = (Path("upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"), Path("upstream/emscripten/emcc.py"), Path(".emscripten"))
SETUP_EMSDK_SCRIPT = Path("CI") / "scripts" / "setup_emsdk.py"
SDK_MISSING_MESSAGE = "the Emscripten SDK is not installed: run: python CI/scripts/setup_emsdk.py (in a game: python engine/CI/scripts/setup_emsdk.py)"
# The suffix of the presets of the single-thread page (CMakePresets.json: web-release-single-thread, web-debug-single-thread)
SINGLE_THREAD_SUFFIX = "-single-thread"
# The web build writes the page into build/<preset>/bin (cmake/BagiEngine.cmake, "Output Directories"); the single-thread presets have a
# binary dir of their own, because their libraries differ
BIN_FOLDER = "bin"
STAGE_FOLDER = Path("build") / "web-deploy"
DIST_FOLDER = Path("dist")
# The options of a hand run that --target refuses: a target is the whole description of a deploy, and an agent that starts it must not
# be able to add arguments (attribute of the parsed arguments -> the flag)
REFUSED_WITH_TARGET = {
    "game": "--game", "preset": "--preset", "service": "--service", "single_thread": "--single-thread", "build_dir": "--build-dir",
    "app": "--app", "dist": "--dist", "stage_dir": "--stage-dir", "skip_alltests": "--skip-alltests", "alltests_results": "--alltests-results",
}
# The game's own web app settings (package.py --app); the file is optional
APP_FILE = Path("web") / "app.json"


class DeployError(Exception):
    """A precondition is missing; the message says what to do."""


class Step(NamedTuple):
    command: list[str]
    cwd: Path
    # The deploy step: the files it needs are staged just before it
    is_deploy: bool = False
    # The stage of the progress file (deploy_status.py) this step belongs to
    stage: str = "build"


def resolve_target(args: argparse.Namespace, source_dir: Path) -> None:
    """Fills game, preset and service: from the target of --target (refusing the free options), else from the options and defaults."""
    args.target_info = None
    if args.target is None:
        args.game = args.game or DEFAULT_GAME
        args.preset = args.preset or DEFAULT_PRESET
        args.service = args.service or DEFAULT_SERVICE
        return
    refused = [flag for name, flag in REFUSED_WITH_TARGET.items() if getattr(args, name)]
    if refused:
        raise DeployError(f"--target takes no free options (the target holds them), remove: {', '.join(refused)}")
    target = deploy_targets.find(source_dir, args.target)
    args.game, args.preset, args.service = target.game, target.preset, target.service
    args.fetch = True
    args.target_info = target
    print(f"target {target.name}: {target.game}, {target.preset}, {target.service}, {target.address}", flush=True)


def resolve_preset(args: argparse.Namespace) -> str:
    """The preset to run: --preset, with the single-thread suffix when --single-thread says so (a preset that has it already stays)."""
    if args.single_thread and not args.preset.endswith(SINGLE_THREAD_SUFFIX):
        return args.preset + SINGLE_THREAD_SUFFIX
    return args.preset


def output_folder(preset: str) -> Path:
    """The folder (relative to the source) the build of `preset` writes its page to."""
    return Path("build") / preset / BIN_FOLDER


def plan_commands(args: argparse.Namespace, source_dir: Path, dist: Path, stage_dir: Path, tools_dir: Path = TOOLS_DIR,
                  setup_sdk: Path | None = None) -> list[Step]:
    """The steps to run, in order. The staging between step 3 and the deploy is done by stage(). `tools_dir` holds package.py (the
    checkout's own in checkout mode); `setup_sdk` is the script that installs the SDK first, for a checkout that has none."""
    preset = resolve_preset(args)
    build_dir = args.build_dir or source_dir / output_folder(preset)
    package_command = [sys.executable, str(tools_dir / PACKAGE_SCRIPT), str(build_dir), str(dist), "--game", args.game]
    app_file = args.app or (source_dir / APP_FILE if (source_dir / APP_FILE).is_file() else None)
    if app_file:
        package_command += ["--app", str(app_file)]
    if preset.endswith(SINGLE_THREAD_SUFFIX):
        # The manifest says threads: false, so serve.py (and the image) send no COOP / COEP for this page
        package_command.append("--single-thread")
    steps = [Step([sys.executable, str(setup_sdk)], source_dir)] if setup_sdk else []
    steps += [
        Step(["cmake", "--preset", preset], source_dir),
        Step(["cmake", "--build", "--preset", preset, "--target", args.game], source_dir),
        Step(package_command, source_dir, stage="package"),
    ]
    if args.deploy:
        steps.append(Step(["railway", "up", str(stage_dir), "--path-as-root", "--service", args.service, "--detach"], stage_dir, True, "deploy"))
    return steps


def stages_of(args: argparse.Namespace) -> list[str]:
    """The stages of the progress file this run goes through: `fetch` only in checkout mode, `deploy` only with --deploy."""
    stages = ["fetch"] if args.fetch else []
    stages += ["build", "package"]
    if args.deploy:
        stages.append("deploy")
    return stages


def format_command(command: list[str]) -> str:
    """The command as a shell of this OS would take it (only for printing)."""
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def is_sdk_installed(emsdk_dir: Path) -> bool:
    return all((emsdk_dir / name).is_file() for name in SDK_FILES)


def check_environment(args: argparse.Namespace, emsdk_dir: Path, which) -> None:
    if not is_sdk_installed(emsdk_dir):
        raise DeployError(SDK_MISSING_MESSAGE)
    check_railway(args, which)


def check_railway(args: argparse.Namespace, which) -> None:
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


def stage(dist: Path, stage_dir: Path, source_dir: Path, tools_dir: Path = TOOLS_DIR) -> None:
    """Puts what the Dockerfile copies into one folder: the files of tools/web and a fresh copy of dist/, nothing else."""
    empty_stage(stage_dir, source_dir)
    for name in STAGED_FILES:
        shutil.copyfile(tools_dir / name, stage_dir / name)
    shutil.copytree(dist, stage_dir / "dist")


def fail(status: deploy_status.Status, message: str, code: int) -> int:
    print(f"build_and_deploy.py: {message}", file=sys.stderr)
    status.fail(message)
    return code


def run(argv: list[str] | None = None, emsdk_dir: Path = EMSDK_DIR, which=shutil.which, runner=subprocess.run, gate=alltests_results.check,
        checkout=deploy_checkout.prepare) -> int:
    parser = argparse.ArgumentParser(description="Build the game for the web, package it and deploy it to Railway.")
    parser.add_argument("--target", help="a named target of <source>/web/targets.json: game, preset and service come from it, and no free option is allowed")
    parser.add_argument("--game", help=f"the game target (default: {DEFAULT_GAME})")
    parser.add_argument("--preset", help=f"the web preset (default: {DEFAULT_PRESET})")
    parser.add_argument("--source-dir", type=Path, help="the project with CMakePresets.json (default: the current folder)")
    parser.add_argument("--single-thread", action="store_true", help=f"build the page without threads: the preset gets the suffix {SINGLE_THREAD_SUFFIX}, the page is built into build/<preset>/bin")
    parser.add_argument("--build-dir", type=Path, help="the folder the build writes the page to (default: <source>/build/<preset>/bin, e.g. build/web-release/bin)")
    parser.add_argument("--app", type=Path, help=f"the game's web app settings for package.py (default: <source>/{APP_FILE.as_posix()} when it exists)")
    parser.add_argument("--dist", type=Path, help=f"the packaged folder (default: <source>/{DIST_FOLDER.as_posix()})")
    parser.add_argument("--stage-dir", type=Path, help=f"the folder `railway up` runs in (default: <source>/{STAGE_FOLDER.as_posix()})")
    parser.add_argument("--deploy", action="store_true", help="after packaging, run `railway up`")
    parser.add_argument("--service", help=f"the Railway service for --deploy (default: {DEFAULT_SERVICE})")
    parser.add_argument("--dry-run", action="store_true", help="print the commands and run nothing")
    parser.add_argument("--skip-alltests", action="store_true", help="deploy even when HEAD of the source has no green full test run or the tree has uncommitted changes (tools/alltests)")
    parser.add_argument("--alltests-results", type=Path, help="the results file of tools/alltests (default: the one of the source's repository)")
    parser.add_argument("--fetch", action="store_true", help="build in a checkout of its own at origin/<base> (implied by --target)")
    parser.add_argument("--checkout-dir", type=Path, help="the folder of that checkout (default: under ~/.cache/bagi-web-deploy)")
    parser.add_argument("--status-file", type=Path, help="a JSON file rewritten at every stage: the progress of the run")
    args = parser.parse_args(argv)
    source_dir = (args.source_dir or Path.cwd()).resolve()

    try:
        resolve_target(args, source_dir)
    except (deploy_targets.TargetsError, DeployError) as error:
        print(f"build_and_deploy.py: {error}", file=sys.stderr)
        return 2

    target = args.target_info
    status = deploy_status.Status(None if args.dry_run else args.status_file, args.target, stages_of(args), target and target.address, args.service)
    if not args.fetch:
        try:
            check_environment(args, emsdk_dir, which)
        except DeployError as error:
            return fail(status, str(error), 2)
        return build_and_package(args, source_dir, TOOLS_DIR, status, which, runner, gate)

    try:
        check_railway(args, which)
    except DeployError as error:
        return fail(status, str(error), 2)
    base = target.base if target else deploy_targets.DEFAULT_BASE
    status.enter("fetch")
    with contextlib.ExitStack() as held:
        try:
            checkout_dir = args.checkout_dir.resolve() if args.checkout_dir else deploy_checkout.default_checkout_dir(source_dir)
            if not args.dry_run:
                held.enter_context(deploy_checkout.lock(checkout_dir))
            fetched = checkout(source_dir, base, checkout_dir, dry_run=args.dry_run)
        except (deploy_checkout.CheckoutError, OSError) as error:
            return fail(status, f"cannot make the checkout: {error}", 2)
        status.set(commit=fetched.commit, engineCommit=fetched.engine_commit)
        # The SDK of a checkout is installed once, by its first run: a step here, where a hand run refuses and says how
        sdk_script = None if is_sdk_installed(fetched.engine_dir / EMSDK_RELATIVE) else fetched.engine_dir / SETUP_EMSDK_SCRIPT
        return build_and_package(args, fetched.path, fetched.engine_dir / "tools" / "web", status, which, runner, gate, sdk_script,
                                 f"alltests gate: asked after the fetch, on origin/{base}")


def build_and_package(args: argparse.Namespace, project: Path, tools_dir: Path, status: deploy_status.Status, which, runner, gate,
                      setup_sdk: Path | None = None, dry_run_gate_note: str | None = None) -> int:
    """Everything after the project is known: `project` is the folder that is built (the checkout in checkout mode) and `tools_dir` the folder of
    package.py, serve.py and the Dockerfile that travel with it. In checkout mode the gate runs after the fetch, so a dry run says
    so instead of asking it about a checkout that does not exist."""
    dist = args.dist or project / DIST_FOLDER
    stage_dir = args.stage_dir or project / STAGE_FOLDER
    if args.deploy:
        if dry_run_gate_note and args.dry_run:
            print(dry_run_gate_note, flush=True)
        else:
            verdict = gate(project, args.alltests_results, (dist, stage_dir))
            if not alltests_results.allows_deploy(verdict, args.skip_alltests, args.dry_run):
                status.fail(verdict.message)
                return 2

    for step in plan_commands(args, project, dist, stage_dir, tools_dir, setup_sdk):
        program = step.command[0]
        status.enter(step.stage)
        if step.is_deploy:
            print(f"stage {dist} and {', '.join(STAGED_FILES)} into {stage_dir}", flush=True)
            if not args.dry_run:
                try:
                    stage(dist, stage_dir, project, tools_dir)
                except (DeployError, OSError) as error:
                    return fail(status, f"cannot stage {dist} into {stage_dir}: {error}", 2)
        print(f"+ ({step.cwd}) {format_command(step.command)}", flush=True)
        if args.dry_run:
            continue
        # The program the PATH search found, not its bare name: CreateProcess does not apply PATHEXT, so `railway` finds only
        # railway.exe and not the railway.cmd that npm installs
        command = [which(program) or program, *step.command[1:]]
        try:
            completed = runner(command, cwd=step.cwd, check=False)
        except OSError as error:
            return fail(status, f"cannot start `{command[0]}`: {error}", 2)
        if completed.returncode != 0:
            return fail(status, f"`{program}` failed with exit code {completed.returncode}", completed.returncode or 1)
    status.done()
    return 0


if __name__ == "__main__":
    sys.exit(run())
