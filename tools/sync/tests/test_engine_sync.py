"""engine_sync.py over real repositories: a bare "origin", a seed clone that fills it and a work clone that the helper reads.
No network, no dependence on the user's git configuration."""

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

SYNC_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SYNC_DIR))

import engine_sync  # noqa: E402

GAMES = ("games/mahjong", "games/chess")


@pytest.fixture(autouse=True)
def isolated_git(monkeypatch, tmp_path):
    empty = tmp_path / "empty.gitconfig"
    empty.write_text("", encoding="utf-8")
    monkeypatch.setenv("GIT_CONFIG_GLOBAL", str(empty))
    monkeypatch.setenv("GIT_CONFIG_SYSTEM", str(empty))
    for who in ("AUTHOR", "COMMITTER"):
        monkeypatch.setenv(f"GIT_{who}_NAME", "Tester")
        monkeypatch.setenv(f"GIT_{who}_EMAIL", "tester@example.com")


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8", check=True)
    return result.stdout.strip()


def commit(repo: Path, name: str) -> str:
    (repo / f"{name}.txt").write_text(name, encoding="utf-8")
    git(repo, "add", f"{name}.txt")
    git(repo, "commit", "-m", name)
    return git(repo, "rev-parse", "HEAD")


def merge_todo(repo: Path, into: str, todo_id: str) -> None:
    """A todo branch cut from `into`, one commit on it, merged back with --no-ff the way todo-merge does."""
    git(repo, "switch", into)
    git(repo, "switch", "-c", f"todo/{todo_id}")
    commit(repo, f"work-{todo_id}")
    git(repo, "switch", into)
    git(repo, "merge", "--no-ff", "-m", f"Merge branch 'todo/{todo_id}'", f"todo/{todo_id}")


class Rig:
    """`origin` (bare) holds main, games/mahjong and games/chess at one commit; `seed` pushes to it; `work` is the clone under test
    with local main, games/mahjong and games/chess, both games included in its engine-sync config."""

    def __init__(self, tmp_path: Path):
        self.origin = tmp_path / "origin.git"
        self.seed = tmp_path / "seed"
        self.work = tmp_path / "work"
        subprocess.run(["git", "init", "--bare", "-b", "main", str(self.origin)], check=True, capture_output=True)
        subprocess.run(["git", "init", "-b", "main", str(self.seed)], check=True, capture_output=True)
        git(self.seed, "remote", "add", "origin", str(self.origin))
        commit(self.seed, "base")
        for name in GAMES:
            git(self.seed, "branch", name)
        git(self.seed, "push", "origin", "main", *GAMES)
        subprocess.run(["git", "clone", str(self.origin), str(self.work)], check=True, capture_output=True)
        for name in GAMES:
            git(self.work, "branch", name, f"origin/{name}")
            git(self.work, "config", "--local", "--add", engine_sync.CONFIG_INCLUDE, name)

    def fetch(self) -> None:
        git(self.work, "fetch", "origin")

    def branches(self, capsys, repo: Path | None = None) -> tuple[int, dict]:
        code, report = self.run(capsys, repo)
        return code, {branch["name"]: branch for branch in report["branches"]}

    def run(self, capsys, repo: Path | None = None) -> tuple[int, dict]:
        capsys.readouterr()
        code = engine_sync.main(["--repo", str(repo or self.work), "status", "--json"])
        return code, json.loads(capsys.readouterr().out)

    def pushes(self, capsys) -> list[str]:
        capsys.readouterr()
        assert engine_sync.main(["--repo", str(self.work), "pushes"]) == 0
        return capsys.readouterr().out.splitlines()


@pytest.fixture
def rig(tmp_path) -> Rig:
    return Rig(tmp_path)


def test_a_clone_that_matches_origin_is_all_equal(rig, capsys):
    code, branches = rig.branches(capsys)
    assert code == 0
    assert list(branches) == ["main", "games/chess", "games/mahjong"]
    assert {branch["relation"] for branch in branches.values()} == {"equal"}
    assert branches["games/chess"]["ahead_of_main"] == 0 and not branches["games/chess"]["own_work"]
    assert rig.pushes(capsys) == []


def test_a_local_branch_ahead_of_origin_is_reported_and_its_push_line_printed(rig, capsys):
    git(rig.work, "switch", "games/mahjong")
    commit(rig.work, "local-work")
    code, branches = rig.branches(capsys)
    assert code == 0
    assert branches["games/mahjong"]["relation"] == "ahead" and branches["games/mahjong"]["local_ahead"] == 1
    assert rig.pushes(capsys) == ["git push origin games/mahjong"]


def test_behind_origin_stops_with_exit_2(rig, capsys):
    commit(rig.seed, "elsewhere")
    git(rig.seed, "push", "origin", "main")
    rig.fetch()
    code, report = rig.run(capsys)
    assert code == 2 and report["stop"] == ["main"]
    assert {b["name"]: b["relation"] for b in report["branches"]}["main"] == "behind"
    assert rig.pushes(capsys) == []


def test_diverged_from_origin_stops_with_exit_2(rig, capsys):
    git(rig.work, "switch", "games/chess")
    commit(rig.work, "here")
    git(rig.seed, "switch", "games/chess")
    commit(rig.seed, "there")
    git(rig.seed, "push", "origin", "games/chess")
    rig.fetch()
    code, report = rig.run(capsys)
    assert code == 2 and report["stop"] == ["games/chess"]
    chess = next(b for b in report["branches"] if b["name"] == "games/chess")
    assert chess["relation"] == "diverged" and (chess["local_ahead"], chess["local_behind"]) == (1, 1)


def test_a_branch_only_origin_has_is_no_local_and_is_not_a_stop(rig, capsys):
    git(rig.work, "branch", "-D", "games/chess")
    code, branches = rig.branches(capsys)
    assert code == 0
    assert branches["games/chess"]["relation"] == "no-local" and branches["games/chess"]["local"] is None
    assert branches["games/chess"]["origin"] is not None


def test_a_local_branch_origin_lacks_is_no_remote_and_gets_a_push_line(rig, capsys):
    git(rig.work, "branch", "games/newgame", "main")
    git(rig.work, "config", "--local", "--add", engine_sync.CONFIG_INCLUDE, "games/newgame")
    code, branches = rig.branches(capsys)
    assert code == 0 and branches["games/newgame"]["relation"] == "no-remote"
    assert rig.pushes(capsys) == ["git push origin games/newgame"]


def test_todo_merges_and_plain_commits_of_a_game_are_listed_in_order(rig, capsys):
    merge_todo(rig.work, "games/mahjong", "first")
    merge_todo(rig.work, "games/mahjong", "second")
    git(rig.work, "switch", "games/mahjong")
    commit(rig.work, "plain")
    _, branches = rig.branches(capsys)
    mahjong = branches["games/mahjong"]
    assert mahjong["merges"] == ["Merge branch 'todo/first'", "Merge branch 'todo/second'"]
    assert mahjong["commits"] == 1 and mahjong["own_work"]
    assert (mahjong["ahead_of_main"], mahjong["behind_main"]) == (5, 0)


def test_a_game_behind_main_is_counted(rig, capsys):
    git(rig.work, "switch", "main")
    commit(rig.work, "showcase")
    commit(rig.work, "showcase-2")
    _, branches = rig.branches(capsys)
    assert (branches["games/mahjong"]["ahead_of_main"], branches["games/mahjong"]["behind_main"]) == (0, 2)
    assert not branches["games/mahjong"]["own_work"]


def test_a_merge_of_main_into_a_game_is_not_work_of_its_own(rig, capsys):
    git(rig.work, "switch", "games/mahjong")
    commit(rig.work, "own")
    git(rig.work, "switch", "main")
    commit(rig.work, "showcase")
    git(rig.work, "switch", "games/chess")
    commit(rig.work, "chess-own")
    git(rig.work, "merge", "--no-ff", "-m", "Merge branch 'main' into games/chess", "main")
    git(rig.work, "switch", "games/mahjong")
    git(rig.work, "merge", "--no-ff", "-m", "Merge branch 'main' into games/mahjong", "main")
    _, branches = rig.branches(capsys)
    mahjong = branches["games/mahjong"]
    assert mahjong["sync_merges"] == 1 and mahjong["merges"] == [] and mahjong["commits"] == 1 and mahjong["behind_main"] == 0
    # a game that only took main in has nothing for Up
    git(rig.work, "switch", "games/chess")
    git(rig.work, "reset", "--hard", "main")
    git(rig.work, "switch", "games/mahjong")
    git(rig.work, "reset", "--hard", "origin/games/mahjong")
    git(rig.work, "merge", "--no-ff", "-m", "Merge branch 'main' into games/mahjong", "main")
    _, branches = rig.branches(capsys)
    assert branches["games/mahjong"]["sync_merges"] == 1 and not branches["games/mahjong"]["own_work"]
    assert branches["games/mahjong"]["ahead_of_main"] == 1


def test_a_branch_held_by_another_worktree_is_named(rig, capsys, tmp_path):
    other = tmp_path / "other"
    git(rig.work, "worktree", "add", str(other), "games/chess")
    git(rig.work, "switch", "games/mahjong")
    _, branches = rig.branches(capsys)
    assert branches["games/chess"]["held_elsewhere"]
    assert os.path.samefile(branches["games/chess"]["worktree"], other)
    assert not branches["games/mahjong"]["held_elsewhere"]  # this checkout is on it
    assert branches["games/mahjong"]["worktree"] is not None
    assert branches["main"]["worktree"] is None


def test_a_linked_worktree_is_told_apart_from_the_main_checkout(rig, capsys, tmp_path):
    other = tmp_path / "other"
    git(rig.work, "worktree", "add", str(other), "games/chess")
    _, report = rig.run(capsys)
    assert not report["linked_worktree"]
    _, linked = rig.run(capsys, other)
    assert linked["linked_worktree"]
    chess = next(b for b in linked["branches"] if b["name"] == "games/chess")
    assert not chess["held_elsewhere"]  # held by this very worktree


def test_excluded_and_undecided_branches_do_not_take_part(rig, capsys):
    for name in ("games/match3", "games/puzzle"):
        git(rig.seed, "branch", name)
        git(rig.seed, "push", "origin", name)
    rig.fetch()
    git(rig.work, "config", "--local", "--add", engine_sync.CONFIG_EXCLUDE, "games/match3")
    code, report = rig.run(capsys)
    assert code == 0
    assert [b["name"] for b in report["branches"]] == ["main", "games/chess", "games/mahjong"]
    assert report["excluded"] == ["games/match3"] and report["undecided"] == ["games/puzzle"]


def test_status_without_json_prints_a_table_and_the_stop_line(rig, capsys):
    merge_todo(rig.work, "games/mahjong", "first")
    commit(rig.seed, "elsewhere")
    git(rig.seed, "push", "origin", "main")
    rig.fetch()
    capsys.readouterr()
    assert engine_sync.main(["--repo", str(rig.work), "status"]) == 2
    out = capsys.readouterr().out
    assert "Merge branch 'todo/first'" in out and "STOP: main" in out


def test_the_helper_changes_nothing(rig, capsys):
    merge_todo(rig.work, "games/mahjong", "first")
    snapshot = lambda: (git(rig.work, "for-each-ref"), git(rig.work, "config", "--local", "--list"), git(rig.work, "status", "--porcelain"))
    before = snapshot()
    rig.run(capsys)
    rig.pushes(capsys)
    assert snapshot() == before


def test_errors_exit_with_1(rig, capsys, tmp_path):
    assert engine_sync.main(["--repo", str(tmp_path / "nowhere"), "status"]) == 1
    empty = tmp_path / "empty"
    subprocess.run(["git", "init", "-b", "trunk", str(empty)], check=True, capture_output=True)
    assert engine_sync.main(["--repo", str(empty), "status"]) == 1
    assert "no branch 'main'" in capsys.readouterr().err


def test_the_script_runs_as_a_command(rig):
    result = subprocess.run([sys.executable, str(SYNC_DIR / "engine_sync.py"), "--repo", str(rig.work), "status", "--json"], capture_output=True, text=True, check=False)
    assert result.returncode == 0
    assert json.loads(result.stdout)["branches"][0]["name"] == "main"
