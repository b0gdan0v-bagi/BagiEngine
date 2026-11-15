import pytest

import changelog
from conftest import BASELINE_DRAFT, draft_text

BREAKING = "## Breaking\n- `Foo` is gone. Adapt: call `Bar`.\n"
MIGRATION = "## Migration\n- Run `migrate.py`, step 6.\n"


def test_a_fragment_survives_parse_and_format():
    text = (
        "# A title\n\n- Version: 0.4.0\n- Entry: some-entry\n- Date: 2026-11-15\n- Bump: minor\n- Content version: 6\n\n"
        "## Added\n- one\n  continued\n- two\n\n## Breaking\n- gone. Adapt: use X.\n\n## Migration\n- run it\n"
    )
    fragment = changelog.parse_fragment(text)
    assert fragment.version == (0, 4, 0) and fragment.content_version == 6
    assert fragment.bullets("Added") == ["one\ncontinued", "two"]
    assert changelog.format_fragment(fragment) == text


@pytest.mark.parametrize(
    "text, message",
    [
        ("no heading\n", "first line"),
        ("# T\n\n- Colour: red\n", "expected"),
        ("# T\n\n## Fixed\n- a\n", "unknown section"),
        ("# T\n\n## Added\n- a\n## Added\n- b\n", "twice"),
        ("# T\n\n## Added\nloose text\n", "bullet"),
        ("# T\n\n- Entry: Bad_Id\n", "not a todo id"),
        ("# T\n\n- Bump: major\n", "minor or patch"),
        ("# T\n\n- Date: 2026-13-45\n", "month"),
    ],
)
def test_a_malformed_fragment_names_the_line(text, message):
    with pytest.raises((changelog.ChangelogError, ValueError), match=message):
        changelog.parse_fragment(text)


@pytest.mark.parametrize("bump, expected", [("patch", (0, 1, 8)), ("minor", (0, 2, 0))])
def test_a_minor_bump_resets_the_patch(bump, expected):
    assert changelog.bumped((0, 1, 7), bump) == expected


def test_propose_follows_the_breaking_section(tree, capsys):
    plain = tree.draft(draft_text(), "plain.md")
    breaking = tree.draft(draft_text(sections=BREAKING), "breaking.md")
    assert changelog.main(["propose", "--draft", str(plain)]) == 0
    assert capsys.readouterr().out.strip() == "patch"
    assert changelog.main(["propose", "--draft", str(breaking)]) == 0
    assert capsys.readouterr().out.strip() == "minor"


def test_the_first_add_records_the_header_version_and_the_next_ones_bump_it(tree):
    tree.with_baseline()
    assert tree.header() == (0, 1, 0)
    assert tree.add(draft_text("one")) == 0 and tree.header() == (0, 1, 1)
    assert tree.add(draft_text("two", sections=BREAKING + MIGRATION)) == 0 and tree.header() == (0, 2, 0)
    assert tree.add(draft_text("three")) == 0 and tree.header() == (0, 2, 1)
    text = tree.fragment_file("two").read_text(encoding="utf-8")
    assert "- Version: 0.2.0" in text and "- Bump: minor" in text and "- Date: 2026-11-15" in text
    assert tree.check() == []
    rendered = (tree.root / changelog.CHANGELOG_FILE).read_text(encoding="utf-8")
    assert rendered.index("## 0.2.1") < rendered.index("## 0.2.0") < rendered.index("## 0.1.1") < rendered.index("## 0.1.0")


def test_add_rewrites_the_macro_lines_only(tree):
    tree.with_baseline()
    path = tree.root / changelog.VERSION_HEADER
    path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
    expected = path.read_bytes().replace(b"PATCH 0", b"PATCH 1")
    assert tree.add(draft_text("one")) == 0
    assert path.read_bytes() == expected


def test_dry_run_writes_nothing(tree, capsys):
    tree.with_baseline()
    before = (tree.root / changelog.CHANGELOG_FILE).read_text(encoding="utf-8")
    code = changelog.main(["add", "--root", str(tree.root), "--draft", str(tree.draft(draft_text())), "--bump", "patch", "--dry-run"])
    assert code == 0 and "would write changelog/feature.md" in capsys.readouterr().out
    assert not tree.fragment_file("feature").exists() and tree.header() == (0, 1, 0)
    assert (tree.root / changelog.CHANGELOG_FILE).read_text(encoding="utf-8") == before


@pytest.mark.parametrize(
    "text, bump, message",
    [
        (draft_text(sections=BREAKING), "patch", "breaking raises MINOR"),
        (draft_text(sections="## Breaking\n- `Foo` is gone.\n"), "minor", "Adapt"),
        (draft_text(sections="## Breaking\n- `Foo` is gone. Adapt:\n"), "minor", "Adapt"),
        (draft_text(extra="- Date: 2026-01-01\n"), "patch", "Version"),
        (draft_text("baseline"), "patch", "already has a fragment"),
        ("# No entry\n\n## Added\n- a\n", "patch", "Entry"),
    ],
)
def test_add_refuses_and_changes_nothing(tree, capsys, text, bump, message):
    tree.with_baseline()
    before = (tree.header(), sorted(p.name for p in (tree.root / changelog.FRAGMENT_DIR).iterdir()))
    assert tree.add(text, bump) == changelog.EXIT_REFUSED
    assert message in capsys.readouterr().err
    assert before == (tree.header(), sorted(p.name for p in (tree.root / changelog.FRAGMENT_DIR).iterdir()))


def test_add_refuses_a_draft_that_forgot_the_content_version(tree, capsys):
    tree.with_baseline()
    (tree.root / changelog.CONTENT_VERSION_HEADER).write_text("inline constexpr uint32_t kContentVersion = 6;\n", encoding="utf-8")
    assert tree.add(draft_text()) == changelog.EXIT_REFUSED
    assert "kContentVersion is 6" in capsys.readouterr().err
    assert tree.add(draft_text(extra="- Content version: 6\n", sections=MIGRATION)) == 0
    assert tree.check() == []


def test_add_refuses_a_content_version_that_does_not_fit(tree, capsys):
    tree.with_baseline()
    assert tree.add(draft_text(extra="- Content version: 7\n")) == changelog.EXIT_REFUSED
    assert tree.add(draft_text(extra="- Content version: 5\n")) == changelog.EXIT_REFUSED
    assert "does not fit" in capsys.readouterr().err


def test_add_without_a_baseline_but_a_content_version_ahead_refuses(tree, capsys):
    assert tree.add(draft_text()) == changelog.EXIT_REFUSED
    assert "kContentVersion is 5" in capsys.readouterr().err


def test_render_check_notices_a_hand_edit(tree, capsys):
    tree.with_baseline()
    path = tree.root / changelog.CHANGELOG_FILE
    assert changelog.main(["render", "--check", "--root", str(tree.root)]) == 0
    path.write_text(path.read_text(encoding="utf-8") + "hand written\n", encoding="utf-8")
    assert changelog.main(["render", "--check", "--root", str(tree.root)]) == changelog.EXIT_REFUSED
    assert changelog.main(["render", "--root", str(tree.root)]) == 0
    assert changelog.main(["render", "--check", "--root", str(tree.root)]) == 0


def test_check_is_clean_on_a_good_tree(tree, capsys):
    tree.with_baseline()
    assert changelog.main(["check", "--root", str(tree.root)]) == 0
    assert "changelog ok: 0.1.0" in capsys.readouterr().out


def test_check_ignores_kcontentversion(tree):
    """A branch that bumped it has no fragment until todo-merge adds one after the merge."""
    tree.with_baseline()
    (tree.root / changelog.CONTENT_VERSION_HEADER).write_text("inline constexpr uint32_t kContentVersion = 9;\n", encoding="utf-8")
    assert tree.check() == []


def test_check_names_the_file_of_each_problem(tree, capsys):
    tree.with_baseline()
    assert tree.add(draft_text("one")) == 0
    directory = tree.root / changelog.FRAGMENT_DIR

    header = tree.root / changelog.VERSION_HEADER
    header.write_text(header.read_text(encoding="utf-8").replace("PATCH 1", "PATCH 0"), encoding="utf-8")
    assert any(p.startswith("src/Modules/BECore/Utils/EngineVersion.h") and "0.1.0" in p for p in tree.check())
    header.write_text(header.read_text(encoding="utf-8").replace("PATCH 0", "PATCH 1"), encoding="utf-8")
    assert tree.check() == []

    (directory / "broken.md").write_text("no heading\n", encoding="utf-8")
    assert any(p.startswith("changelog/broken.md") for p in tree.check())
    (directory / "broken.md").unlink()

    text = (directory / "one.md").read_text(encoding="utf-8")
    (directory / "twin.md").write_text(text.replace("Entry: one", "Entry: twin"), encoding="utf-8")
    assert any(p.startswith("changelog/twin.md") and "also the version" in p for p in tree.check())
    (directory / "twin.md").unlink()

    (directory / "wrong-name.md").write_text(text.replace("Entry: one", "Entry: other").replace("0.1.1", "0.1.2"), encoding="utf-8")
    assert any(p.startswith("changelog/wrong-name.md") and "other.md" in p for p in tree.check())
    (directory / "wrong-name.md").unlink()

    (directory / "one.md").write_text(text.replace("Bump: patch", "Bump: patch").replace("## Added", "## Breaking\n- gone.\n\n## Added"), encoding="utf-8")
    problems = tree.check()
    assert any(p.startswith("changelog/one.md") and "breaking raises MINOR" in p for p in problems)
    assert any(p.startswith("changelog/one.md") and "Adapt" in p for p in problems)
    (directory / "one.md").write_text(text, encoding="utf-8")

    (tree.root / changelog.CHANGELOG_FILE).unlink()
    assert tree.check()[0].startswith("CHANGELOG.md")
    assert changelog.main(["check", "--root", str(tree.root)]) == changelog.EXIT_REFUSED
    assert "CHANGELOG.md" in capsys.readouterr().err


def test_check_on_an_empty_tree_says_so(tree):
    assert any("no fragment" in p for p in tree.check())


def test_a_missing_header_is_an_error_of_the_layout(tmp_path, capsys):
    assert changelog.main(["current", "--root", str(tmp_path)]) == changelog.EXIT_ERROR
    assert "EngineVersion.h" in capsys.readouterr().err


def test_current_prints_the_header_version(tree, capsys):
    assert changelog.main(["current", "--root", str(tree.root)]) == 0
    assert capsys.readouterr().out.strip() == "0.1.0"


def test_the_baseline_draft_is_valid():
    assert changelog.parse_fragment(BASELINE_DRAFT).content_version == 5
