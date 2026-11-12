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
    python3 engine/tools/todo/worker.py relaunch [k ...]  # continue the sessions the usage limit stopped (after the reset)
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
from datetime import datetime, timezone
from pathlib import Path

if sys.version_info < (3, 10):  # before the first `X | None` evaluated at import, which 3.9 turns into a TypeError
    sys.exit(f"error: worker.py needs Python 3.10 or newer, this is {sys.version.split()[0]} ({sys.executable})")

SHARED_SUBMODULE = "engine"
WORKER_PREFIX = "w"
WORKERS_SUFFIX = ".workers"
DEFAULT_BASE = "main"
# Ignored files of the main checkout that a worker needs and shares by a symbolic link: the store's
# token, the permissions of Claude Code, the Python venv of the reflection generator.
LINKED_FILES = (".env", ".claude/settings.local.json", ".venv", f"{SHARED_SUBMODULE}/.venv")
DEFAULT_PRESETS = {"darwin": "macos-debug"}
# The name of a worker's background session (`claude --bg --name`): shared by all its sessions, so attach / logs go by id (session_ref).
SESSION_PREFIX = "todo-w"
DEFAULT_DELAY_S = 60
# The conductor of /todo-loop runs git, the store and agents, it writes no code: a cheaper model and less
# thinking than an interactive session (todo-loop/SKILL.md, "Roles and models"). `start --model/--effort`
# or the same flags after `--` change it.
DEFAULT_LOOP_MODEL = "sonnet"
DEFAULT_LOOP_EFFORT = "medium"
REPORT_TO_NAME = re.compile(r"[A-Za-z0-9._-]+")
# What a relaunched session is told (`relaunch`): the conversation is resumed, so the loop knows where it was.
RELAUNCH_PROMPT = (
    "The usage limit has reset. Go on with the /todo-loop exactly where it stopped: an agent that the limit cut off "
    "before it reported is started again with the same task (look at the branch and the slice plan first: some of its "
    "work may be committed already), then continue as the skill says."
)
# The usage limit in a transcript: an API error record (`You've hit your 5-hour limit · resets 10:30pm (Europe/...)`).
LIMIT_ERROR = "rate_limit"
LIMIT_STATUS = 429
RESETS = re.compile(r"resets\s+([^·]+?)\s*(?:·|$)")


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
    # git writes core.worktree with forward slashes on every platform; relpath gives backslashes on Windows
    return Path(os.path.relpath(layout.shared, gitdir)).as_posix()


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
    """The first configure of the worker's own build/, so its first build needs none. ENGINE_CCACHE_BASE_DIR
    is the folder of the workers: ccache hashes the paths under it relative to the build folder, so the
    workers share cache hits (cmake/BagiCompilerCache.cmake; __FILE__ and debug paths are relative there,
    the main checkout keeps absolute ones and shares nothing with them)."""
    env = dict(os.environ)
    for venv in (layout.main / SHARED_SUBMODULE / ".venv" / "bin", layout.main / ".venv" / "bin"):
        if venv.is_dir():
            env["PATH"] = f"{venv}{os.pathsep}{env.get('PATH', '')}"
            break
    command = ["cmake", "--preset", preset, f"-DENGINE_CCACHE_BASE_DIR={layout.root}"]
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


def loop_session_flags(args) -> list[str]:
    """`--model` / `--effort` of a loop session, unless the arguments after `--` name them."""
    flags = []
    if args.model and "--model" not in args.rest:
        flags += ["--model", args.model]
    if args.effort and "--effort" not in args.rest:
        flags += ["--effort", args.effort]
    return flags


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


def session_ref(session: dict) -> str:
    """How to name a session to `claude attach` / `logs` / `stop`: its id. The name (todo-w<k>) is shared by every
    earlier session of the worker that `claude agents` still lists, and claude refuses a name that is not unique."""
    return str(session.get("id") or session.get("sessionId") or session.get("name"))


def limit_hit(session: dict | None) -> dict | None:
    """The usage limit that stopped a session: its last answer in the transcript is the API's rate-limit error.
    `{"text", "resets", "time"}` (`resets` is the time the message names, e.g. `10:30pm (Europe/Belgrade)`, or None),
    or None when the session answered after it or has no transcript."""
    if not session or not session.get("cwd") or not session.get("sessionId"):
        return None
    for record in reversed(tail_records(transcript_of(session["cwd"], session["sessionId"]))):
        if record.get("type") != "assistant":
            continue
        if record.get("error") != LIMIT_ERROR and record.get("apiErrorStatus") != LIMIT_STATUS:
            return None
        content = (record.get("message") or {}).get("content")
        text = content if isinstance(content, str) else " ".join(str(p.get("text", "")) for p in content if isinstance(p, dict)) if isinstance(content, list) else ""
        text = one_line(text, 200)
        found = RESETS.search(text)
        return {"text": text, "resets": found.group(1) if found else None, "time": record.get("timestamp")}
    return None


def stopped_file() -> Path:
    """The sessions `stop` ended: `{session id: ms since the epoch}`, outside every repository."""
    return claude_home() / "todo" / "stopped-sessions.json"


def read_stopped() -> dict:
    try:
        marks = json.loads(stopped_file().read_text())
    except (OSError, ValueError):
        return {}
    return marks if isinstance(marks, dict) else {}


def mark_stopped(session: dict) -> None:
    """Remember that `stop` ended this session, so `list` says stopped and not done (`claude agents` shows a stopped
    session as finished). The newest STOPPED_KEEP are kept; a mark that cannot be written is skipped."""
    ident = session.get("id") or session.get("sessionId")
    if not ident:
        return
    marks = read_stopped()
    marks[str(ident)] = int(time.time() * 1000)
    newest = dict(sorted(((k, v) for k, v in marks.items() if isinstance(v, (int, float))), key=lambda kv: kv[1])[-STOPPED_KEEP:])
    path = stopped_file()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(newest))
        temporary.replace(path)
    except OSError:
        pass


def stopped_at(session: dict | None) -> int | None:
    """When `stop` ended this session (ms since the epoch), or None: it is running, or it ended by itself. A session
    started or resumed afterwards has another id, so the mark does not follow it."""
    if not session or session.get("state") != "done":
        return None
    when = read_stopped().get(str(session.get("id") or session.get("sessionId")))
    return int(when) if isinstance(when, (int, float)) else None


def is_waiting(session: dict | None) -> bool:
    """A session that waits for the person (`claude agents`): an interactive one says status `waiting`, a background one state
    `blocked` (a permission, a question, or a run that ended asking the owner something)."""
    return session is not None and session.get("state") != "done" and (session.get("status") == "waiting" or session.get("state") == "blocked")


def turn_ended(session: dict | None) -> bool:
    """The session's last answer in its transcript ended the turn with text (`stop_reason` `end_turn`, no tool call). A
    background run that ends its report with an offer to the owner stays `blocked` in `claude agents` for hours, though
    nothing waits: a permission or an `AskUserQuestion` is a pending tool call, so it never looks like this."""
    if not session or not session.get("cwd") or not session.get("sessionId"):
        return False
    for record in reversed(tail_records(transcript_of(session["cwd"], session["sessionId"]))):
        if record.get("type") != "assistant":
            continue
        message = record.get("message") or {}
        content = message.get("content") if isinstance(message.get("content"), list) else []
        if any(isinstance(part, dict) and part.get("type") == "tool_use" for part in content):
            return False
        return message.get("stop_reason") == "end_turn"
    return False


def waits_for_owner(session: dict | None) -> bool:
    """`is_waiting`, less a run that only ended its turn (`turn_ended`)."""
    return is_waiting(session) and not turn_ended(session)


def is_active(session: dict | None) -> bool:
    """A session that has not finished its work: busy, or waiting for the person (a permission)."""
    return session is not None and session.get("state") != "done"


def is_running(session: dict | None) -> bool:
    """`is_active`, less a run that ended its turn and only stays listed as blocked (`turn_ended`): `start` and `update`
    treat it as finished, and `start` stops it before it starts the next loop (one session per worker)."""
    return is_active(session) and not (is_waiting(session) and turn_ended(session))


def describe(session: dict | None) -> str:
    if session is None:
        return "no session"
    started = session.get("startedAt")
    since = ""
    if isinstance(started, (int, float)):
        minutes = int((time.time() * 1000 - started) / 60000)
        since = f", {minutes // 60} h {minutes % 60} min" if minutes >= 60 else f", {minutes} min"
    state = session.get("state") or session.get("status") or "?"
    limit = limit_hit(session)
    if limit:
        resets = f", resets {limit['resets']}" if limit["resets"] else ""
        return f"{session.get('name') or session.get('id')}: {state}/{session.get('status', '?')}{since}, stopped by the usage limit{resets}: worker.py relaunch after the reset"
    stopped = stopped_at(session)
    if stopped is not None:
        minutes = int((time.time() * 1000 - stopped) / 60000)
        when = f"{minutes // 60} h {minutes % 60} min" if minutes >= 60 else f"{minutes} min"
        return f"{session.get('name') or session.get('id')}: stopped by worker.py stop {when} ago (start or relaunch to go on)"
    if is_waiting(session) and turn_ended(session):
        return f"{session.get('name') or session.get('id')}: {state}/{session.get('status', '?')}{since}, its run ended (nothing waits; stop clears it)"
    waiting = f", waits for you ({session.get('waitingFor') or 'input'}): claude attach {session_ref(session)}" if is_waiting(session) else ""
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
    keep = ("id", "area", "title", "status", "statusNote", "branch", "planShown", "planProgress", "blocks", "slicing", "costCap", "slices", "stagesPlan")
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
        # where the claim recorded the branch was cut from and made (null for an entry claimed before that); `base` above is the area's
        row["takenBase"] = entry.get("base") or None
        row["takenOrigin"] = entry.get("origin") or None
        row.update(
            branch_origin(Path(row["repo"]), row["branch"], row["base"], row["takenBase"]) if area and row["branch"] else {"local": None, "foreignBase": None}
        )
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


def todo_cli(layout: Layout, *args: str, actor: str = MERGE_ACTOR) -> str:
    """todo.py (next to this file) run from the main checkout; its error as a WorkerError."""
    todo = Path(__file__).resolve().parent / "todo.py"
    try:
        result = subprocess.run(
            [sys.executable, str(todo), "--actor", actor, *args], cwd=layout.main, capture_output=True, text=True, timeout=60
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
    """Only for an entry claimed before the store recorded its base (`branch_origin` uses the stored one
    otherwise). The base branch other than `base` that `branch` was cut from, if it looks so: the branch holds commits
    of that branch (local or fetched) that `base` has not. Such a branch is another checkout's (a loop of
    another game took an entry of the engine area); merged here it would bring those commits along. None when
    no other base branch is known or the branch shares nothing with one beyond `base`."""
    for other in base_candidates(repo, base):
        shared = git("merge-base", branch, other, cwd=repo, check=False)
        if shared and not git_ok("merge-base", "--is-ancestor", shared, base, cwd=repo):
            return other
    return None


def branch_origin(repo: Path, branch: str, base: str, taken_base: str | None = None) -> dict:
    """`local`: the branch exists in `repo` (an entry taken in another checkout or on another machine has its
    branch there, never here); `foreignBase`, for a local branch: `taken_base` (the base the claim stored)
    when it differs from `base`, else None; with no `taken_base` (an entry claimed before the store recorded
    it) what the commit guess `foreign_base` says."""
    if not repo.is_dir() or not git_ok("rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", cwd=repo):
        return {"local": False, "foreignBase": None}
    if taken_base:
        return {"local": True, "foreignBase": taken_base if taken_base != base else None}
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
    taken_base = entry.get("base") or None
    if taken_base:
        if taken_base != base:
            raise WorkerError(f"{branch} was cut from {taken_base} in {entry.get('origin') or 'another checkout'}, not from {base}: merge it there")
    else:
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
        "blocks": [b["id"] for b in entry.get("blocks") or [] if isinstance(b, dict) and b.get("status") != "closed"],
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
    # the entries this close let go: their last open blocker was this one (only when the entry is closed now)
    result["unblocked"] = []
    if result.get("status") == "closed":
        for other in plan.get("blocks") or []:
            try:
                shown = json.loads(todo_cli(layout, "show", other, "--json"))
            except (WorkerError, ValueError):
                continue
            if not shown.get("blocked") and shown.get("status") == "open":
                result["unblocked"].append({"id": other, "title": shown.get("title")})
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
STOPPED_KEEP = 50  # the stop marks kept (stopped_file)
REPORT_TOOLS = ("SubagentHandback", "SendMessage")  # the tool an agent hands its final report back with


def claude_home() -> Path:
    return Path(os.environ.get("CLAUDE_CONFIG_DIR") or Path.home() / ".claude")


def projects_dir() -> Path:
    return claude_home() / "projects"


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
    records = tail_records(main)
    return {"updated": last_step_ms(records, main), "events": events_of(records, ACTIVITY_EVENTS), "agents": agents[:4]}


def last_step_ms(records: list[dict], path: Path) -> int:
    """When the session last did something: the newest `timestamp` of its records. Claude Code appends
    records without one (`mode`, `permission-mode`, `session-owner`, ...) to a session that ended long ago,
    so the file's mtime would say a finished worker "ended" minutes ago; the mtime is only the fallback."""
    for record in reversed(records):
        stamp = record.get("timestamp")
        if isinstance(stamp, str):
            try:
                return int(datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp() * 1000)
            except ValueError:
                continue
    return int(path.stat().st_mtime * 1000)


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


# --- the panel's checks: what a review branch meets, what a session asks, what an entry cost --------------
#
# Everything below feeds tools/vscode/todo-workers. Reads only, except the two local files of the panel
# (the usage ledger and the worker queues), both in ~/.claude/todo/, outside every repository.

REVIEW_SUBJECTS = 8  # the base's commits named for a review branch whose base moved
POINTER_SUBJECTS = 10  # the commits of the engine's base the game's pointer has not yet


def merge_conflicts(repo: Path, ours: str, theirs: str) -> list[str]:
    """The files `git merge-tree` cannot merge when `theirs` is merged into `ours` (empty: a clean merge)."""
    tree = subprocess.run(["git", "merge-tree", "--write-tree", "--name-only", ours, theirs], cwd=repo, capture_output=True, text=True)
    if tree.returncode == 0:
        return []
    files = []
    for line in tree.stdout.splitlines()[1:]:
        if not line.strip():
            break
        files.append(line.strip())
    return files or [(tree.stderr or tree.stdout).strip() or "git merge-tree failed"]


def changed_files(repo: Path, since: str, until: str) -> set[str]:
    return {line for line in git("diff", "--name-only", since, until, cwd=repo, check=False).splitlines() if line}


def review_checks(entries: list[dict]) -> None:
    """For every local entry in review, in place: `conflicts` with its base, `behind` (commits of the base since
    the branch was cut, with their subjects), `riskFiles` (files those commits changed that the branch changes
    too: no conflict for git, but the place a merge breaks), and `conflictsWith` the other review branches of the
    same repository (merging one first makes the other conflict)."""
    local = [e for e in entries if e.get("status") == "review" and e.get("local") and not e.get("foreignBase") and e.get("repo") and e.get("branch")]
    for entry in local:
        repo, base, branch = Path(entry["repo"]), entry["base"], entry["branch"]
        shared = git("merge-base", base, branch, cwd=repo, check=False)
        entry["conflicts"] = merge_conflicts(repo, base, branch)
        behind = git("log", "--format=%h %s", f"{shared}..{base}", cwd=repo, check=False).splitlines() if shared else []
        entry["behind"] = {"count": len(behind), "subjects": behind[:REVIEW_SUBJECTS]}
        entry["riskFiles"] = sorted(changed_files(repo, shared, base) & changed_files(repo, shared, branch)) if shared and behind else []
        entry["conflictsWith"] = []
    for i, first in enumerate(local):
        for second in local[i + 1 :]:
            if first["repo"] != second["repo"]:
                continue
            files = merge_conflicts(Path(first["repo"]), first["branch"], second["branch"])
            if files:
                first["conflictsWith"].append({"id": second["id"], "files": files})
                second["conflictsWith"].append({"id": first["id"], "files": files})


def pending_ask(session: dict | None) -> dict | None:
    """What a waiting session asks: the tool call of its last answer that has no result yet (a permission, or the
    questions of `AskUserQuestion`), as `{"tool", "text"}`; None when nothing is pending."""
    if not session or not session.get("cwd") or not session.get("sessionId"):
        return None
    answered: set[str] = set()
    for record in reversed(tail_records(transcript_of(session["cwd"], session["sessionId"]))):
        content = (record.get("message") or {}).get("content")
        parts = content if isinstance(content, list) else []
        if record.get("type") == "user":
            answered |= {str(p.get("tool_use_id")) for p in parts if isinstance(p, dict) and p.get("type") == "tool_result"}
            continue
        if record.get("type") != "assistant":
            continue
        for part in reversed(parts):
            if not isinstance(part, dict) or part.get("type") != "tool_use" or str(part.get("id")) in answered:
                continue
            given = part.get("input") if isinstance(part.get("input"), dict) else {}
            name = str(part.get("name"))
            if name == "AskUserQuestion":
                questions = [str(q.get("question", "")) for q in given.get("questions", []) if isinstance(q, dict)]
                return {"tool": name, "text": one_line(" / ".join(q for q in questions if q), 400)}
            return {"tool": name, "text": tool_text(name, given)}
        return None
    return None


def ledger_file() -> Path:
    """The tokens of every loop session by entry: `{session id: {entry, worker, models, updated}}`. A session is
    counted for the entry its worker held when the panel last saw it (a loop of N slices: the last one)."""
    return claude_home() / "todo" / "usage-ledger.json"


LEDGER_KEEP = 500


def update_ledger(workers: list[dict]) -> dict[str, dict]:
    """Write the usage of the sessions that hold an entry now into the ledger and return the tokens by entry and
    model: `{entry id: {model: counts}}`. A ledger that cannot be read or written only loses the history."""
    path = ledger_file()
    try:
        ledger = json.loads(path.read_text())
        ledger = ledger if isinstance(ledger, dict) else {}
    except (OSError, ValueError):
        ledger = {}
    changed = False
    for row in workers:
        session, usage, entry = row.get("session"), row.get("usage"), row.get("entry")
        ident = session and session.get("id")
        if not ident or not usage or not entry:
            continue
        ledger[str(ident)] = {"entry": entry["id"], "worker": row["name"], "models": usage.get("models") or {}, "updated": int(time.time() * 1000)}
        changed = True
    if changed:
        newest = dict(sorted(ledger.items(), key=lambda kv: kv[1].get("updated", 0) if isinstance(kv[1], dict) else 0)[-LEDGER_KEEP:])
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_suffix(".tmp")
            temporary.write_text(json.dumps(newest))
            temporary.replace(path)
        except OSError:
            pass
        ledger = newest
    by_entry: dict[str, dict] = {}
    for record in ledger.values():
        if not isinstance(record, dict) or not record.get("entry"):
            continue
        models = by_entry.setdefault(record["entry"], {})
        for model, counts in (record.get("models") or {}).items():
            add_counts(models.setdefault(model, {}), counts)
    return by_entry


def pointer_state(layout: Layout) -> dict | None:
    """In a game: where the pointer of `engine/` stands against the engine's checkout and its base branch.
    `recorded` (the commit of HEAD), `checkout` (engine/'s HEAD), `branch`, `base`, `moved` (the checkout is not
    the recorded commit: `git status` shows engine modified), `ahead` (commits of the base the pointer has not,
    with subjects). None outside a game."""
    if layout.shared is None:
        return None
    engine = layout.main / SHARED_SUBMODULE
    tree = git("ls-tree", "HEAD", SHARED_SUBMODULE, cwd=layout.main, check=False).split()
    recorded = tree[2] if len(tree) >= 3 else ""
    checkout = git("rev-parse", "HEAD", cwd=engine, check=False)
    row = next((r for r in rule_area_table(layout.main) if (layout.main / r["repo"]).resolve() == engine.resolve()), None)
    base = row["base"] if row else None
    ahead = git("log", "--format=%h %s", f"{recorded}..{base}", cwd=engine, check=False).splitlines() if base and recorded else []
    return {
        "recorded": recorded[:9],
        "checkout": checkout[:9],
        "branch": git("symbolic-ref", "--quiet", "--short", "HEAD", cwd=engine, check=False) or None,
        "base": base,
        "moved": bool(recorded) and checkout != recorded,
        "ahead": {"count": len(ahead), "subjects": ahead[:POINTER_SUBJECTS]},
    }


# --- cost: what an entry cost so far against its cap (todo-loop/SKILL.md, "Through") -----------------------

# API list prices in dollars per million tokens, the same table as the panel's `todoWorkers.prices` default; the
# first key that is part of a model id wins. `~/.claude/todo/prices.json` (same shape) replaces it.
DEFAULT_PRICES = {
    "fable": {"input": 8, "output": 40, "cacheWrite": 10, "cacheRead": 0.2},
    "mythos": {"input": 8, "output": 40, "cacheWrite": 10, "cacheRead": 0.2},
    "opus": {"input": 4, "output": 20, "cacheWrite": 5, "cacheRead": 0.2},
    "sonnet": {"input": 2, "output": 10, "cacheWrite": 2.5, "cacheRead": 0.1},
    "haiku": {"input": 0.1, "output": 0.5, "cacheWrite": 0.125, "cacheRead": 0.01},
}


def prices() -> dict:
    try:
        own = json.loads((claude_home() / "todo" / "prices.json").read_text())
        return own if isinstance(own, dict) and own else DEFAULT_PRICES
    except (OSError, ValueError):
        return DEFAULT_PRICES


def dollars_of(models: dict, table: dict | None = None) -> float:
    """The tokens by model (`entryUsage[id]`) at the prices; a model no key names costs nothing (said by `unpriced`)."""
    table = table or prices()
    total = 0.0
    for model, counts in models.items():
        price = next((value for key, value in table.items() if key in str(model)), None)
        if price:
            total += sum(counts.get(key, 0) * price.get(key, 0) for key in USAGE_KEYS) / 1_000_000
    return round(total, 2)


def entry_cost(layout: Layout, entry_id: str, claude: str = "claude") -> dict:
    """The entry's cost over every loop session that held it: the ledger brought up to date with the sessions of
    the workers now (a session counts for the entry whose branch its worker has checked out at this moment)."""
    state = list_json(layout, argparse.Namespace(claude=claude, store=False, usage=True, activity=False))
    models = (state.get("entryUsage") or {}).get(entry_id) or {}
    table = prices()
    entry = json.loads(todo_cli(layout, "show", entry_id, "--json", actor="worker"))
    cap = entry.get("costCap")
    spent = dollars_of(models, table)
    unpriced = sorted(model for model in models if not any(key in str(model) for key in table))
    return {"entry": entry_id, "dollars": spent, "cap": cap, "over": cap is not None and spent >= cap, "models": models, "unpriced": unpriced}


def cmd_cost(layout: Layout, args) -> None:
    cost = entry_cost(layout, args.id, args.claude)
    if args.json:
        print(json.dumps(cost))
        return
    cap = f" of the cap ${cost['cap']:g}{' (over)' if cost['over'] else ''}" if cost["cap"] is not None else " (no cap)"
    print(f"{cost['entry']}: ${cost['dollars']:.2f}{cap}" + (f"; no price for {', '.join(cost['unpriced'])}" if cost["unpriced"] else ""))


# --- remarks: the owner sends a review branch back (todo-loop/SKILL.md, "Remarks") -------------------------

REMARKS_ACTOR = "owner"


def free_worker(layout: Layout, sessions: list[dict] | None) -> tuple[int, Path] | None:
    """A worker with no running session and a clean tree, the ones that hold no `todo/*` branch first."""
    free = []
    for k, path in layout.workers():
        if is_running(session_in(sessions or [], path)):
            continue
        if dirty(path, ignore_submodules=True) or (layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists() and dirty(path / SHARED_SUBMODULE)):
            continue
        heads = [head_json(path)] + ([head_json(path / SHARED_SUBMODULE)] if layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists() else [])
        holds = any(str(h.get("branch") or "").startswith("todo/") for h in heads)
        free.append((holds, k, path))
    if not free:
        return None
    _holds, k, path = min(free, key=lambda item: (item[0], item[1]))
    return k, path


def cmd_remarks(layout: Layout, args) -> None:
    """Store the owner's remarks on an entry in review as a plan of kind `remarks`, set the entry back to
    `in progress` and queue it first on the worker that holds its branch (or a free one); `--start` starts it."""
    entry = json.loads(todo_cli(layout, "show", args.id, "--json", actor=REMARKS_ACTOR))
    branch = entry.get("branch")
    if entry.get("status") != "review" or not branch:
        raise WorkerError(f"{args.id} is {entry.get('status')}, not in review with a branch")
    table = {row["area"]: row for row in rule_area_table(layout.main)}
    area = table.get(entry.get("area"))
    if area is None:
        raise WorkerError(f"{args.id} is of the area {entry.get('area')}, not of this checkout")
    repo = (layout.main / area["repo"]).resolve()
    if not git_ok("rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", cwd=repo):
        raise WorkerError(f"no branch {branch} in {repo}: it was made in another checkout")
    text = (sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")).strip()
    if not text:
        raise WorkerError("the remarks are empty")
    branch_id = branch.split("/", 1)[1]
    plans = json.loads(todo_cli(layout, "plan", "list", "--entry", args.id, "--kind", "remarks", "--json", actor=REMARKS_ACTOR) or "[]")
    round_no = len(plans) + 1
    name = f"todo-loop-{branch_id}-remarks-{round_no}.md"
    local = claude_home() / "plans" / name
    local.parent.mkdir(parents=True, exist_ok=True)
    local.write_text(f"# Remarks of the owner, round {round_no}: {entry['title']}\n\nBranch `{branch}`, entry `{args.id}`.\n\n{text}\n", encoding="utf-8")
    todo_cli(layout, "plan", "put", name, "--file", str(local), "--entry", args.id, "--kind", "remarks", actor=REMARKS_ACTOR)
    todo_cli(layout, "edit", args.id, "--status", "in progress", actor=REMARKS_ACTOR)
    # the review note stays at the end: its slice suffix ("slice K of M, after the merge ...", "stacked on ...") is the loop's to keep
    was = re.sub(r"^returned with remarks, round \d+ \([^)]*\); was: ", "", entry.get("statusNote") or "")
    todo_cli(layout, "note", args.id, "--note", f"returned with remarks, round {round_no} ({name}); was: {was}", actor=REMARKS_ACTOR)
    sessions = list_sessions(args.claude)
    holder = worktree_of(repo, branch)
    chosen = None
    for k, path in layout.workers():
        if holder is not None and (holder.resolve() == path.resolve() or holder.resolve() == (path / SHARED_SUBMODULE).resolve()):
            chosen = (k, path)
    chosen = chosen or free_worker(layout, sessions)
    result = {"entry": args.id, "plan": name, "round": round_no, "worker": None, "started": False}
    if chosen is not None:
        queues = read_queues(layout)
        key = f"{WORKER_PREFIX}{chosen[0]}"
        for items in queues.values():
            if args.id in items:
                items.remove(args.id)
        queues.setdefault(key, []).insert(0, args.id)
        write_queues(layout, queues)
        result["worker"] = key
        if args.start and not is_running(session_in(sessions or [], chosen[1])):
            start_args = argparse.Namespace(
                workers=[chosen[0]], slices=1, loop_args="", report_to=args.report_to, delay=0, model=DEFAULT_LOOP_MODEL,
                effort=DEFAULT_LOOP_EFFORT, claude=args.claude, print_only=False, rest=[], queued_only=True, wait_free=0, detach=False,
            )
            cmd_start(layout, start_args)
            result["started"] = True
    if args.json:
        print(json.dumps(result))
        return
    where = f"queued first on {result['worker']}" + (", started" if result["started"] else "") if result["worker"] else "no free worker: start it with `/todo-loop " + args.id + "`"
    print(f"{args.id}: remarks round {round_no} stored as {name}, back to in progress; {where}")


def queue_file() -> Path:
    """The entries the panel starts next in each worker: `{"<root>": {"w1": [ids]}}`, by the workers' root."""
    return claude_home() / "todo" / "worker-queues.json"


def read_queues(layout: Layout) -> dict[str, list[str]]:
    try:
        data = json.loads(queue_file().read_text())
    except (OSError, ValueError):
        return {}
    mine = data.get(str(layout.root)) if isinstance(data, dict) else None
    return {k: [str(i) for i in v] for k, v in mine.items() if isinstance(v, list)} if isinstance(mine, dict) else {}


def write_queues(layout: Layout, queues: dict[str, list[str]]) -> None:
    path = queue_file()
    try:
        data = json.loads(path.read_text())
        data = data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        data = {}
    data[str(layout.root)] = {k: v for k, v in queues.items() if v}
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=1))
    temporary.replace(path)


def cmd_queue(layout: Layout, args) -> None:
    """`queue` (all), `queue <k> --add <id> [--first]`, `queue <k> --remove <id>`, `queue <k> --pop`, `queue <k> --clear`."""
    queues = read_queues(layout)
    names = {f"{WORKER_PREFIX}{k}" for k, _path in layout.workers()}
    if args.k is not None:
        name = f"{WORKER_PREFIX}{args.k}"
        if name not in names:
            raise WorkerError(f"no worker {name} in {layout.root}")
        items = queues.get(name, [])
        popped = None
        if args.add:
            if not re.fullmatch(r"[A-Za-z0-9_-]+", args.add):
                raise WorkerError(f"not an entry id: {args.add}")
            for other in queues.values():
                if args.add in other:
                    other.remove(args.add)  # an entry waits in one queue only
            items = queues.get(name, [])
            items.insert(0, args.add) if args.first else items.append(args.add)
        if args.remove and args.remove in items:
            items.remove(args.remove)
        if args.pop and items:
            popped = items.pop(0)
        if args.clear:
            items = []
        queues[name] = items
        if args.add or args.remove or args.pop or args.clear:
            write_queues(layout, queues)
        if args.pop:
            print(json.dumps({"popped": popped}) if args.json else (popped or ""))
            return
    shown = {name: queues.get(name, []) for name in sorted(names)}
    if args.json:
        print(json.dumps(shown))
        return
    for name, items in shown.items():
        print(f"{name}: {' '.join(items) if items else '(empty)'}")


PATH_IN_TEXT = re.compile(r"`([A-Za-z0-9_.\-/]+/[A-Za-z0-9_.\-/{},*]*|[A-Za-z0-9_\-/]+\.[A-Za-z]{1,5})`")


def paths_in(text: str) -> set[str]:
    """The repository paths an entry or a plan names in code spans (`src/Mahjong/Table/`, `config/nodes/X.xml`),
    with a brace list split and a glob or a `{h,cpp}` cut to its folder."""
    found = set()
    for match in PATH_IN_TEXT.findall(text or ""):
        path = match.split("{")[0].split("*")[0].rstrip(".")
        if path and "/" in path:
            found.add(path)
    return found


def plan_files(text: str) -> set[str]:
    """The `## Files` section of a slice plan (`- <path>` lines)."""
    section = re.search(r"^## Files[ \t]*$(.*?)(?=^## |\Z)", text or "", re.M | re.S)
    if not section:
        return set()
    return {m.strip().strip("`") for m in re.findall(r"^\s*-\s+(\S+)", section.group(1), re.M)}


def overlapping(ours: set[str], theirs: set[str]) -> list[str]:
    """The paths of `ours` that are one of `theirs`, inside one of them or hold one (a folder and its file, a stem and its file)."""
    def meets(a: str, b: str) -> bool:
        a, b = a.rstrip("/"), b.rstrip("/")
        # a folder holds its files; a stem (`TableLayout` of `TableLayout.{h,cpp}`) holds its extensions
        return a == b or any(x.startswith(y + sep) for x, y in ((a, b), (b, a)) for sep in ("/", "."))
    return sorted({a for a in ours for b in theirs if meets(a, b)})


def cmd_overlap(layout: Layout, args) -> None:
    """The busy entries (in progress, in review) whose slice plans name files the entry `id` names: what a loop
    on `id` would meet. The entry's paths come from its text (its classifier is not run here), so it is a hint."""
    entry = json.loads(todo_cli(layout, "show", args.id, "--json"))
    ours = paths_in(entry.get("body") or "")
    busy, _error = store_entries(layout)
    found = []
    for other in busy:
        if other["id"] == args.id or not other.get("branch"):
            continue
        theirs = planned_files(layout, other)
        if theirs is None:
            continue
        shared = overlapping(ours, theirs)
        if shared:
            found.append({"id": other["id"], "status": other["status"], "files": shared})
    result = {"id": args.id, "paths": sorted(ours), "overlaps": found}
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
        return
    for item in found:
        print(f"{item['id']} ({item['status']}): {', '.join(item['files'])}")
    if not found:
        print(f"{args.id}: no busy entry names its files ({len(ours)} paths in its text)")


# --- dispatch: one read of what /todo-dispatch decides on --------------------------------------------
#
# The owner's preferences (`prefer`, `avoid`: ids, id prefixes, words of a title, area names) and the classifier's
# verdicts live in ~/.claude/todo/, by the workers' root, like the queues. A verdict is reused while the entry's
# `version` is the same and it is not older than its time to live, so the classifier reads only new or edited
# entries, and only as many as the free slots need.

VERDICT_TTL_S = 14 * 24 * 3600
VERDICT_SHORT_TTL_S = 24 * 3600  # reasons that depend on other entries
VOLATILE_REASONS = ("blocked by", "depends on unmerged", "conflicts with")  # never reused
SHORT_REASONS = ("needs engine change",)
CLASSIFY_MARGIN = 2  # candidates sent to the classifier per free slot (conflicts eat some)


def prefs_file() -> Path:
    return claude_home() / "todo" / "dispatch-prefs.json"


def verdicts_file() -> Path:
    return claude_home() / "todo" / "dispatch-verdicts.json"


def read_rooted(path: Path, layout: Layout) -> dict:
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    mine = data.get(str(layout.root)) if isinstance(data, dict) else None
    return mine if isinstance(mine, dict) else {}


def write_rooted(path: Path, layout: Layout, value: dict) -> None:
    try:
        data = json.loads(path.read_text())
        data = data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        data = {}
    data[str(layout.root)] = value
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=1, ensure_ascii=False))
    temporary.replace(path)


def split_terms(text: str | None) -> list[str]:
    return [term.strip() for term in (text or "").split(",") if term.strip()]


def term_matches(term: str, entry: dict) -> bool:
    """A term names an entry by its area, its id, a prefix of a word of its id (`ui` for `mahjong-ui-skin`) or of
    a word of its title (`UI` for "UI colours are literals"), ignoring case."""
    if term.lower() == (entry.get("area") or "").lower():
        return True
    pattern = re.compile(r"(?<![A-Za-z0-9])" + re.escape(term), re.I)
    return bool(pattern.search(entry.get("id") or "") or pattern.search(entry.get("title") or ""))


def preference_rank(prefer: list[str], entry: dict) -> int | None:
    """The index of the first `prefer` term that names the entry; None when none does."""
    return next((i for i, term in enumerate(prefer) if term_matches(term, entry)), None)


def cmd_prefs(layout: Layout, args) -> None:
    """`prefs` shows, `prefs --prefer a,b --avoid c` replaces the given fields, `prefs --clear` empties them."""
    prefs = read_rooted(prefs_file(), layout)
    if args.clear:
        prefs = {}
    for field in ("prefer", "avoid"):
        given = getattr(args, field)
        if given is not None:
            prefs[field] = split_terms(given)
    if args.clear or args.prefer is not None or args.avoid is not None:
        prefs["updated"] = datetime.now(timezone.utc).date().isoformat()
        write_rooted(prefs_file(), layout, prefs)
    shown = {"prefer": prefs.get("prefer", []), "avoid": prefs.get("avoid", []), "updated": prefs.get("updated")}
    if args.json:
        print(json.dumps(shown, ensure_ascii=False))
        return
    print(f"prefer: {', '.join(shown['prefer']) or '-'}\navoid: {', '.join(shown['avoid']) or '-'}")


def fresh_verdict(cached: dict | None, version, now: float) -> dict | None:
    if not cached or cached.get("version") != version:
        return None
    reason = cached.get("reason") or ""
    if cached.get("verdict") == "skip" and reason.startswith(VOLATILE_REASONS):
        return None
    ttl = VERDICT_SHORT_TTL_S if reason.startswith(SHORT_REASONS) else VERDICT_TTL_S
    return cached if now - float(cached.get("at") or 0) <= ttl else None


def open_entries(layout: Layout) -> list[dict]:
    """The entries open, in progress and in review of the areas of the rules file."""
    entries = json.loads(todo_cli(layout, "list", "--status", "open,in progress,review", "--json") or "[]")
    areas = rule_areas(layout.main)
    return [e for e in entries if isinstance(e, dict) and (areas is None or e.get("area") in areas)]


def record_verdicts(layout: Layout, source: str) -> int:
    """Stores the classifier's answer (its JSON, a file or `-`) with each entry's current version."""
    text = sys.stdin.read() if source == "-" else Path(source).read_text()
    answer = json.loads(text[text.find("{") : text.rfind("}") + 1])
    versions = {e["id"]: e.get("version") for e in open_entries(layout)}
    cache = read_rooted(verdicts_file(), layout)
    now = time.time()
    count = 0
    for item in answer.get("entries", []):
        entry_id = item.get("id")
        if entry_id not in versions:
            continue
        keep = {key: item.get(key) for key in ("verdict", "size", "level", "touches", "reason")}
        cache[entry_id] = {**keep, "version": versions[entry_id], "at": now}
        count += 1
    known = set(versions)
    write_rooted(verdicts_file(), layout, {k: v for k, v in cache.items() if k in known})  # closed entries drop out
    return count


def planned_files(layout: Layout, entry: dict) -> set[str] | None:
    """The `## Files` of a taken entry's plans: its first slice plan, its newest one and, for a through entry, its stage
    plan (every stage). None when the store has none of them."""
    branch_id = entry["branch"].removeprefix("todo/")
    names = dict.fromkeys(name for name in (f"todo-loop-{branch_id}.md", entry.get("planShown"), entry.get("stagesPlan")) if name)
    found = None
    for name in names:
        try:
            files = plan_files(todo_cli(layout, "plan", "get", name, "--stdout"))
        except WorkerError:
            continue
        found = (found or set()) | files
    return found


def busy_files(layout: Layout, entry: dict, area: dict) -> list[str]:
    """What the branch of a taken entry changed plus the `## Files` of its plans (todo-loop, "Busy files")."""
    repo = (layout.main / area["repo"]).resolve()
    files = set(git("diff", "--name-only", f"{area['base']}...{entry['branch']}", cwd=repo, check=False).split())
    return sorted(files | (planned_files(layout, entry) or set()))


def dispatch_state(layout: Layout, args) -> dict:
    listing = list_json(layout, argparse.Namespace(claude=args.claude, store=False, usage=False, activity=False))
    saved = read_rooted(prefs_file(), layout)
    prefer = split_terms(args.prefer) if args.prefer is not None else saved.get("prefer", [])
    avoid = split_terms(args.avoid) if args.avoid is not None else saved.get("avoid", [])
    table = {row["area"]: row for row in rule_area_table(layout.main)}
    entries = open_entries(layout)
    by_id = {e["id"]: e for e in entries}
    unmerged = set()
    for area in table.values():
        repo = (layout.main / area["repo"]).resolve()
        listed = git("branch", "--list", "todo/*", "--no-merged", area["base"], "--format=%(refname:short)", cwd=repo, check=False)
        unmerged |= {(area["area"], b) for b in listed.split()}

    held = {}
    for w in listing["workers"]:
        for head in (w.get("head"), w.get("engine")):
            if head and head.get("branch"):
                held[head["branch"]] = w["name"]
    workers = []
    for w in listing["workers"]:
        branches = [h["branch"] for h in (w.get("head"), w.get("engine")) if h and h.get("branch")]
        holds = next((e["id"] for e in entries if e.get("branch") in branches and e.get("status") != "review"), None)
        why = ("running" if w["active"] and not w["waiting"] else None) or ("waiting" if w["waiting"] else None) \
            or ("dirty" if w["dirty"] else None) or ("limited" if w["limited"] else None) or (f"holds {holds}" if holds else None)
        workers.append({"k": w["k"], "name": w["name"], "free": why is None, "busy": why, "branches": branches, "queue": w["queue"]})

    cache = read_rooted(verdicts_file(), layout)
    now = time.time()
    queued = {i for w in workers for i in w["queue"]}
    busy = []
    for e in entries:
        if e.get("status") in ("in progress", "review") and e.get("branch") and e.get("area") in table:
            busy.append({"id": e["id"], "area": e["area"], "status": e["status"], "branch": e["branch"],
                         "worker": held.get(e["branch"]), "files": busy_files(layout, e, table[e["area"]])})
    for entry_id in sorted(queued):
        cached = cache.get(entry_id) or {}
        if entry_id in by_id:
            busy.append({"id": entry_id, "area": by_id[entry_id]["area"], "status": "queued", "branch": None, "worker": None,
                         "files": cached.get("touches") or []})

    candidates, skipped = [], {"avoided": [], "blocked": [], "waits": [], "branch": [], "queued": sorted(queued)}
    for e in entries:
        entry_id, status = e["id"], e.get("status")
        if entry_id in queued:
            continue
        stopped = status == "in progress" and e.get("branch") and e["branch"] not in held
        if status != "open" and not stopped:
            continue
        if any(term_matches(t, e) for t in avoid):
            skipped["avoided"].append(entry_id)
            continue
        open_blockers = [b for b in e.get("blockedBy") or [] if b.get("status") != "closed"]
        if open_blockers and not stopped:
            if all(b.get("status") == "review" for b in open_blockers):
                skipped["waits"].append({"id": entry_id, "for": [b["id"] for b in open_blockers]})
            else:
                skipped["blocked"].append({"id": entry_id, "by": [b["id"] for b in open_blockers]})
            continue
        if not stopped and any(a == e["area"] and (b == f"todo/{entry_id}" or b.startswith(f"todo/{entry_id}-")) for a, b in unmerged):
            skipped["branch"].append(entry_id)
            continue
        unblocks = sum(1 for b in e.get("blocks") or [] if b.get("status") != "closed")
        verdict = fresh_verdict(cache.get(entry_id), e.get("version"), now)
        candidates.append({
            "area": e["area"], "id": entry_id, "title": e.get("title"), "stage": e.get("stage"), "summary": e.get("summary"),
            "continue": bool(stopped), "prefer": preference_rank(prefer, e), "unblocks": unblocks,
            "verdict": None if verdict is None else {k: verdict.get(k) for k in ("verdict", "size", "level", "touches", "reason")},
        })
    candidates.sort(key=lambda c: (not c["continue"], c["prefer"] is None, c["prefer"] or 0, -c["unblocks"],
                                   -1 if c["stage"] is None else c["stage"], c["id"]))
    free = [w for w in workers if w["free"] and (not args.workers or w["k"] in args.workers)]
    slots = len(free) * args.depth
    want = slots * CLASSIFY_MARGIN
    classify, ready = [], 0
    for c in candidates:
        if ready + len(classify) >= want:
            break
        if c["verdict"] is None:
            classify.append(c["id"])
        elif c["verdict"]["verdict"] == "eligible":
            ready += 1
    return {"prefs": {"prefer": prefer, "avoid": avoid, "saved": bool(saved)}, "workers": workers, "slots": slots,
            "review": [{"id": e["id"], "area": e["area"], "branch": e.get("branch")} for e in entries if e.get("status") == "review"],
            "busy": busy, "candidates": candidates, "classify": classify, "skipped": skipped}


def cmd_dispatch(layout: Layout, args) -> None:
    """`dispatch --json`: the workers (free or why busy), the review pile, the busy files, the candidates in the
    dispatcher's order with the cached verdicts, and `classify`, the ids the classifier still has to read.
    `dispatch --record <file|->`: store a classifier answer for the next runs."""
    if args.record:
        print(f"recorded {record_verdicts(layout, args.record)} verdicts")
        return
    sys.stdout.reconfigure(encoding="utf-8")
    state = dispatch_state(layout, args)
    if args.json:
        print(json.dumps(state, ensure_ascii=False, indent=1))
        return
    for w in state["workers"]:
        print(f"{w['name']}: {'free' if w['free'] else w['busy']}  queue: {' '.join(w['queue']) or '-'}")
    print(f"review: {' '.join(e['id'] for e in state['review']) or '-'}")
    for c in state["candidates"][:20]:
        v = c["verdict"]
        print(f"  {c['id']:40} {'continue ' if c['continue'] else ''}prefer={c['prefer']} unblocks={c['unblocks']} {v['verdict'] if v else '?'}")
    print(f"classify: {' '.join(state['classify']) or '-'}")


BUILD_RULES = Path(".claude") / "rules" / "build-project.md"


def build_field(text: str, field: str) -> list[str]:
    """The code spans of the value cell of a `| Field | Meaning | Value |` row of build-project.md."""
    for line in text.splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) >= 3 and cells[0] == field:
            return re.findall(r"`([^`]+)`", cells[-1])
    return []


def default_preset(main: Path) -> str | None:
    """The debug preset of this platform among `cmake --list-presets` (no web, editor or RmlUi variant)."""
    platform = {"darwin": "macos", "win32": "windows"}.get(sys.platform, "linux")
    try:
        listed = subprocess.run(["cmake", "--list-presets"], cwd=main, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return DEFAULT_PRESETS.get(sys.platform)
    names = re.findall(r'^\s*"([^"]+)"', listed, re.M)
    for name in names:
        if platform in name and "debug" in name and not any(word in name for word in ("web", "editor", "rmlui")):
            return name
    return DEFAULT_PRESETS.get(sys.platform)


def cmd_build_info(layout: Layout, args) -> None:
    """What the panel builds and runs for a branch: the product, the test targets, the preset and, when the
    repository has it, the screenshot tool (`tools/ui_shots.py`, which takes --exe, --label and --out)."""
    try:
        text = (layout.main / BUILD_RULES).read_text(encoding="utf-8")
    except OSError:
        text = ""
    preset = args.preset or default_preset(layout.main)
    product = (build_field(text, "Product") or [None])[0]
    shots = layout.main / "tools" / "ui_shots.py"
    info = {
        "preset": preset,
        "product": product,
        "tests": build_field(text, "Tests"),
        "exe": f"build/{preset}/bin/{product}" if preset and product else None,
        "shots": "tools/ui_shots.py" if shots.is_file() else None,
    }
    print(json.dumps(info) if args.json else "\n".join(f"{k}: {v}" for k, v in info.items()))


def list_json(layout: Layout, args) -> dict:
    """What `list --json` prints: the workers, their sessions and entries, and the entries in review
    (the VS Code panel of tools/vscode/todo-workers reads it)."""
    sessions = list_sessions(args.claude)
    entries, store_error = store_entries(layout) if args.store else ([], None)
    if args.store:
        review_checks(entries)
    queues = read_queues(layout)
    by_branch = {entry["branch"]: entry for entry in entries if entry.get("branch")}
    workers = []
    for k, path in layout.workers():
        game = head_json(path)
        engine = head_json(path / SHARED_SUBMODULE) if layout.shared is not None and (path / SHARED_SUBMODULE / ".git").exists() else None
        changed = dirty(path, ignore_submodules=True) or (engine is not None and dirty(path / SHARED_SUBMODULE))
        session = session_in(sessions or [], path)
        entry = next((by_branch[h["branch"]] for h in (engine, game) if h and h["branch"] in by_branch), None)
        limited = limit_hit(session)
        workers.append({
            "k": k,
            "name": f"{WORKER_PREFIX}{k}",
            "path": str(path),
            "head": game,
            "engine": engine,
            "dirty": bool(changed),
            "session": None if session is None else {key: session.get(key) for key in ("id", "name", "state", "status", "waitingFor", "startedAt")},
            "active": is_running(session),
            "waiting": waits_for_owner(session) and limited is None,
            "asking": pending_ask(session) if waits_for_owner(session) and limited is None else None,
            "endedTurn": is_waiting(session) and limited is None and turn_ended(session),
            "limited": limited,
            "stopped": stopped_at(session),
            "activity": session_activity(session) if args.activity else None,
            "usage": session_usage(session) if args.usage else None,
            "entry": entry,
            "queue": queues.get(f"{WORKER_PREFIX}{k}", []),
        })
    entry_usage = update_ledger(workers) if args.usage else {}
    usage: dict = {}
    recent: dict = {}
    models: dict[str, dict] = {}
    for row in workers:
        if row["usage"]:
            add_counts(usage, row["usage"]["total"])
            add_counts(recent, row["usage"]["recent"])
            for model, counts in (row["usage"].get("models") or {}).items():
                add_counts(models.setdefault(model, {}), counts)
    return {
        "usage": {"total": usage, "recent": recent, "models": models, "recentMinutes": USAGE_RECENT_MINUTES} if args.usage else None,
        # the tokens of every entry by model, over every loop session that held it (update_ledger)
        "entryUsage": entry_usage if args.usage else None,
        "pointer": pointer_state(layout),
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
        # A pipe on Windows gets the ANSI code page (cp1251), and the panel reads UTF-8: the JSON is always UTF-8
        sys.stdout.reconfigure(encoding="utf-8")
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
        print(f"\nclaude agents (all of them), {tool_command(layout)} attach <k> (answer a session that waits), {tool_command(layout)} logs <k>")


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


def cmd_session(layout: Layout, args) -> None:
    """attach / logs: `claude attach|logs` of the newest session of worker k, named by its id (see session_ref)."""
    worker = layout.worker(args.k)
    if not worker.is_dir():
        raise WorkerError(f"no worker {worker}; create it first")
    sessions = list_sessions(args.claude)
    if sessions is None:
        raise WorkerError(f"`{args.claude} agents --json` did not answer")
    session = session_in(sessions, worker)
    if session is None:
        raise WorkerError(f"w{args.k}: no session")
    command = [args.claude, args.command, session_ref(session)]
    print(" ".join(command))
    if args.print_only:
        return
    sys.stdout.flush()
    os.execvp(command[0], command)


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


def loop_prompt(args, entry: str | None = None) -> str:
    """The prompt of a worker's session: `/todo-loop N [entry] [loop args] [report-to=<session>]`."""
    report = ""
    if args.report_to:
        if not REPORT_TO_NAME.fullmatch(args.report_to):
            raise WorkerError(f"--report-to {args.report_to!r}: a session name (letters, digits, '.', '_', '-')")
        report = f"report-to={args.report_to}"
    return " ".join(part for part in ("/todo-loop", str(args.slices), entry, args.loop_args, report) if part)


WAIT_POLL_S = 15


def wait_until_free(args, k: int, path: Path) -> bool:
    """Poll `claude agents` until the worker's session no longer runs (a loop that just ended its turn counts as
    finished), at most `args.wait_free` seconds."""
    deadline = time.time() + args.wait_free
    while True:
        if not is_running(session_in(list_sessions(args.claude) or [], path)):
            return True
        if time.time() >= deadline:
            print(f"w{k}: its session still runs after {args.wait_free} s; not started")
            return False
        time.sleep(WAIT_POLL_S)


def start_detached(args) -> None:
    """Run the same `start` again in a process of its own (without `--detach`) and return at once: a loop that
    queues its own next slice starts it this way and then ends, and the child waits for that end (`--wait-free`)."""
    argv = [part for part in args.argv if part != "--detach"]
    log = claude_home() / "todo" / f"start-{'-'.join(map(str, args.workers)) or 'all'}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("a", encoding="utf-8") as out:
        out.write(f"\n--- {datetime.now().isoformat(timespec='seconds')} worker.py {' '.join(argv)}\n")
        out.flush()
        child = subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), *argv], cwd=Path.cwd(), stdout=out, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, start_new_session=True,
        )
    print(f"detached: pid {child.pid}, log {log}")


def cmd_start(layout: Layout, args) -> None:
    """`/todo-loop` as a background Claude session (`claude --bg`) in each chosen worker, `--delay`
    seconds apart: two loops that choose at the same moment could take entries that meet in their
    files (todo-loop/SKILL.md, "Parallel workers"), the store's claim only keeps them off one entry."""
    problems = check_shared_config(layout, fix=False)
    if problems:
        raise WorkerError(problems[0])
    workers = chosen_workers(layout, args.workers)
    if args.detach and not args.print_only:
        start_detached(args)
        return
    sessions = list_sessions(args.claude)
    if sessions is None and not args.print_only:
        raise WorkerError(f"`{args.claude} agents --json` did not answer; is Claude Code installed and new enough for --bg?")
    env = build_env(layout)
    env.pop("CLAUDE_EFFORT", None)  # the launcher's own effort must not leak into the loops
    prompt = loop_prompt(args)
    flags = loop_session_flags(args)
    started = 0
    failed = False
    for k, path in workers:
        if args.wait_free and not args.print_only:
            if not wait_until_free(args, k, path):
                failed = True
                continue
            sessions = list_sessions(args.claude)
        session = session_in(sessions or [], path)
        if is_running(session):
            print(f"w{k}: already runs ({describe(session)}), left alone")
            continue
        worker_prompt = prompt
        if args.queued_only:
            queues = read_queues(layout)
            items = queues.get(f"{WORKER_PREFIX}{k}", [])
            if not items:
                print(f"w{k}: its queue is empty, not started")
                continue
            entry = items[0] if args.print_only else items.pop(0)
            if not args.print_only:
                write_queues(layout, queues)
            worker_prompt = loop_prompt(args, entry)
        if is_active(session) and not args.print_only:
            subprocess.run([args.claude, "stop", str(session.get("id") or session["sessionId"])], capture_output=True, text=True)
        if not args.print_only:
            # The loop's own "go to the base" is a step of its conductor and may miss a repository: a game left on an
            # old commit beside a newer engine base makes a red baseline, and the loop stops without taking anything.
            # A worker with uncommitted changes holds work the loop continues, so it is started where it stands.
            try:
                moved = update_one(layout, k, path, None)
                parts = [f"{Path(r['repo']).name if Path(r['repo']) != path else 'game'} at {r['base']} ({r['to'][:9]})" for r in moved["repos"] if r["changed"]]
                if parts:
                    print(f"w{k}: moved to the base: {', '.join(parts)}")
            except WorkerError as error:
                print(f"w{k}: not moved to the base ({error}); started where it stands")
        command = [args.claude, "--bg", "--name", session_name(k), *flags, *args.rest, worker_prompt]
        if started and args.delay > 0:
            print(f"  waiting {args.delay} s before w{k} (the loops must not choose at the same moment)")
            if not args.print_only:
                time.sleep(args.delay)
        print(f"w{k}: cd {path} && CMAKE_BUILD_PARALLEL_LEVEL={env['CMAKE_BUILD_PARALLEL_LEVEL']} {' '.join(command[:-1])} '{worker_prompt}'")
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
        print(f"\nWatch: {tool_command(layout)} list; claude agents; {tool_command(layout)} attach <k> when one waits for you")
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
        if result.returncode == 0:
            mark_stopped(session)
        print(f"w{k}: {(result.stdout + result.stderr).strip() or f'exit code {result.returncode}'}")
    print("A loop stopped in the middle leaves its entry `in progress` with its branch; the next /todo-loop in that worker continues it.")


def cmd_relaunch(layout: Layout, args) -> None:
    """Continue the sessions that the usage limit stopped, once it has reset: `claude --bg --resume <session>` with
    RELAUNCH_PROMPT, so the loop keeps its conversation (a session still listed as running is stopped first; Claude
    Code then starts a copy of the conversation under a new id, and `list` follows the newest session of the folder).
    Without worker numbers only the workers whose session hit the limit; a named worker is relaunched whenever its
    session is not busy (a loop that ended for another reason, e.g. a lost connection)."""
    workers = chosen_workers(layout, args.workers)
    sessions = list_sessions(args.claude)
    if sessions is None:
        raise WorkerError(f"`{args.claude} agents --json` did not answer")
    env = build_env(layout)
    prompt = args.prompt or RELAUNCH_PROMPT
    result = {"relaunched": [], "skipped": [], "errors": []}
    for k, path in workers:
        session = session_in(sessions, path)
        limited = limit_hit(session)
        name = f"{WORKER_PREFIX}{k}"
        if session is None or not session.get("sessionId"):
            if args.workers:
                result["errors"].append(f"{name}: no session to continue; start one with `worker.py start {k}`")
            continue
        if limited is None and not args.workers:
            result["skipped"].append({"k": k, "reason": "not stopped by the usage limit"})
            continue
        if limited is None and session.get("state") != "done" and session.get("status") == "busy":
            result["errors"].append(f"{name}: its session is busy; nothing to relaunch")
            continue
        command = [args.claude, "--bg", "--resume", str(session["sessionId"]), "--name", session_name(k), *args.rest, prompt]
        if args.print_only:
            result["relaunched"].append({"k": k, "name": name, "output": " ".join(command[:-1]) + f" '{prompt}'"})
            continue
        if is_active(session):
            subprocess.run([args.claude, "stop", str(session.get("id") or session["sessionId"])], capture_output=True, text=True)
        done = subprocess.run(command, cwd=path, env=env, capture_output=True, text=True)
        output = (done.stdout + done.stderr).strip()
        if done.returncode != 0 or "backgrounded" not in output:
            result["errors"].append(f"{name}: {output or f'exit code {done.returncode}'}")
            continue
        line = next((l.strip() for l in output.splitlines() if l.strip().startswith("backgrounded")), output.splitlines()[0])
        result["relaunched"].append({"k": k, "name": name, "output": line})
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        for row in result["relaunched"]:
            print(f"{row['name']}: {row['output']}")
        for row in result["skipped"]:
            print(f"{WORKER_PREFIX}{row['k']}: {row['reason']}, left alone")
        for error in result["errors"]:
            print(f"error: {error}", file=sys.stderr)
        if not result["relaunched"] and not result["errors"]:
            print("no worker was stopped by the usage limit")
    if result["errors"] and not args.json:
        raise WorkerError(f"{len(result['errors'])} worker(s) not relaunched")


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
    if is_running(session):
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
    p.add_argument("--model", default=DEFAULT_LOOP_MODEL, help=f"the model of the loop sessions (default {DEFAULT_LOOP_MODEL}; '' leaves Claude Code's default)")
    p.add_argument("--effort", default=DEFAULT_LOOP_EFFORT, help=f"the effort of the loop sessions (default {DEFAULT_LOOP_EFFORT}; '' leaves Claude Code's default)")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--print", dest="print_only", action="store_true", help="only print the commands")
    p.add_argument("--queued-only", action="store_true", help="start only workers with a queue, each on the first entry of its queue (taken out)")
    p.add_argument("--wait-free", type=int, default=0, metavar="S", help="wait up to S seconds for each worker's session to end before starting it")
    p.add_argument("--detach", action="store_true", help="do it in a background process and return at once (a loop starts its own next slice so)")
    p = sub.add_parser("merge", help="merge the branch of an entry in review into its base, close the entry, delete the branch (the quick merge of the VS Code panel)")
    p.add_argument("id")
    p.add_argument("--dry-run", action="store_true", help="only say what it would do and what stops it")
    p.add_argument("--json", action="store_true", help="print JSON (for tools/vscode/todo-workers)")
    p = sub.add_parser("update", help="detach the repositories of the workers at the tips of their base branches (a finished session, a clean folder)")
    p.add_argument("workers", nargs="*", type=int, help="the workers (default: all)")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true", help="print JSON (for tools/vscode/todo-workers)")
    p = sub.add_parser("relaunch", help="continue the sessions the usage limit stopped (claude --bg --resume), after the reset (arguments for claude after --)")
    p.add_argument("workers", nargs="*", type=int, help="the workers (default: those stopped by the limit; a named one also when its session ended otherwise)")
    p.add_argument("--prompt", help="what the resumed session is told (default: go on where the loop stopped)")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--print", dest="print_only", action="store_true", help="only print the commands")
    p.add_argument("--json", action="store_true", help="print JSON (for tools/vscode/todo-workers)")
    for verb, about in (("attach", "open the newest session of a worker (answer what it waits for)"), ("logs", "show the log of the newest session of a worker")):
        p = sub.add_parser(verb, help=f"{about}: claude {verb} <its id>, since every session of the worker has the same name")
        p.add_argument("k", type=int)
        p.add_argument("--claude", default="claude", help="the command (default claude)")
        p.add_argument("--print", dest="print_only", action="store_true", help="only print the command")
    p = sub.add_parser("queue", help="the entries the VS Code panel starts next in each worker (a local file, ~/.claude/todo/worker-queues.json)")
    p.add_argument("k", nargs="?", type=int, help="the worker (default: show every queue)")
    p.add_argument("--add", help="put this entry at the end (taken out of any other queue)")
    p.add_argument("--first", action="store_true", help="with --add: at the front")
    p.add_argument("--remove", help="take this entry out")
    p.add_argument("--pop", action="store_true", help="take the first entry out and print it")
    p.add_argument("--clear", action="store_true", help="empty the queue")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("overlap", help="the busy entries whose slice plans name files the entry names (a hint before a start)")
    p.add_argument("id")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("prefs", help="the owner's preferences of /todo-dispatch (a local file, ~/.claude/todo/dispatch-prefs.json)")
    p.add_argument("--prefer", help="comma list of ids, id or title word prefixes, areas, first is strongest ('' clears)")
    p.add_argument("--avoid", help="comma list of the same: never dispatched ('' clears)")
    p.add_argument("--clear", action="store_true", help="forget both")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("dispatch", help="one read for /todo-dispatch: free workers, review pile, busy files, candidates in order with cached verdicts")
    p.add_argument("workers", nargs="*", type=int, help="only these workers count as free slots (default: all)")
    p.add_argument("--depth", type=int, default=2, help="entries per free worker (default 2)")
    p.add_argument("--prefer", help="this run only, instead of the saved `prefer`")
    p.add_argument("--avoid", help="this run only, instead of the saved `avoid`")
    p.add_argument("--record", help="store a classifier answer (a JSON file, or - for stdin) and exit")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("cost", help="what an entry cost so far (every loop session that held it, at API prices) against its cap")
    p.add_argument("id")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("remarks", help="send an entry in review back with the owner's remarks: a remarks plan, in progress again, queued first on its worker")
    p.add_argument("id")
    p.add_argument("--file", required=True, help="the remarks (markdown), '-' for stdin")
    p.add_argument("--start", action="store_true", help="start the worker at once when it is free")
    p.add_argument("--report-to", help="the session the loop reports to")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("build-info", help="the product, tests, preset and screenshot tool of this checkout (for the panel)")
    p.add_argument("--preset", help="this preset instead of the platform's debug one")
    p.add_argument("--json", action="store_true")
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
    args.argv = argv + (["--", *rest] if rest else [])
    try:
        layout = find_layout(Path.cwd())
        commands = {"create": cmd_create, "list": cmd_list, "check": cmd_check, "remove": cmd_remove, "run": cmd_run, "start": cmd_start, "stop": cmd_stop, "relaunch": cmd_relaunch, "merge": cmd_merge, "update": cmd_update, "attach": cmd_session, "logs": cmd_session, "queue": cmd_queue, "overlap": cmd_overlap, "prefs": cmd_prefs, "dispatch": cmd_dispatch, "build-info": cmd_build_info, "cost": cmd_cost, "remarks": cmd_remarks}
        commands[args.command](layout, args)
    except WorkerError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
