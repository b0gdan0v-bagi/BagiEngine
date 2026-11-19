"""The results file of the full test run (`alltests.py`) and the deploy gate that reads it.

The deploy scripts (`tools/web/build_and_deploy.py`, `tools/rooms/build_and_deploy.py`) import this module by path, so it stays
standard library only and knows nothing of the run itself. The file is JSON, one per repository:

    {"format": 1, "repo": "<name>", "results": {"<sha>": {"status": "green" | "red", "preset": ..., "base": ..., "finished": ...,
     "failed": [...], "flaky": [...], "skipped": [...], "entries": [...]}}, "failing": {"<test>": {"since": "<sha>", "entry": "alltests-..."}}}
"""

import json
import os
import subprocess
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path
from typing import NamedTuple

FORMAT = 1
HOME_VARIABLE = "BAGI_ALLTESTS_HOME"
DEFAULT_HOME = Path.home() / ".cache" / "bagi-alltests"
RESULTS_FILE_NAME = "results.json"
GREEN = "green"
RED = "red"
# The oldest results are dropped beyond this many commits, so the file of a repository tested for years stays small
MAX_RESULTS = 200
# The script a person runs, relative to the engine folder (`<engine>`: `.` in the engine, `engine` in a game)
SCRIPT = Path("tools") / "alltests" / "alltests.py"


class GitError(Exception):
    """A git command failed; the message is git's."""


def git(repo: Path, *args: str) -> str:
    completed = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise GitError(completed.stderr.strip() or f"git {' '.join(args)} failed with exit code {completed.returncode}")
    return completed.stdout.strip()


def home() -> Path:
    value = os.environ.get(HOME_VARIABLE)
    return Path(value).expanduser() if value else DEFAULT_HOME


def repo_key(repo: Path) -> str:
    """The name the results of `repo` are kept under: the folder of its main checkout, the same for every worktree of it."""
    common = Path(git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir"))
    # A submodule keeps its repository in <super>/.git/modules/<name>, a bare one in <name>.git
    return common.parent.name if common.name == ".git" else common.name


def default_work_dir(repo: Path) -> Path:
    return home() / repo_key(repo)


def default_results_file(repo: Path) -> Path:
    return default_work_dir(repo) / RESULTS_FILE_NAME


def empty_results(repo_name: str) -> dict:
    return {"format": FORMAT, "repo": repo_name, "results": {}, "failing": {}}


def load(path: Path, repo_name: str = "") -> dict:
    """The file's content, or an empty one when there is no file yet. A file of another format is an error, never overwritten."""
    if not path.is_file():
        return empty_results(repo_name)
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("format") != FORMAT:
        raise ValueError(f"{path}: format {data.get('format')!r}, expected {FORMAT}")
    data.setdefault("results", {})
    data.setdefault("failing", {})
    return data


def save(path: Path, data: dict) -> None:
    """Writes the file through a temp file and a rename, so a reader (a deploy) never sees half of it."""
    results = data["results"]
    if len(results) > MAX_RESULTS:
        oldest = sorted(results, key=lambda sha: results[sha].get("finished", ""))
        for sha in oldest[: len(results) - MAX_RESULTS]:
            del results[sha]
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temp = tempfile.mkstemp(dir=path.parent, prefix=path.name, suffix=".tmp")
    with os.fdopen(handle, "w", encoding="utf-8") as stream:
        json.dump(data, stream, indent=1, sort_keys=True)
        stream.write("\n")
    os.replace(temp, path)


class Verdict(NamedTuple):
    green: bool
    commit: str
    message: str


def head_of(source_dir: Path) -> str:
    return git(source_dir, "rev-parse", "--verify", "HEAD^{commit}")


def check(source_dir: Path, results_file: Path | None = None, head: Callable[[Path], str] = head_of) -> Verdict:
    """Whether HEAD of `source_dir` has a green full run. The message says what the result is and, when it is not green, how to get one."""
    try:
        commit = head(source_dir)
        results_file = results_file or default_results_file(source_dir)
    except GitError as error:
        return Verdict(False, "", f"cannot read the commit of {source_dir}: {error}")
    short = commit[:12]
    how = f"run the full test run first: /alltests, or python3 <engine>/{SCRIPT.as_posix()} --repo {source_dir} --commit {short}"
    try:
        result = load(results_file).get("results", {}).get(commit)
    except (OSError, ValueError) as error:
        return Verdict(False, commit, f"cannot read the alltests results {results_file}: {error}; {how}")
    if result is None:
        return Verdict(False, commit, f"commit {short} has no alltests result in {results_file}; {how}")
    if result.get("status") != GREEN:
        failed = ", ".join(result.get("failed", [])) or "unknown"
        return Verdict(False, commit, f"alltests is red for commit {short} (failed: {failed}); fix it, then {how}")
    return Verdict(True, commit, f"alltests is green for commit {short} ({result.get('preset', '?')}, {result.get('finished', '?')})")


def allows_deploy(verdict: Verdict, skip: bool, dry_run: bool) -> bool:
    """The deploy gate of the deploy scripts: prints the verdict and says whether the deploy goes on. --skip-alltests passes a gate
    that is not green and says so; a dry run only reports it."""
    if verdict.green:
        print(f"alltests gate: {verdict.message}", flush=True)
    elif skip:
        print(f"alltests gate skipped (--skip-alltests), deploying anyway: {verdict.message}", flush=True)
    elif dry_run:
        print(f"alltests gate: --deploy would stop here: {verdict.message}", flush=True)
    else:
        print(f"build_and_deploy.py: {verdict.message} (--skip-alltests deploys anyway)", file=sys.stderr)
        return False
    return True
