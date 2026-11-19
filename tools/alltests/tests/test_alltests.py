"""alltests.py over a real repository made in a temp folder, with fake cmake, ctest and todo CLI (tests/fakes.py): no build, no
store, no network. Run: .venv/bin/python -m pytest tools/alltests/tests"""

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS_DIR.parent))

import alltests  # noqa: E402
import alltests_results  # noqa: E402

FAKES = TESTS_DIR / "fakes.py"
TESTS = "Core.Adds,Core.Sorts,Scene/Param.Case/0,BagiStressControlSmoke"


@pytest.fixture(autouse=True)
def isolated(monkeypatch, tmp_path):
    empty = tmp_path / "empty.gitconfig"
    empty.write_text("", encoding="utf-8")
    monkeypatch.setenv("GIT_CONFIG_GLOBAL", str(empty))
    monkeypatch.setenv("GIT_CONFIG_SYSTEM", str(empty))
    for who in ("AUTHOR", "COMMITTER"):
        monkeypatch.setenv(f"GIT_{who}_NAME", "Tester")
        monkeypatch.setenv(f"GIT_{who}_EMAIL", "tester@example.com")
    monkeypatch.setenv("FAKE_CALLS", str(tmp_path / "calls.jsonl"))
    monkeypatch.setenv("FAKE_STORE", str(tmp_path / "store.json"))
    monkeypatch.setenv("FAKE_TESTS", TESTS)
    for name in ("FAKE_FAILING", "FAKE_FLAKY", "FAKE_FAIL_STEP"):
        monkeypatch.delenv(name, raising=False)


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=True).stdout.strip()


def commit(repo: Path, name: str) -> str:
    (repo / f"{name}.txt").write_text(name, encoding="utf-8")
    git(repo, "add", f"{name}.txt")
    git(repo, "commit", "-q", "-m", name)
    return git(repo, "rev-parse", "HEAD")


class Rig:
    def __init__(self, tmp_path: Path):
        self.tmp = tmp_path
        self.repo = tmp_path / "Game"
        self.work = tmp_path / "cache" / "Game"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        self.first = commit(self.repo, "first")

    def run(self, *argv: str) -> int:
        fake = f"{sys.executable} {FAKES}"
        return alltests.run(["--repo", str(self.repo), "--work-dir", str(self.work), "--preset", "macos-debug",
                             "--cmake", f"{fake} cmake", "--ctest", f"{fake} ctest", "--todo", f"{fake} todo", *argv])

    def calls(self, tool: str) -> list[list[str]]:
        path = self.tmp / "calls.jsonl"
        lines = path.read_text(encoding="utf-8").splitlines() if path.is_file() else []
        return [call[1:] for call in map(json.loads, lines) if call[0] == tool]

    def store(self) -> dict:
        path = self.tmp / "store.json"
        return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}

    def results(self) -> dict:
        return alltests_results.load(self.work / alltests_results.RESULTS_FILE_NAME)


@pytest.fixture
def rig(tmp_path):
    return Rig(tmp_path)


def test_a_green_run_builds_tests_with_the_all_preset_and_records_the_commit(rig, capsys):
    assert rig.run() == alltests.EXIT_GREEN
    worktree = str(rig.work / "worktree")
    assert [call[1:] for call in rig.calls("cmake")] == [["--preset", "macos-debug"], ["--build", "--preset", "macos-debug"]]
    assert all(call[0] == worktree for call in rig.calls("cmake") + rig.calls("ctest"))
    assert rig.calls("ctest")[0][1:3] == ["--preset", "macos-debug-all"]
    result = rig.results()["results"][rig.first]
    assert (result["status"], result["preset"], result["failed"], result["tests"]) == ("green", "macos-debug-all", [], 4)
    assert rig.calls("todo") == []
    assert "alltests: green" in capsys.readouterr().out


def test_the_worktree_is_detached_so_the_base_branch_stays_free_and_follows_the_local_tip(rig):
    rig.run()
    worktree = rig.work / "worktree"
    assert git(worktree, "rev-parse", "HEAD") == rig.first
    with pytest.raises(subprocess.CalledProcessError):
        git(worktree, "symbolic-ref", "-q", "HEAD")
    second = commit(rig.repo, "second")
    rig.run()
    assert git(worktree, "rev-parse", "HEAD") == second
    assert set(rig.results()["results"]) == {rig.first, second}


def test_commit_tests_the_named_commit_instead_of_the_base_tip(rig):
    commit(rig.repo, "second")
    assert rig.run("--commit", rig.first) == alltests.EXIT_GREEN
    assert git(rig.work / "worktree", "rev-parse", "HEAD") == rig.first
    assert list(rig.results()["results"]) == [rig.first]


def test_a_failure_files_one_entry_with_the_range_since_the_last_green_and_the_failure_text(rig, monkeypatch, capsys):
    rig.run()
    second = commit(rig.repo, "second")
    monkeypatch.setenv("FAKE_FAILING", "Scene/Param.Case/0")
    assert rig.run() == alltests.EXIT_RED
    entry = rig.store()["alltests-Scene-Param-Case-0"]
    assert (entry["area"], entry["status"], entry["title"]) == ("engine", "open", "alltests: Scene/Param.Case/0 fails")
    assert f"`{rig.first[:12]}..{second[:12]}`, 1" in entry["body"]
    assert "second" in entry["body"] and "(rerun)" in entry["body"]
    assert ["--actor", "alltests", "add", "alltests-Scene-Param-Case-0"] == rig.calls("todo")[-1][1:5]
    result = rig.results()["results"][second]
    assert (result["status"], result["failed"]) == ("red", ["Scene/Param.Case/0"])
    assert "alltests-Scene-Param-Case-0 (filed)" in capsys.readouterr().out


def test_a_second_failure_of_the_same_test_updates_the_open_entry(rig, monkeypatch):
    monkeypatch.setenv("FAKE_FAILING", "Core.Sorts")
    rig.run()
    second = commit(rig.repo, "second")
    rig.run()
    assert list(rig.store()) == ["alltests-Core-Sorts"]
    entry = rig.store()["alltests-Core-Sorts"]
    assert entry["version"] == 2
    assert f"First seen failing at `{rig.first[:12]}`; last at `{second[:12]}`" in entry["body"]
    edit = rig.calls("todo")[-1][1:]
    assert edit[:4] == ["--actor", "alltests", "edit", "alltests-Core-Sorts"] and "--expected-version" in edit


def test_a_closed_entry_of_the_same_test_is_reopened(rig, monkeypatch):
    monkeypatch.setenv("FAKE_FAILING", "Core.Sorts")
    rig.run()
    store = rig.store()
    store["alltests-Core-Sorts"]["status"] = "closed"
    (rig.tmp / "store.json").write_text(json.dumps(store), encoding="utf-8")
    commit(rig.repo, "second")
    rig.run()
    assert rig.store()["alltests-Core-Sorts"]["status"] == "open"


def test_a_test_that_passes_alone_is_flaky_not_a_failure(rig, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_FLAKY", "BagiStressControlSmoke")
    assert rig.run() == alltests.EXIT_GREEN
    result = rig.results()["results"][rig.first]
    assert (result["status"], result["failed"], result["flaky"]) == ("green", [], ["BagiStressControlSmoke"])
    assert "--rerun-failed" in rig.calls("ctest")[1]
    assert rig.store() == {}
    assert "flaky" in capsys.readouterr().out


def test_a_broken_build_is_one_entry_and_runs_no_test(rig, monkeypatch):
    monkeypatch.setenv("FAKE_FAIL_STEP", "build")
    assert rig.run() == alltests.EXIT_RED
    assert rig.calls("ctest") == []
    entry = rig.store()["alltests-build"]
    assert "src/Broken.cpp:12: error" in entry["body"]
    assert rig.results()["results"][rig.first]["failed"] == ["<build>"]


def test_more_failures_than_the_limit_file_one_entry_that_lists_them(rig, monkeypatch):
    monkeypatch.setenv("FAKE_FAILING", TESTS)
    assert rig.run("--max-entries", "2") == alltests.EXIT_RED
    assert list(rig.store()) == ["alltests-many-failures"]
    assert "`Core.Adds`" in rig.store()["alltests-many-failures"]["body"]


def test_a_dry_run_writes_neither_the_store_nor_the_results(rig, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_FAILING", "Core.Adds")
    assert rig.run("--dry-run") == alltests.EXIT_RED
    assert rig.store() == {}
    assert [call[3] for call in rig.calls("todo")] == ["show"]
    assert not (rig.work / alltests_results.RESULTS_FILE_NAME).exists()
    out = capsys.readouterr().out
    assert "would run: todo --actor alltests add alltests-Core-Adds" in out
    assert "+ (" in out and "macos-debug-all" in out


def test_a_green_run_clears_the_failing_tests_so_a_new_failure_starts_a_new_range(rig, monkeypatch):
    monkeypatch.setenv("FAKE_FAILING", "Core.Adds")
    rig.run()
    assert "Core.Adds" in rig.results()["failing"]
    monkeypatch.delenv("FAKE_FAILING")
    commit(rig.repo, "second")
    rig.run()
    assert rig.results()["failing"] == {}


def test_a_submodule_is_cloned_from_the_local_checkout(rig, tmp_path):
    library = tmp_path / "library"
    library.mkdir()
    git(library, "init", "-q", "-b", "main")
    pinned = commit(library, "lib")
    git(rig.repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(library), "external/lib")
    git(rig.repo, "commit", "-q", "-m", "submodule")
    commit(library, "later")
    # The library's own folder goes away: only the checkout's copy can serve the clone
    os.rename(library, tmp_path / "gone")
    assert rig.run() == alltests.EXIT_GREEN
    copy = rig.work / "worktree" / "external" / "lib"
    assert git(copy, "rev-parse", "HEAD") == pinned
    assert (copy / "lib.txt").read_text(encoding="utf-8") == "lib"


def test_a_missing_base_branch_is_an_error_and_records_nothing(rig, capsys):
    assert rig.run("--base", "nope") == alltests.EXIT_ERROR
    assert "has no commit branch nope" in capsys.readouterr().err
    assert not rig.work.joinpath(alltests_results.RESULTS_FILE_NAME).exists()


def test_a_live_lock_refuses_a_second_run(rig, capsys):
    rig.work.mkdir(parents=True)
    (rig.work / "alltests.lock").write_text(str(os.getppid()), encoding="utf-8")
    assert rig.run() == alltests.EXIT_ERROR
    assert "another alltests run" in capsys.readouterr().err


@pytest.mark.parametrize("test, expected", [
    ("Core.Adds", "alltests-Core-Adds"),
    ("Suite/Name.Case/3", "alltests-Suite-Name-Case-3"),
    ("<build>", "alltests-build"),
])
def test_entry_ids_hold_only_the_characters_the_store_takes(test, expected):
    assert alltests.entry_id(test) == expected


def test_two_long_names_that_share_a_prefix_get_two_ids():
    first, second = "A" * 100 + ".One", "A" * 100 + ".Two"
    assert alltests.entry_id(first) != alltests.entry_id(second)
    assert len(alltests.entry_id(first)) <= alltests.MAX_ID_LENGTH


# What ctest 4.2 writes with --output-junit for a passing, a failing and a skipped test
REAL_JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="(empty)"
	tests="3"
	failures="1"
	skipped="1"
	>
	<testcase name="Good.Case" classname="Good.Case" time="0.00254662" status="run">
		<properties/>
		<system-out>fine
</system-out>
	</testcase>
	<testcase name="Bad/Case.0" classname="Bad/Case.0" time="0.00233746" status="fail">
		<failure message="Failed"/>
		<properties/>
		<system-out>line1 &lt;a &amp; b&gt;
boom
</system-out>
	</testcase>
	<testcase name="Skip" classname="Skip" time="0.00262896" status="notrun">
		<skipped message="SKIP_RETURN_CODE=77"/>
		<properties/>
		<system-out></system-out>
	</testcase>
</testsuite>
"""


def test_the_junit_of_ctest_gives_the_failed_names_the_count_and_the_output(tmp_path):
    path = tmp_path / "ctest.xml"
    path.write_text(REAL_JUNIT, encoding="utf-8")
    assert alltests.read_junit(path) == (["Bad/Case.0"], 3, {"Bad/Case.0": "line1 <a & b>\nboom\n"})


class TestGate:
    def write(self, path: Path, commit: str, status: str) -> Path:
        data = alltests_results.empty_results("Game")
        data["results"][commit] = {"status": status, "preset": "macos-debug-all", "finished": "now", "failed": ["X.Y"] if status == "red" else []}
        alltests_results.save(path, data)
        return path

    def test_green_passes(self, tmp_path):
        state = self.write(tmp_path / "r.json", "a" * 40, "green")
        assert alltests_results.check(tmp_path, state, head=lambda _: "a" * 40).green

    def test_no_result_refuses_and_says_how_to_get_one(self, tmp_path):
        state = self.write(tmp_path / "r.json", "a" * 40, "green")
        verdict = alltests_results.check(tmp_path, state, head=lambda _: "b" * 40)
        assert not verdict.green and "alltests.py" in verdict.message and "--commit bbbbbbbbbbbb" in verdict.message

    def test_red_refuses_and_names_the_failures(self, tmp_path):
        state = self.write(tmp_path / "r.json", "a" * 40, "red")
        verdict = alltests_results.check(tmp_path, state, head=lambda _: "a" * 40)
        assert not verdict.green and "X.Y" in verdict.message

    def test_a_folder_that_is_not_a_repository_refuses(self, tmp_path):
        assert not alltests_results.check(tmp_path / "nowhere", tmp_path / "r.json").green

    def test_the_default_file_is_shared_by_every_worktree_of_the_repository(self, rig, monkeypatch):
        monkeypatch.setenv(alltests_results.HOME_VARIABLE, str(rig.tmp / "home"))
        git(rig.repo, "worktree", "add", "-q", "--detach", str(rig.tmp / "w1"))
        assert alltests_results.default_results_file(rig.tmp / "w1") == rig.tmp / "home" / "Game" / "results.json"
        assert alltests_results.default_results_file(rig.repo) == rig.tmp / "home" / "Game" / "results.json"
