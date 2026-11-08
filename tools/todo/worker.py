#!/usr/bin/env python3
"""
worker: the folders of the parallel todo-loop workers.

A worker is a git worktree of this repository in `<parent>/<name>.workers/w<k>`, where one Claude
session runs `/todo-loop` on its own (the store's `claim` makes sure two workers never take the same
entry). Worktrees, not clones: every folder shares the one `.git`, so a branch `todo/<id>` made by a
worker is at once visible in the main checkout (where the owner tries it and `/todo-merge` merges
it) and in the other workers (whose loops check that they do not touch the same files).

In a game that carries the engine as the submodule `engine/`, a worker's `engine/` is a worktree of
the main checkout's engine module (`.git/modules/engine`), not a clone, so engine branches are shared
as well. Two guards keep the shared config of that module right (a `git submodule update` in a
worker superproject would rewrite its `core.worktree` to the worker and break the main `engine/`):
`extensions.worktreeConfig` on the main repository with `submodule.engine.update=none` for the
worker only, and the nested submodules (`external/*`) are cloned from the main checkout's modules
with the URL given on the command line, never written to the shared config.

Run it from anywhere inside the main checkout:

    python3 engine/tools/todo/worker.py create 3     # w1..w3, each with its first configure
    python3 engine/tools/todo/worker.py list
    python3 engine/tools/todo/worker.py check [--fix]
    python3 engine/tools/todo/worker.py run 2        # a Claude session in w2
    python3 engine/tools/todo/worker.py remove 3

Python 3.10+, standard library only.
"""

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SHARED_SUBMODULE = "engine"
WORKER_PREFIX = "w"
WORKERS_SUFFIX = ".workers"
DEFAULT_BASE = "main"
# Ignored files of the main checkout that a worker needs and shares by a symbolic link: the store's
# token, the permissions of Claude Code, the Python venv of the reflection generator.
LINKED_FILES = (".env", ".claude/settings.local.json", ".venv", f"{SHARED_SUBMODULE}/.venv")
DEFAULT_PRESETS = {"darwin": "macos-debug"}


class WorkerError(Exception):
    """A message for the person at the terminal; the exit code is 1."""


def git(*args: str, cwd: Path, check: bool = True) -> str:
    result = subprocess.run(["git", "-c", "protocol.file.allow=always", *args], cwd=cwd, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise WorkerError(f"git {' '.join(args)} (in {cwd}) failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout.rstrip()


# --- the layout --------------------------------------------------------------------------------


@dataclass
class Layout:
    main: Path  # the main checkout (the owner's)
    root: Path  # the folder of the workers
    shared: Path | None  # the main checkout's shared submodule (engine/), if any

    def worker(self, k: int) -> Path:
        return self.root / f"{WORKER_PREFIX}{k}"

    def workers(self) -> list[tuple[int, Path]]:
        found = []
        if self.root.is_dir():
            for path in self.root.iterdir():
                name = path.name
                if path.is_dir() and name.startswith(WORKER_PREFIX) and name[len(WORKER_PREFIX) :].isdigit():
                    found.append((int(name[len(WORKER_PREFIX) :]), path))
        return sorted(found)


def find_layout(start: Path) -> Layout:
    common = Path(git("rev-parse", "--path-format=absolute", "--git-common-dir", cwd=start))
    if common.name != ".git":
        raise WorkerError(f"run this from the main checkout of a repository, not from a submodule ({common})")
    main = common.parent
    shared = main / SHARED_SUBMODULE
    has_shared = (main / ".gitmodules").is_file() and SHARED_SUBMODULE in submodule_paths(main)
    return Layout(main=main, root=main.parent / f"{main.name}{WORKERS_SUFFIX}", shared=shared if has_shared else None)


def submodule_paths(repo: Path) -> dict[str, str]:
    """path -> name of every submodule in the .gitmodules of `repo`."""
    if not (repo / ".gitmodules").is_file():
        return {}
    out = git("config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$", cwd=repo, check=False)
    paths = {}
    for line in out.splitlines():
        key, _, path = line.partition(" ")
        paths[path] = key[len("submodule.") : -len(".path")]
    return paths


def module_dir(main_repo: Path, name: str) -> Path:
    """The git dir of the submodule `name` of the main checkout `main_repo`."""
    return Path(git("rev-parse", "--path-format=absolute", "--git-common-dir", cwd=main_repo)) / "modules" / name


def expected_core_worktree(layout: Layout) -> str | None:
    if layout.shared is None:
        return None
    gitdir = module_dir(layout.main, submodule_paths(layout.main)[SHARED_SUBMODULE])
    return os.path.relpath(layout.shared, gitdir)


def actual_core_worktree(layout: Layout) -> str | None:
    if layout.shared is None:
        return None
    gitdir = module_dir(layout.main, submodule_paths(layout.main)[SHARED_SUBMODULE])
    return git("config", "-f", str(gitdir / "config"), "core.worktree", cwd=layout.main, check=False) or None


def head_of(repo: Path) -> str:
    branch = git("symbolic-ref", "--quiet", "--short", "HEAD", cwd=repo, check=False)
    return branch or f"detached {git('rev-parse', '--short', 'HEAD', cwd=repo, check=False)}"


def dirty(repo: Path, ignore_submodules: bool = False) -> str:
    """The changes of `repo`; with `ignore_submodules` a submodule on another commit (a worker's engine/ on an
    engine branch, by design) does not count: the submodule is checked on its own."""
    extra = ["--ignore-submodules=all"] if ignore_submodules else []
    return git("status", "--porcelain", *extra, cwd=repo, check=False)


# --- create ------------------------------------------------------------------------------------


def init_submodules(repo: Path, main_repo: Path) -> None:
    """Clone the submodules of the worktree `repo` from the modules of `main_repo`, without the network and
    without writing a URL into the shared config."""
    overrides = []
    for _path, name in submodule_paths(repo).items():
        local = module_dir(main_repo, name)
        if local.is_dir():
            overrides += ["-c", f"submodule.{name}.url={local}"]
    if submodule_paths(repo):
        git(*overrides, "submodule", "update", "--init", "--recursive", cwd=repo)


def exclude(repo: Path, rel: str) -> None:
    """Make `rel` (a path inside `repo`) ignored through the repository's info/exclude when it is not yet."""
    if subprocess.run(["git", "check-ignore", "-q", rel], cwd=repo).returncode == 0:
        return
    path = Path(git("rev-parse", "--path-format=absolute", "--git-path", "info/exclude", cwd=repo))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as stream:
        stream.write(f"/{rel}\n")


def link_files(layout: Layout, worker: Path) -> None:
    for rel in LINKED_FILES:
        source = layout.main / rel
        target = worker / rel
        if not source.exists() or target.exists() or target.is_symlink():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(source)
        inner = SHARED_SUBMODULE if layout.shared is not None and rel.startswith(f"{SHARED_SUBMODULE}/") else None
        if inner:
            exclude(worker / inner, rel[len(inner) + 1 :])
        else:
            exclude(worker, rel)


def configure(layout: Layout, worker: Path, preset: str) -> None:
    """The first configure of the worker's own build/, so its first build needs none (no ccache: a full
    build of the game takes about 20 s on the owner's machine, and across worktrees the absolute paths of
    the PCH and of the generated reflection code let ccache hit only about a third)."""
    env = dict(os.environ)
    for venv in (layout.main / SHARED_SUBMODULE / ".venv" / "bin", layout.main / ".venv" / "bin"):
        if venv.is_dir():
            env["PATH"] = f"{venv}{os.pathsep}{env.get('PATH', '')}"
            break
    command = ["cmake", "--preset", preset]
    print(f"  configure: {' '.join(command)}")
    result = subprocess.run(command, cwd=worker, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        tail = "\n    ".join((result.stdout + result.stderr).strip().splitlines()[-8:])
        print(f"  configure failed (the worker is usable; the build skill configures again):\n    {tail}")


def create_one(layout: Layout, k: int, base: str, preset: str | None) -> None:
    worker = layout.worker(k)
    if worker.exists():
        print(f"{worker.name}: exists, left alone")
        return
    print(f"{worker.name}: {worker}")
    layout.root.mkdir(parents=True, exist_ok=True)
    git("worktree", "add", "--detach", str(worker), base, cwd=layout.main)
    if layout.shared is not None:
        git("config", "extensions.worktreeConfig", "true", cwd=layout.main)
        git("config", "--worktree", f"submodule.{SHARED_SUBMODULE}.update", "none", cwd=worker)
        line = git("ls-tree", "HEAD", SHARED_SUBMODULE, cwd=worker)
        sha = line.split()[2]
        target = worker / SHARED_SUBMODULE
        if target.is_dir() and not any(target.iterdir()):
            target.rmdir()
        git("worktree", "add", "--detach", str(target), sha, cwd=layout.shared)
        init_submodules(target, layout.shared)
    else:
        init_submodules(worker, layout.main)
    link_files(layout, worker)
    problems = verify_worker(layout, worker, at_pointer=True)
    if problems:
        raise WorkerError(f"{worker.name}: " + "; ".join(problems))
    if preset:
        configure(layout, worker, preset)
    print(f"  ready: {head_of(worker)}")


def verify_worker(layout: Layout, worker: Path, at_pointer: bool) -> list[str]:
    problems = []
    if dirty(worker):
        problems.append(f"not clean: {dirty(worker).splitlines()[:3]}")
    if layout.shared is not None:
        status = git("submodule", "status", SHARED_SUBMODULE, cwd=worker, check=False)
        allowed = (" ",) if at_pointer else (" ", "+")
        if not status.startswith(allowed):
            problems.append(f"{SHARED_SUBMODULE}/ is not checked out at the pointer: {status}")
        update = git("config", "--worktree", f"submodule.{SHARED_SUBMODULE}.update", cwd=worker, check=False)
        if update != "none":
            problems.append(f"submodule.{SHARED_SUBMODULE}.update is not 'none' for the worker")
    return problems


def check_shared_config(layout: Layout, fix: bool) -> list[str]:
    expected = expected_core_worktree(layout)
    actual = actual_core_worktree(layout)
    if expected is None or actual == expected:
        return []
    if fix:
        gitdir = module_dir(layout.main, submodule_paths(layout.main)[SHARED_SUBMODULE])
        git("config", "-f", str(gitdir / "config"), "core.worktree", expected, cwd=layout.main)
        return [f"core.worktree of {SHARED_SUBMODULE} was {actual}, set back to {expected}"]
    return [f"core.worktree of {SHARED_SUBMODULE} is {actual}, expected {expected} (a `git submodule update` in a worker superproject? run `check --fix`)"]


# --- commands ----------------------------------------------------------------------------------


def cmd_create(layout: Layout, args) -> None:
    problems = check_shared_config(layout, fix=False)
    if problems:
        raise WorkerError(problems[0])
    preset = None if args.no_configure else (args.preset or DEFAULT_PRESETS.get(sys.platform))
    for k in range(1, args.count + 1):
        create_one(layout, k, args.base, preset)
    problems = check_shared_config(layout, fix=False)
    if problems:
        raise WorkerError(problems[0])
    print("\nStart a session per worker: python3 <engine>/tools/todo/worker.py run <k> (one terminal each, a minute apart)")


def cmd_list(layout: Layout, args) -> None:
    workers = layout.workers()
    if not workers:
        print(f"no workers in {layout.root}")
        return
    for k, path in workers:
        line = f"w{k}  {head_of(path):40}"
        if layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists():
            line += f"  {SHARED_SUBMODULE}: {head_of(path / SHARED_SUBMODULE):32}"
        changed = dirty(path, ignore_submodules=True) or (layout.shared is not None and dirty(path / SHARED_SUBMODULE))
        line += "  dirty" if changed else "  clean"
        print(line)


def cmd_check(layout: Layout, args) -> None:
    problems = check_shared_config(layout, fix=args.fix)
    for _k, path in layout.workers():
        problems += [f"{path.name}: {p}" for p in verify_worker(layout, path, at_pointer=False) if not p.startswith("not clean")]
    for problem in problems:
        print(problem)
    if not problems:
        print("ok")
    elif not args.fix:
        sys.exit(1)


def cmd_remove(layout: Layout, args) -> None:
    worker = layout.worker(args.k)
    if not worker.is_dir():
        raise WorkerError(f"no worker {worker}")
    for repo in (worker, worker / SHARED_SUBMODULE):
        changes = dirty(repo, ignore_submodules=True) if (repo / ".git").exists() else ""
        if changes:
            raise WorkerError(f"{repo} is not clean; commit or drop the changes first:\n{changes}")
    # `git worktree remove` refuses a worktree with submodules; the branches are refs of the shared .git,
    # so deleting the folder loses nothing that was committed.
    print(f"removing {worker} (on {head_of(worker)})")
    shutil.rmtree(worker)
    git("worktree", "prune", cwd=layout.main)
    if layout.shared is not None:
        git("worktree", "prune", cwd=layout.shared)


def cmd_run(layout: Layout, args) -> None:
    worker = layout.worker(args.k)
    if not worker.is_dir():
        raise WorkerError(f"no worker {worker}; create it first")
    count = max(1, len(layout.workers()))
    env = dict(os.environ)
    env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(max(2, (os.cpu_count() or 4) // count)))
    command = [args.claude, *args.rest]
    print(f"cd {worker} && CMAKE_BUILD_PARALLEL_LEVEL={env['CMAKE_BUILD_PARALLEL_LEVEL']} {' '.join(command)}")
    if args.print_only:
        return
    os.chdir(worker)
    os.execvpe(command[0], command, env)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="worker.py", description="The folders of the parallel todo-loop workers (git worktrees).")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("create", help="make the workers w1..wN that are missing")
    p.add_argument("count", type=int)
    p.add_argument("--base", default=DEFAULT_BASE, help=f"the commit a new worker starts on (default {DEFAULT_BASE})")
    p.add_argument("--preset", help="the configure preset (default: macos-debug on macOS)")
    p.add_argument("--no-configure", action="store_true", help="do not run the first configure")
    sub.add_parser("list", help="the workers, their branches, clean or not")
    p = sub.add_parser("check", help="the shared config and every worker")
    p.add_argument("--fix", action="store_true", help="set a broken core.worktree of the shared submodule back")
    p = sub.add_parser("remove", help="delete a clean worker folder")
    p.add_argument("k", type=int)
    p = sub.add_parser("run", help="start a Claude session in a worker (arguments for claude after --)")
    p.add_argument("k", type=int)
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--print", dest="print_only", action="store_true", help="only print the command")
    return parser


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    rest = argv[argv.index("--") + 1 :] if "--" in argv else []
    argv = argv[: argv.index("--")] if "--" in argv else argv
    args = build_parser().parse_args(argv)
    args.rest = rest
    try:
        layout = find_layout(Path.cwd())
        {"create": cmd_create, "list": cmd_list, "check": cmd_check, "remove": cmd_remove, "run": cmd_run}[args.command](layout, args)
    except WorkerError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
