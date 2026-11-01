from datetime import date

import pytest

from dashboard import render_dashboard
from todo_parser import parse_todo

TODAY = date(2026, 11, 11)


@pytest.fixture
def page(sample_todo, sample_root):
    return render_dashboard(parse_todo(sample_todo, sample_root), TODAY)


def test_is_a_self_contained_page(page):
    assert page.startswith("<!doctype html>")
    assert "<script" not in page
    assert "http://" not in page.replace("http://example.com", "")  # no external resources
    assert "prefers-color-scheme: dark" in page


def test_roadmap_stages_with_counts_by_status(page):
    assert "Stage 1: Hygiene" in page
    assert "Stage 2: Control API (in progress)" in page
    # stage 1 holds the deferred entry only
    assert "0 open · 0 in progress · 1 deferred" in page
    # stage 2 holds the in-progress entry only
    assert "0 open · 1 in progress · 0 deferred" in page


def test_in_progress_entry_shows_plan_progress(page):
    assert "2 / 4" in page
    assert 'style="width: 50%"' in page


def test_deferred_entry_shows_its_age(page):
    # 2026-10-31 to 2026-11-11
    assert "since 2026-10-31, 11 days" in page


def test_entry_without_a_date_says_so(sample_todo, sample_root):
    data = parse_todo(sample_todo, sample_root)
    data["entries"][1]["status"] = "deferred"
    data["entries"][1]["date"] = None
    assert "no date" in render_dashboard(data, TODAY)


def test_entries_without_a_known_stage_are_listed_apart(page):
    section = page.split("<h2>No stage</h2>")[1]
    assert "Bare entry" in section
    assert "Unknown stage entry" in section  # stage 9 is not on the roadmap
    assert "Open entry with a stage" not in section


def test_in_progress_entry_with_a_plan_without_checkboxes(sample_todo, sample_root):
    data = parse_todo(sample_todo, sample_root)
    for entry in data["entries"]:
        entry["planProgress"] = None
    assert "no checklist" in render_dashboard(data, TODAY)


def test_text_from_todo_md_is_escaped(page):
    assert "<b>& co</b>" not in page
    assert "&lt;b&gt;&amp; co&lt;/b&gt;" in page
