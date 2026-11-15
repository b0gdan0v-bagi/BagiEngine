import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent
ENGINE_ROOT = TOOL_DIR.parent.parent

sys.path.insert(0, str(TOOL_DIR))

import changelog  # noqa: E402

VERSION_HEADER_TEXT = """#pragma once

#define BE_ENGINE_VERSION_MAJOR 0
#define BE_ENGINE_VERSION_MINOR 1
#define BE_ENGINE_VERSION_PATCH 0

#define BE_ENGINE_VERSION_STRING "0.1.0"
"""

CONTENT_VERSION_TEXT = "inline constexpr uint32_t kContentVersion = 5;\n"

BASELINE_DRAFT = """# The log starts here

- Entry: baseline
- Content version: 5

## Added
- The changelog starts at this version; earlier changes are in `git log`.
"""


def draft_text(entry: str = "feature", title: str = "A feature", sections: str = "## Added\n- Something new.\n", extra: str = "") -> str:
    return f"# {title}\n\n- Entry: {entry}\n{extra}\n{sections}"


class Tree:
    """A copy of the layout the tool reads, in a temp folder: never the real files."""

    def __init__(self, root: Path):
        self.root = root
        (root / changelog.VERSION_HEADER).parent.mkdir(parents=True)
        (root / changelog.VERSION_HEADER).write_text(VERSION_HEADER_TEXT, encoding="utf-8")
        (root / changelog.CONTENT_VERSION_HEADER).parent.mkdir(parents=True)
        (root / changelog.CONTENT_VERSION_HEADER).write_text(CONTENT_VERSION_TEXT, encoding="utf-8")

    def draft(self, text: str, name: str = "draft.md") -> Path:
        path = self.root.parent / name
        path.write_text(text, encoding="utf-8")
        return path

    def add(self, text: str, bump: str = "auto", date: str = "2026-11-15") -> int:
        return changelog.main(["add", "--root", str(self.root), "--draft", str(self.draft(text)), "--bump", bump, "--date", date])

    def with_baseline(self) -> "Tree":
        assert self.add(BASELINE_DRAFT, "patch") == 0
        return self

    def header(self) -> changelog.Version:
        return changelog.read_version_header(self.root)

    def fragment_file(self, entry: str) -> Path:
        return self.root / changelog.FRAGMENT_DIR / f"{entry}.md"

    def check(self) -> list[str]:
        return changelog.check_tree(self.root)


@pytest.fixture
def tree(tmp_path) -> Tree:
    root = tmp_path / "engine"
    root.mkdir()
    return Tree(root)
