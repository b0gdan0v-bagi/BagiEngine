"""alltests.py over a real repository made in a temp folder, with fake cmake, ctest and todo CLI (tests/fakes.py): no build, no
store, no network. Run: .venv/bin/python -m pytest tools/alltests/tests"""

import json
import os
import shlex
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
    monkeypatch.setenv("FAKE_SMOKE", "BagiStressControlSmoke")
    for name in ("FAKE_FAILING", "FAKE_FLAKY", "FAKE_FAIL_STEP", "FAKE_SKIPPED", "FAKE_TODO_DOWN", "FAKE_TODO_REFUSED"):
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

    def run(self, *argv: str, fake_todo: bool = True) -> int:
        fake = f"{sys.executable} {FAKES}"
        todo = ["--todo", f"{fake} todo"] if fake_todo else []
        return alltests.run(["--repo", str(self.repo), "--work-dir", str(self.work), "--preset", "macos-debug",
                             "--cmake", f"{fake} cmake", "--ctest", f"{fake} ctest", *todo, *argv])

    def drafts(self) -> list[str]:
        folder = self.work / "logs" / alltests.DRAFTS_FOLDER
        return sorted(path.name for path in folder.iterdir()) if folder.is_dir() else []

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


def test_a_skipped_test_is_counted_and_named_and_leaves_the_run_green(rig, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_SKIPPED", "Core.Sorts")
    assert rig.run() == alltests.EXIT_GREEN
    assert rig.results()["results"][rig.first]["skipped"] == ["Core.Sorts"]
    assert "4 tests, 1 skipped: Core.Sorts" in capsys.readouterr().out


def test_a_skipped_smoke_test_makes_the_run_red_and_files_it_with_the_reason(rig, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_SKIPPED", "BagiStressControlSmoke")
    assert rig.run() == alltests.EXIT_RED
    result = rig.results()["results"][rig.first]
    assert (result["status"], result["failed"]) == ("red", ["BagiStressControlSmoke"])
    assert alltests.SMOKE_SKIPPED in rig.store()["alltests-BagiStressControlSmoke"]["body"]
    assert "smoke tests were skipped" in capsys.readouterr().out


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


def test_the_venv_of_the_repository_and_of_its_engine_is_linked_so_the_configure_finds_its_python(rig, tmp_path):
    engine = tmp_path / "engine-origin"
    engine.mkdir()
    git(engine, "init", "-q", "-b", "main")
    commit(engine, "engine")
    git(rig.repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(engine), "engine")
    git(rig.repo, "commit", "-q", "-m", "engine")
    for folder in (rig.repo / ".venv", rig.repo / "engine" / ".venv"):
        (folder / "bin").mkdir(parents=True)
    assert rig.run() == alltests.EXIT_GREEN
    for folder in alltests.VENV_FOLDERS:
        link = rig.work / "worktree" / folder
        assert link.is_symlink() and link.resolve() == (rig.repo / folder).resolve()


def test_a_venv_the_worktree_has_is_kept_and_a_dangling_link_is_made_again(rig):
    (rig.repo / ".venv").mkdir()
    rig.run()
    worktree_venv = rig.work / "worktree" / ".venv"
    worktree_venv.unlink()
    worktree_venv.symlink_to(rig.tmp / "gone", target_is_directory=True)
    rig.run()
    assert worktree_venv.resolve() == (rig.repo / ".venv").resolve()
    worktree_venv.unlink()
    (worktree_venv / "bin").mkdir(parents=True)
    rig.run()
    assert not worktree_venv.is_symlink() and (worktree_venv / "bin").is_dir()


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
    assert alltests.read_junit(path) == (["Bad/Case.0"], 3, {"Bad/Case.0": "line1 <a & b>\nboom\n"}, ["Skip"], {})


def test_a_skipped_test_labelled_smoke_is_read_from_the_labels_ctest_writes(tmp_path):
    path = tmp_path / "ctest.xml"
    path.write_text(REAL_JUNIT.replace("<properties/>\n\t\t<system-out></system-out>",
                                       '<properties>\n\t\t\t<property name="cmake_labels" value="smoke;extended"/>\n\t\t</properties>\n\t\t<system-out>gone</system-out>'),
                    encoding="utf-8")
    assert alltests.read_junit(path).smoke_skipped == {"Skip": "gone"}


class TestGate:
    def write(self, path: Path, commit: str, status: str) -> Path:
        data = alltests_results.empty_results("Game")
        data["results"][commit] = {"status": status, "preset": "macos-debug-all", "finished": "now", "failed": ["X.Y"] if status == "red" else []}
        alltests_results.save(path, data)
        return path

    def test_green_passes(self, tmp_path):
        state = self.write(tmp_path / "r.json", "a" * 40, "green")
        assert alltests_results.check(tmp_path, state, head=lambda _: "a" * 40, changes=lambda *_: []).green

    def test_a_tree_with_uncommitted_changes_refuses_on_the_green_of_its_head(self, tmp_path):
        state = self.write(tmp_path / "r.json", "a" * 40, "green")
        verdict = alltests_results.check(tmp_path, state, head=lambda _: "a" * 40, changes=lambda *_: [" M a.txt"])
        assert not verdict.green and "uncommitted changes ( M a.txt)" in verdict.message

    def test_the_tree_is_read_from_git_status_less_the_untracked_files_of_the_ignored_folders(self, rig):
        assert alltests_results.uncommitted(rig.repo) == []
        (rig.repo / "first.txt").write_text("changed", encoding="utf-8")
        (rig.repo / "dist").mkdir()
        (rig.repo / "dist" / "index.html").write_text("built", encoding="utf-8")
        (rig.repo / "new.txt").write_text("new", encoding="utf-8")
        assert alltests_results.uncommitted(rig.repo, [rig.repo / "dist"]) == [" M first.txt", "?? new.txt"]
        assert "?? dist/index.html" in alltests_results.uncommitted(rig.repo)

    def test_a_tracked_change_inside_an_ignored_folder_still_counts(self, rig):
        (rig.repo / "dist").mkdir()
        commit(rig.repo, "dist/kept")
        (rig.repo / "dist" / "kept.txt").write_text("changed", encoding="utf-8")
        assert alltests_results.uncommitted(rig.repo, [rig.repo / "dist"]) == [" M dist/kept.txt"]

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


def test_a_red_run_without_the_store_records_its_result_and_drafts_the_entries(rig, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_TODO_DOWN", "1")
    monkeypatch.setenv("FAKE_FAILING", "Core.Adds,Core.Sorts")
    assert rig.run() == alltests.EXIT_RED
    data = rig.results()
    result = data["results"][rig.first]
    assert (result["status"], result["entries"]) == ("red", ["alltests-Core-Adds (not filed)", "alltests-Core-Sorts (not filed)"])
    assert data["failing"]["Core.Sorts"] == {"since": rig.first, "entry": "alltests-Core-Sorts"}
    assert len(rig.calls("todo")) == 1
    assert rig.drafts() == ["alltests-Core-Adds.md", "alltests-Core-Sorts.md"]
    draft = (rig.work / "logs" / alltests.DRAFTS_FOLDER / "alltests-Core-Sorts.md").read_text(encoding="utf-8")
    assert draft.startswith("# alltests: Core.Sorts fails\n\n**What is wrong:**") and "(rerun)" in draft
    said = [line for line in capsys.readouterr().out.splitlines() if "drafts in" in line]
    assert len(said) == 1 and said[0].startswith("alltests: ")
    assert "todo: the todo service does not answer" in said[0] and "running the last copy" not in said[0]
    assert str(rig.work / "logs" / alltests.DRAFTS_FOLDER) in said[0] and "/todo-add" in said[0]


def test_a_refused_write_drafts_that_entry_and_files_the_others(rig, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_TODO_REFUSED", "alltests-Core-Adds")
    monkeypatch.setenv("FAKE_FAILING", "Core.Adds,Core.Sorts")
    assert rig.run() == alltests.EXIT_RED
    assert rig.results()["results"][rig.first]["entries"] == ["alltests-Core-Adds (not filed)", "alltests-Core-Sorts (filed)"]
    assert list(rig.store()) == ["alltests-Core-Sorts"] and rig.drafts() == ["alltests-Core-Adds.md"]
    assert "HTTP 409: refused" in capsys.readouterr().out


def test_a_dry_run_without_the_store_drafts_and_records_nothing(rig, monkeypatch):
    monkeypatch.setenv("FAKE_TODO_DOWN", "1")
    monkeypatch.setenv("FAKE_FAILING", "Core.Sorts")
    assert rig.run("--dry-run") == alltests.EXIT_RED
    assert not (rig.work / alltests_results.RESULTS_FILE_NAME).exists()
    assert rig.drafts() == ["alltests-Core-Sorts.md"]


def test_a_later_run_with_the_store_files_the_entry_with_the_first_failing_commit(rig, monkeypatch):
    monkeypatch.setenv("FAKE_TODO_DOWN", "1")
    monkeypatch.setenv("FAKE_FAILING", "Core.Sorts")
    rig.run()
    second = commit(rig.repo, "second")
    monkeypatch.delenv("FAKE_TODO_DOWN")
    rig.run()
    assert f"First seen failing at `{rig.first[:12]}`; last at `{second[:12]}`" in rig.store()["alltests-Core-Sorts"]["body"]
    assert rig.drafts() == []


def test_a_todo_cli_that_hangs_does_not_hold_the_run(rig, monkeypatch, capsys):
    monkeypatch.setattr(alltests, "STORE_TIMEOUT_S", 0.2)
    monkeypatch.setenv("FAKE_FAILING", "Core.Sorts")
    assert rig.run("--todo", shlex.join([sys.executable, "-c", "import time; time.sleep(60)"])) == alltests.EXIT_RED
    assert rig.drafts() == ["alltests-Core-Sorts.md"] and "no answer within" in capsys.readouterr().out


def test_the_real_todo_cli_of_a_clone_without_a_store_leaves_a_recorded_run_and_a_draft(rig, monkeypatch, capsys):
    for name in ("TODO_API_URL", "TODO_API_TOKEN", "BAGITODO_CLI_PIN"):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("HOME", str(rig.tmp / "home"))
    monkeypatch.setenv("BAGITODO_CACHE", str(rig.tmp / "cli-cache"))
    monkeypatch.setenv("TODO_LOGIN_FILE", str(rig.tmp / "login.json"))
    monkeypatch.setenv("FAKE_FAILING", "Core.Sorts")
    assert rig.run(fake_todo=False) == alltests.EXIT_RED
    assert rig.results()["results"][rig.first]["entries"] == ["alltests-Core-Sorts (not filed)"]
    assert rig.drafts() == ["alltests-Core-Sorts.md"]
    assert "TODO_API_URL" in capsys.readouterr().out
