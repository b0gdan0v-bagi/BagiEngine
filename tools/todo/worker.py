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
    python3 engine/tools/todo/worker.py run 2        # a Claude session in w2, in this terminal
    python3 engine/tools/todo/worker.py start        # /todo-loop in every worker, background sessions a minute apart
    python3 engine/tools/todo/worker.py stop [k ...] # stop those background sessions
    python3 engine/tools/todo/worker.py merge <id> [--dry-run]  # the quick merge of an entry in review
    python3 engine/tools/todo/worker.py update [k ...] # detach a finished, clean worker at the tips of the bases
    python3 engine/tools/todo/worker.py remove 3

Python 3.10+, standard library only.
"""

import argparse
import json
import re
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

SHARED_SUBMODULE = "engine"
WORKER_PREFIX = "w"
WORKERS_SUFFIX = ".workers"
DEFAULT_BASE = "main"
# Ignored files of the main checkout that a worker needs and shares by a symbolic link: the store's
# token, the permissions of Claude Code, the Python venv of the reflection generator.
LINKED_FILES = (".env", ".claude/settings.local.json", ".venv", f"{SHARED_SUBMODULE}/.venv")
DEFAULT_PRESETS = {"darwin": "macos-debug"}
# The name of a worker's background session (`claude --bg --name`), how `claude attach` finds it.
SESSION_PREFIX = "todo-w"
DEFAULT_DELAY_S = 60
REPORT_TO_NAME = re.compile(r"[A-Za-z0-9._-]+")


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


# --- background sessions -----------------------------------------------------------------------


def build_env(layout: Layout) -> dict[str, str]:
    """The environment of a worker's session: a share of the cores for each build, so the builds of
    all the workers at once do not fight for the machine."""
    count = max(1, len(layout.workers()))
    env = dict(os.environ)
    env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(max(2, (os.cpu_count() or 4) // count)))
    return env


def session_name(k: int) -> str:
    return f"{SESSION_PREFIX}{k}"


def list_sessions(claude: str) -> list[dict] | None:
    """The sessions `claude agents --json` lists (running ones), or None when it cannot be asked."""
    try:
        result = subprocess.run([claude, "agents", "--json"], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    try:
        sessions = json.loads(result.stdout or "[]")
    except json.JSONDecodeError:
        return None
    return sessions if isinstance(sessions, list) else None


def session_in(sessions: list[dict], worker: Path) -> dict | None:
    """The newest background session that runs in `worker` (its cwd is the folder or inside it)."""
    found = []
    for session in sessions:
        cwd = session.get("cwd")
        if session.get("kind") != "background" or not cwd:
            continue
        path = Path(cwd)
        if path == worker or worker in path.parents:
            found.append(session)
    return max(found, key=lambda s: s.get("startedAt") or 0) if found else None


def is_waiting(session: dict | None) -> bool:
    """A session that waits for the person (`claude agents`): an interactive one says status `waiting`, a background one state
    `blocked` (a permission, a question, or a run that ended asking the owner something)."""
    return session is not None and session.get("state") != "done" and (session.get("status") == "waiting" or session.get("state") == "blocked")


def is_active(session: dict | None) -> bool:
    """A session that has not finished its work: busy, or waiting for the person (a permission)."""
    return session is not None and session.get("state") != "done"


def describe(session: dict | None) -> str:
    if session is None:
        return "no session"
    started = session.get("startedAt")
    since = ""
    if isinstance(started, (int, float)):
        minutes = int((time.time() * 1000 - started) / 60000)
        since = f", {minutes // 60} h {minutes % 60} min" if minutes >= 60 else f", {minutes} min"
    state = session.get("state") or session.get("status") or "?"
    waiting = f", waits for you ({session.get('waitingFor') or 'input'}): claude attach {session.get('name') or session.get('id')}" if is_waiting(session) else ""
    return f"{session.get('name') or session.get('id')}: {state}/{session.get('status', '?')}{since}{waiting}"


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
    print(f"\nStart them: /todo-loop-parallel, or {tool_command(layout)} start (one background session per worker, a minute apart)")


def head_json(repo: Path) -> dict:
    branch = git("symbolic-ref", "--quiet", "--short", "HEAD", cwd=repo, check=False)
    return {"branch": branch or None, "commit": git("rev-parse", "--short", "HEAD", cwd=repo, check=False)}


RULES_FILE = Path(".claude") / "rules" / "todo-project.md"


def rule_area_table(main: Path) -> list[dict]:
    """The rows of the table under "## Areas" in .claude/rules/todo-project.md: `area` (the store's), `repo`
    (the git repository, relative to the checkout) and `base` (its base branch), the first code span of each
    cell. These are the only areas the loops of this checkout work (todo-loop/SKILL.md, "Only the areas of the
    rules file"); the store also holds other games' entries. Empty when the file or the table is missing."""
    try:
        text = (main / RULES_FILE).read_text(encoding="utf-8")
    except OSError:
        return []
    section = re.search(r"^## Areas[ \t]*$(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not section:
        return []
    rows = []
    for line in section.group(1).splitlines():
        if not line.lstrip().startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        area = re.fullmatch(r"`([A-Za-z0-9_-]+)`", cells[1]) if len(cells) > 3 else None
        if not area:
            continue
        repo = re.match(r"`([^`]+)`", cells[2])
        base = re.match(r"`([^`]+)`", cells[3])
        rows.append({"area": area.group(1), "repo": repo.group(1) if repo else ".", "base": base.group(1) if base else DEFAULT_BASE})
    return rows


def rule_areas(main: Path) -> list[str] | None:
    """The `area` of each row of `rule_area_table`; None when it has none (then every area counts)."""
    return [row["area"] for row in rule_area_table(main)] or None


def store_entries(layout: Layout) -> tuple[list[dict], str | None]:
    """The entries `in progress` and in `review` of the areas of this checkout (`rule_areas`; all of them
    when it names none) from todo.py, run from the main checkout, or an error."""
    todo = Path(__file__).resolve().parent / "todo.py"
    try:
        result = subprocess.run(
            [sys.executable, str(todo), "list", "--status", "in progress,review", "--json"],
            cwd=layout.main, capture_output=True, text=True, timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return [], str(error)
    if result.returncode != 0:
        text = (result.stderr or result.stdout).strip()
        return [], text.splitlines()[-1] if text else f"exit code {result.returncode}"
    try:
        entries = json.loads(result.stdout or "[]")
    except json.JSONDecodeError as error:
        return [], f"todo.py list: {error}"
    keep = ("id", "area", "title", "status", "statusNote", "branch", "planProgress")
    table = {row["area"]: row for row in rule_area_table(layout.main)}
    shown = []
    for entry in entries:
        if not isinstance(entry, dict) or (table and entry.get("area") not in table):
            continue
        row = {key: entry.get(key) for key in keep}
        area = table.get(entry.get("area"))
        # where the panel finds the branch and what it is compared with (Review, Quick merge)
        row["repo"] = str((layout.main / area["repo"]).resolve()) if area else None
        row["base"] = area["base"] if area else None
        row.update(branch_origin(Path(row["repo"]), row["branch"], row["base"]) if area and row["branch"] else {"local": None, "foreignBase": None})
        shown.append(row)
    return shown, None


# --- merge: the quick merge of an entry in review -------------------------------------------------
#
# What /todo-merge does after the owner's yes (.claude/skills/todo-merge/SKILL.md, steps 6 to 9), without a
# Claude session: the VS Code panel shows `merge <id> --dry-run --json` in a dialog and runs `merge <id>`
# on a yes. It refuses what the skill refuses (conflicts, a dirty checkout, an entry not in review) and,
# unlike the skill, never builds: a base that moved since the branch was cut is only reported.

MERGE_ACTOR = "quick-merge"
SLICE_REOPEN = "after the merge set the status back to open"


def todo_cli(layout: Layout, *args: str) -> str:
    """todo.py (next to this file) run from the main checkout; its error as a WorkerError."""
    todo = Path(__file__).resolve().parent / "todo.py"
    try:
        result = subprocess.run(
            [sys.executable, str(todo), "--actor", MERGE_ACTOR, *args], cwd=layout.main, capture_output=True, text=True, timeout=60
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise WorkerError(f"todo.py {args[0]}: {error}") from error
    if result.returncode != 0:
        text = (result.stderr or result.stdout).strip()
        raise WorkerError(f"todo.py {' '.join(args[:2])}: {text.splitlines()[-1] if text else f'exit code {result.returncode}'}")
    return result.stdout


def git_ok(*args: str, cwd: Path) -> bool:
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True).returncode == 0


def base_candidates(repo: Path, base: str) -> list[str]:
    """The other base branches `repo` knows: `main` and `games/*`, local or of a remote, but not `base` itself
    nor a remote copy of it (`origin/games/mahjong` for `games/mahjong`)."""
    names = git("for-each-ref", "--format=%(refname)", "refs/heads/main", "refs/heads/games/", "refs/remotes/", cwd=repo, check=False)
    found = []
    for ref in names.splitlines():
        if ref.startswith("refs/heads/"):
            short = plain = ref[len("refs/heads/") :]
        else:
            short = ref[len("refs/remotes/") :]
            plain = short.partition("/")[2]
        if plain != base and (plain == "main" or plain.startswith("games/")):
            found.append(short)
    return found


def foreign_base(repo: Path, branch: str, base: str) -> str | None:
    """The base branch other than `base` that `branch` was cut from, if it looks so: the branch holds commits
    of that branch (local or fetched) that `base` has not. Such a branch is another checkout's (a loop of
    another game took an entry of the engine area); merged here it would bring those commits along. None when
    no other base branch is known or the branch shares nothing with one beyond `base`."""
    for other in base_candidates(repo, base):
        shared = git("merge-base", branch, other, cwd=repo, check=False)
        if shared and not git_ok("merge-base", "--is-ancestor", shared, base, cwd=repo):
            return other
    return None


def branch_origin(repo: Path, branch: str, base: str) -> dict:
    """`local`: the branch exists in `repo` (an entry taken in another checkout or on another machine has its
    branch there, never here); `foreignBase`: what `foreign_base` says, for a local branch."""
    if not repo.is_dir() or not git_ok("rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", cwd=repo):
        return {"local": False, "foreignBase": None}
    return {"local": True, "foreignBase": foreign_base(repo, branch, base)}


def worktree_of(repo: Path, branch: str) -> Path | None:
    """The worktree of `repo` (a main worktree) that has `branch` checked out, if any. The first entry of the
    list is the main worktree, `repo` itself (a submodule lists it as its git dir, `.git/modules/<name>`)."""
    path = None
    for line in git("worktree", "list", "--porcelain", cwd=repo).splitlines():
        if line.startswith("worktree "):
            path = Path(line[len("worktree ") :]) if path is not None else repo
        elif line == f"branch refs/heads/{branch}" and path is not None:
            return path
    return None


def merge_plan(layout: Layout, entry_id: str) -> dict:
    """What merging the branch of `entry_id` would do, and what stops it (`conflicts`, `problems`)."""
    table = rule_area_table(layout.main)
    if not table:
        raise WorkerError(f"no Areas table in {RULES_FILE}: the merge needs the repository and the base branch of each area")
    entry = json.loads(todo_cli(layout, "show", entry_id, "--json"))
    area = next((row for row in table if row["area"] == entry.get("area")), None)
    if area is None:
        raise WorkerError(f"{entry_id} is of the area {entry.get('area')}, not of this checkout ({', '.join(row['area'] for row in table)})")
    branch = entry.get("branch")
    if entry.get("status") != "review" or not branch:
        raise WorkerError(f"{entry_id} is {entry.get('status')}, not in review with a branch")
    repo = (layout.main / area["repo"]).resolve()
    base = area["base"]
    if not git_ok("rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", cwd=repo):
        raise WorkerError(f"no branch {branch} in {repo}: it was made in another checkout or on another machine, review and merge it there")
    if git_ok("merge-base", "--is-ancestor", branch, base, cwd=repo):
        raise WorkerError(f"{branch} is already merged into {base}")
    foreign = foreign_base(repo, branch, base)
    if foreign:
        raise WorkerError(f"{branch} holds commits of {foreign} that {base} has not: it was cut from {foreign} in another checkout, merge it there")
    tree = subprocess.run(["git", "merge-tree", "--write-tree", "--name-only", base, branch], cwd=repo, capture_output=True, text=True)
    conflicts = []
    if tree.returncode != 0:
        for line in tree.stdout.splitlines()[1:]:
            if not line.strip():
                break
            conflicts.append(line.strip())
        conflicts = conflicts or [(tree.stderr or tree.stdout).strip() or "git merge-tree failed"]
    problems = []
    if dirty(layout.main, ignore_submodules=True):
        problems.append(f"{layout.main} has uncommitted changes")
    if repo != layout.main.resolve() and dirty(repo):
        problems.append(f"{repo} has uncommitted changes")
    held = worktree_of(repo, branch)
    current = git("symbolic-ref", "--quiet", "--short", "HEAD", cwd=repo, check=False)
    base_elsewhere = worktree_of(repo, base)
    if base_elsewhere is not None and base_elsewhere.resolve() != repo:
        problems.append(f"{base} is checked out in {base_elsewhere}, not in {repo}")
    return {
        "id": entry_id,
        "area": entry.get("area"),
        "title": entry.get("title"),
        "note": entry.get("statusNote"),
        "branch": branch,
        "base": base,
        "repo": str(repo),
        "current": current or None,
        "commits": git("log", "--oneline", f"{base}..{branch}", cwd=repo).splitlines(),
        "stat": (git("diff", "--shortstat", f"{base}...{branch}", cwd=repo) or "").strip(),
        "files": git("diff", "--name-only", f"{base}...{branch}", cwd=repo).splitlines(),
        "conflicts": conflicts,
        "moved": git("merge-base", base, branch, cwd=repo) != git("rev-parse", base, cwd=repo),
        "reopen": SLICE_REOPEN in (entry.get("statusNote") or ""),
        "pointer": layout.shared is not None and repo == layout.shared.resolve(),
        "heldBy": str(held) if held is not None and held.resolve() != repo else None,
        "problems": problems,
    }


def do_merge(layout: Layout, plan: dict) -> dict:
    """Merge (--no-ff), close or reopen the entry, free and delete the branch, move the engine pointer."""
    if plan["conflicts"]:
        raise WorkerError(f"{plan['branch']} conflicts with {plan['base']}: {', '.join(plan['conflicts'])}")
    if plan["problems"]:
        raise WorkerError("; ".join(plan["problems"]))
    repo, base, branch = Path(plan["repo"]), plan["base"], plan["branch"]
    notes = []
    if plan["current"] != base:
        git("switch", base, cwd=repo)
    merged = subprocess.run(["git", "merge", "--no-ff", "-m", f"Merge branch '{branch}'", branch], cwd=repo, capture_output=True, text=True)
    if merged.returncode != 0:
        git("merge", "--abort", cwd=repo, check=False)
        if plan["current"] and plan["current"] != base:
            git("switch", plan["current"], cwd=repo, check=False)
        raise WorkerError(f"git merge {branch} failed: {(merged.stderr or merged.stdout).strip()}")
    result = {"id": plan["id"], "branch": branch, "base": base, "merge": git("rev-parse", "--short", "HEAD", cwd=repo)}
    try:
        todo_cli(layout, "close", plan["id"], *(["--reopen"] if plan["reopen"] else []))
        result["status"] = "open" if plan["reopen"] else "closed"
    except WorkerError as error:
        result["status"] = None
        notes.append(f"the entry is still in review: {error}")
    held = Path(plan["heldBy"]) if plan["heldBy"] else None
    if held is not None:
        if dirty(held):
            notes.append(f"{held} has changes: the branch stays")
        else:
            git("switch", "--detach", base, cwd=held)
            result["detached"] = str(held)
    if held is None or "detached" in result:
        deleted = subprocess.run(["git", "branch", "-d", branch], cwd=repo, capture_output=True, text=True)
        result["deleted"] = deleted.returncode == 0
        if not result["deleted"]:
            notes.append(f"git branch -d {branch}: {(deleted.stderr or deleted.stdout).strip()}")
    if plan["pointer"]:
        git("add", SHARED_SUBMODULE, cwd=layout.main)
        if not git_ok("diff", "--cached", "--quiet", "--", SHARED_SUBMODULE, cwd=layout.main):
            git("commit", "-m", f"Update engine: {plan['title']}", "--", SHARED_SUBMODULE, cwd=layout.main)
            result["pointer"] = git("rev-parse", "--short", "HEAD", cwd=layout.main)
    result["notes"] = notes
    return result


def cmd_merge(layout: Layout, args) -> None:
    plan = merge_plan(layout, args.id)
    if args.dry_run:
        if args.json:
            print(json.dumps(plan, ensure_ascii=False, indent=1))
            return
        print(f"{plan['branch']} into {plan['base']} ({plan['repo']}): {len(plan['commits'])} commits, {plan['stat']}")
        for line in plan["commits"]:
            print(f"  {line}")
        for what in plan["conflicts"]:
            print(f"conflict: {what}")
        for what in plan["problems"]:
            print(f"problem: {what}")
        if plan["moved"]:
            print("the base moved since the branch was cut: the merge is not built or tested")
        return
    result = do_merge(layout, plan)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=1))
        return
    print(f"merged {result['branch']} into {result['base']}: {result['merge']}; the entry is {result['status'] or 'still in review'}")
    if result.get("pointer"):
        print(f"engine pointer: {result['pointer']}")
    for note in result["notes"]:
        print(f"note: {note}")


# --- activity: what a session does now, from its transcript ---------------------------------------
#
# Claude Code writes every session to ~/.claude/projects/<cwd with each non-alphanumeric char as '-'>/
# <sessionId>.jsonl, and the agents it starts to <sessionId>/subagents/agent-<id>.jsonl with a .meta.json
# (agentType, description, model). The panel shows the last steps of both; nothing here writes.

ACTIVITY_EVENTS = 8
ACTIVITY_AGENT_MINUTES = 15  # an agent whose transcript changed within this time counts as running
TAIL_BYTES = 768 * 1024
REPORT_TOOLS = ("SubagentHandback", "SendMessage")  # the tool an agent hands its final report back with


def projects_dir() -> Path:
    return Path(os.environ.get("CLAUDE_CONFIG_DIR") or Path.home() / ".claude") / "projects"


def transcript_of(cwd: str, session_id: str) -> Path:
    return projects_dir() / re.sub(r"[^A-Za-z0-9]", "-", cwd) / f"{session_id}.jsonl"


def tail_records(path: Path) -> list[dict]:
    """The JSON records of the last TAIL_BYTES of a transcript (the first, cut line is dropped)."""
    try:
        with path.open("rb") as file:
            size = file.seek(0, os.SEEK_END)
            file.seek(max(0, size - TAIL_BYTES))
            data = file.read()
    except OSError:
        return []
    lines = data.split(b"\n")
    if size > TAIL_BYTES:
        lines = lines[1:]
    records = []
    for line in lines:
        try:
            record = json.loads(line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if isinstance(record, dict):
            records.append(record)
    return records


def one_line(text: str, limit: int) -> str:
    text = " ".join(str(text).split())
    return text if len(text) <= limit else text[: limit - 1] + "…"


def tool_text(name: str, given: dict) -> str:
    for key in ("description", "message", "file_path", "pattern", "command", "prompt", "query", "url"):
        value = given.get(key)
        if isinstance(value, str) and value.strip():
            return one_line(value, 160)
    return one_line(json.dumps(given, ensure_ascii=False), 160)


def events_of(records: list[dict], limit: int) -> list[dict]:
    """The last `limit` steps: what the session said (`say`) and which tools it called (`tool`)."""
    events = []
    for record in records:
        if record.get("type") != "assistant":
            continue
        content = (record.get("message") or {}).get("content")
        if isinstance(content, str):
            content = [{"type": "text", "text": content}]
        for part in content if isinstance(content, list) else []:
            if not isinstance(part, dict):
                continue
            if part.get("type") == "text" and str(part.get("text", "")).strip():
                events.append({"time": record.get("timestamp"), "kind": "say", "text": one_line(part["text"], 400)})
            elif part.get("type") == "tool_use":
                given = part.get("input") if isinstance(part.get("input"), dict) else {}
                events.append({"time": record.get("timestamp"), "kind": "tool", "tool": part.get("name"), "text": tool_text(str(part.get("name")), given)})
    return events[-limit:]


def finished(records: list[dict]) -> bool:
    """An agent is done when its last answer ended the turn or was its report (REPORT_TOOLS)."""
    for record in reversed(records):
        if record.get("type") != "assistant":
            continue
        message = record.get("message") or {}
        content = message.get("content") if isinstance(message.get("content"), list) else []
        if any(isinstance(part, dict) and part.get("type") == "tool_use" and part.get("name") in REPORT_TOOLS for part in content):
            return True
        return message.get("stop_reason") == "end_turn"
    return False


def session_activity(session: dict | None) -> dict | None:
    """The last steps of a worker's session and of the agents it runs now, or None without a transcript."""
    if not session or not session.get("cwd") or not session.get("sessionId"):
        return None
    main = transcript_of(session["cwd"], session["sessionId"])
    if not main.is_file():
        return None
    agents = []
    folder = main.with_suffix("") / "subagents"
    now = time.time()
    if folder.is_dir():
        for path in folder.glob("agent-*.jsonl"):
            try:
                changed = path.stat().st_mtime
            except OSError:
                continue
            if now - changed > ACTIVITY_AGENT_MINUTES * 60:
                continue
            try:
                meta = json.loads(path.with_suffix(".meta.json").read_text())
            except (OSError, json.JSONDecodeError):
                meta = {}
            records = tail_records(path)
            last = events_of(records, 2)
            agents.append({
                "type": meta.get("agentType"),
                "description": meta.get("description"),
                "model": meta.get("model"),
                "updated": int(changed * 1000),
                "done": finished(records),
                "last": last,
            })
    agents.sort(key=lambda agent: (not agent["done"], agent["updated"]), reverse=True)
    return {"updated": int(main.stat().st_mtime * 1000), "events": events_of(tail_records(main), ACTIVITY_EVENTS), "agents": agents[:4]}


USAGE_RECENT_MINUTES = 10  # the window of "now": tokens of the answers in the last minutes
USAGE_KEYS = ("input", "cacheWrite", "cacheRead", "output")


def usage_counts(usage: dict) -> dict:
    return {
        "input": int(usage.get("input_tokens") or 0),
        "cacheWrite": int(usage.get("cache_creation_input_tokens") or 0),
        "cacheRead": int(usage.get("cache_read_input_tokens") or 0),
        "output": int(usage.get("output_tokens") or 0),
    }


def add_counts(into: dict, counts: dict) -> None:
    for key in USAGE_KEYS:
        into[key] = into.get(key, 0) + counts.get(key, 0)
    into["total"] = sum(into.get(key, 0) for key in USAGE_KEYS)


def context_of(counts: dict) -> int:
    """The tokens an answer read: what the session holds in its context at that moment."""
    return counts["input"] + counts["cacheWrite"] + counts["cacheRead"]


def file_usage(path: Path) -> tuple[dict, dict, str | None]:
    """The answers of one transcript: message id -> (time, model, counts), and the last one's counts and
    model. Claude Code writes an answer once per content block with the same id and usage, so the id
    counts once (with the largest output, which grows while it streams)."""
    answers: dict[str, tuple[float, str, dict]] = {}
    last: tuple[dict, str | None] = ({}, None)
    try:
        file = path.open(encoding="utf-8", errors="replace")
    except OSError:
        return {}, {}, None
    with file:
        for line in file:
            if '"usage"' not in line or '"assistant"' not in line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            message = record.get("message") if record.get("type") == "assistant" else None
            if not isinstance(message, dict) or not isinstance(message.get("usage"), dict):
                continue
            counts = usage_counts(message["usage"])
            model = str(message.get("model") or "?")
            if model == "<synthetic>":
                continue
            ident = str(message.get("id") or record.get("uuid"))
            try:
                when = datetime.fromisoformat(str(record.get("timestamp")).replace("Z", "+00:00")).timestamp()
            except ValueError:
                when = 0.0
            known = answers.get(ident)
            if known is None or counts["output"] >= known[2]["output"]:
                answers[ident] = (when, model, counts)
            last = (counts, model)
    return answers, last[0], last[1]


def session_usage(session: dict | None) -> dict | None:
    """The tokens of a worker's session and of all its agents, by model, in the last minutes, and the
    size of its context now (the last answer of the session itself)."""
    if not session or not session.get("cwd") or not session.get("sessionId"):
        return None
    main = transcript_of(session["cwd"], session["sessionId"])
    if not main.is_file():
        return None
    since = time.time() - USAGE_RECENT_MINUTES * 60
    total: dict = {}
    recent: dict = {}
    models: dict[str, dict] = {}
    parts = {"session": {}, "agents": {}}
    answers, last, last_model = file_usage(main)
    files = [("session", answers)]
    folder = main.with_suffix("") / "subagents"
    if folder.is_dir():
        files += [("agents", file_usage(path)[0]) for path in sorted(folder.glob("agent-*.jsonl"))]
    for part, found in files:
        for when, model, counts in found.values():
            add_counts(total, counts)
            add_counts(parts[part], counts)
            add_counts(models.setdefault(model, {}), counts)
            if when >= since:
                add_counts(recent, counts)
    return {
        "total": total,
        "session": parts["session"],
        "agents": parts["agents"],
        "models": models,
        "recent": recent,
        "recentMinutes": USAGE_RECENT_MINUTES,
        "context": context_of(last) if last else 0,
        "contextModel": last_model,
    }


def list_json(layout: Layout, args) -> dict:
    """What `list --json` prints: the workers, their sessions and entries, and the entries in review
    (the VS Code panel of tools/vscode/todo-workers reads it)."""
    sessions = list_sessions(args.claude)
    entries, store_error = store_entries(layout) if args.store else ([], None)
    by_branch = {entry["branch"]: entry for entry in entries if entry.get("branch")}
    workers = []
    for k, path in layout.workers():
        game = head_json(path)
        engine = head_json(path / SHARED_SUBMODULE) if layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists() else None
        changed = dirty(path, ignore_submodules=True) or (engine is not None and dirty(path / SHARED_SUBMODULE))
        session = session_in(sessions or [], path)
        entry = next((by_branch[h["branch"]] for h in (engine, game) if h and h["branch"] in by_branch), None)
        workers.append({
            "k": k,
            "name": f"{WORKER_PREFIX}{k}",
            "path": str(path),
            "head": game,
            "engine": engine,
            "dirty": bool(changed),
            "session": None if session is None else {key: session.get(key) for key in ("id", "name", "state", "status", "waitingFor", "startedAt")},
            "active": is_active(session),
            "waiting": is_waiting(session),
            "activity": session_activity(session) if args.activity else None,
            "usage": session_usage(session) if args.usage else None,
            "entry": entry,
        })
    usage: dict = {}
    recent: dict = {}
    for row in workers:
        if row["usage"]:
            add_counts(usage, row["usage"]["total"])
            add_counts(recent, row["usage"]["recent"])
    return {
        "usage": {"total": usage, "recent": recent, "recentMinutes": USAGE_RECENT_MINUTES} if args.usage else None,
        "main": str(layout.main),
        "root": str(layout.root),
        "sessionsKnown": sessions is not None,
        "store": "off" if not args.store else (store_error or "ok"),
        "areas": rule_areas(layout.main) if args.store else None,
        "workers": workers,
        "review": [entry for entry in entries if entry.get("status") == "review"],
        # entries whose branch is not this checkout's to review, merge or continue (`branch_origin`)
        "elsewhere": [entry["id"] for entry in entries if entry.get("local") is False or entry.get("foreignBase")],
    }


def cmd_list(layout: Layout, args) -> None:
    if args.json:
        print(json.dumps(list_json(layout, args), ensure_ascii=False, indent=1))
        return
    workers = layout.workers()
    if not workers:
        print(f"no workers in {layout.root}")
        return
    sessions = list_sessions(args.claude)
    for k, path in workers:
        line = f"w{k}  {head_of(path):40}"
        if layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists():
            line += f"  {SHARED_SUBMODULE}: {head_of(path / SHARED_SUBMODULE):32}"
        changed = dirty(path, ignore_submodules=True) or (layout.shared is not None and dirty(path / SHARED_SUBMODULE))
        line += "  dirty" if changed else "  clean"
        if sessions is not None:
            line += f"  {describe(session_in(sessions, path))}"
        print(line)
    if sessions is not None and any(session_in(sessions, path) for _k, path in workers):
        print(f"\nclaude agents (all of them), claude attach {SESSION_PREFIX}<k> (answer a session that waits), claude logs {SESSION_PREFIX}<k>")


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
    env = build_env(layout)
    command = [args.claude, *args.rest]
    print(f"cd {worker} && CMAKE_BUILD_PARALLEL_LEVEL={env['CMAKE_BUILD_PARALLEL_LEVEL']} {' '.join(command)}")
    if args.print_only:
        return
    os.chdir(worker)
    os.execvpe(command[0], command, env)


def chosen_workers(layout: Layout, numbers: list[int]) -> list[tuple[int, Path]]:
    workers = layout.workers()
    if not numbers:
        if not workers:
            raise WorkerError(f"no workers in {layout.root}; create them first")
        return workers
    known = dict(workers)
    missing = [k for k in numbers if k not in known]
    if missing:
        raise WorkerError(f"no worker {', '.join(f'w{k}' for k in missing)} in {layout.root}")
    return [(k, known[k]) for k in dict.fromkeys(numbers)]


def tool_command(layout: Layout) -> str:
    """How the owner calls this script from the main checkout (`engine/tools/...` in a game)."""
    here = Path(__file__).resolve()
    try:
        return f"python3 {here.relative_to(layout.main.resolve())}"
    except ValueError:
        return f"python3 {here}"


def loop_prompt(args) -> str:
    """The prompt of a worker's session: `/todo-loop N [loop args] [report-to=<session>]`."""
    report = ""
    if args.report_to:
        if not REPORT_TO_NAME.fullmatch(args.report_to):
            raise WorkerError(f"--report-to {args.report_to!r}: a session name (letters, digits, '.', '_', '-')")
        report = f"report-to={args.report_to}"
    return " ".join(part for part in ("/todo-loop", str(args.slices), args.loop_args, report) if part)


def cmd_start(layout: Layout, args) -> None:
    """`/todo-loop` as a background Claude session (`claude --bg`) in each chosen worker, `--delay`
    seconds apart: two loops that choose at the same moment could take entries that meet in their
    files (todo-loop/SKILL.md, "Parallel workers"), the store's claim only keeps them off one entry."""
    problems = check_shared_config(layout, fix=False)
    if problems:
        raise WorkerError(problems[0])
    workers = chosen_workers(layout, args.workers)
    sessions = list_sessions(args.claude)
    if sessions is None and not args.print_only:
        raise WorkerError(f"`{args.claude} agents --json` did not answer; is Claude Code installed and new enough for --bg?")
    env = build_env(layout)
    prompt = loop_prompt(args)
    started = 0
    failed = False
    for k, path in workers:
        session = session_in(sessions or [], path)
        if is_active(session):
            print(f"w{k}: already runs ({describe(session)}), left alone")
            continue
        command = [args.claude, "--bg", "--name", session_name(k), *args.rest, prompt]
        if started and args.delay > 0:
            print(f"  waiting {args.delay} s before w{k} (the loops must not choose at the same moment)")
            if not args.print_only:
                time.sleep(args.delay)
        print(f"w{k}: cd {path} && CMAKE_BUILD_PARALLEL_LEVEL={env['CMAKE_BUILD_PARALLEL_LEVEL']} {' '.join(command[:-1])} '{prompt}'")
        if args.print_only:
            started += 1
            continue
        result = subprocess.run(command, cwd=path, env=env, capture_output=True, text=True)
        output = (result.stdout + result.stderr).strip()
        if result.returncode != 0 or "backgrounded" not in output:
            failed = True
            print(f"  failed: {output or f'exit code {result.returncode}'}")
            if "trust" in output.lower():
                print(f"  w{k} is not trusted by Claude Code yet: run `{tool_command(layout)} run {k}` once, accept the prompt, quit with /exit, then start again")
            continue
        started += 1
        print(f"  {output.splitlines()[0]}")
    if started and not args.print_only:
        print(f"\nWatch: {tool_command(layout)} list; claude agents; claude attach {SESSION_PREFIX}<k> when one waits for you")
    if failed:
        raise WorkerError("some workers did not start (above)")


def cmd_stop(layout: Layout, args) -> None:
    workers = chosen_workers(layout, args.workers)
    sessions = list_sessions(args.claude)
    if sessions is None:
        raise WorkerError(f"`{args.claude} agents --json` did not answer")
    for k, path in workers:
        session = session_in(sessions, path)
        if session is None:
            print(f"w{k}: no session")
            continue
        ident = session.get("id") or session.get("sessionId")
        result = subprocess.run([args.claude, "stop", str(ident)], capture_output=True, text=True)
        print(f"w{k}: {(result.stdout + result.stderr).strip() or f'exit code {result.returncode}'}")
    print("A loop stopped in the middle leaves its entry `in progress` with its branch; the next /todo-loop in that worker continues it.")


# --- update ------------------------------------------------------------------------------------


def worker_repos(layout: Layout, path: Path) -> list[tuple[Path, str]]:
    """The repositories of a worker and their base branches, from the "Areas" table of the rules file (the
    game's first, so its pointer does not decide where the engine goes); the game alone at `main` without one."""
    rows = rule_area_table(layout.main) or [{"area": None, "repo": ".", "base": DEFAULT_BASE}]
    rows = sorted(rows, key=lambda row: row["repo"] != ".")
    return [(path / row["repo"], row["base"]) for row in rows]


def update_one(layout: Layout, k: int, path: Path, sessions: list[dict] | None) -> dict:
    """Detach every repository of a worker at the tip of its base, what the loop calls "go to the base"; the
    branch it held stays as it is. Refused for a session that has not finished and for uncommitted changes."""
    session = session_in(sessions or [], path)
    if is_active(session):
        raise WorkerError(f"w{k}: its session runs ({describe(session)}); stop it first")
    repos = worker_repos(layout, path)
    for repo, _base in repos:
        if not (repo / ".git").exists():
            raise WorkerError(f"w{k}: no repository {repo}")
        changed = dirty(repo, ignore_submodules=repo == path)
        if changed:
            files = [line[3:] for line in changed.splitlines()]
            more = f" and {len(files) - 3} more" if len(files) > 3 else ""
            raise WorkerError(f"w{k}: {repo} has uncommitted changes ({', '.join(files[:3])}{more})")
    moved = []
    for repo, base in repos:
        before = head_json(repo)
        git("switch", "--quiet", "--detach", base, cwd=repo)
        after = head_json(repo)
        moved.append({"repo": str(repo), "base": base, "from": before["branch"] or before["commit"], "to": after["commit"], "changed": before["commit"] != after["commit"] or before["branch"] is not None})
    notes = []
    if layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists() and any(repo == path / SHARED_SUBMODULE for repo, _ in repos):
        pointer = git("rev-parse", f"HEAD:{SHARED_SUBMODULE}", cwd=path, check=False)
        engine = git("rev-parse", "HEAD", cwd=path / SHARED_SUBMODULE, check=False)
        if pointer and engine and pointer != engine:
            notes.append(f"the game's pointer of {SHARED_SUBMODULE}/ is {pointer[:9]}, not the tip of its base ({engine[:9]}): the pointer commit is not made yet")
    return {"k": k, "name": f"{WORKER_PREFIX}{k}", "repos": moved, "notes": notes}


def cmd_update(layout: Layout, args) -> None:
    workers = chosen_workers(layout, args.workers)
    sessions = list_sessions(args.claude)
    if sessions is None:
        raise WorkerError(f"`{args.claude} agents --json` did not answer: cannot tell whether a session runs")
    results, errors = [], []
    for k, path in workers:
        try:
            results.append(update_one(layout, k, path, sessions))
        except WorkerError as error:
            errors.append(str(error))
    if args.json:
        print(json.dumps({"updated": results, "errors": errors}, indent=2))
    else:
        for result in results:
            parts = [f"{Path(r['repo']).name if Path(r['repo']) != layout.worker(result['k']) else 'game'} {r['from']} -> {r['base']} ({r['to']})" for r in result["repos"]]
            print(f"{result['name']}: {'; '.join(parts)}")
            for note in result["notes"]:
                print(f"  note: {note}")
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
    if errors:
        raise WorkerError("; ".join(errors) if args.json else f"{len(errors)} worker(s) not updated")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="worker.py", description="The folders of the parallel todo-loop workers (git worktrees).")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("create", help="make the workers w1..wN that are missing")
    p.add_argument("count", type=int)
    p.add_argument("--base", default=DEFAULT_BASE, help=f"the commit a new worker starts on (default {DEFAULT_BASE})")
    p.add_argument("--preset", help="the configure preset (default: macos-debug on macOS)")
    p.add_argument("--no-configure", action="store_true", help="do not run the first configure")
    p = sub.add_parser("list", help="the workers, their branches, clean or not, their background sessions")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true", help="print JSON (for tools/vscode/todo-workers)")
    p.add_argument("--store", action="store_true", help="with --json: also the entries in progress and in review of the todo store")
    p.add_argument("--usage", action="store_true", help="with --json: also the tokens of each session and its agents (from the transcripts)")
    p.add_argument("--activity", action="store_true", help="with --json: also the last steps of each session and of its running agents (from the transcripts)")
    p = sub.add_parser("check", help="the shared config and every worker")
    p.add_argument("--fix", action="store_true", help="set a broken core.worktree of the shared submodule back")
    p = sub.add_parser("remove", help="delete a clean worker folder")
    p.add_argument("k", type=int)
    p = sub.add_parser("run", help="start a Claude session in a worker (arguments for claude after --)")
    p.add_argument("k", type=int)
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--print", dest="print_only", action="store_true", help="only print the command")
    p = sub.add_parser("start", help="start /todo-loop as a background Claude session in the workers (arguments for claude after --)")
    p.add_argument("workers", nargs="*", type=int, help="the workers (default: all)")
    p.add_argument("--slices", type=int, default=1, help="N of `/todo-loop N`: the slices each loop takes (default 1)")
    p.add_argument("--loop-args", default="", help="more arguments of /todo-loop, e.g. 'models=reviewer:sonnet'")
    p.add_argument("--report-to", help="the session (a name of `claude agents`) every loop sends its reports to (`report-to=` of /todo-loop)")
    p.add_argument("--delay", type=int, default=DEFAULT_DELAY_S, help=f"seconds between two starts (default {DEFAULT_DELAY_S})")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--print", dest="print_only", action="store_true", help="only print the commands")
    p = sub.add_parser("merge", help="merge the branch of an entry in review into its base, close the entry, delete the branch (the quick merge of the VS Code panel)")
    p.add_argument("id")
    p.add_argument("--dry-run", action="store_true", help="only say what it would do and what stops it")
    p.add_argument("--json", action="store_true", help="print JSON (for tools/vscode/todo-workers)")
    p = sub.add_parser("update", help="detach the repositories of the workers at the tips of their base branches (a finished session, a clean folder)")
    p.add_argument("workers", nargs="*", type=int, help="the workers (default: all)")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true", help="print JSON (for tools/vscode/todo-workers)")
    p = sub.add_parser("stop", help="stop the background sessions of the workers")
    p.add_argument("workers", nargs="*", type=int, help="the workers (default: all)")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    return parser


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    rest = argv[argv.index("--") + 1 :] if "--" in argv else []
    argv = argv[: argv.index("--")] if "--" in argv else argv
    args = build_parser().parse_args(argv)
    args.rest = rest
    try:
        layout = find_layout(Path.cwd())
        commands = {"create": cmd_create, "list": cmd_list, "check": cmd_check, "remove": cmd_remove, "run": cmd_run, "start": cmd_start, "stop": cmd_stop, "merge": cmd_merge, "update": cmd_update}
        commands[args.command](layout, args)
    except WorkerError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
