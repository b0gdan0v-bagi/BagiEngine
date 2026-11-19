"""The full test run: every test of a repository (the ctest preset `<p>-all`, the `extended` checks included) on a commit of its
base branch, in a worktree and a build folder of its own, with the result kept per commit for the deploy gate.

    python3 tools/alltests/alltests.py                                   # the tip of the local main of this repository
    python3 tools/alltests/alltests.py --commit HEAD                      # the commit HEAD names (the deploy gate's hint)
    python3 engine/tools/alltests/alltests.py --repo . --area mahjong     # a game, for itself
    python3 tools/alltests/alltests.py --dry-run                          # the same run; the store and the results file are not written

The steps, in order (each command is printed before it runs; its output goes to <work>/logs/):
  1. the worktree <work>/worktree of the repository, detached at the commit (made on the first run); every submodule that is
     initialised in the repository is cloned from that local copy, so nothing is fetched from a remote; the repository's .venv
     (and engine/.venv of a game) is linked into it, so the configure finds the Python of the reflection generator;
  2. cmake --preset <p>, cmake --build --preset <p> (every target), ctest --preset <p>-all, and once more with --rerun-failed:
     a test that passes the second time is reported as flaky and is not a failure;
  3. the result of the commit goes to <work>/results.json (alltests_results.py), which the deploy scripts read;
  4. each failure adds or updates the todo entry alltests-<test> (actor alltests): the range of commits since the last green run
     and the failure text. A failing build is alltests-build.
<work> is ~/.cache/bagi-alltests/<repository> (BAGI_ALLTESTS_HOME moves the parent; --work-dir names another).
Exit code 0 green, 3 red, 1 an error (no result is recorded). Standard library only, Python 3.10+. Never fetches, pulls or pushes.
"""

import argparse
import datetime
import hashlib
import html
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import alltests_results as results  # noqa: E402
from alltests_results import GitError, git  # noqa: E402

TOOLS_DIR = Path(__file__).resolve().parents[1]
TODO_SCRIPT = TOOLS_DIR / "todo" / "todo.py"
ACTOR = "alltests"
ENTRY_PREFIX = "alltests-"
DEFAULT_BASE = "main"
DEFAULT_AREA = "engine"
DEFAULT_PRESETS = {"Darwin": "macos-debug", "Windows": "windows-msvc-debug"}
ALL_SUFFIX = "-all"
EXIT_GREEN, EXIT_ERROR, EXIT_RED = 0, 1, 3
# The pseudo test names of a run that did not reach the tests
BUILD_FAILURE = "<build>"
MANY_FAILURES = "<many>"
SPECIAL_IDS = {BUILD_FAILURE: "build", MANY_FAILURES: "many-failures"}
# More failing tests than this are one broken thing more often than many: one entry lists them instead of one entry each
DEFAULT_MAX_ENTRIES = 10
# An entry id is letters, digits, '_' and '-' (tools/todo/CLAUDE.md); a long test name is cut and keeps a hash, so two stay apart
MAX_ID_LENGTH = 80
ID_HASH_LENGTH = 8
FAILURE_TEXT_LINES = 60
RANGE_LOG_LINES = 30
MANY_NAMES_LISTED = 50
# Where cmake/BagiEngine.cmake looks for the venv Python of the reflection generator (`<engine>/.venv`): the root of the engine, and
# engine/ of a game. A worktree has none of them, and the Python cmake falls back to can lack the generator's packages.
VENV_FOLDERS = (Path(".venv"), Path("engine") / ".venv")
JUNIT_CASE = re.compile(r"<testcase\b(?P<attributes>[^>]*?)(?:/>|>(?P<content>.*?)</testcase>)", re.S)
JUNIT_ATTRIBUTE = re.compile(r'(\w+)="([^"]*)"')
JUNIT_OUTPUT = re.compile(r"<system-out>(.*?)</system-out>", re.S)


class RunError(Exception):
    """The run cannot go on; the message says why. Nothing is recorded."""


class Tools(NamedTuple):
    cmake: list[str]
    ctest: list[str]
    todo: list[str]


class Outcome(NamedTuple):
    status: str
    failed: list[str]
    flaky: list[str]
    tests: int
    texts: dict[str, str]


def now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def split_command(text: str) -> list[str]:
    return shlex.split(text, posix=os.name != "nt")


def entry_id(test: str) -> str:
    if test in SPECIAL_IDS:
        return ENTRY_PREFIX + SPECIAL_IDS[test]
    slug = re.sub(r"[^A-Za-z0-9_-]+", "-", test).strip("-") or "unnamed"
    name = ENTRY_PREFIX + slug
    if len(name) > MAX_ID_LENGTH:
        digest = hashlib.sha1(test.encode("utf-8")).hexdigest()[:ID_HASH_LENGTH]
        name = name[: MAX_ID_LENGTH - ID_HASH_LENGTH - 1].rstrip("-") + "-" + digest
    return name


def tail(text: str, lines: int = FAILURE_TEXT_LINES) -> str:
    return "\n".join(text.rstrip().splitlines()[-lines:])


# --- The worktree --------------------------------------------------------------------------------------------------------------


def run_logged(command: list[str], cwd: Path, log: Path) -> int:
    print(f"+ ({cwd}) {shlex.join(command)} > {log}", flush=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as stream:
        try:
            return subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, check=False).returncode
        except OSError as error:
            raise RunError(f"cannot start `{command[0]}`: {error}") from error


def git_step(repo: Path, *args: str) -> str:
    print(f"+ git -C {repo} {shlex.join(args)}", flush=True)
    return git(repo, *args)


def update_worktree(repo: Path, worktree: Path, commit: str) -> None:
    """The worktree of `repo` at `commit`, detached: it holds no branch, so the owner's checkout can still switch to the base."""
    if (worktree / ".git").exists():
        own = Path(git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir")).resolve()
        theirs = Path(git(worktree, "rev-parse", "--path-format=absolute", "--git-common-dir")).resolve()
        if own != theirs:
            raise RunError(f"{worktree} is a worktree of another repository ({theirs}); name another --work-dir")
        git_step(worktree, "checkout", "--quiet", "--force", "--detach", commit)
    else:
        if worktree.exists() and any(worktree.iterdir()):
            raise RunError(f"{worktree} exists and is not a worktree of {repo}: remove it or name another --work-dir")
        # -f: a worktree folder deleted by hand is still registered until `git worktree prune`
        git_step(repo, "worktree", "add", "--quiet", "-f", "--detach", str(worktree), commit)
    sync_submodules(repo, worktree)
    link_venvs(repo, worktree)


def link_venvs(repo: Path, worktree: Path) -> None:
    """Each venv of `repo` that the build looks for is linked into the worktree, as the loop's workers share theirs.

    A venv the worktree already has, or a link that leads to one, is kept; a link that leads nowhere is made again; a folder whose
    parent is not in the worktree (a game whose engine/ is not initialised) is left out."""
    for folder in VENV_FOLDERS:
        origin, target = repo / folder, worktree / folder
        if not origin.is_dir() or target.exists() or not target.parent.is_dir():
            continue
        if target.is_symlink():
            target.unlink()
        print(f"+ ln -s {origin} {target}", flush=True)
        target.symlink_to(origin, target_is_directory=True)


def submodule_paths(root: Path) -> list[str]:
    if not (root / ".gitmodules").is_file():
        return []
    try:
        lines = git(root, "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$")
    except GitError:
        return []
    return [line.split(" ", 1)[1] for line in lines.splitlines() if " " in line]


def sync_submodules(source: Path, dest: Path) -> None:
    """Every submodule initialised in `source` is checked out in `dest` at the commit `dest` records, cloned from `source`'s copy.

    The clone is local (hard links), so a submodule commit that is not on this machine is an error, never a fetch."""
    for path in submodule_paths(dest):
        entry = git(dest, "ls-tree", "HEAD", "--", path).split()
        if len(entry) < 3 or entry[1] != "commit":
            continue
        commit, origin, target = entry[2], source / path, dest / path
        if not (origin / ".git").exists():
            print(f"submodule {path}: not initialised in {source}, left out", flush=True)
            continue
        if (target / ".git").exists() and not has_commit(target, commit):
            shutil.rmtree(target)
        if not (target / ".git").exists():
            if target.exists():
                shutil.rmtree(target)
            git_step(dest, "clone", "--quiet", "--no-checkout", str(origin), str(target))
        if not has_commit(target, commit):
            raise RunError(f"submodule {path}: commit {commit[:12]} is not in {origin}: update it there (git submodule update)")
        git_step(target, "checkout", "--quiet", "--force", "--detach", commit)
        sync_submodules(origin, target)


def has_commit(repo: Path, commit: str) -> bool:
    try:
        git(repo, "cat-file", "-e", f"{commit}^{{commit}}")
    except GitError:
        return False
    return True


# --- The build and the tests -----------------------------------------------------------------------------------------------------


def read_junit(path: Path) -> tuple[list[str], int, dict[str, str]]:
    """The failed test names in the file's order, the count of tests and the output of each failed one.

    Read with patterns rather than an XML parser: ctest writes a fixed form, and the parser needs pyexpat, which a Homebrew Python
    can lack (a library of the OS older than the one it was built against)."""
    if not path.is_file():
        raise RunError(f"ctest wrote no {path.name}: see the log beside it")
    failed, texts, count = [], {}, 0
    for match in JUNIT_CASE.finditer(path.read_text(encoding="utf-8", errors="replace")):
        count += 1
        attributes = {key: html.unescape(value) for key, value in JUNIT_ATTRIBUTE.findall(match["attributes"])}
        content = match["content"] or ""
        if attributes.get("status") == "fail" or "<failure" in content or "<error" in content:
            name = attributes.get("name", "")
            failed.append(name)
            output = JUNIT_OUTPUT.search(content)
            texts[name] = html.unescape(output[1]) if output else ""
    return failed, count, texts


def build_and_test(tools: Tools, worktree: Path, preset: str, logs: Path) -> Outcome:
    for name in ("ctest.xml", "rerun.xml"):
        (logs / name).unlink(missing_ok=True)
    for step, command in (("configure", [*tools.cmake, "--preset", preset]), ("build", [*tools.cmake, "--build", "--preset", preset])):
        if run_logged(command, worktree, logs / f"{step}.log"):
            text = (logs / f"{step}.log").read_text(encoding="utf-8", errors="replace")
            return Outcome(results.RED, [BUILD_FAILURE], [], 0, {BUILD_FAILURE: f"$ {shlex.join(command)}\n{tail(text)}"})
    test_preset = preset + ALL_SUFFIX
    code = run_logged([*tools.ctest, "--preset", test_preset, "--output-junit", str(logs / "ctest.xml")], worktree, logs / "ctest.log")
    failed, count, _ = read_junit(logs / "ctest.xml")
    if code and not failed:
        raise RunError(f"ctest failed with exit code {code} and no failed test: {tail((logs / 'ctest.log').read_text(errors='replace'), 10)}")
    if not failed:
        return Outcome(results.GREEN, [], [], count, {})
    # Once more alone: a case that is only slow under the load of a full run passes here, and the second output is the one filed
    run_logged([*tools.ctest, "--preset", test_preset, "--rerun-failed", "--output-junit", str(logs / "rerun.xml")], worktree, logs / "rerun.log")
    again, _, texts = read_junit(logs / "rerun.xml")
    still = [name for name in failed if name in again]
    flaky = [name for name in failed if name not in again]
    return Outcome(results.RED if still else results.GREEN, still, flaky, count, texts)


# --- The todo entries ----------------------------------------------------------------------------------------------------------


def last_green(repo: Path, data: dict, commit: str) -> str | None:
    """The newest green commit that is an ancestor of `commit`."""
    greens = sorted(((r.get("finished", ""), sha) for sha, r in data["results"].items() if r.get("status") == results.GREEN and sha != commit), reverse=True)
    for _, sha in greens:
        try:
            git(repo, "merge-base", "--is-ancestor", sha, commit)
        except GitError:
            continue
        return sha
    return None


def commit_range(repo: Path, green: str | None, commit: str) -> str:
    if green is None:
        return "- Commits since the last green run: none is recorded for an ancestor of this commit, so the range is unknown."
    count = git(repo, "rev-list", "--count", f"{green}..{commit}")
    log = git(repo, "log", "--no-decorate", f"--max-count={RANGE_LOG_LINES}", "--format=%h %s", f"{green}..{commit}")
    lines = [f"- Commits since the last green run (`{green[:12]}..{commit[:12]}`, {count}):"]
    lines += [f"  - `{line.split(' ', 1)[0]}` {line.split(' ', 1)[1] if ' ' in line else ''}" for line in log.splitlines()]
    return "\n".join(lines)


def fenced(text: str) -> str:
    longest = max((len(run) for run in re.findall(r"`+", text)), default=0)
    fence = "`" * max(3, longest + 1)
    return f"{fence}text\n{text}\n{fence}"


class Context(NamedTuple):
    repo: Path
    repo_name: str
    commit: str
    preset: str
    base: str
    area: str
    tools: Tools
    dry_run: bool


def entry_text(context: Context, test: str, since: str, text: str, range_text: str, names: list[str]) -> tuple[str, str]:
    short, test_preset = context.commit[:12], context.preset + ALL_SUFFIX
    if test == BUILD_FAILURE:
        title = f"alltests: the build of {context.preset} fails"
        wrong = f"the build of `{context.preset}` (configure or build) fails in the full test run of `{context.repo_name}` at `{short}`."
        again = f"- Reproduce: `cmake --preset {context.preset} && cmake --build --preset {context.preset}` on that commit."
    elif test == MANY_FAILURES:
        title = f"alltests: {len(names)} tests fail"
        listed = ", ".join(f"`{name}`" for name in names[:MANY_NAMES_LISTED])
        wrong = f"{len(names)} tests fail in the full test run (`{test_preset}`) of `{context.repo_name}` at `{short}`: {listed}."
        again = f"- Reproduce: `ctest --preset {test_preset}` on that commit; so many failures are usually one cause."
    else:
        title = f"alltests: {test} fails"
        wrong = f"`{test}` fails twice in a row in the full test run (`{test_preset}`) of `{context.repo_name}` at `{short}`."
        again = f"- Reproduce: `ctest --preset {test_preset} -R '^{re.escape(test)}$' --output-on-failure` on that commit."
    body = "\n".join([
        f"**What is wrong:** {wrong}",
        "",
        f"- First seen failing at `{since[:12]}`; last at `{short}` ({now()}, base `{context.base}`).",
        range_text,
        again,
        "- Filed by `alltests` (`tools/alltests/CLAUDE.md`): the next failure of the same test updates this entry; a green run does not close it.",
        "",
        f"**Failure text** (the last {FAILURE_TEXT_LINES} lines of the second run):",
        "",
        fenced(tail(text) or "(no output)"),
        "",
    ])
    return title, body


def todo(context: Context, *args: str, body: str | None = None) -> subprocess.CompletedProcess:
    command = [*context.tools.todo, "--actor", ACTOR, *args]
    return subprocess.run(command, cwd=context.repo, input=body, capture_output=True, text=True, check=False)


def file_entry(context: Context, test: str, title: str, body: str) -> str:
    """Adds the entry, or updates the one of an earlier failure (reopened when it was closed). Returns what was done."""
    entry = entry_id(test)
    shown = todo(context, "show", entry, "--json")
    if shown.returncode not in (0, 2):
        raise RunError(f"todo show {entry} failed: {shown.stderr.strip() or shown.stdout.strip()}")
    if shown.returncode == 2:
        args = ["add", entry, "--area", context.area, "--title", title, "--body-file", "-"]
        done = "filed"
    else:
        data = json.loads(shown.stdout)
        data = data.get("entry", data)
        args = ["edit", entry, "--body-file", "-"]
        if str(data.get("version", "")):
            args += ["--expected-version", str(data["version"])]
        done = "updated"
        if data.get("status") == "closed":
            args += ["--status", "open"]
            done = "reopened"
    if context.dry_run:
        print(f"would run: todo --actor {ACTOR} {shlex.join(args)} <<body ({len(body.splitlines())} lines)", flush=True)
        return f"{done} (dry run)"
    written = todo(context, *args, body=body)
    if written.returncode != 0:
        raise RunError(f"todo {args[0]} {entry} failed: {written.stderr.strip() or written.stdout.strip()}")
    return done


def file_entries(context: Context, data: dict, outcome: Outcome, max_entries: int) -> list[str]:
    """Files the failures of the run and marks them failing in `data`; returns one line per entry."""
    failing = data["failing"]
    failed = outcome.failed
    if len(failed) > max_entries:
        failed = [MANY_FAILURES]
    green = last_green(context.repo, data, context.commit)
    range_text = commit_range(context.repo, green, context.commit)
    done = []
    for test in failed:
        since = failing.get(test, {}).get("since", context.commit)
        text = outcome.texts.get(test, "\n".join(outcome.failed) if test == MANY_FAILURES else "")
        title, body = entry_text(context, test, since, text, range_text, outcome.failed)
        done.append(f"{entry_id(test)} ({file_entry(context, test, title, body)})")
        failing[test] = {"since": since, "entry": entry_id(test)}
    return done


def forget_passing(data: dict, outcome: Outcome, max_entries: int) -> None:
    """Only what failed in this run stays failing (its first failure is kept for the entry); after a broken build nothing is known
    about the tests, so they keep theirs."""
    if outcome.failed == [BUILD_FAILURE]:
        return
    filed = set(outcome.failed) if len(outcome.failed) <= max_entries else {MANY_FAILURES}
    for test in list(data["failing"]):
        if test not in filed:
            del data["failing"][test]


# --- The run ----------------------------------------------------------------------------------------------------------------------


class Lock:
    """One run per work folder: the worktree and the build folder are shared by every run of the repository."""

    def __init__(self, path: Path):
        self.path = path

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self.path.exists():
            pid = int(self.path.read_text().strip() or 0)
            if pid and pid_alive(pid):
                raise RunError(f"another alltests run (pid {pid}) holds {self.path}")
            self.path.unlink()
        handle = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        with os.fdopen(handle, "w") as stream:
            stream.write(str(os.getpid()))
        return self

    def __exit__(self, *exc):
        self.path.unlink(missing_ok=True)


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run every test of a repository on a commit of its base branch and record the result.")
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="the repository to test (default: the current folder); a game names its root")
    parser.add_argument("--base", default=DEFAULT_BASE, help=f"the local base branch whose tip is tested (default: {DEFAULT_BASE})")
    parser.add_argument("--commit", help="test this commit instead of the base tip (the deploy gate: the commit to deploy)")
    parser.add_argument("--preset", default=DEFAULT_PRESETS.get(platform.system()), help="the configure preset; the tests run with <preset>-all (default: macos-debug on macOS)")
    parser.add_argument("--area", default=DEFAULT_AREA, help=f"the todo area of the entries (default: {DEFAULT_AREA}; a game names its own)")
    parser.add_argument("--work-dir", type=Path, help=f"the worktree, build, logs and results folder (default: ~/.cache/bagi-alltests/<repository>, {results.HOME_VARIABLE} moves the parent)")
    parser.add_argument("--max-entries", type=int, default=DEFAULT_MAX_ENTRIES, help=f"more failing tests than this file one entry for all (default: {DEFAULT_MAX_ENTRIES})")
    parser.add_argument("--dry-run", action="store_true", help="run the build and the tests, but write nothing to the todo store or the results file")
    parser.add_argument("--cmake", default="cmake", help=argparse.SUPPRESS)
    parser.add_argument("--ctest", default="ctest", help=argparse.SUPPRESS)
    parser.add_argument("--todo", default=shlex.join([sys.executable, str(TODO_SCRIPT)]), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if not args.preset:
        parser.error(f"name --preset: no default on {platform.system()}")
    return args


def run(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        return run_checked(args)
    except (RunError, GitError, OSError, ValueError) as error:
        print(f"alltests.py: {error}", file=sys.stderr)
        return EXIT_ERROR


def run_checked(args: argparse.Namespace) -> int:
    repo = Path(git(args.repo.resolve(), "rev-parse", "--show-toplevel")).resolve()
    revision = args.commit or f"refs/heads/{args.base}"
    try:
        commit = git(repo, "rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}")
    except GitError:
        raise RunError(f"{repo} has no commit {args.commit or 'branch ' + args.base}") from None
    repo_name = results.repo_key(repo)
    work = (args.work_dir or results.home() / repo_name).resolve()
    results_file, logs = work / results.RESULTS_FILE_NAME, work / "logs"
    tools = Tools(split_command(args.cmake), split_command(args.ctest), split_command(args.todo))
    context = Context(repo, repo_name, commit, args.preset, args.base, args.area, tools, args.dry_run)
    print(f"alltests: {repo_name} at {commit[:12]} ({args.commit or args.base}), preset {args.preset}{ALL_SUFFIX}, work folder {work}", flush=True)
    with Lock(work / "alltests.lock"):
        data = results.load(results_file, repo_name)
        update_worktree(repo, work / "worktree", commit)
        outcome = build_and_test(tools, work / "worktree", args.preset, logs)
        entries = file_entries(context, data, outcome, args.max_entries) if outcome.status == results.RED else []
        forget_passing(data, outcome, args.max_entries)
        data["results"][commit] = {
            "status": outcome.status, "preset": args.preset + ALL_SUFFIX, "base": args.base, "finished": now(),
            "failed": outcome.failed, "flaky": outcome.flaky, "entries": entries, "tests": outcome.tests,
        }
        if args.dry_run:
            print(f"dry run: {results_file} is not written", flush=True)
        else:
            results.save(results_file, data)
    report(outcome, commit, entries, results_file, logs)
    return EXIT_GREEN if outcome.status == results.GREEN else EXIT_RED


def report(outcome: Outcome, commit: str, entries: list[str], results_file: Path, logs: Path) -> None:
    if outcome.status == results.GREEN:
        print(f"alltests: green {commit[:12]}, {outcome.tests} tests", flush=True)
    else:
        print(f"alltests: red {commit[:12]}, {len(outcome.failed)} failed: {', '.join(outcome.failed)}", flush=True)
    if outcome.flaky:
        print(f"alltests: flaky (failed once, passed alone): {', '.join(outcome.flaky)}", flush=True)
    if entries:
        print(f"alltests: entries: {', '.join(entries)}", flush=True)
    print(f"alltests: results {results_file}, logs {logs}", flush=True)


if __name__ == "__main__":
    sys.exit(run())
