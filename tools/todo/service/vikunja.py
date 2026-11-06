"""
The Vikunja side of the mirror: a REST client (urllib, Bearer token) and what a task of an entry looks like.

Checked against Vikunja 2.7.1. A managed task's description starts with the visible text
`todo-sync:id=<entry id>` (the identity a task keeps if the store loses its task id), then the facts
of the entry, a rule, the body of the entry and, when the entry has a plan, the plan's checklist as
the editor's task list. Owned fields: title, description, done and the labels "in progress",
"review", "deferred", "stage N"; everything else on a task is kept (read-modify-write, because
POST /tasks/{id} replaces the whole task).
"""

import json
import re
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Mapping
from html import escape

from todo_markdown import html_to_markdown, inline_html, markdown_to_html

API_PREFIX = "/api/v1"
PAGE_SIZE = 50
REQUEST_TIMEOUT_SECONDS = 15

MARKER_PREFIX = "todo-sync:id="
_MARKER_RE = re.compile(r"todo-sync:id=([\w-]+)")
PLAN_MARKER_PREFIX = "todo-sync:plan="
PLAN_BLOCK_START = f"<p>{PLAN_MARKER_PREFIX}"
PLAN_MAX_ITEMS = 300
PLAN_ITEM_MAX_LENGTH = 240
MANAGED_LABEL_RE = re.compile(r"^(?:in progress|review|deferred|stage \d+)$")
_STAGE_LABEL_RE = re.compile(r"^stage (\d+)$")
_BETWEEN_TAGS_RE = re.compile(r">\s+<")
_FACT_LINE_RE = re.compile(r"^(?:todo-sync:id=\S+|\*\*(?:Status|Stage|Plan):\*\*.*|---)$")
WEBHOOK_EVENTS = ["task.created", "task.updated", "task.deleted"]


class VikunjaError(Exception):
    """A failed request (the message never contains the token). `status` is the HTTP status, if any."""

    def __init__(self, message: str, status: int | None = None):
        super().__init__(message)
        self.status = status


class VikunjaClient:
    def __init__(self, url: str, token: str, timeout: int = REQUEST_TIMEOUT_SECONDS):
        base = url.rstrip("/")
        self._base = base if base.endswith(API_PREFIX) else base + API_PREFIX
        self._token = token
        self._timeout = timeout

    def request(self, method: str, path: str, body: dict | list | None = None) -> tuple[object, Mapping[str, str]]:
        data = None if body is None else json.dumps(body).encode("utf-8")
        request = urllib.request.Request(self._base + path, data=data, method=method)
        request.add_header("Authorization", f"Bearer {self._token}")
        request.add_header("Accept", "application/json")
        if data is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as response:
                payload = response.read()
                return (json.loads(payload) if payload else None), response.headers
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")[:200]
            raise VikunjaError(f"{method} {path} -> HTTP {error.code}: {detail}", error.code) from None
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as error:
            raise VikunjaError(f"{method} {path} failed: {error}") from None

    def get_all(self, path: str) -> list[dict]:
        """GET a paged collection and return every item. `path` may already carry a query string."""
        separator = "&" if "?" in path else "?"
        items: list[dict] = []
        page = 1
        while True:
            chunk, headers = self.request("GET", f"{path}{separator}per_page={PAGE_SIZE}&page={page}")
            items.extend(chunk or [])
            total_pages = headers.get("X-Pagination-Total-Pages")
            last_page = page >= int(total_pages) if total_pages is not None else len(chunk or []) < PAGE_SIZE
            if last_page or not chunk:
                return items
            page += 1

    def get_task(self, task_id: int) -> dict | None:
        try:
            task, _ = self.request("GET", f"/tasks/{task_id}")
            return task
        except VikunjaError as error:
            if error.status == 404:
                return None
            raise

    def find_project(self, title: str) -> dict | None:
        found = [p for p in self.get_all(f"/projects?s={urllib.parse.quote(title)}") if p.get("title") == title and not p.get("is_archived")]
        return found[0] if found else None

    def project_tasks(self, project_id: int) -> list[dict]:
        task_filter = urllib.parse.quote(f"project_id = {project_id}")
        return self.get_all(f"/tasks?filter={task_filter}")


# --- what a task should look like -------------------------------------------------------------


def _plan_item_html(text: str) -> str:
    if len(text) > PLAN_ITEM_MAX_LENGTH:
        text = text[:PLAN_ITEM_MAX_LENGTH].rsplit(" ", 1)[0].rstrip(" ,;:") + "…"
    return inline_html(text)


def render_plan_block(plan: str, checklist: dict) -> str:
    """The copy of a plan's checklist: a marker paragraph, the plan's title, its boxes under their headings."""
    parts = [f"{PLAN_BLOCK_START}{escape(plan)}</p>", f"<h3>{escape(checklist['title'])}</h3>"]
    shown = 0
    total = sum(len(section["items"]) for section in checklist["sections"])
    for section in checklist["sections"]:
        items = section["items"][: max(PLAN_MAX_ITEMS - shown, 0)]
        if not items:
            break
        shown += len(items)
        if section["heading"] and section["heading"] != checklist["title"]:
            parts.append(f"<h4>{escape(section['heading'])}</h4>")
        parts.append('<ul data-type="taskList">')
        for item in items:
            checked = "true" if item["done"] else "false"
            box = ' checked="checked"' if item["done"] else ""
            parts.append(
                f'<li data-checked="{checked}" data-type="taskItem"><label><input type="checkbox"{box}><span></span></label>'
                f"<div><p>{_plan_item_html(item['text'])}</p></div></li>"
            )
        parts.append("</ul>")
    if shown < total:
        parts.append(f"<p>…cut: {shown} of {total} items shown, the rest is in the plan</p>")
    return "".join(parts)


def strip_plan_block(description: str) -> str:
    start = (description or "").find(PLAN_BLOCK_START)
    return description[:start] if start >= 0 else (description or "")


def render_task(entry: dict, status_line: str, stage_title: str | None, plan: dict | None) -> dict:
    """The owned fields of the entry's task: {title, description, done, labels}."""
    facts = [f"{MARKER_PREFIX}{entry['id']}", f"<strong>Status:</strong> {escape(status_line)}"]
    if entry["stage"] is not None:
        stage = f"{entry['stage']}: {stage_title}" if stage_title else str(entry["stage"])
        facts.append(f"<strong>Stage:</strong> {escape(stage)}")
    if plan is not None:
        facts.append(f"<strong>Plan:</strong> {escape(plan['name'])}")
    description = "".join(f"<p>{fact}</p>" for fact in facts) + "<hr>" + markdown_to_html(entry["body"])
    if plan is not None and plan["checklist"]:
        description += render_plan_block(plan["name"], plan["checklist"])
    return {"title": entry["title"], "description": description, "done": entry["status"] == "closed", "labels": sorted(desired_labels(entry))}


def desired_labels(entry: dict) -> set[str]:
    labels = set()
    if entry["status"] in ("in progress", "review", "deferred"):
        labels.add(entry["status"])
    if entry["stage"] is not None:
        labels.add(f"stage {entry['stage']}")
    return labels


def normalize_html(text: str) -> str:
    return _BETWEEN_TAGS_RE.sub("><", (text or "").strip())


def marker_of(task: dict) -> str | None:
    match = _MARKER_RE.search(re.sub(r"<[^>]*>", " ", task.get("description") or ""))
    return match.group(1) if match else None


def managed_labels(task: dict) -> list[str]:
    return sorted(label["title"] for label in task.get("labels") or [] if MANAGED_LABEL_RE.match(label["title"]))


def snapshot_of(task: dict) -> dict:
    """The owned fields of a task as Vikunja has them."""
    return {
        "title": task.get("title") or "",
        "description": normalize_html(task.get("description")),
        "done": bool(task.get("done")),
        "labels": managed_labels(task),
    }


def body_of(description: str) -> str:
    """The body of an entry from a task description: no marker, no facts, no rule, no plan block."""
    lines = html_to_markdown(strip_plan_block(description)).splitlines()
    while lines and (not lines[0].strip() or _FACT_LINE_RE.match(lines[0].strip())):
        lines.pop(0)
    return "\n".join(lines).strip() + "\n" if any(line.strip() for line in lines) else ""


def status_from_labels(labels: list[str]) -> str | None:
    """`in progress` or `deferred` when the labels name one (review comes from the loop, never from here)."""
    for status in ("in progress", "deferred"):
        if status in labels:
            return status
    return None


def stage_from_labels(labels: list[str]) -> int | None:
    stages = sorted(int(match.group(1)) for label in labels if (match := _STAGE_LABEL_RE.match(label)))
    return stages[0] if stages else None
