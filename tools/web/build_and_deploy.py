"""One command from the sources to the Railway service: configure and build the game for the web, package the output, deploy it.

    python tools/web/build_and_deploy.py --game BagiStress --dry-run             # print the commands, run nothing
    python tools/web/build_and_deploy.py --game BagiStress                       # build and package into dist/
    python tools/web/build_and_deploy.py --game BagiStress --single-thread       # the page without threads (no COOP / COEP needed)
    python tools/web/build_and_deploy.py --game BagiStress --deploy --service bagi-web
    python tools/web/build_and_deploy.py --target mahjong-editor --dry-run       # game, preset and service of web/targets.json
    python tools/web/build_and_deploy.py --target engine-demo --deploy           # a page of a site: the package goes up as one tar
    python tools/web/build_and_deploy.py --game BagiStress --preset web-editor   # the editor page, into build/web-editor-dist

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
     A target that names a `site` and a `page` has no Railway step: the deploy gate as above, then the package folder is written as one
     tar and `PUT https://<site>/api/v1/builds/<page>` sends it (site_upload.py) with the token ENGINE_SITE_UPLOAD_TOKEN of the
     environment or of <source>/.env. The token is checked before the fetch and the build, since a build takes minutes; a dry run reads
     none and sends nothing. The site decides who may open the page, so there is no access check here.
An editor page (a web-editor* preset, or a package whose manifest.json says `editor`) can change everything: a web-editor* preset
packages into build/web-editor-dist, --deploy of an editor page needs the service named (--service or the target), and before the
staging `railway variable list` of that service must show Basic auth or sign-in with SIGNIN_ALLOW (serve.editor_door). An answer that
fails or cannot be read is a refusal, no option skips the check, and nothing of the answer is printed.
--target NAME takes the game, preset and service from <source>/web/targets.json (deploy_targets.py) and refuses every free option of
the command line (REFUSED_WITH_TARGET); only --deploy, --dry-run, --source-dir, --status-file and --checkout-dir go with it.
--fetch (implied by --target) builds in a checkout of its own instead of the working copy (deploy_checkout.py): a detached worktree of
the project's repository at origin/<base> under ~/.cache/bagi-web-deploy (BAGI_WEB_DEPLOY_HOME moves it, --checkout-dir names another),
made with a fetch of that one branch and never a push, pull, merge, reset or branch switch. Its package.py, serve.py and Dockerfile are
the ones that travel with the page, a missing SDK is installed there, and with --deploy a commit without a green full test run gets
tools/alltests/alltests.py started here (a hand run without --fetch only says how). --alltests-results does not go with --fetch: that
run records into the default results file.
--status-file FILE is rewritten at every stage (fetch, tests, build, package, deploy) with the state, the commits and the error
(deploy_status.py); `done` at deploy means railway accepted the upload, not that the page is live (for a site page: the site answered
200 and switched it; the file then holds `url` and the site's `answer`). A dry run writes nothing.
package.py gets --commit engine=<sha> --commit game=<sha> when git answers, so the deployed manifest.json names its commits.
tools/web/DEPLOY.md has the whole story.
Standard library only. Nothing here writes a secret; the variables of an editor service are read for that check and dropped, and the
upload token goes into one header of the upload.
"""

import argparse
import contextlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NamedTuple

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR.parent / "alltests"))

import alltests_results  # noqa: E402
import deploy_checkout  # noqa: E402
import deploy_status  # noqa: E402
import deploy_targets  # noqa: E402
import site_upload  # noqa: E402
import serve  # noqa: E402  (the access rule of an editor page is the server's own)

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
# alltests.py: 0 green, 3 red (a result is recorded), 1 an error (nothing is recorded)
ALLTESTS_EXIT_RED = 3
SETUP_EMSDK_SCRIPT = Path("CI") / "scripts" / "setup_emsdk.py"
SDK_MISSING_MESSAGE = "the Emscripten SDK is not installed: run: python CI/scripts/setup_emsdk.py (in a game: python engine/CI/scripts/setup_emsdk.py)"
# The suffix of the presets of the single-thread page (CMakePresets.json: web-release-single-thread, web-debug-single-thread)
SINGLE_THREAD_SUFFIX = "-single-thread"
# The web build writes the page into build/<preset>/bin (cmake/BagiEngine.cmake, "Output Directories"); the single-thread presets have a
# binary dir of their own, because their libraries differ
BIN_FOLDER = "bin"
STAGE_FOLDER = Path("build") / "web-deploy"
DIST_FOLDER = Path("dist")
# The presets of the editor page and its package folder: apart from dist/, so the release package is never overwritten, and inside
# build/, which a game ignores already
EDITOR_PRESET_PREFIX = "web-editor"
EDITOR_DIST_FOLDER = Path("build") / "web-editor-dist"
RAILWAY_VARIABLES_TIMEOUT_SECONDS = 60
# The options of a hand run that --target refuses: a target is the whole description of a deploy, and an agent that starts it must not
# be able to add arguments (attribute of the parsed arguments -> the flag)
REFUSED_WITH_TARGET = {
    "game": "--game", "preset": "--preset", "service": "--service", "single_thread": "--single-thread", "build_dir": "--build-dir",
    "app": "--app", "dist": "--dist", "stage_dir": "--stage-dir", "skip_alltests": "--skip-alltests", "alltests_results": "--alltests-results",
}
# The game's own web app settings (package.py --app); the file is optional
APP_FILE = Path("web") / "app.json"
# What the site says about a switched page (docs/builds.md of the site); nothing else of its answer is printed or recorded
ANSWER_KEYS = ("page", "game", "threads", "editor", "commits", "uploadedAt", "bytes")
# A folder with this file is a package of tools/export (its MARKER_NAME): an export's default output lies in dist/
EXPORT_MARKER_NAME = "bagi-package.txt"


class DeployError(Exception):
    """A precondition is missing; the message says what to do."""


class RefusedSettings(Exception):
    """The variables of a service are settings serve.py does not start with. No message is kept: the server's ones quote values."""


class CheckoutPlan(NamedTuple):
    """What checkout mode adds to a run: the script that installs the SDK of the checkout (None when it has one), the dry run's note on
    the gate and, for the gate that starts the test run itself, `alltests.py` of the checkout's engine and the todo area it files to."""
    setup_sdk: Path | None
    dry_run_gate_note: str
    alltests_script: Path
    area: str


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
    args.site = args.page = None
    # The project that runs the script owns the .env with the upload token, not the fetched checkout
    args.env_dir = source_dir
    # The default service is the page the testers play, so an editor page goes only to a service somebody named
    args.service_named = bool(args.service) or args.target is not None
    if args.target is None:
        args.game = args.game or DEFAULT_GAME
        args.preset = args.preset or DEFAULT_PRESET
        if args.deploy and is_editor_preset(args.preset) and not args.service_named:
            raise DeployError(f"--deploy of an editor page ({args.preset}) needs --service: the default {DEFAULT_SERVICE} is the release page's")
        args.service = args.service or DEFAULT_SERVICE
        return
    refused = [flag for name, flag in REFUSED_WITH_TARGET.items() if getattr(args, name)]
    if refused:
        raise DeployError(f"--target takes no free options (the target holds them), remove: {', '.join(refused)}")
    target = deploy_targets.find(source_dir, args.target)
    args.game, args.preset, args.service = target.game, target.preset, target.service
    args.fetch = True
    args.target_info = target
    if target.kind == deploy_targets.KIND_SITE:
        args.site, args.page = target.site, target.page
        print(f"target {target.name}: {target.game}, {target.preset}, site {target.site}, page {target.page}", flush=True)
        return
    print(f"target {target.name}: {target.game}, {target.preset}, {target.service}, {target.address}", flush=True)


def resolve_preset(args: argparse.Namespace) -> str:
    """The preset to run: --preset, with the single-thread suffix when --single-thread says so (a preset that has it already stays)."""
    if args.single_thread and not args.preset.endswith(SINGLE_THREAD_SUFFIX):
        return args.preset + SINGLE_THREAD_SUFFIX
    return args.preset


def is_editor_preset(preset: str) -> bool:
    return preset.startswith(EDITOR_PRESET_PREFIX)


def output_folder(preset: str) -> Path:
    """The folder (relative to the source) the build of `preset` writes its page to."""
    return Path("build") / preset / BIN_FOLDER


def head_commit(folder: Path) -> str | None:
    """HEAD of the repository whose top folder is `folder`; None when git does not answer, there is no commit yet, or `folder` only sits
    inside another repository (a build folder in a temp folder of someone else's checkout must not name that checkout's commit)."""
    try:
        if alltests_results.git(folder, "rev-parse", "--show-cdup"):
            return None
        return alltests_results.git(folder, "rev-parse", "--verify", "--quiet", "HEAD^{commit}")
    except (alltests_results.GitError, OSError):
        return None


def commit_arguments(project: Path) -> list[str]:
    """`--commit engine=<sha> --commit game=<sha>` for package.py: the engine is `<project>/engine` of a game, else the project itself."""
    engine_dir = project / "engine" if (project / "engine" / "tools" / "web" / PACKAGE_SCRIPT).is_file() else project
    arguments = []
    for name, folder in (("engine", engine_dir), ("game", project)):
        commit = head_commit(folder)
        if commit:
            arguments += ["--commit", f"{name}={commit}"]
    return arguments


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
    package_command += commit_arguments(source_dir)
    if preset.endswith(SINGLE_THREAD_SUFFIX):
        # The manifest says threads: false, so serve.py (and the image) send no COOP / COEP for this page
        package_command.append("--single-thread")
    steps = [Step([sys.executable, str(setup_sdk)], source_dir)] if setup_sdk else []
    steps += [
        Step(["cmake", "--preset", preset], source_dir),
        Step(["cmake", "--build", "--preset", preset, "--target", args.game], source_dir),
        Step(package_command, source_dir, stage="package"),
    ]
    if args.deploy and not args.site:
        steps.append(Step(["railway", "up", str(stage_dir), "--path-as-root", "--service", args.service, "--detach"], stage_dir, True, "deploy"))
    return steps


def stages_of(args: argparse.Namespace) -> list[str]:
    """The stages of the progress file this run goes through: `fetch` only in checkout mode, `tests` only there with --deploy (the gate
    may run the test run), `deploy` only with --deploy."""
    stages = ["fetch"] if args.fetch else []
    if args.fetch and args.deploy and not args.skip_alltests:
        stages.append("tests")
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
    if args.deploy and not args.site and not which("railway"):
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


def export_packages(folder: str, names: list[str]) -> list[str]:
    """The names of `folder` that are export packages (the `ignore` of the staging)."""
    return [name for name in names if (Path(folder) / name / EXPORT_MARKER_NAME).is_file()]


def stage(dist: Path, stage_dir: Path, source_dir: Path, tools_dir: Path = TOOLS_DIR) -> None:
    """Puts what the Dockerfile copies into one folder: the files of tools/web and a fresh copy of dist/ without the export
    packages inside it, nothing else."""
    empty_stage(stage_dir, source_dir)
    for name in STAGED_FILES:
        shutil.copyfile(tools_dir / name, stage_dir / name)
    shutil.copytree(dist, stage_dir / "dist", ignore=export_packages)


def refuse_settings(_message: str):
    raise RefusedSettings


def editor_access(service: str, stage_dir: Path, which, runner) -> str:
    """The door of `service` for an editor page, by its variables and the rule of serve.py; DeployError when it has none or the answer
    of the Railway CLI fails or cannot be read. The answer holds the secrets of the service: no part of it goes into a message."""
    # Where `railway up` will run, so both resolve the same linked project; the staging may not exist yet, and its parent resolves alike
    cwd = next(folder for folder in (stage_dir.resolve(), *stage_dir.resolve().parents) if folder.is_dir())
    command = ["railway", "variable", "list", "--service", service, "--json"]
    print(f"+ ({cwd}) {format_command(command)}", flush=True)
    unread = f"cannot check the access of the service {service}, so the editor page is not deployed"
    try:
        completed = runner([which(command[0]) or command[0], *command[1:]], cwd=cwd, check=False, capture_output=True,
                           timeout=RAILWAY_VARIABLES_TIMEOUT_SECONDS)
    except (OSError, subprocess.SubprocessError) as error:
        raise DeployError(f"{unread}: `railway variable list` did not run to its end ({type(error).__name__})") from None
    if completed.returncode != 0:
        raise DeployError(f"{unread}: `railway variable list` failed with exit code {completed.returncode} (run it by hand to see why)")
    try:
        variables = json.loads(completed.stdout)
    except ValueError:
        variables = None
    if not isinstance(variables, dict) or not all(isinstance(value, str) for value in variables.values()):
        raise DeployError(f"{unread}: the answer of `railway variable list` is not a JSON object of strings")
    try:
        basic_auth = serve.basic_auth_from(variables, refuse_settings)
        door = serve.editor_door(basic_auth, serve.sign_in_from(variables, serve.DEFAULT_HOST, serve.DEFAULT_PORT, basic_auth, refuse_settings))
    except (RefusedSettings, ValueError):
        # ValueError: an address urlsplit rejects, in a message that quotes it
        door = None
    if door is None:
        raise DeployError(f"the service {service} has no access control an editor page may be published behind: set "
                          f"{serve.BASIC_AUTH_USER_VARIABLE} and {serve.BASIC_AUTH_PASSWORD_VARIABLE}, or a complete sign-in with "
                          f"{serve.SIGNIN_ALLOW_VARIABLE} (tools/web/ACCESS.md); nothing was staged or deployed")
    return door


def page_url(args: argparse.Namespace) -> str | None:
    return f"https://{args.site}/{args.page}/" if args.site else None


def preflight_token(args: argparse.Namespace) -> None:
    """A real deploy of a site page needs the upload token, and a build takes minutes: look for it first. The value is read and dropped."""
    if args.site and args.deploy and not args.dry_run:
        site_upload.read_token(args.env_dir)


def deploy_site(args: argparse.Namespace, dist: Path, status: deploy_status.Status, upload) -> int | None:
    """The deploy stage of a site page: the package folder as one tar in a temp folder (removed in every case), the sha256, the upload.
    None on success (the caller ends the status), else the exit code. A dry run says what would go where and reads no token."""
    status.enter("deploy")
    endpoint = f"https://{args.site}{site_upload.BUILDS_PATH}{args.page}"
    if args.dry_run:
        print(f"deploy: the files of {dist} would be written as one tar and sent: PUT {endpoint} (the count, bytes and sha256 are known after the build)", flush=True)
        print(f"deploy: the token would be read from {site_upload.ENV_VARIABLE} of the environment or of {args.env_dir / site_upload.ENV_FILE}; nothing is sent", flush=True)
        return None
    with tempfile.TemporaryDirectory(prefix="bagi-site-upload-") as folder:
        tar_path = Path(folder) / "package.tar"
        try:
            count = site_upload.build_tar(dist, tar_path)
            size = tar_path.stat().st_size
            sha256 = site_upload.file_sha256(tar_path)
            print(f"package: {count} files, {size} bytes, sha256 {sha256}", flush=True)
            print(f"PUT {endpoint}", flush=True)
            answer = upload(args.site, args.page, tar_path, sha256, site_upload.read_token(args.env_dir))
        except (site_upload.UploadError, OSError) as error:
            return fail(status, str(error), 2)
    shown = {key: answer[key] for key in ANSWER_KEYS if key in answer}
    status.set(answer=shown)
    print(f"the site answered 200: {json.dumps(shown, sort_keys=True)}", flush=True)
    print(f"page: {page_url(args)}", flush=True)
    return None


def fail(status: deploy_status.Status, message: str, code: int) -> int:
    print(f"build_and_deploy.py: {message}", file=sys.stderr)
    status.fail(message)
    return code


def run(argv: list[str] | None = None, emsdk_dir: Path = EMSDK_DIR, which=shutil.which, runner=subprocess.run, gate=alltests_results.check,
        checkout=deploy_checkout.prepare, changes=alltests_results.uncommitted, upload=site_upload.upload) -> int:
    parser = argparse.ArgumentParser(description="Build the game for the web, package it and deploy it to Railway.")
    parser.add_argument("--target", help="a named target of <source>/web/targets.json: game, preset and service come from it, and no free option is allowed")
    parser.add_argument("--game", help=f"the game target (default: {DEFAULT_GAME})")
    parser.add_argument("--preset", help=f"the web preset (default: {DEFAULT_PRESET})")
    parser.add_argument("--source-dir", type=Path, help="the project with CMakePresets.json (default: the current folder)")
    parser.add_argument("--single-thread", action="store_true", help=f"build the page without threads: the preset gets the suffix {SINGLE_THREAD_SUFFIX}, the page is built into build/<preset>/bin")
    parser.add_argument("--build-dir", type=Path, help="the folder the build writes the page to (default: <source>/build/<preset>/bin, e.g. build/web-release/bin)")
    parser.add_argument("--app", type=Path, help=f"the game's web app settings for package.py (default: <source>/{APP_FILE.as_posix()} when it exists)")
    parser.add_argument("--dist", type=Path, help=f"the packaged folder (default: <source>/{DIST_FOLDER.as_posix()}, of a {EDITOR_PRESET_PREFIX}* preset <source>/{EDITOR_DIST_FOLDER.as_posix()})")
    parser.add_argument("--stage-dir", type=Path, help=f"the folder `railway up` runs in (default: <source>/{STAGE_FOLDER.as_posix()})")
    parser.add_argument("--deploy", action="store_true", help="after packaging, run `railway up`")
    parser.add_argument("--service", help=f"the Railway service for --deploy (default: {DEFAULT_SERVICE}; an editor page has no default)")
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
    status = deploy_status.Status(None if args.dry_run else args.status_file, args.target, stages_of(args), target and (target.address or target.site), args.service,
                                  url=page_url(args))
    try:
        preflight_token(args)
    except site_upload.UploadError as error:
        return fail(status, str(error), 2)
    if not args.fetch:
        try:
            check_environment(args, emsdk_dir, which)
        except DeployError as error:
            return fail(status, str(error), 2)
        return build_and_package(args, source_dir, TOOLS_DIR, status, which, runner, gate, upload=upload)

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
        plan = CheckoutPlan(None if is_sdk_installed(fetched.engine_dir / EMSDK_RELATIVE) else fetched.engine_dir / SETUP_EMSDK_SCRIPT,
                            f"alltests gate: asked after the fetch, on origin/{base}", fetched.engine_dir / alltests_results.SCRIPT,
                            target.area if target else deploy_targets.DEFAULT_AREA)
        return build_and_package(args, fetched.path, fetched.engine_dir / "tools" / "web", status, which, runner, gate, plan, changes, upload)


def recorded_result(results_file: Path | None, project: Path, commit: str) -> dict | None:
    """The record of `commit` in the results file of tools/alltests (None: there is none, or the file cannot be read)."""
    try:
        results_file = results_file or alltests_results.default_results_file(project)
        return alltests_results.load(results_file).get("results", {}).get(commit)
    except (alltests_results.GitError, OSError, ValueError):
        return None


def gate_with_tests(args: argparse.Namespace, project: Path, dist: Path, stage_dir: Path, status: deploy_status.Status, runner, gate,
                    plan: CheckoutPlan, changes) -> int | None:
    """The deploy gate of checkout mode: a commit without a green record gets the full test run started here, and the deploy goes on
    only when the record is green. None: go on; else the exit code. A tree with uncommitted changes is refused without a run (the
    checkout is clean after the fetch, so that is a bug), and the message carries no --skip-alltests hint: this mode refuses it."""
    status.enter("tests")
    # Before the run: a result of the full run is of a commit, and the build takes the tree, so a dirty tree gets no run at all
    try:
        changed = changes(project, (dist, stage_dir))
    except alltests_results.GitError as error:
        return fail(status, f"cannot read the working tree of {project}: {error}", 2)
    if changed:
        shown = "; ".join(changed[:alltests_results.CHANGES_SHOWN])
        more = f" and {len(changed) - alltests_results.CHANGES_SHOWN} more" if len(changed) > alltests_results.CHANGES_SHOWN else ""
        return fail(status, f"{project} has uncommitted changes ({shown}{more}), and the deploy builds the working tree", 2)
    verdict = gate(project, args.alltests_results, (dist, stage_dir))
    record = recorded_result(args.alltests_results, project, verdict.commit) if verdict.commit else None
    if not verdict.green and verdict.commit and (record is None or record.get("status") != alltests_results.GREEN):
        command = [sys.executable, str(plan.alltests_script), "--repo", str(project), "--commit", verdict.commit, "--area", plan.area]
        print(f"alltests gate: commit {verdict.commit[:12]} has no green full test run: starting it", flush=True)
        print(f"+ ({project}) {format_command(command)}", flush=True)
        try:
            completed = runner(command, cwd=project, check=False)
        except OSError as error:
            return fail(status, f"cannot start `{command[0]}`: {error}", 2)
        if completed.returncode not in (0, ALLTESTS_EXIT_RED):
            return fail(status, f"alltests.py failed with exit code {completed.returncode} and recorded nothing: see its output above", 2)
        verdict = gate(project, args.alltests_results, (dist, stage_dir))
        record = recorded_result(args.alltests_results, project, verdict.commit)
    if verdict.green:
        print(f"alltests gate: {verdict.message}", flush=True)
        return None
    if record and record.get("status") == alltests_results.RED:
        failed = record.get("failed", [])
        status.set(failedTests=failed)
        return fail(status, f"alltests is red for commit {verdict.commit[:12]} (failed: {', '.join(failed) or 'unknown'})", 2)
    return fail(status, verdict.message, 2)


def build_and_package(args: argparse.Namespace, project: Path, tools_dir: Path, status: deploy_status.Status, which, runner, gate,
                      plan: CheckoutPlan | None = None, changes=alltests_results.uncommitted, upload=site_upload.upload) -> int:
    """Everything after the project is known: `project` is the folder that is built (the checkout in checkout mode) and `tools_dir` the folder of
    package.py, serve.py and the Dockerfile that travel with it. In checkout mode the gate runs after the fetch, so a dry run says
    so instead of asking it about a checkout that does not exist."""
    editor_preset = is_editor_preset(resolve_preset(args))
    dist = args.dist or project / (EDITOR_DIST_FOLDER if editor_preset else DIST_FOLDER)
    stage_dir = args.stage_dir or project / STAGE_FOLDER
    if args.deploy:
        if plan and not args.skip_alltests and not args.dry_run:
            code = gate_with_tests(args, project, dist, stage_dir, status, runner, gate, plan, changes)
            if code is not None:
                return code
        elif plan and not args.skip_alltests:
            print(plan.dry_run_gate_note, flush=True)
        else:
            verdict = gate(project, args.alltests_results, (dist, stage_dir))
            if not alltests_results.allows_deploy(verdict, args.skip_alltests, args.dry_run):
                status.fail(verdict.message)
                return 2

    for step in plan_commands(args, project, dist, stage_dir, tools_dir, plan and plan.setup_sdk):
        program = step.command[0]
        status.enter(step.stage)
        if step.is_deploy:
            if args.dry_run:
                if editor_preset:
                    print(f"editor page: before the staging, the access of the service {args.service} would be checked "
                          "(`railway variable list`)", flush=True)
            # The manifest says what was packaged (--build-dir may point anywhere); the preset counts too, in case a package says less
            elif editor_preset or serve.manifest_editor(dist):
                if not args.service_named:
                    return fail(status, f"{dist} holds an editor page: --deploy of it needs --service", 2)
                try:
                    door = editor_access(args.service, step.cwd, which, runner)
                except DeployError as error:
                    return fail(status, str(error), 2)
                print(f"editor page: the service {args.service} is behind {door}", flush=True)
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
    if args.site and args.deploy:
        code = deploy_site(args, dist, status, upload)
        if code is not None:
            return code
    status.done()
    return 0


if __name__ == "__main__":
    sys.exit(run())
