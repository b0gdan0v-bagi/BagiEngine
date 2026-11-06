#!/usr/bin/env python3
"""
The pages of the service for a browser: the dashboard of an area, one entry, one plan.

Inline CSS, no scripts, no network. Light and dark follow prefers-color-scheme.
Every piece of text from the store goes through html.escape (markdown through markdown_to_html, which escapes).
"""

from datetime import date
from html import escape
from urllib.parse import quote

from todo_markdown import markdown_to_html

STATUS_LABELS = {"open": "open", "in progress": "in progress", "review": "review", "deferred": "deferred"}
ALL_STATUS_LABELS = {**STATUS_LABELS, "closed": "closed"}
NO_STAGE_TITLE = "No stage"

_STYLE = """
:root {
  --bg: #f7f7f5; --panel: #ffffff; --text: #1f2328; --muted: #656d76; --line: #d8dee4;
  --open: #656d76; --progress: #1a7f37; --review: #0969da; --deferred: #9a6700; --bar: #d8dee4;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #0e1116; --panel: #161b22; --text: #e6edf3; --muted: #8d96a0; --line: #30363d;
    --open: #8d96a0; --progress: #3fb950; --review: #58a6ff; --deferred: #d29922; --bar: #30363d;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text);
  font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 960px; margin: 0 auto; padding: 24px 16px 48px; }
h1 { font-size: 24px; margin: 0 0 4px; }
h2 { font-size: 18px; margin: 32px 0 12px; }
.muted { color: var(--muted); }
.totals { display: flex; flex-wrap: wrap; gap: 8px 24px; margin: 12px 0 0; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 8px;
  padding: 12px 16px; margin: 0 0 12px; }
.panel h3 { font-size: 16px; margin: 0 0 2px; }
.counts { color: var(--muted); font-size: 13px; margin: 0 0 8px; }
ul { list-style: none; margin: 0; padding: 0; }
li { display: flex; flex-wrap: wrap; align-items: baseline; gap: 4px 10px; padding: 4px 0;
  border-top: 1px solid var(--line); }
li:first-child { border-top: 0; }
.badge { font-size: 12px; border: 1px solid currentColor; border-radius: 10px; padding: 0 8px; }
.s-open { color: var(--open); } .s-in-progress { color: var(--progress); }
.s-review { color: var(--review); } .s-deferred { color: var(--deferred); }
.bar { width: 160px; height: 8px; background: var(--bar); border-radius: 4px; overflow: hidden;
  align-self: center; }
.bar span { display: block; height: 100%; background: var(--progress); }
.title { flex: 1 1 320px; }
.meta { color: var(--muted); font-size: 13px; }
.summary { flex-basis: 100%; color: var(--muted); font-size: 13px; }
a { color: var(--review); }
nav { display: flex; gap: 16px; margin: 0 0 16px; }
.doc { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 8px 20px; overflow-x: auto; }
.doc pre { overflow-x: auto; }
table.events { border-collapse: collapse; font-size: 13px; width: 100%; }
table.events td { border-top: 1px solid var(--line); padding: 4px 8px 4px 0; vertical-align: top; }
"""


def _badge(status: str) -> str:
    css = "s-" + status.replace(" ", "-")
    return f'<span class="badge {css}">{escape(ALL_STATUS_LABELS.get(status, status))}</span>'


def _age(entry: dict, today: date) -> str:
    if not entry["statusDate"]:
        return "no date"
    days = (today - date.fromisoformat(entry["statusDate"])).days
    return f"since {entry['statusDate']}, {days} day{'' if days == 1 else 's'}"


def _progress(entry: dict) -> str:
    progress = entry["planProgress"]
    if not progress:
        if not entry["plan"]:
            return ""
        text = "no checklist" if entry["planShown"] else "plan not in the store"
        return f'<span class="meta">{text}</span>'
    percent = round(100 * progress["done"] / progress["total"])
    return (
        f'<span class="bar" title="{percent}%"><span style="width: {percent}%"></span></span>'
        f'<span class="meta">{progress["done"]} / {progress["total"]}</span>'
    )


def _entry_row(entry: dict, today: date, with_age: bool = False, with_summary: bool = False) -> str:
    link = f'<a href="entry/{quote(entry["id"])}">{escape(entry["title"])}</a>'
    parts = [f'<span class="title">{link}</span>', _badge(entry["status"])]
    if with_age:
        parts.append(f'<span class="meta">{escape(_age(entry, today))}</span>')
    parts.append(_progress(entry))
    if with_summary and entry["summary"]:
        parts.append(f'<span class="summary">{escape(entry["summary"])}</span>')
    return "<li>" + "".join(part for part in parts if part) + "</li>"


def _counts(entries: list[dict]) -> str:
    return " · ".join(
        f"{sum(1 for entry in entries if entry['status'] == status)} {label}"
        for status, label in STATUS_LABELS.items()
    )


def _panel(title: str, entries: list[dict], rows: str) -> str:
    return (
        f'<div class="panel"><h3>{escape(title)}</h3>'
        f'<p class="counts">{escape(_counts(entries))}</p><ul>{rows}</ul></div>'
    )


def _page(title: str, body: str) -> str:
    return (
        "<!doctype html>\n"
        '<html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        f"<title>{escape(title)}</title>"
        f"<style>{_STYLE}</style></head><body><main>{body}</main></body></html>\n"
    )


def _nav(areas: list[dict], current: str | None) -> str:
    links = [
        f"<strong>{escape(area['title'])}</strong>" if area["id"] == current else f'<a href="?area={quote(area["id"])}">{escape(area["title"])}</a>'
        for area in areas
    ]
    return f"<nav>{''.join(links)}</nav>"


def render_dashboard(data: dict, today: date) -> str:
    """data: {areas, area, stages, entries (to_json without the body, closed ones left out), warnings}."""
    entries = data["entries"]
    stage_numbers = {stage["number"] for stage in data["stages"]}

    stage_panels = []
    for stage in data["stages"]:
        stage_entries = [entry for entry in entries if entry["stage"] == stage["number"]]
        rows = "".join(_entry_row(entry, today) for entry in stage_entries)
        title = f"Stage {stage['number']}: {stage['title']}"
        stage_panels.append(_panel(title, stage_entries, rows or '<li class="meta">no entries</li>'))

    in_review = [entry for entry in entries if entry["status"] == "review"]
    in_progress = [entry for entry in entries if entry["status"] == "in progress"]
    deferred = [entry for entry in entries if entry["status"] == "deferred"]
    no_stage = [entry for entry in entries if entry["stage"] not in stage_numbers]

    def section(heading: str, body: str) -> str:
        return f"<h2>{escape(heading)}</h2>{body}"

    in_review_rows = "".join(_entry_row(entry, today, with_age=True, with_summary=True) for entry in in_review)
    in_progress_rows = "".join(_entry_row(entry, today, with_age=True, with_summary=True) for entry in in_progress)
    deferred_rows = "".join(_entry_row(entry, today, with_age=True, with_summary=True) for entry in deferred)
    no_stage_rows = "".join(_entry_row(entry, today) for entry in no_stage)

    totals = "".join(
        f'<span>{_badge(status)} {sum(1 for entry in entries if entry["status"] == status)}</span>'
        for status in STATUS_LABELS
    )
    warnings = "".join(f"<li class='meta'>{escape(warning)}</li>" for warning in data.get("warnings", []))

    body = [
        _nav(data.get("areas", []), data.get("area")),
        "<h1>TODO dashboard</h1>",
        f'<div class="muted">{today.isoformat()}, from the todo store. {len(entries)} open entries.</div>',
        f'<div class="totals">{totals}</div>',
        section("Roadmap", "".join(stage_panels)),
        section("In review", f"<ul class='panel'>{in_review_rows}</ul>" if in_review else '<p class="muted">Nothing waits for review.</p>'),
        section("In progress", f"<ul class='panel'>{in_progress_rows}</ul>" if in_progress else '<p class="muted">Nothing in progress.</p>'),
        section("Deferred", f"<ul class='panel'>{deferred_rows}</ul>" if deferred else '<p class="muted">Nothing deferred.</p>'),
        section(NO_STAGE_TITLE, f"<ul class='panel'>{no_stage_rows}</ul>" if no_stage else '<p class="muted">Every entry has a stage.</p>'),
    ]
    if warnings:
        body.append(section("Warnings", f"<ul class='panel'>{warnings}</ul>"))

    return _page("TODO dashboard", "".join(body))


def render_entry(entry: dict, plans: list[dict], events: list[dict]) -> str:
    """One entry: its facts, its body, its plans and its history."""
    facts = [f"<strong>Status:</strong> {escape(entry['statusLine'])}", f"<strong>Area:</strong> {escape(entry['area'])}"]
    if entry["stage"] is not None:
        facts.append(f"<strong>Stage:</strong> {entry['stage']}")
    plan_rows = "".join(
        f'<li><a href="../plan/{quote(plan["name"])}">{escape(plan["name"])}</a> <span class="meta">{escape(plan["kind"])}, '
        f'{escape(plan["updatedAt"][:16])}</span></li>'
        for plan in plans
    )
    event_rows = "".join(
        f"<tr><td>{escape(event['at'][:19])}</td><td>{escape(event['actor'])}</td><td>{escape(event['kind'])}</td>"
        f"<td>{escape(str(event['data'] or ''))[:300]}</td></tr>"
        for event in events
    )
    body = [
        '<nav><a href="..">Dashboard</a></nav>',
        f"<h1>{escape(entry['title'])}</h1>",
        f'<p class="muted">{escape(entry["id"])} · version {entry["version"]}</p>',
        "".join(f"<p>{fact}</p>" for fact in facts),
        f'<div class="doc">{markdown_to_html(entry["body"])}</div>',
        f"<h2>Plans</h2><ul class='panel'>{plan_rows}</ul>" if plans else "",
        f"<h2>History</h2><table class='events'>{event_rows}</table>" if events else "",
    ]
    return _page(entry["title"], "".join(body))


def render_plan(plan: dict) -> str:
    progress = plan["progress"]
    done = f"{progress['done']} / {progress['total']}" if progress else "no checklist"
    entry_link = f' · <a href="../entry/{quote(plan["entry"])}">{escape(plan["entry"])}</a>' if plan["entry"] else ""
    body = [
        '<nav><a href="..">Dashboard</a></nav>',
        f'<p class="muted">{escape(plan["name"])} · {escape(plan["kind"])} · {done}{entry_link}</p>',
        f'<div class="doc">{markdown_to_html(plan["content"])}</div>',
    ]
    return _page(plan["name"], "".join(body))
