from pathlib import Path

import pytest

from conftest import REPO_ROOT
from todo_parser import STATUSES, ParseError, parse_todo, slugify


@pytest.fixture
def parsed(sample_todo, sample_plans):
    return parse_todo(sample_todo, sample_plans)


def by_id(parsed, entry_id):
    return next(entry for entry in parsed["entries"] if entry["id"] == entry_id)


def write_entry(folder: Path, name: str, text: str) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / name
    path.write_text(text, encoding="utf-8")
    return path


def test_roadmap_and_readme_are_not_entries_and_the_roadmap_gives_the_stages(parsed):
    assert parsed["stages"] == [
        {"number": 1, "title": "Hygiene"},
        {"number": 2, "title": "Control API (in progress)"},
        {"number": 3, "title": "Minimum for a 2D game, in this order"},
    ]
    ids = [entry["id"] for entry in parsed["entries"]]
    assert "roadmap" not in ids
    assert "README" not in ids


def test_entry_count_and_unique_ids(parsed):
    ids = [entry["id"] for entry in parsed["entries"]]
    assert len(ids) == 6  # the heading inside the code fence is not a title
    assert len(set(ids)) == len(ids)


def test_the_id_is_the_file_name_and_the_title_is_the_first_heading(parsed):
    entry = by_id(parsed, "bare-entry")
    assert entry["title"] == "Bare entry <b>& co</b>"
    assert entry["file"] == "bare-entry.md"


def test_id_field_overrides_the_file_name(parsed):
    entry = by_id(parsed, "the-old-id")
    assert entry["file"] == "renamed-file.md"
    assert "renamed-file" not in [item["id"] for item in parsed["entries"]]


def test_entry_without_front_matter_is_open_with_no_stage_or_plan(parsed):
    entry = by_id(parsed, "bare-entry")
    assert entry["status"] == "open"
    assert entry["date"] is None
    assert entry["stage"] is None
    assert entry["plan"] is None
    assert entry["planProgress"] is None


def test_stage_only(parsed):
    entry = by_id(parsed, "open-with-stage")
    assert (entry["status"], entry["stage"]) == ("open", 3)


def test_deferred_status_and_date(parsed):
    entry = by_id(parsed, "deferred")
    assert (entry["status"], entry["date"], entry["stage"]) == ("deferred", "2026-10-31", 1)


def test_in_progress_with_plan_progress(parsed):
    entry = by_id(parsed, "in-progress-with-plan")
    assert entry["status"] == "in progress"
    assert entry["date"] == "2026-11-01"
    assert entry["plan"] == "sample-plan.md"
    assert entry["planProgress"] == {"done": 2, "total": 4}


def test_unknown_stage_number_is_kept(parsed):
    assert by_id(parsed, "unknown-stage")["stage"] == 9


def test_summary_is_plain_text_from_the_what_is_label(parsed):
    assert by_id(parsed, "open-with-stage")["summary"] == "The first problem: x is bold and links here."
    assert by_id(parsed, "deferred")["summary"].startswith("Nothing is built yet")
    assert by_id(parsed, "in-progress-with-plan")["summary"] == "A transport."
    assert by_id(parsed, "bare-entry")["summary"] == ""


def test_entries_are_sorted_by_file_name(parsed):
    files = [entry["file"] for entry in parsed["entries"]]
    assert files == sorted(files)


def test_missing_plan_file_is_a_note_and_keeps_the_name(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nplan: nope.md\n---\n# An entry\n")
    result = parse_todo(todo, tmp_path / "plans")
    entry = result["entries"][0]
    assert entry["plan"] == "nope.md"
    assert entry["planFound"] is False
    assert entry["planProgress"] is None
    assert result["warnings"] == []
    assert any("nope.md" in note for note in result["notes"])


def test_a_found_plan_gives_the_checklist(parsed):
    entry = by_id(parsed, "in-progress-with-plan")
    assert entry["planFound"] is True
    assert entry["planChecklist"] == {
        "title": "Sample plan",
        "sections": [
            {
                "heading": "Sample plan",
                "items": [
                    {"done": True, "text": "one"},
                    {"done": True, "text": "two"},
                    {"done": False, "text": "three"},
                    {"done": False, "text": "nested four"},
                ],
            }
        ],
    }


def test_a_complete_plan_warns_that_it_can_be_deleted(tmp_path):
    plans = tmp_path / "plans"
    write_entry(plans, "done.md", "# Done\n\n- [x] a\n- [X] b\n")
    write_entry(plans, "half.md", "# Half\n\n- [x] a\n- [ ] b\n")
    todo = tmp_path / "todo"
    write_entry(todo, "one.md", "---\nplan: done.md\n---\n# One\n")
    write_entry(todo, "two.md", "---\nplan: half.md\n---\n# Two\n")
    result = parse_todo(todo, plans)
    assert len(result["warnings"]) == 1
    assert "done.md" in result["warnings"][0] and "complete (2/2)" in result["warnings"][0]


def test_checkboxes_in_a_code_fence_are_not_counted(tmp_path):
    plans = tmp_path / "plans"
    write_entry(plans, "p.md", "# P\n\n- [x] real\n\n```\n- [ ] example\n```\n")
    todo = tmp_path / "todo"
    write_entry(todo, "one.md", "---\nplan: p.md\n---\n# One\n")
    assert parse_todo(todo, plans)["entries"][0]["planProgress"] == {"done": 1, "total": 1}


def test_plan_without_checkboxes_has_no_progress(tmp_path):
    plans = tmp_path / "plans"
    write_entry(plans, "prose.md", "# Prose only\n")
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nplan: prose.md\n---\n# An entry\n")
    entry = parse_todo(todo, plans)["entries"][0]
    assert entry["plan"] == "prose.md"
    assert entry["planProgress"] is None


@pytest.mark.parametrize("plan", [".claude/plans/x.md", "../x.md", "x.txt", "two words.md"])
def test_plan_must_be_a_bare_file_name(tmp_path, plan):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", f"---\nplan: {plan}\n---\n# An entry\n")
    with pytest.raises(ParseError, match="bare file name"):
        parse_todo(todo)


def test_non_numeric_stage_warns_and_is_ignored(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nstage: soon\n---\n# An entry\n")
    result = parse_todo(todo)
    assert result["entries"][0]["stage"] is None
    assert any("stage" in warning for warning in result["warnings"])


def test_bad_status_keyword_is_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nstatus: blocked since 2026-11-01\n---\n# An entry\n")
    with pytest.raises(ParseError, match="status"):
        parse_todo(todo)


def test_impossible_date_is_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nstatus: deferred since 2026-13-45\n---\n# An entry\n")
    with pytest.raises(ParseError, match="not a date"):
        parse_todo(todo)


def test_unknown_front_matter_key_is_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nowner: me\n---\n# An entry\n")
    with pytest.raises(ParseError, match="unknown front matter key 'owner'"):
        parse_todo(todo)


def test_repeated_front_matter_key_is_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nstage: 1\nstage: 2\n---\n# An entry\n")
    with pytest.raises(ParseError, match="repeated"):
        parse_todo(todo)


def test_unclosed_front_matter_is_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nstage: 1\n")
    with pytest.raises(ParseError, match="not closed"):
        parse_todo(todo)


def test_missing_title_is_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "an-entry.md", "---\nstage: 1\n---\n\nJust text.\n")
    with pytest.raises(ParseError, match="# Title"):
        parse_todo(todo)


def test_duplicate_ids_are_an_error(tmp_path):
    todo = tmp_path / "todo"
    write_entry(todo, "one.md", "# One\n")
    write_entry(todo, "two.md", "---\nid: one\n---\n# Two\n")
    with pytest.raises(ParseError, match="same id"):
        parse_todo(todo)


def test_a_missing_folder_is_an_error(tmp_path):
    with pytest.raises(ParseError, match="not a folder"):
        parse_todo(tmp_path / "missing")


def test_slug_is_cut_on_a_word_boundary():
    title = "Config Editor: the sink `type` dropdown offers values the logger factory cannot resolve"
    slug = slugify(title)
    assert slug == "config-editor-the-sink-type-dropdown-offers-values-the"
    assert len(slug) <= 60
    assert slugify("Short title") == "short-title"


def test_real_todo_folder_parses_with_known_statuses():
    result = parse_todo(REPO_ROOT / "todo")
    assert result["entries"], "todo/ has no entries"
    stage_numbers = {stage["number"] for stage in result["stages"]}
    assert stage_numbers, "the roadmap list was not found"
    for entry in result["entries"]:
        assert entry["id"]
        assert entry["status"] in STATUSES
        if entry["stage"] is not None:
            assert entry["stage"] in stage_numbers, entry["title"]
    assert not [warning for warning in result["warnings"] if "stage" in warning]
