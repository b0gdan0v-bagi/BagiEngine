"""The checkout of its own that `build_and_deploy.py --fetch` builds from: a detached git worktree of the project's repository at
`origin/<base>`, so the page and the server of one commit travel together and the owner's working copy is never touched.

    <home>/<repository>/checkout       the worktree (<home> is ~/.cache/bagi-web-deploy, BAGI_WEB_DEPLOY_HOME moves it)
    <home>/<repository>/deploy.lock    one run at a time: the build folder and the SDK are shared by every run

`prepare` runs only these git commands (each printed before it runs): `fetch` of the one branch into `refs/remotes/origin/<base>`,
`rev-parse`, `worktree add` or `checkout --force --detach`, `clean -fdq`, `submodule sync` and `submodule update --init`. Never a
push, a pull, a merge, a reset, a branch switch, `clean -x` or `--remote`: a submodule stays at the commit the superproject records.
Ignored files (`build/`, `.venv`, the SDK) survive `clean`, which is what makes the second run fast. Standard library only.
"""

import contextlib
import os
import shlex
import sys
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import NamedTuple

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR.parent / "alltests"))

import alltests  # noqa: E402
import alltests_results  # noqa: E402

HOME_VARIABLE = "BAGI_WEB_DEPLOY_HOME"
DEFAULT_HOME = Path.home() / ".cache" / "bagi-web-deploy"
CHECKOUT_FOLDER = "checkout"
LOCK_FILE = "deploy.lock"
# The folder of the engine inside a game, and the file that tells an engine folder from any other
ENGINE_SUBMODULE = "engine"
ENGINE_MARKER = Path("tools") / "web" / "package.py"
# What the dry run prints where the commit is not known yet
DRY_RUN_COMMIT = "<origin/{base}>"


class CheckoutError(Exception):
    """The checkout could not be made; the message says why and names the folder."""


class Checkout(NamedTuple):
    path: Path
    commit: str
    engine_dir: Path
    engine_commit: str


def home() -> Path:
    value = os.environ.get(HOME_VARIABLE)
    return Path(value).expanduser() if value else DEFAULT_HOME


def default_checkout_dir(project: Path) -> Path:
    """<home>/<repository>/checkout; every worktree of one repository shares it, as it shares the alltests results."""
    try:
        return home() / alltests_results.repo_key(project) / CHECKOUT_FOLDER
    except alltests_results.GitError as error:
        raise CheckoutError(f"{project} is not a git repository: {error}") from error


def lock_path(checkout_dir: Path) -> Path:
    return checkout_dir.parent / LOCK_FILE


@contextlib.contextmanager
def lock(checkout_dir: Path) -> Iterator[None]:
    """Holds the lock of the checkout for the whole run; a second run at the same time is refused with the pid of the first."""
    held = alltests.Lock(lock_path(checkout_dir))
    try:
        held.__enter__()
    except (alltests.RunError, OSError, ValueError) as error:
        raise CheckoutError(str(error)) from error
    try:
        yield
    finally:
        held.__exit__(None, None, None)


def engine_dir_of(checkout: Path) -> Path:
    """`<checkout>/engine` in a game, the checkout itself in the engine."""
    own = checkout / ENGINE_SUBMODULE
    return own if (own / ENGINE_MARKER).is_file() else checkout


def prepare(project: Path, base: str, checkout_dir: Path, *, dry_run: bool, git: Callable[..., str] = alltests_results.git) -> Checkout:
    """Fetches `origin/<base>` into the repository of `project` and makes `checkout_dir` a detached worktree at that commit with its
    submodules initialised at the commits it records. A dry run prints the commands and runs none."""
    try:
        return _prepare(project, base, checkout_dir, dry_run, git)
    except alltests_results.GitError as error:
        raise CheckoutError(str(error)) from error


def _prepare(project: Path, base: str, checkout_dir: Path, dry_run: bool, git: Callable[..., str]) -> Checkout:
    def step(repo: Path, *args: str) -> str:
        print(f"+ git -C {repo} {shlex.join(args)}", flush=True)
        return "" if dry_run else git(repo, *args)

    remote_ref = f"refs/remotes/origin/{base}"
    step(project, "fetch", "--quiet", "origin", f"+refs/heads/{base}:{remote_ref}")
    if not dry_run and Path(git(project, "rev-parse", "--show-toplevel")).resolve() != project.resolve():
        raise CheckoutError(f"{project} is not the root of its git repository: name the folder that holds .git")
    commit = step(project, "rev-parse", "--verify", f"{remote_ref}^{{commit}}") or DRY_RUN_COMMIT.format(base=base)
    if (checkout_dir / ".git").exists():
        if not dry_run:
            _require_own_worktree(project, checkout_dir, git)
        step(checkout_dir, "checkout", "--quiet", "--force", "--detach", commit)
        step(checkout_dir, "clean", "-fdq")
    else:
        if checkout_dir.exists() and (not checkout_dir.is_dir() or any(checkout_dir.iterdir())):
            raise CheckoutError(f"{checkout_dir} exists and is not a worktree of {project}: remove it or name another --checkout-dir")
        if not dry_run:
            checkout_dir.parent.mkdir(parents=True, exist_ok=True)
        # -f: a worktree folder deleted by hand is still registered until `git worktree prune`
        step(project, "worktree", "add", "--quiet", "-f", "--detach", str(checkout_dir), commit)
    step(checkout_dir, "submodule", "sync", "--recursive", "--quiet")
    step(checkout_dir, "submodule", "update", "--init", "--recursive", "--force", "--quiet")
    if dry_run:
        engine = checkout_dir / ENGINE_SUBMODULE if (project / ENGINE_SUBMODULE / ENGINE_MARKER).is_file() else checkout_dir
        return Checkout(checkout_dir, commit, engine, commit)
    alltests.link_venvs(project, checkout_dir)
    engine = engine_dir_of(checkout_dir)
    return Checkout(checkout_dir, commit, engine, git(engine, "rev-parse", "--verify", "HEAD^{commit}"))


def _require_own_worktree(project: Path, checkout_dir: Path, git: Callable[..., str]) -> None:
    """The folder is reused only when it is a worktree of the same repository: anything else may hold somebody's work."""
    query = ("rev-parse", "--path-format=absolute", "--git-common-dir")
    try:
        own, theirs = Path(git(project, *query)).resolve(), Path(git(checkout_dir, *query)).resolve()
    except alltests_results.GitError:
        theirs = None
    if theirs != own:
        raise CheckoutError(f"{checkout_dir} is not a worktree of {project}: remove it or name another --checkout-dir")
