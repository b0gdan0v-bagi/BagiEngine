"""todo_markdown: the old entry files, checklists, summaries and the markdown <-> HTML of a task description."""

import pytest

from conftest import FIXTURES
from todo_markdown import (
    ParseError,
    checklist_progress,
    extract_summary,
    html_to_markdown,
    markdown_to_html,
    parse_entry_text,
    parse_stages,
    read_checklist,
    slugify,
)


def test_an_old_entry_file_becomes_a_record():
    record = parse_entry_text((FIXTURES / "todo" / "in-review.md").read_text(), "in-review.md")
    assert record["id"] == "in-review" and record["title"] == "Entry waiting for review"
    assert record["status"] == "review" and record["statusDate"] == "2026-11-05"
    assert record["branch"] == "todo/in-review" and record["statusNote"] == "done and checked with the tests."
    assert record["stage"] == 3 and not record["body"].startswith("#")


def test_an_entry_without_front_matter_is_open():
    record = parse_entry_text("# Title\n\n**What is missing:** x\n", "bare.md")
    assert (record["status"], record["statusDate"], record["branch"], record["stage"], record["plan"]) == ("open", None, None, None, None)


def test_a_renamed_file_keeps_its_id():
    record = parse_entry_text((FIXTURES / "todo" / "renamed-file.md").read_text(), "renamed-file.md")
    assert record["id"] != "renamed-file"


@pytest.mark.parametrize(
    "text",
    [
        "---\nstatus: done 2026-01-01\n---\n# T\n",
        "---\nwho: me\n---\n# T\n",
        "---\nstatus: open\n# T\n",
        "no title\n",
        "---\nplan: dir/p.md\n---\n# T\n",
        "---\nstatus: open 2026-02-30\n---\n# T\n",
    ],
)
def test_a_broken_entry_file_is_an_error(text):
    with pytest.raises(ParseError):
        parse_entry_text(text, "broken.md")


def test_stages_come_from_the_numbered_list_of_the_roadmap():
    stages = parse_stages((FIXTURES / "todo" / "roadmap.md").read_text())
    assert stages and stages[0]["number"] == 1


def test_the_checklist_skips_code_fences():
    checklist = read_checklist("# Plan\n## A\n- [x] one\n```\n- [ ] no\n```\n- [ ] two\n", "p.md")
    assert checklist["title"] == "Plan" and checklist_progress(checklist) == {"done": 1, "total": 2}
    assert read_checklist("no boxes", "p.md") is None


def test_the_summary_is_the_what_is_missing_paragraph():
    assert extract_summary("**Where:** x\n\n**What is missing:** a `build` on **every** push.\n") == "A build on every push."
    assert extract_summary("**What is wrong** (found by reading):\n\n1. the first point\n") == "The first point"
    assert extract_summary("nothing") == ""


def test_slugify():
    assert slugify("Show the Clock!") == "show-the-clock"


BODY = """**Where:** `src/` and [the plan](plan.md).

**What is missing:** *two* things:

1. the first
2. the second
   - nested one
   - [x] a box

## A heading

```
code <kept>
```

---

Last paragraph with **bold**.
"""


def test_markdown_survives_the_round_trip_through_html():
    html = markdown_to_html(BODY)
    assert "<code>src/</code>" in html and '<a href="plan.md">the plan</a>' in html and "&lt;kept&gt;" in html
    assert 'data-type="taskItem"' in html and "<hr>" in html
    back = html_to_markdown(html)
    assert back == html_to_markdown(markdown_to_html(back))  # stable after one pass
    assert " ".join(back.split()) == " ".join(BODY.split())


def test_a_numbered_list_keeps_its_first_number():
    assert html_to_markdown(markdown_to_html("6. six\n7. seven\n")) == "6. six\n7. seven"


def test_html_from_the_vikunja_editor_becomes_markdown():
    html = "<p>One <strong>bold</strong> line</p><ul><li><p>a</p></li><li><p>b</p></li></ul><p>end</p>"
    assert html_to_markdown(html) == "One **bold** line\n\n- a\n- b\n\nend"
    assert html_to_markdown("plain text") == "plain text"


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("deferred 2026-10-31 free text", ("deferred", "2026-10-31", "free text")),
        ("deferred, 2026-11-01. Revisit later", ("deferred", "2026-11-01", "Revisit later")),
        ("in progress since 2026-11-01. Stage 2", ("in progress", "2026-11-01", "Stage 2")),
        ("deferred by the owner on 2026-10-31. Skip it", ("deferred", "2026-10-31", "by the owner on 2026-10-31. Skip it")),
        ("open", ("open", None, "")),
    ],
)
def test_the_status_date_is_found_anywhere_and_not_repeated_in_the_note(text, expected):
    from todo_markdown import parse_status

    assert parse_status(text, "x.md") == expected
