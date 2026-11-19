"""baseline_cache.py on small git repositories made in a temp folder, one of them with a second worktree."""

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[3]

sys.path.insert(0, str(ENGINE / "CI" / "scripts"))

import baseline_cache  # noqa: E402
import pytest  # noqa: E402

pytestmark = pytest.mark.skipif(shutil.which("git") is None, reason="git is not installed")


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True, text=True).stdout.strip()


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    git(root, "init", "-q", "-b", "main")
    git(root, "config", "user.email", "test@example.com")
    git(root, "config", "user.name", "test")
    (root / "a.txt").write_text("one\n")
    git(root, "add", "a.txt")
    git(root, "commit", "-q", "-m", "one")
    return root


def commit_change(root: Path, text: str) -> None:
    (root / "a.txt").write_text(text)
    git(root, "commit", "-q", "-am", text)


def names_file(tmp_path: Path, text: str) -> Path:
    path = tmp_path / "names.txt"
    path.write_text(text)
    return path


def run(root: Path, *args: str) -> int:
    return baseline_cache.main([*args, "--root", str(root)])


def test_a_stored_baseline_is_read_back_on_the_same_commit_and_preset(repo, tmp_path):
    out = tmp_path / "out.txt"
    assert run(repo, "get", "--preset", "macos-debug", "--out", str(out)) == baseline_cache.EXIT_MISS
    assert not out.exists()

    assert run(repo, "put", "--preset", "macos-debug", "--file", str(names_file(tmp_path, "FooTest.Bar\n"))) == 0
    assert run(repo, "get", "--preset", "macos-debug", "--out", str(out)) == 0
    assert out.read_text() == "FooTest.Bar\n"


def test_an_all_green_baseline_is_a_hit(repo, tmp_path):
    out = tmp_path / "out.txt"
    assert run(repo, "put", "--preset", "macos-debug", "--file", str(names_file(tmp_path, ""))) == 0
    assert run(repo, "get", "--preset", "macos-debug", "--out", str(out)) == 0
    assert out.read_text() == ""


def test_another_preset_or_commit_misses(repo, tmp_path):
    first = git(repo, "rev-parse", "HEAD")
    assert run(repo, "put", "--preset", "macos-debug", "--file", str(names_file(tmp_path, "A.B\n"))) == 0
    out = tmp_path / "out.txt"
    assert run(repo, "get", "--preset", "macos-debug-all", "--out", str(out)) == baseline_cache.EXIT_MISS

    commit_change(repo, "two\n")
    assert run(repo, "get", "--preset", "macos-debug", "--out", str(out)) == baseline_cache.EXIT_MISS
    assert run(repo, "get", "--preset", "macos-debug", "--commit", first, "--out", str(out)) == 0
    assert out.read_text() == "A.B\n"


def test_a_worktree_reads_what_the_main_checkout_stored(repo, tmp_path):
    worker = tmp_path / "worker"
    git(repo, "worktree", "add", "-q", "--detach", str(worker), "HEAD")
    assert run(repo, "put", "--preset", "macos-debug", "--file", str(names_file(tmp_path, "A.B\n"))) == 0

    out = tmp_path / "out.txt"
    assert run(worker, "get", "--preset", "macos-debug", "--out", str(out)) == 0
    assert out.read_text() == "A.B\n"


def test_a_dirty_tree_is_not_stored(repo, tmp_path):
    (repo / "a.txt").write_text("changed\n")
    assert run(repo, "put", "--preset", "macos-debug", "--file", str(names_file(tmp_path, ""))) == baseline_cache.EXIT_REFUSED
    out = tmp_path / "out.txt"
    assert run(repo, "get", "--preset", "macos-debug", "--out", str(out)) == baseline_cache.EXIT_MISS


def test_put_stores_for_head_only(repo, tmp_path):
    names = str(names_file(tmp_path, ""))
    assert run(repo, "put", "--preset", "macos-debug", "--commit", "HEAD~0", "--file", names) == baseline_cache.EXIT_ERROR


def test_a_preset_name_cannot_leave_the_folder(repo, tmp_path):
    out = tmp_path / "out.txt"
    assert run(repo, "get", "--preset", "../escape", "--out", str(out)) == baseline_cache.EXIT_ERROR


def test_put_removes_stale_baselines(repo, tmp_path):
    folder = baseline_cache.cache_folder(repo)
    folder.mkdir(parents=True)
    stale = folder / "old-macos-debug.txt"
    stale.write_text("")
    long_ago = time.time() - (baseline_cache.STALE_DAYS + 1) * baseline_cache.SECONDS_PER_DAY
    os.utime(stale, (long_ago, long_ago))

    assert run(repo, "put", "--preset", "macos-debug", "--file", str(names_file(tmp_path, ""))) == 0
    assert not stale.exists()
    assert len(list(folder.glob("*.txt"))) == 1


def test_a_bad_commit_is_an_error(repo, tmp_path):
    out = tmp_path / "out.txt"
    assert run(repo, "get", "--preset", "macos-debug", "--commit", "no-such-rev", "--out", str(out)) == baseline_cache.EXIT_ERROR
