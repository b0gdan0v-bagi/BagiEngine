"""The merge driver and `settle` over real git repositories in a temp folder: two branches that each add a fragment, then merge."""

import argparse
import shutil
import subprocess
from pathlib import Path

import pytest
from conftest import ENGINE_ROOT, TOOL_DIR, Tree, changelog, draft_text

ATTRIBUTES = f"/{changelog.VERSION_HEADER.as_posix()} merge={changelog.MERGE_DRIVER}\n/{changelog.CHANGELOG_FILE} merge={changelog.MERGE_DRIVER}\n"


@pytest.fixture(autouse=True)
def isolated_git(monkeypatch, tmp_path):
    empty = tmp_path / "empty.gitconfig"
    empty.write_text("", encoding="utf-8")
    monkeypatch.setenv("GIT_CONFIG_GLOBAL", str(empty))
    monkeypatch.setenv("GIT_CONFIG_SYSTEM", str(empty))
    for who in ("AUTHOR", "COMMITTER"):
        monkeypatch.setenv(f"GIT_{who}_NAME", "Tester")
        monkeypatch.setenv(f"GIT_{who}_EMAIL", "tester@example.com")


def run_git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8", check=check)


class Repo:
    """An engine layout under git: main holds the baseline, the tool itself (the driver runs it from the checkout) and `.gitattributes`."""

    def __init__(self, root: Path, *, driver: bool):
        root.mkdir()
        self.root = root
        self.tree = Tree(root)
        subprocess.run(["git", "init", "-b", "main", str(root)], check=True, capture_output=True)
        (root / ".gitattributes").write_text(ATTRIBUTES, encoding="utf-8", newline="\n")
        script = root / changelog.DRIVER_SCRIPT
        script.parent.mkdir(parents=True)
        shutil.copy(TOOL_DIR / "changelog.py", script)
        self.tree.with_baseline()
        self.commit("baseline")
        if driver:
            assert changelog.main(["install-driver", "--root", str(root)]) == 0

    def commit(self, message: str) -> None:
        run_git(self.root, "add", "-A")
        run_git(self.root, "commit", "-m", message)

    def add_entry(self, entry: str, version: str) -> None:
        draft = self.tree.draft(draft_text(entry=entry, title=f"Entry {entry}"), f"{entry}.md")
        assert changelog.main(["add", "--root", str(self.root), "--draft", str(draft), "--bump", "patch", "--version", version, "--date", "2026-11-20"]) == 0
        self.commit(f"Changelog: {entry} ({version})")

    def branch_with_entry(self, branch: str, entry: str, version: str) -> None:
        run_git(self.root, "switch", "-c", branch, "main")
        self.add_entry(entry, version)
        run_git(self.root, "switch", "main")

    def merge(self, branch: str, *extra: str) -> subprocess.CompletedProcess:
        return run_git(self.root, "merge", "--no-ff", *extra, "-m", f"Merge {branch}", branch, check=False)

    def unmerged(self) -> list[str]:
        return run_git(self.root, "diff", "--name-only", "--diff-filter=U").stdout.split()

    def settle(self) -> int:
        return changelog.main(["settle", "--root", str(self.root)])


def two_sides(tmp_path: Path, *, driver: bool) -> Repo:
    repo = Repo(tmp_path / "engine", driver=driver)
    repo.branch_with_entry("games/x", "from-game", "0.1.4")
    repo.add_entry("on-main", "0.1.2")
    return repo


def test_driver_merges_both_derived_files_cleanly(tmp_path):
    repo = two_sides(tmp_path, driver=True)
    assert repo.merge("games/x", "--no-commit").returncode == 0
    assert repo.unmerged() == []
    assert repo.settle() == 0
    run_git(repo.root, "commit", "--no-edit")
    assert repo.tree.check() == []
    assert repo.tree.header() == (0, 1, 4)


def test_driver_result_is_already_exact_without_settle(tmp_path):
    repo = two_sides(tmp_path, driver=True)
    assert repo.merge("games/x").returncode == 0
    assert repo.tree.check() == []


def test_without_the_driver_the_two_files_conflict_and_settle_resolves_them(tmp_path):
    repo = two_sides(tmp_path, driver=False)
    assert repo.merge("games/x", "--no-commit").returncode != 0
    assert sorted(repo.unmerged()) == sorted(changelog.DERIVED_PATHS)
    assert repo.settle() == 0
    assert repo.unmerged() == []
    run_git(repo.root, "commit", "--no-edit")
    assert repo.tree.check() == []
    assert repo.tree.header() == (0, 1, 4)


def test_a_checkout_without_the_tool_leaves_a_conflict_that_settle_resolves(tmp_path):
    repo = Repo(tmp_path / "engine", driver=True)
    repo.branch_with_entry("games/x", "from-game", "0.1.4")
    run_git(repo.root, "rm", "-q", changelog.DRIVER_SCRIPT)
    repo.add_entry("on-main", "0.1.2")
    assert repo.merge("games/x", "--no-commit").returncode != 0
    assert sorted(repo.unmerged()) == sorted(changelog.DERIVED_PATHS)
    assert changelog.main(["settle", "--root", str(repo.root)]) == 0
    assert repo.unmerged() == []


def test_settle_stops_on_a_conflict_in_another_file_and_touches_nothing(tmp_path):
    repo = two_sides(tmp_path, driver=False)
    run_git(repo.root, "switch", "games/x")
    (repo.root / "notes.txt").write_text("game\n", encoding="utf-8")
    repo.commit("notes on the game")
    run_git(repo.root, "switch", "main")
    (repo.root / "notes.txt").write_text("main\n", encoding="utf-8")
    repo.commit("notes on main")
    run_git(repo.root, "switch", "games/x")
    run_git(repo.root, "merge", "--no-commit", "main", check=False)
    header_before = (repo.root / changelog.VERSION_HEADER).read_bytes()
    assert "notes.txt" in repo.unmerged()
    assert repo.settle() == changelog.EXIT_REFUSED
    assert "notes.txt" in repo.unmerged()
    assert (repo.root / changelog.VERSION_HEADER).read_bytes() == header_before


def test_settle_refuses_a_version_two_fragments_share(tmp_path):
    repo = Repo(tmp_path / "engine", driver=True)
    repo.branch_with_entry("games/x", "from-game", "0.1.4")
    repo.add_entry("on-main", "0.1.4")
    assert repo.merge("games/x", "--no-commit").returncode == 0
    assert repo.settle() == changelog.EXIT_REFUSED


def test_settle_refuses_outside_a_merge(tmp_path):
    repo = two_sides(tmp_path, driver=True)
    assert repo.settle() == changelog.EXIT_REFUSED


def test_install_driver_registers_the_interpreter_by_name(tmp_path):
    repo = Repo(tmp_path / "engine", driver=True)
    command = run_git(repo.root, "config", "--local", f"merge.{changelog.MERGE_DRIVER}.driver").stdout.strip()
    assert command == f"python3 {changelog.DRIVER_SCRIPT} merge-driver %O %A %B %P"


def test_every_command_renders_its_help():
    """argparse %-formats help strings: an unescaped `%O` fails on newer Pythons for the whole tool."""
    parser = changelog.build_parser()
    (subparsers,) = (action for action in parser._actions if isinstance(action, argparse._SubParsersAction))
    assert subparsers.choices
    assert parser.format_help()
    for sub in subparsers.choices.values():
        assert sub.format_help()


def test_header_merge_takes_the_highest_version_and_keeps_other_edits():
    base = "// top\n#define BE_ENGINE_VERSION_MAJOR 0\n#define BE_ENGINE_VERSION_MINOR 1\n#define BE_ENGINE_VERSION_PATCH 0\n// end\n"
    ours = base.replace("// top", "// top, edited")
    theirs = base.replace("PATCH 0", "PATCH 7")
    merged = changelog.merge_version_header_text(base, ours, theirs)
    assert merged == ours.replace("PATCH 0", "PATCH 7")


def test_header_merge_conflicts_when_both_sides_edit_the_same_other_line():
    base = "// top\n#define BE_ENGINE_VERSION_MAJOR 0\n#define BE_ENGINE_VERSION_MINOR 1\n#define BE_ENGINE_VERSION_PATCH 0\n"
    assert changelog.merge_version_header_text(base, base.replace("top", "a"), base.replace("top", "b").replace("PATCH 0", "PATCH 1")) is None


def test_log_merge_drops_a_block_one_side_removed():
    def log(*versions: str) -> str:
        return changelog.CHANGELOG_PREAMBLE + "".join(f"\n## {v} - 2026-11-20 - T{v}\n\nEntry: `e{v}`. Bump: patch.\n" for v in versions)

    merged = changelog.merge_log_text(log("0.1.3", "0.1.2"), log("0.1.3", "0.1.2", "0.1.9"), log("0.1.3"))
    assert merged == log("0.1.9", "0.1.3")


def test_real_files_declare_the_driver_for_exactly_the_derived_paths():
    attributes = (ENGINE_ROOT / ".gitattributes").read_text(encoding="utf-8")
    declared = sorted(line.split()[0].lstrip("/") for line in attributes.splitlines() if f"merge={changelog.MERGE_DRIVER}" in line)
    assert declared == sorted(changelog.DERIVED_PATHS)
