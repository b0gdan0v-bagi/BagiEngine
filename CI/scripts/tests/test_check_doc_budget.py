"""check_doc_budget.py: which CLAUDE.md files the walk finds, on small trees made in a temp folder."""

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_doc_budget as budget  # noqa: E402


def write_doc(folder: Path) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    doc = folder / "CLAUDE.md"
    doc.write_text("doc")
    return doc


def make_symlink(target: Path, link: Path, target_is_directory: bool = False) -> None:
    try:
        os.symlink(target, link, target_is_directory=target_is_directory)
    except (OSError, NotImplementedError):
        pytest.skip("this machine cannot make a symlink")


def test_a_nested_claude_md_is_found_and_the_root_one_is_not(tmp_path):
    write_doc(tmp_path)
    nested = write_doc(tmp_path / "src" / "Game")
    assert budget.walk_claude_files(tmp_path, set()) == [nested]


def test_a_directory_symlink_is_not_walked(tmp_path):
    real = write_doc(tmp_path / "real")
    make_symlink(tmp_path / "real", tmp_path / "linked", target_is_directory=True)
    assert budget.walk_claude_files(tmp_path, set()) == [real]


def test_a_broken_symlink_does_not_raise_and_is_not_counted(tmp_path):
    make_symlink(tmp_path / "missing", tmp_path / "broken")
    assert budget.walk_claude_files(tmp_path, set()) == []


def test_a_file_symlink_to_a_directory_does_not_raise_and_is_not_counted(tmp_path):
    # Windows makes a file link when the target is missing at the moment of the call (os.symlink looks at the target
    # to choose the type), as a checkout does; stat() on it raises WinError 5 once the directory exists.
    make_symlink(tmp_path / "real", tmp_path / "filelink")
    (tmp_path / "real").mkdir()
    assert budget.walk_claude_files(tmp_path, set()) == []


def test_a_symlinked_claude_md_is_not_counted_twice(tmp_path):
    real = write_doc(tmp_path / "real")
    (tmp_path / "other").mkdir()
    make_symlink(real, tmp_path / "other" / "CLAUDE.md")
    assert budget.walk_claude_files(tmp_path, set()) == [real]


def test_skipped_folders_and_a_folder_with_its_own_git_are_skipped(tmp_path):
    for name in budget.SKIP_DIRS:
        write_doc(tmp_path / name)
    write_doc(tmp_path / "submodule")
    (tmp_path / "submodule" / ".git").write_text("gitdir: elsewhere")
    kept = write_doc(tmp_path / "kept")
    assert budget.walk_claude_files(tmp_path, set()) == [kept]
