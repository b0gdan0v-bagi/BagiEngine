import os
import subprocess

import pytest

import changelog
from conftest import BASELINE_DRAFT, draft_text

FLOOR = "0.1.0"
BREAKING = "## Breaking\n- `Foo` is gone. Adapt: call `Bar`.\n"


class Repo:
    """A git repository with the layout of the tool: `main` and two `games/*` branches that counted on their own."""

    def __init__(self, tree):
        self.tree = tree
        self.root = tree.root
        self.clock = 1_700_000_000
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "user.name", "test")
        assert tree.add(BASELINE_DRAFT, "patch") == 0
        self.commit("baseline")

    def git(self, *args: str) -> str:
        self.clock += 60
        stamp = f"{self.clock} +0000"
        env = {**os.environ, "GIT_AUTHOR_DATE": stamp, "GIT_COMMITTER_DATE": stamp}
        return subprocess.run(["git", "-C", str(self.root), *args], check=True, capture_output=True, text=True, env=env).stdout

    def commit(self, message: str) -> None:
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", message)

    def merged(self, entry: str, version: str, date: str, sections: str = "## Added\n- New.\n", bump: str = "patch") -> None:
        draft = self.tree.draft(draft_text(entry, sections=sections))
        code = changelog.main(["add", "--root", str(self.root), "--draft", str(draft), "--bump", bump, "--date", date, "--version", version])
        assert code == 0
        self.commit(f"Changelog: {entry}")

    def plan(self) -> dict[str, str]:
        items = changelog.renumber_plan(self.root, changelog.renumber_refs(self.root), changelog.parse_version(FLOOR))
        return {item.entry: changelog.version_text(item.new) for item in items}

    def renumber(self, *mode: str) -> int:
        return changelog.main(["renumber", "--root", str(self.root), "--floor", FLOOR, *mode])


@pytest.fixture
def repo(tree) -> Repo:
    repo = Repo(tree)
    repo.git("switch", "-q", "-c", "games/a")
    repo.merged("late", "0.1.1", "2026-11-17")
    repo.git("switch", "-q", "-c", "games/b", "main")
    repo.merged("early", "0.1.1", "2026-11-15")
    repo.merged("breaks", "0.2.0", "2026-11-16", BREAKING, "minor")
    repo.git("switch", "-q", "main")
    repo.merged("early", "0.1.2", "2026-11-15")
    repo.merged("late", "0.1.3", "2026-11-17")
    return repo


def test_the_plan_is_one_sequence_by_merge_date_and_keeps_the_bumps(repo):
    assert repo.plan() == {"early": "0.1.1", "breaks": "0.2.0", "late": "0.2.1"}


def test_the_plan_writes_nothing(repo, capsys):
    assert repo.renumber("--plan") == 0
    out = capsys.readouterr().out
    assert "0.2.1  2026-11-17  patch  late" in out and "highest: 0.2.1" in out
    assert repo.git("status", "--porcelain") == ""


@pytest.mark.parametrize(
    "branch, header, versions",
    [
        ("main", (0, 2, 1), {"early": "0.1.1", "late": "0.2.1"}),
        ("games/a", (0, 2, 1), {"late": "0.2.1"}),
        ("games/b", (0, 2, 0), {"early": "0.1.1", "breaks": "0.2.0"}),
    ],
)
def test_apply_leaves_each_branch_consistent_with_gaps(repo, branch, header, versions):
    repo.git("switch", "-q", branch)
    assert repo.renumber("--apply") == 0
    assert repo.tree.header() == header and repo.tree.check() == []
    for entry, version in versions.items():
        assert f"- Version: {version}\n" in repo.tree.fragment_file(entry).read_text(encoding="utf-8")


def test_a_branch_already_renumbered_does_not_change_the_plan_of_the_others(repo):
    before = repo.plan()
    assert repo.renumber("--apply") == 0
    repo.commit("Changelog: renumber after the shared counter")
    assert repo.plan() == before
    repo.git("switch", "-q", "games/b")
    assert repo.renumber("--apply") == 0
    repo.commit("Changelog: renumber after the shared counter")
    assert repo.plan() == before
    assert repo.renumber("--apply") == 0
    assert repo.git("status", "--porcelain") == ""


def test_apply_changes_only_the_version_line_of_a_fragment(repo):
    path = repo.tree.fragment_file("late")
    before = path.read_text(encoding="utf-8").splitlines()
    assert repo.renumber("--apply") == 0
    after = path.read_text(encoding="utf-8").splitlines()
    assert [(old, new) for old, new in zip(before, after) if old != new] == [("- Version: 0.1.3", "- Version: 0.2.1")]


def test_apply_refuses_a_fragment_no_branch_of_the_plan_holds(repo, capsys):
    repo.merged("local", "0.1.9", "2026-11-18")
    assert changelog.main(["renumber", "--root", str(repo.root), "--floor", FLOOR, "--apply", "--ref", "games/a", "--ref", "games/b"]) == changelog.EXIT_ERROR
    assert "not on any branch of the plan: local" in capsys.readouterr().err
    assert "- Version: 0.1.9" in repo.tree.fragment_file("local").read_text(encoding="utf-8")


def test_the_plan_refuses_an_entry_with_two_bumps(repo, capsys):
    repo.git("switch", "-q", "games/a")
    repo.merged("early", "0.3.0", "2026-11-15", bump="minor")
    assert repo.renumber("--plan") == changelog.EXIT_ERROR
    assert "no single bump" in capsys.readouterr().err
