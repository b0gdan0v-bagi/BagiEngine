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
# The order of the lists: the owner's levels, unset counting as normal (the same as PRIORITY_RANK of todo.py).
PRIORITY_RANK = {"now": 0, "high": 1, "normal": 2, None: 2, "low": 3}
FILTER_LINKS = (("all", None), ("now", "now"), ("high", "high"), ("normal", "normal"), ("low", "low"), ("unset", "none"))

_STYLE = """
:root {
  --bg: #f7f7f5; --panel: #ffffff; --text: #1f2328; --muted: #656d76; --line: #d8dee4;
  --open: #656d76; --progress: #1a7f37; --review: #0969da; --deferred: #9a6700; --bar: #d8dee4;
  --p-now: #cf222e; --p-high: #bc4c00; --p-normal: #8c959f; --p-low: #6e7781;
  --area-0: #eef4fb; --area-1: #f6f0e4; --area-2: #eef6ee; --area-3: #f4eef6;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #0e1116; --panel: #161b22; --text: #e6edf3; --muted: #8d96a0; --line: #30363d;
    --open: #8d96a0; --progress: #3fb950; --review: #58a6ff; --deferred: #d29922; --bar: #30363d;
    --p-now: #ff7b72; --p-high: #ffa657; --p-normal: #6e7681; --p-low: #8d96a0;
    --area-0: #15202e; --area-1: #261f12; --area-2: #13241a; --area-3: #221a28;
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
.p-now { color: var(--p-now); font-weight: 600; } .p-high { color: var(--p-high); }
.p-normal { color: var(--p-normal); } .p-low { color: var(--p-low); border-style: dashed; }
nav.filters { margin: 0 0 8px; font-size: 14px; }
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
.blocked { color: var(--deferred); font-size: 13px; }
.graph { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; overflow-x: auto; }
.graph svg { display: block; }
.graph .node rect { fill: var(--panel); stroke-width: 2; }
.graph .node.area-0 rect { fill: var(--area-0); } .graph .node.area-1 rect { fill: var(--area-1); }
.graph .node.area-2 rect { fill: var(--area-2); } .graph .node.area-3 rect { fill: var(--area-3); }
.graph .node.s-open rect { stroke: var(--open); } .graph .node.s-in-progress rect { stroke: var(--progress); }
.graph .node.s-review rect { stroke: var(--review); } .graph .node.s-deferred rect { stroke: var(--deferred); }
.graph .node.s-closed rect { stroke: var(--line); stroke-dasharray: 4 3; }
.graph .node.s-closed text { fill: var(--muted); }
.graph .node.ready rect { stroke: var(--progress); stroke-width: 3; }
main.wide { max-width: none; }
.graph text { fill: var(--text); font: 13px system-ui, -apple-system, "Segoe UI", sans-serif; }
.graph text.small { fill: var(--muted); font-size: 11px; }
.graph .edge { fill: none; stroke: var(--muted); stroke-width: 1.5; }
.graph .edge.done { stroke: var(--line); stroke-dasharray: 4 3; }
.graph .arrow { fill: var(--muted); }
.legend { display: flex; flex-wrap: wrap; gap: 4px 16px; font-size: 13px; color: var(--muted); margin: 8px 0; }
.swatch { display: inline-block; width: 12px; height: 12px; border-radius: 3px; vertical-align: -1px;
  border: 1px solid var(--line); }
"""


def _badge(status: str) -> str:
    css = "s-" + status.replace(" ", "-")
    return f'<span class="badge {css}">{escape(ALL_STATUS_LABELS.get(status, status))}</span>'


def _priority_badge(entry: dict) -> str:
    level = entry.get("priority")
    return f'<span class="badge p-{level}" title="priority">{escape(level)}</span>' if level else ""


def _by_priority(entries: list[dict]) -> list[dict]:
    """Stable: the server's order (stage, id) breaks the ties of one priority."""
    return sorted(entries, key=lambda entry: PRIORITY_RANK.get(entry.get("priority"), PRIORITY_RANK[None]))


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
    parts = [f'<span class="title">{link}</span>', _badge(entry["status"]), _priority_badge(entry)]
    if with_age:
        parts.append(f'<span class="meta">{escape(_age(entry, today))}</span>')
    parts.append(_progress(entry))
    waits = _open_blockers(entry)
    if waits:
        parts.append(f'<span class="blocked">blocked by {escape(", ".join(waits))}</span>')
    if with_summary and entry["summary"]:
        parts.append(f'<span class="summary">{escape(entry["summary"])}</span>')
    return "<li>" + "".join(part for part in parts if part) + "</li>"


def _open_blockers(entry: dict) -> list[str]:
    return [link["id"] for link in entry.get("blockedBy", []) if link["status"] != "closed"]


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


_WIDE = ' class="wide"'


def _page(title: str, body: str, wide: bool = False) -> str:
    return (
        "<!doctype html>\n"
        '<html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        f"<title>{escape(title)}</title>"
        f"<style>{_STYLE}</style></head><body><main{_WIDE if wide else ''}>{body}</main></body></html>\n"
    )


def _nav(areas: list[dict], current: str | None, page: str = "", graph_link: bool = True, keep: str = "") -> str:
    """`keep`: extra query text (`&priority=now`) the area links carry along."""
    links = [
        f"<strong>{escape(area['title'])}</strong>"
        if area["id"] == current
        else f'<a href="{page}?area={quote(area["id"])}{escape(keep)}">{escape(area["title"])}</a>'
        for area in areas
    ]
    if graph_link:
        links.append(f'<a href="graph{"?area=" + quote(current) if current else ""}">Dependency graph</a>')
    return f"<nav>{''.join(links)}</nav>"


def _filter_links(area: str | None, current: str | None) -> str:
    """The row under the nav: all, one level, or the unset ones; the area stays."""
    where = f"area={quote(area)}" if area else ""
    links = []
    for label, value in FILTER_LINKS:
        if value == current:
            links.append(f"<strong>{label}</strong>")
            continue
        query = "&amp;".join(part for part in (where, f"priority={value}" if value else "") if part)
        links.append(f'<a href="?{query}">{label}</a>')
    return f'<nav class="filters"><span class="muted">Priority:</span>{"".join(links)}</nav>'


def _filter_note(priority: str | None, hidden: int) -> str:
    if not priority:
        return ""
    names = ", ".join("unset" if part == "none" else part for part in priority.split(","))
    return f'<p class="muted">Filter: priority {escape(names)}; {hidden} {"entry" if hidden == 1 else "entries"} hidden.</p>'


def render_dashboard(data: dict, today: date) -> str:
    """data: {areas, area, stages, entries (to_json without the body, closed ones left out), warnings,
    priority (the filter text of the page or None), hidden (how many entries the filter left out)}."""
    entries = _by_priority(data["entries"])
    priority = data.get("priority")
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
        _nav(data.get("areas", []), data.get("area"), keep=f"&priority={priority}" if priority else ""),
        _filter_links(data.get("area"), priority),
        "<h1>TODO dashboard</h1>",
        f'<div class="muted">{today.isoformat()}, from the todo store. {len(entries)} open entries.</div>',
        _filter_note(priority, data.get("hidden", 0)),
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
    if entry.get("priority"):
        facts.append(f"<strong>Priority:</strong> {escape(entry['priority'])}")
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
        _links_section("Blocked by", entry.get("blockedBy", [])),
        _links_section("Blocks", entry.get("blocks", [])),
        f'<div class="doc">{markdown_to_html(entry["body"])}</div>',
        f"<h2>Plans</h2><ul class='panel'>{plan_rows}</ul>" if plans else "",
        f"<h2>History</h2><table class='events'>{event_rows}</table>" if events else "",
    ]
    return _page(entry["title"], "".join(body))


def _links_section(heading: str, links: list[dict]) -> str:
    if not links:
        return ""
    rows = "".join(
        f'<li><a href="{quote(link["id"])}">{escape(link["id"])}</a> {_badge(link["status"])} '
        f'<span class="meta">{escape(link["title"])}{" · " + escape(link["note"]) if link["note"] else ""}</span></li>'
        for link in links
    )
    return f"<p><strong>{escape(heading)}:</strong></p><ul class='panel'>{rows}</ul>"


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


# --- the dependency graph ------------------------------------------------------------------------

NODE_WIDTH = 220
NODE_HEIGHT = 46
COLUMN_STRIDE = NODE_WIDTH + 80
ROW_STRIDE = NODE_HEIGHT + 18
GRAPH_MARGIN = 16
NODE_ID_CHARS = 28
ORDER_SWEEPS = 4
AREA_TINTS = 4


def graph_subset(entries: list[dict], area: str | None, with_closed: bool) -> tuple[dict[str, dict], list[tuple[str, str]]]:
    """
    The nodes and edges (blocker, entry) to draw. An edge whose two ends are closed is left out unless
    `with_closed`; with `area`, only edges with an end in that area (the other end may be of any area).
    """
    by_id = {entry["id"]: entry for entry in entries}
    edges = []
    for entry in entries:
        for link in entry.get("blockedBy", []):
            blocker = by_id.get(link["id"])
            if blocker is None:
                continue
            if not with_closed and entry["status"] == "closed" and blocker["status"] == "closed":
                continue
            if area and area not in (entry["area"], blocker["area"]):
                continue
            edges.append((blocker["id"], entry["id"]))
    nodes = {node: by_id[node] for edge in edges for node in edge}
    return nodes, sorted(set(edges))


def layout_graph(nodes: list[str], edges: list[tuple[str, str]], key=None) -> dict[str, tuple[int, int]]:
    """
    node -> (column, row): a blocker is left of what it blocks (column = the longest chain of blockers
    before the node), rows ordered by the mean row of the neighbours (a few sweeps) to keep lines short.
    """
    blockers: dict[str, list[str]] = {node: [] for node in nodes}
    blocked: dict[str, list[str]] = {node: [] for node in nodes}
    for blocker, entry in edges:
        blockers[entry].append(blocker)
        blocked[blocker].append(entry)
    column: dict[str, int] = {}

    def depth(node: str, seen: frozenset = frozenset()) -> int:
        if node not in column:
            # the store refuses cycles; `seen` only keeps a broken graph from recursing forever
            column[node] = 1 + max((depth(b, seen | {node}) for b in blockers[node] if b not in seen | {node}), default=-1)
        return column[node]

    for node in nodes:
        depth(node)
    columns: dict[int, list[str]] = {}
    for node in sorted(nodes, key=key or (lambda n: n)):
        columns.setdefault(column[node], []).append(node)
    row = {node: index for members in columns.values() for index, node in enumerate(members)}

    def mean(neighbours: list[str], fallback: int) -> float:
        return sum(row[n] for n in neighbours) / len(neighbours) if neighbours else fallback

    order = sorted(columns)
    for sweep in range(ORDER_SWEEPS):
        forward = sweep % 2 == 0
        for number in order if forward else reversed(order):
            members = columns[number]
            members.sort(key=lambda n: (mean(blockers[n] if forward else blocked[n], row[n]), row[n]))
            for index, node in enumerate(members):
                row[node] = index
    return {node: (column[node], row[node]) for node in nodes}


def _node_svg(entry: dict, x: int, y: int, area_index: int, ready: bool) -> str:
    css = f"node s-{entry['status'].replace(' ', '-')} area-{area_index % AREA_TINTS}" + (" ready" if ready else "")
    label = entry["id"] if len(entry["id"]) <= NODE_ID_CHARS else entry["id"][: NODE_ID_CHARS - 1] + "…"
    detail = f"{entry['status']} · {entry['area']}" + (f" · stage {entry['stage']}" if entry["stage"] is not None else "")
    return (
        f'<a href="entry/{quote(entry["id"])}"><g class="{css}"><title>{escape(entry["id"])}: {escape(entry["title"])}</title>'
        f'<rect x="{x}" y="{y}" width="{NODE_WIDTH}" height="{NODE_HEIGHT}" rx="8"/>'
        f'<text x="{x + 10}" y="{y + 19}">{escape(label)}</text>'
        f'<text class="small" x="{x + 10}" y="{y + 36}">{escape(detail)}</text></g></a>'
    )


def render_graph_svg(nodes: dict[str, dict], edges: list[tuple[str, str]], areas: list[str]) -> str:
    if not nodes:
        return ""
    area_index = {area: index for index, area in enumerate(areas)}
    place = layout_graph(list(nodes), edges, key=lambda n: (area_index.get(nodes[n]["area"], 99), n))
    width = GRAPH_MARGIN * 2 + max(c for c, _ in place.values()) * COLUMN_STRIDE + NODE_WIDTH
    height = GRAPH_MARGIN * 2 + max(r for _, r in place.values()) * ROW_STRIDE + NODE_HEIGHT

    def corner(node: str) -> tuple[int, int]:
        c, r = place[node]
        return GRAPH_MARGIN + c * COLUMN_STRIDE, GRAPH_MARGIN + r * ROW_STRIDE

    lines = []
    for blocker, entry in edges:
        (bx, by), (ex, ey) = corner(blocker), corner(entry)
        x1, y1, x2, y2 = bx + NODE_WIDTH, by + NODE_HEIGHT // 2, ex - 4, ey + NODE_HEIGHT // 2
        bend = max((x2 - x1) // 2, 30)
        css = "edge done" if nodes[blocker]["status"] == "closed" else "edge"
        lines.append(
            f'<path class="{css}" marker-end="url(#arrow)" d="M{x1},{y1} C{x1 + bend},{y1} {x2 - bend},{y2} {x2},{y2}">'
            f"<title>{escape(entry)} is blocked by {escape(blocker)}</title></path>"
        )
    boxes = []
    for node, entry in nodes.items():
        ready = entry["status"] == "open" and not entry["blocked"] and any(link["status"] != "closed" for link in entry.get("blocks", []))
        boxes.append(_node_svg(entry, *corner(node), area_index.get(entry["area"], 0), ready))
    arrow = '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto">' \
            '<path class="arrow" d="M0,0 L10,5 L0,10 z"/></marker></defs>'
    return (
        f'<div class="graph"><svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-label="Dependency graph">{arrow}{"".join(lines)}{"".join(boxes)}</svg></div>'
    )


def render_graph(data: dict, today: date) -> str:
    """data: {areas, area (None: every area), closed (draw links between closed entries), entries (to_json of every linked entry)}."""
    area_ids = [area["id"] for area in data["areas"]]
    nodes, edges = graph_subset(data["entries"], data.get("area"), data.get("closed", False))
    blocked = sorted((entry for entry in nodes.values() if entry["status"] != "closed" and entry["blocked"]), key=lambda e: e["id"])
    ready = sorted(
        (
            entry
            for entry in nodes.values()
            if entry["status"] == "open" and not entry["blocked"] and any(link["status"] != "closed" for link in entry.get("blocks", []))
        ),
        key=lambda e: e["id"],
    )

    def row(entry: dict, links: list[dict], word: str) -> str:
        names = ", ".join(f'<a href="entry/{quote(link["id"])}">{escape(link["id"])}</a>' for link in links)
        return (
            f'<li><span class="title"><a href="entry/{quote(entry["id"])}">{escape(entry["id"])}</a> '
            f'<span class="meta">{escape(entry["title"])}</span></span>{_badge(entry["status"])}'
            f'<span class="summary">{word} {names}</span></li>'
        )

    blocked_rows = "".join(row(e, [l for l in e["blockedBy"] if l["status"] != "closed"], "waits for") for e in blocked)
    ready_rows = "".join(row(e, [l for l in e["blocks"] if l["status"] != "closed"], "then") for e in ready)
    area = data.get("area")
    closed_query = "closed=1" if not data.get("closed") else ""
    toggle_href = "graph?" + "&".join(part for part in (f"area={quote(area)}" if area else "", closed_query) if part)
    toggle = f'<a href="{toggle_href}">{"hide" if data.get("closed") else "show"} links between closed entries</a>'
    nav_areas = [{"id": "", "title": "All areas"}, *data["areas"]]
    nav = _nav(nav_areas, area or "", page="graph", graph_link=False).replace('href="graph?area="', 'href="graph"')
    tints = "".join(
        f'<span><span class="swatch" style="background: var(--area-{index % AREA_TINTS})"></span> {escape(area_id)}</span>'
        for index, area_id in enumerate(area_ids)
    )
    body = [
        '<nav><a href=".">Dashboard</a></nav>',
        nav,
        "<h1>Dependency graph</h1>",
        f'<div class="muted">{today.isoformat()}. An arrow goes from a blocker to the entry that waits for it; '
        f"a green frame is an open entry that nothing blocks and that others wait for (take it first). {toggle}.</div>",
        f'<div class="legend">{tints}<span>dashed: closed</span></div>',
        render_graph_svg(nodes, edges, area_ids) or '<p class="muted">No links between entries yet: <code>todo.py dep add &lt;id&gt; &lt;blocker&gt;</code>.</p>',
        f"<h2>Blocked</h2><ul class='panel'>{blocked_rows}</ul>" if blocked else "",
        f"<h2>Ready, and others wait for them</h2><ul class='panel'>{ready_rows}</ul>" if ready else "",
    ]
    return _page("Dependency graph", "".join(body), wide=True)
