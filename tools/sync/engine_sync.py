"""The read-only git-state helper of the sync-engine skill (`.claude/skills/sync-engine/SKILL.md`). Standard library only.

    python3 <engine>/tools/sync/engine_sync.py --repo <E> status [--json]
    python3 <engine>/tools/sync/engine_sync.py --repo <E> pushes

It reads the refs the skill's own `git fetch` left (it never fetches and never writes anything: only `for-each-ref`, `rev-list`,
`log`, `merge-base --is-ancestor`, `worktree list`, `rev-parse` and `config --get-all`) and tells, for `main` and every
participating `games/*`, how the local branch stands against `origin` and, for a `games/*`, against `main`. Exit code of `status`:
0 all fine, 2 a participating branch is behind or diverged from its origin (the skill stops), 1 an error.
"""

import argparse
import json
import os
import subprocess
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

REMOTE = "origin"
MAIN = "main"
GAMES_PREFIX = "games/"
CONFIG_EXCLUDE = "engine-sync.exclude"
CONFIG_INCLUDE = "engine-sync.include"
EXIT_OK = 0
EXIT_ERROR = 1
EXIT_STOP = 2
FIELD_SEPARATOR = "\x1f"

EQUAL = "equal"
AHEAD = "ahead"
BEHIND = "behind"
DIVERGED = "diverged"
NO_LOCAL = "no-local"
NO_REMOTE = "no-remote"
STOPPING_RELATIONS = (BEHIND, DIVERGED)
PUSHING_RELATIONS = (AHEAD, NO_REMOTE)


class SyncError(Exception):
    pass


@dataclass
class BranchState:
    name: str
    local: str | None
    origin: str | None
    relation: str
    local_ahead: int = 0  # commits the local branch has that origin has not
    local_behind: int = 0  # commits origin has that the local branch has not
    worktree: str | None = None  # the folder that has the branch checked out, if any
    held_elsewhere: bool = False  # ... and it is not this checkout
    # `games/*` against `main` (the tips used: the local branch, else origin's)
    ahead_of_main: int = 0
    behind_main: int = 0
    merges: list[str] = field(default_factory=list)  # first-parent merge subjects not in main, oldest first, sync merges left out
    commits: int = 0  # first-parent commits that are not merges
    sync_merges: int = 0  # first-parent merges of `main` into the branch: no work of its own
    own_work: bool = False  # merges or commits: what `Up` brings to main


def git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    """One `git -C <repo>` call, no shell. `check=False` where a missing ref is a normal answer."""
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0", LC_ALL="C")
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8", env=env, check=False)
    if check and result.returncode != 0:
        raise SyncError(f"git {' '.join(args)}: {result.stderr.strip() or 'exit code ' + str(result.returncode)}")
    return result


def ref_tips(repo: Path) -> tuple[dict[str, str], dict[str, str]]:
    """The local branches and the branches of `origin` as name -> sha."""
    out = git(repo, "for-each-ref", f"--format=%(refname){FIELD_SEPARATOR}%(objectname)", "refs/heads", f"refs/remotes/{REMOTE}").stdout
    local: dict[str, str] = {}
    remote: dict[str, str] = {}
    heads, remotes = "refs/heads/", f"refs/remotes/{REMOTE}/"
    for line in out.splitlines():
        ref, _, sha = line.partition(FIELD_SEPARATOR)
        if ref.startswith(heads):
            local[ref[len(heads):]] = sha
        elif ref.startswith(remotes) and ref != f"{remotes}HEAD":
            remote[ref[len(remotes):]] = sha
    return local, remote


def config_values(repo: Path, key: str) -> list[str]:
    result = git(repo, "config", "--local", "--get-all", key, check=False)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()] if result.returncode == 0 else []


def count_left_right(repo: Path, left: str, right: str) -> tuple[int, int]:
    """(commits only in `left`, commits only in `right`)."""
    out = git(repo, "rev-list", "--left-right", "--count", f"{left}...{right}").stdout.split()
    return int(out[0]), int(out[1])


def relation_to_origin(repo: Path, local: str | None, origin: str | None) -> tuple[str, int, int]:
    if local is None:
        return NO_LOCAL, 0, 0
    if origin is None:
        return NO_REMOTE, 0, 0
    if local == origin:
        return EQUAL, 0, 0
    ahead, behind = count_left_right(repo, local, origin)
    if ahead and behind:
        return DIVERGED, ahead, behind
    return (AHEAD if ahead else BEHIND), ahead, behind


def worktrees(repo: Path) -> dict[str, str]:
    """Branch name -> the folder that has it checked out (`git worktree list --porcelain`)."""
    held: dict[str, str] = {}
    path = None
    prefix = "refs/heads/"
    for line in git(repo, "worktree", "list", "--porcelain").stdout.splitlines():
        if line.startswith("worktree "):
            path = line[len("worktree "):]
        elif line.startswith("branch ") and path is not None and line[len("branch "):].startswith(prefix):
            held[line[len("branch ") + len(prefix):]] = path
    return held


def normalized(path: str) -> str:
    return os.path.normcase(os.path.realpath(path))


def checkout_identity(repo: Path) -> tuple[set[str], bool]:
    """The folders `git worktree list` may call this checkout, and whether it is a linked worktree (not the main checkout)."""
    absolute_git_dir = git(repo, "rev-parse", "--absolute-git-dir").stdout.strip()
    common_dir = git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir").stdout.strip()
    toplevel = git(repo, "rev-parse", "--show-toplevel").stdout.strip()
    linked = normalized(absolute_git_dir) != normalized(common_dir)
    own = {normalized(toplevel)}
    if not linked:
        own.add(normalized(common_dir))  # the main checkout of a submodule is listed by its git dir
    return own, linked


def is_ancestor(repo: Path, commit: str, tip: str) -> bool:
    return git(repo, "merge-base", "--is-ancestor", commit, tip, check=False).returncode == 0


def history_against_main(repo: Path, state: BranchState, main_tip: str, tip: str) -> None:
    """What the branch holds that `main` has not: the todo-branch merges and plain commits on its first-parent line."""
    state.behind_main, state.ahead_of_main = count_left_right(repo, main_tip, tip)
    if state.ahead_of_main == 0:
        return
    fmt = f"--format=%H{FIELD_SEPARATOR}%P{FIELD_SEPARATOR}%s"
    lines = git(repo, "log", "--first-parent", fmt, f"{main_tip}..{tip}").stdout.splitlines()
    for line in reversed(lines):
        _sha, parents_text, subject = line.split(FIELD_SEPARATOR, 2)
        parents = parents_text.split()
        if len(parents) < 2:
            state.commits += 1
        elif all(is_ancestor(repo, parent, main_tip) for parent in parents[1:]):
            state.sync_merges += 1  # `main` merged into the branch: nothing to bring back
        else:
            state.merges.append(subject)
    state.own_work = bool(state.merges or state.commits)


def read_state(repo: Path) -> dict:
    local, remote = ref_tips(repo)
    if MAIN not in local and MAIN not in remote:
        raise SyncError(f"no branch '{MAIN}' (local or {REMOTE}/{MAIN}) in {repo}: fetch first?")
    excluded_names = set(config_values(repo, CONFIG_EXCLUDE))
    included_names = set(config_values(repo, CONFIG_INCLUDE))
    games = sorted(name for name in set(local) | set(remote) if name.startswith(GAMES_PREFIX))
    undecided = [name for name in games if name not in excluded_names and name not in included_names]
    excluded = [name for name in games if name in excluded_names]
    participating = [MAIN] + [name for name in games if name in included_names and name not in excluded_names]

    holders = worktrees(repo)
    own_folders, linked = checkout_identity(repo)
    main_tip = local.get(MAIN) or remote[MAIN]
    branches: list[BranchState] = []
    for name in participating:
        relation, ahead, behind = relation_to_origin(repo, local.get(name), remote.get(name))
        state = BranchState(name, local.get(name), remote.get(name), relation, ahead, behind)
        holder = holders.get(name)
        if holder is not None:
            state.worktree = holder
            state.held_elsewhere = normalized(holder) not in own_folders
        if name != MAIN:
            history_against_main(repo, state, main_tip, state.local or state.origin)
        branches.append(state)

    head = git(repo, "rev-parse", "HEAD", check=False)
    symbolic = git(repo, "symbolic-ref", "--short", "-q", "HEAD", check=False)
    return {
        "repo": str(repo),
        "head": {"branch": symbolic.stdout.strip() or None, "sha": head.stdout.strip() or None},
        "linked_worktree": linked,
        "branches": [asdict(state) for state in branches],
        "undecided": undecided,
        "excluded": excluded,
        "stop": [state.name for state in branches if state.relation in STOPPING_RELATIONS],
    }


def push_lines(report: dict) -> list[str]:
    return [f"git push {REMOTE} {branch['name']}" for branch in report["branches"] if branch["relation"] in PUSHING_RELATIONS]


def print_status(report: dict) -> None:
    where = "linked worktree" if report["linked_worktree"] else "main checkout"
    head = report["head"]["branch"] or f"detached {str(report['head']['sha'])[:10]}"
    print(f"{report['repo']} ({where}, on {head})")
    for branch in report["branches"]:
        parts = [f"{branch['name']:<20} {branch['relation']}"]
        if branch["relation"] in (AHEAD, BEHIND, DIVERGED):
            parts.append(f"(+{branch['local_ahead']} -{branch['local_behind']} vs {REMOTE})")
        if branch["name"] != MAIN:
            parts.append(f"main: +{branch['ahead_of_main']} -{branch['behind_main']}")
            if branch["own_work"]:
                parts.append(f"own work: {len(branch['merges'])} merges, {branch['commits']} commits")
            elif branch["sync_merges"]:
                parts.append("only sync merges")
        if branch["held_elsewhere"]:
            parts.append(f"held by {branch['worktree']}")
        print("  " + "  ".join(parts))
        for subject in branch["merges"]:
            print(f"      {subject}")
    if report["undecided"]:
        print(f"  undecided (neither {CONFIG_INCLUDE} nor {CONFIG_EXCLUDE}): {', '.join(report['undecided'])}")
    if report["excluded"]:
        print(f"  excluded: {', '.join(report['excluded'])}")
    if report["stop"]:
        print(f"  STOP: {', '.join(report['stop'])} behind or diverged from {REMOTE}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--repo", default=".", help="the engine repository (<E>: '.' in the engine, 'engine' in a game)")
    commands = parser.add_subparsers(dest="command", required=True)
    status = commands.add_parser("status", help="how main and the participating games/* stand against origin and main")
    status.add_argument("--json", action="store_true", help="print the report as JSON")
    commands.add_parser("pushes", help="the 'git push origin <branch>' line of every local branch ahead of its origin")
    args = parser.parse_args(argv)
    repo = Path(args.repo)
    try:
        report = read_state(repo)
    except (SyncError, OSError) as error:
        print(f"engine_sync: {error}", file=sys.stderr)
        return EXIT_ERROR
    if args.command == "pushes":
        for line in push_lines(report):
            print(line)
        return EXIT_OK
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_status(report)
    return EXIT_STOP if report["stop"] else EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
