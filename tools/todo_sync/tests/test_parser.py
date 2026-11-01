from pathlib import Path

import pytest

from conftest import REPO_ROOT
from todo_parser import STATUSES, ParseError, parse_todo, slugify


@pytest.fixture
def parsed(sample_todo, sample_root):
    return parse_todo(sample_todo, sample_root)


def by_id(parsed, entry_id):
    return next(entry for entry in parsed["entries"] if entry["id"] == entry_id)


def test_roadmap_is_not_an_entry_and_gives_the_stages(parsed):
    assert parsed["stages"] == [
        {"number": 1, "title": "Hygiene"},
        {"number": 2, "title": "Control API (in progress)"},
        {"number": 3, "title": "Minimum for a 2D game, in this order"},
    ]
    assert "roadmap" not in [entry["id"] for entry in parsed["entries"]]


def test_entry_count_and_unique_ids(parsed):
    ids = [entry["id"] for entry in parsed["entries"]]
    assert len(ids) == 6  # the heading inside the code fence is not counted
    assert len(set(ids)) == len(ids)


def test_entry_without_fields_is_open_with_no_stage_or_plan(parsed):
    entry = by_id(parsed, "bare-entry-b-co-b")
    assert entry["status"] == "open"
    assert entry["date"] is None
    assert entry["stage"] is None
    assert entry["plan"] is None
    assert entry["planProgress"] is None


def test_stage_only(parsed):
    entry = by_id(parsed, "open-entry-with-a-stage")
    assert (entry["status"], entry["stage"]) == ("open", 3)


def test_deferred_status_and_date(parsed):
    entry = by_id(parsed, "deferred-entry")
    assert (entry["status"], entry["date"], entry["stage"]) == ("deferred", "2026-10-31", 1)


def test_in_progress_with_plan_progress(parsed):
    entry = by_id(parsed, "in-progress-with-a-plan")
    assert entry["status"] == "in progress"
    assert entry["date"] == "2026-11-01"
    assert entry["plan"] == ".claude/plans/sample-plan.md"
    assert entry["planProgress"] == {"done": 2, "total": 4}


def test_legacy_prose_status_gives_keyword_date_and_plan(parsed):
    entry = by_id(parsed, "legacy-prose-status")
    assert (entry["status"], entry["date"]) == ("in progress", "2026-10-20")
    assert entry["plan"] == ".claude/plans/sample-plan.md"


def test_unknown_stage_number_is_kept(parsed):
    assert by_id(parsed, "unknown-stage-entry")["stage"] == 9


def test_summary_is_plain_text_from_the_what_is_label(parsed):
    assert (
        by_id(parsed, "open-entry-with-a-stage")["summary"]
        == "The first problem: x is bold and links here."
    )
    assert by_id(parsed, "deferred-entry")["summary"].startswith("Nothing is built yet")
    assert by_id(parsed, "in-progress-with-a-plan")["summary"] == "A transport."
    assert by_id(parsed, "bare-entry-b-co-b")["summary"] == ""


def test_line_is_the_heading_line(parsed, sample_todo):
    lines = sample_todo.read_text(encoding="utf-8").splitlines()
    for entry in parsed["entries"]:
        assert lines[entry["line"] - 1] == f"## {entry['title']}"


def test_missing_plan_file_warns_and_keeps_the_path(tmp_path):
    todo = tmp_path / "TODO.md"
    todo.write_text("## An entry\n\n**Plan:** `.claude/plans/nope.md`\n", encoding="utf-8")
    result = parse_todo(todo)
    assert result["entries"][0]["plan"] == ".claude/plans/nope.md"
    assert result["entries"][0]["planProgress"] is None
    assert any("nope.md" in warning for warning in result["warnings"])


def test_plan_without_checkboxes_has_no_progress(tmp_path):
    (tmp_path / ".claude" / "plans").mkdir(parents=True)
    (tmp_path / ".claude" / "plans" / "prose.md").write_text("# Prose only\n", encoding="utf-8")
    todo = tmp_path / "TODO.md"
    todo.write_text("## An entry\n\n**Plan:** `.claude/plans/prose.md`\n", encoding="utf-8")
    entry = parse_todo(todo)["entries"][0]
    assert entry["plan"] == ".claude/plans/prose.md"
    assert entry["planProgress"] is None


def test_non_numeric_stage_warns_and_is_ignored(tmp_path):
    todo = tmp_path / "TODO.md"
    todo.write_text("## An entry\n\n**Stage:** soon\n", encoding="utf-8")
    result = parse_todo(todo)
    assert result["entries"][0]["stage"] is None
    assert any("Stage" in warning for warning in result["warnings"])


def test_bad_status_keyword_is_an_error(tmp_path):
    todo = tmp_path / "TODO.md"
    todo.write_text("## An entry\n\n**Status:** blocked since 2026-11-01\n", encoding="utf-8")
    with pytest.raises(ParseError, match="Status"):
        parse_todo(todo)


def test_impossible_date_is_an_error(tmp_path):
    todo = tmp_path / "TODO.md"
    todo.write_text("## An entry\n\n**Status:** deferred since 2026-13-45\n", encoding="utf-8")
    with pytest.raises(ParseError, match="not a date"):
        parse_todo(todo)


def test_duplicate_ids_are_an_error(tmp_path):
    todo = tmp_path / "TODO.md"
    todo.write_text("## Same title\n\n## Same: title!\n", encoding="utf-8")
    with pytest.raises(ParseError, match="same id"):
        parse_todo(todo)


def test_unreadable_file_is_an_error(tmp_path):
    with pytest.raises(ParseError, match="cannot read"):
        parse_todo(tmp_path / "missing.md")


def test_slug_is_cut_on_a_word_boundary():
    title = "Config Editor: the sink `type` dropdown offers values the logger factory cannot resolve"
    slug = slugify(title)
    assert slug == "config-editor-the-sink-type-dropdown-offers-values-the"
    assert len(slug) <= 60
    assert slugify("Short title") == "short-title"


def test_real_todo_md_parses_with_known_statuses():
    result = parse_todo(REPO_ROOT / "TODO.md", REPO_ROOT)
    assert result["entries"], "TODO.md has no entries"
    stage_numbers = {stage["number"] for stage in result["stages"]}
    assert stage_numbers, "the roadmap list was not found"
    for entry in result["entries"]:
        assert entry["id"]
        assert entry["status"] in STATUSES
        if entry["stage"] is not None:
            assert entry["stage"] in stage_numbers, entry["title"]
        if entry["plan"]:
            assert Path(REPO_ROOT / entry["plan"]).is_file(), entry["title"]
