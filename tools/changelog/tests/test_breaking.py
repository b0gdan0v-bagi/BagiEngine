"""`breaking --since` over real repositories made in a temp folder (no network, no dependence on the user's git configuration)."""

import json
import subprocess
from pathlib import Path

import pytest

import changelog
from conftest import Tree, draft_text

BREAKING = "## Breaking\n- `Foo` is gone. Adapt: call `Bar`.\n"
MIGRATION = "## Migration\n- Run `migrate.py`, step 6.\n"


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


def commit_all(repo: Path, message: str) -> str:
    git(repo, "add", "-A")
    git(repo, "commit", "-m", message)
    return git(repo, "rev-parse", "HEAD")


def merge_entry(tree: Tree, entry: str, sections: str = "## Added\n- Something.\n", extra: str = "") -> str:
    """A todo branch with the fragment on it, merged back with --no-ff; returns the commit before the merge."""
    before = git(tree.root, "rev-parse", "HEAD")
    git(tree.root, "switch", "-c", f"todo/{entry}")
    assert tree.add(draft_text(entry, f"Entry {entry}", sections, extra)) == 0
    commit_all(tree.root, f"work {entry}")
    git(tree.root, "switch", "main")
    git(tree.root, "merge", "--no-ff", "-m", f"Merge branch 'todo/{entry}'", f"todo/{entry}")
    return before


@pytest.fixture
def repo(tree) -> Tree:
    """An engine repository whose log starts at 0.1.0; `initial` is the commit before the baseline."""
    git(tree.root, "init", "-b", "main")
    (tree.root / "README.md").write_text("engine\n", encoding="utf-8")
    tree.initial = commit_all(tree.root, "initial")
    tree.with_baseline()
    commit_all(tree.root, "baseline")
    tree.baseline = git(tree.root, "rev-parse", "HEAD")
    return tree


def run(capsys, *args: str, repo: Tree) -> tuple[int, str, str]:
    capsys.readouterr()
    code = changelog.main(["breaking", "--repo", str(repo.root), *args])
    captured = capsys.readouterr()
    return code, captured.out, captured.err


def report(capsys, repo: Tree, *args: str) -> dict:
    code, out, _ = run(capsys, "--json", *args, repo=repo)
    assert code == 0
    return json.loads(out)


def test_a_commit_lists_the_entries_merged_after_it_newest_first(repo, capsys):
    (repo.root / changelog.CONTENT_VERSION_HEADER).write_text("inline constexpr uint32_t kContentVersion = 6;\n", encoding="utf-8")
    point = merge_entry(repo, "first", BREAKING + MIGRATION, "- Content version: 6\n")
    merge_entry(repo, "second", "## Added\n- A thing.\n")
    merge_entry(repo, "third", MIGRATION)
    data = report(capsys, repo, "--since", point)
    assert [e["entry"] for e in data["entries"]] == ["third", "first"]
    assert [e["version"] for e in data["entries"]] == ["0.2.2", "0.2.0"]
    assert data["entries"][1]["breaking"] == ["`Foo` is gone. Adapt: call `Bar`."]
    assert data["entries"][1]["migration"] == ["Run `migrate.py`, step 6."]
    assert data["entries"][1]["content_version"] == 6
    assert data["breaking"] is True and data["kind"] == "commit" and data["before_the_log"] is False
    assert data["commit"] == point


def test_all_lists_every_entry(repo, capsys):
    point = merge_entry(repo, "first", "## Added\n- A thing.\n")
    merge_entry(repo, "second", BREAKING)
    assert [e["entry"] for e in report(capsys, repo, "--since", point)["entries"]] == ["second"]
    assert [e["entry"] for e in report(capsys, repo, "--since", point, "--all")["entries"]] == ["second", "first"]


def test_a_point_after_the_last_merge_lists_nothing(repo, capsys):
    merge_entry(repo, "first", BREAKING)
    head = git(repo.root, "rev-parse", "HEAD")
    code, out, _ = run(capsys, "--since", head, repo=repo)
    assert code == 0 and out.strip() == f"Nothing listed since {head}."


def test_a_point_before_the_log_says_where_the_log_starts(repo, capsys):
    merge_entry(repo, "first", BREAKING)
    data = report(capsys, repo, "--since", repo.initial, "--all")
    assert data["before_the_log"] is True and data["log_starts"] == "0.1.0"
    assert [e["entry"] for e in data["entries"]] == ["first", "baseline"]
    code, out, _ = run(capsys, "--since", repo.initial, repo=repo)
    assert out.splitlines()[0].startswith("The log starts at 0.1.0") and f"git log {repo.initial}..HEAD" in out
    _, out, _ = run(capsys, "--since", repo.baseline, repo=repo)
    assert "The log starts" not in out


def test_a_version_point_needs_no_git(repo, capsys):
    merge_entry(repo, "first", BREAKING)
    merge_entry(repo, "second", MIGRATION)
    data = report(capsys, repo, "--since", "0.2.0")
    assert data["kind"] == "version" and data["commit"] is None
    assert [e["entry"] for e in data["entries"]] == ["second"]
    assert report(capsys, repo, "--since", "0.0.1", "--all")["before_the_log"] is True
    assert report(capsys, repo, "--since", "0.2.1")["entries"] == []


def test_check_exits_1_only_for_a_breaking_entry(repo, capsys):
    point = merge_entry(repo, "first", MIGRATION)
    assert run(capsys, "--since", point, "--check", repo=repo)[0] == 0
    merge_entry(repo, "second", BREAKING)
    code, out, _ = run(capsys, "--since", point, "--check", repo=repo)
    assert code == 1 and "BREAKING" in out
    assert run(capsys, "--since", point, repo=repo)[0] == 0


def test_the_text_report_shows_the_adapt_text_and_the_migration(repo, capsys):
    point = merge_entry(repo, "first", BREAKING + MIGRATION)
    _, out, _ = run(capsys, "--since", point, repo=repo)
    assert "0.2.0 Entry first (first, 2026-11-15) BREAKING" in out
    assert "    - `Foo` is gone. Adapt: call `Bar`." in out and "    - Run `migrate.py`, step 6." in out


def test_json_has_the_documented_shape(repo, capsys):
    point = merge_entry(repo, "first", BREAKING)
    data = report(capsys, repo, "--since", point)
    assert set(data) == {"since", "kind", "commit", "log_starts", "before_the_log", "breaking", "entries"}
    assert set(data["entries"][0]) == {"version", "title", "entry", "merge", "date", "bump", "content_version", "added", "changed", "breaking", "migration"}


def test_a_bad_point_is_an_error(repo, capsys):
    code, _, err = run(capsys, "--since", "no-such-commit", repo=repo)
    assert code == changelog.EXIT_ERROR and "no-such-commit" in err


def test_pointer_is_the_commit_the_superproject_records(repo, tmp_path, capsys):
    superproject = tmp_path / "game"
    superproject.mkdir()
    git(superproject, "init", "-b", "main")
    git(superproject, "-c", "protocol.file.allow=always", "submodule", "add", str(repo.root), "engine")
    commit_all(superproject, "engine at the baseline")
    engine = superproject / "engine"
    clone = Tree(engine, create=False)
    assert changelog.main(["breaking", "--repo", str(engine), "--since", "pointer", "--json"]) == 0
    assert json.loads(capsys.readouterr().out)["entries"] == []

    git(engine, "switch", "-c", "todo/next")
    assert clone.add(draft_text("next", "Next", BREAKING)) == 0
    commit_all(engine, "next entry")
    data = json.loads(run(capsys, "--since", "pointer", "--json", repo=clone)[1])
    assert data["kind"] == "pointer" and data["commit"] == repo.baseline
    assert [e["entry"] for e in data["entries"]] == ["next"]
    assert changelog.main(["breaking", "--repo", str(engine), "--since", "pointer", "--check"]) == 1
    capsys.readouterr()


def test_pointer_outside_a_submodule_is_an_error(repo, capsys):
    code, _, err = run(capsys, "--since", "pointer", repo=repo)
    assert code == changelog.EXIT_ERROR and "not a submodule" in err
