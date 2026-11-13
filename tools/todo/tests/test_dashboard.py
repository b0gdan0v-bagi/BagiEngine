"""The pages of the dashboard: the render of dashboard.py without a database."""

from datetime import date

from service import dashboard

TODAY = date(2026, 11, 13)
STAGES = [{"number": 1, "title": "First"}]


def entry(entry_id: str, priority: str | None = None, **fields) -> dict:
    return {
        "id": entry_id, "area": "engine", "title": entry_id.upper(), "status": "open", "statusLine": "open", "statusDate": None,
        "stage": 1, "plan": None, "planShown": None, "planProgress": None, "summary": "", "version": 1, "priority": priority,
        "blockedBy": [], "blocks": [], **fields,
    }


def page(entries: list[dict], **data) -> str:
    areas = [{"id": "engine", "title": "Engine"}]
    return dashboard.render_dashboard({"areas": areas, "area": "engine", "stages": STAGES, "entries": entries, **data}, TODAY)


def test_a_badge_per_level_and_none_for_unset():
    html = page([entry("a", "now"), entry("b", "high"), entry("c", "normal"), entry("d", "low"), entry("e")])
    for level in ("now", "high", "normal", "low"):
        assert f'<span class="badge p-{level}" title="priority">{level}</span>' in html
    assert html.count('title="priority"') == 4


def test_a_list_is_ordered_by_priority_then_by_the_given_order():
    html = page([entry("a", "low"), entry("b"), entry("c", "now"), entry("d", "normal"), entry("e", "high")])
    ids = [html.index(f">{name}</a>") for name in ("C", "E", "B", "D", "A")]
    assert ids == sorted(ids)


def test_the_filter_links_keep_the_area_and_the_note_names_the_filter():
    html = page([entry("a", "now")], priority="now,high", hidden=3)
    assert '<a href="?area=engine">all</a>' in html and '<a href="?area=engine&amp;priority=none">unset</a>' in html
    assert "Filter: priority now, high; 3 entries hidden." in html
    assert '<a href="?area=engine&amp;priority=now">now</a>' in html
    assert "Filter:" not in page([entry("a")])
    assert "<strong>now</strong>" in page([entry("a", "now")], priority="now", hidden=1)


def test_the_entry_page_names_the_priority():
    assert "<strong>Priority:</strong> high" in dashboard.render_entry(entry("a", "high", body=""), [], [])
    assert "Priority:" not in dashboard.render_entry(entry("a", body=""), [], [])
