#!/usr/bin/env python3
"""
One-way export of the parsed TODO.md (see todo_parser.py) to a Vikunja project.

Checked against Vikunja 2.7.1 (REST /api/v1, scoped API token as a Bearer token).

Identity: a managed task's description starts with the visible text "todo-sync:id=<entry id>".
Nothing is stored locally, so another machine finds the same tasks.

Owned fields: title, description, done and the labels "in progress", "deferred", "stage N".
Everything else on a task (priority, due date, assignees, comments, other labels) is kept:
an update is read-modify-write, because POST /tasks/{id} replaces the whole task.
A task whose entry left TODO.md is marked done, never deleted. Tasks without the marker are never touched.

The token is never printed. Standard library only.
"""

import json
import os
import re
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Mapping
from dataclasses import dataclass, field
from html import escape
from pathlib import Path

API_PREFIX = "/api/v1"
PAGE_SIZE = 50
REQUEST_TIMEOUT_SECONDS = 15
DEFAULT_PROJECT = "BagiEngine"

ENV_URL = "VIKUNJA_URL"
ENV_TOKEN = "VIKUNJA_TOKEN"
ENV_PROJECT = "VIKUNJA_PROJECT"

MARKER_PREFIX = "todo-sync:id="
_MARKER_RE = re.compile(r"todo-sync:id=([\w-]+)")
_MANAGED_LABEL_RE = re.compile(r"^(?:in progress|deferred|stage \d+)$")
_BETWEEN_TAGS_RE = re.compile(r">\s+<")
_ENV_LINE_RE = re.compile(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")

CREATE, UPDATE, CLOSE, REOPEN, UNCHANGED = "create", "update", "close", "reopen", "unchanged"


class VikunjaError(Exception):
    """A settings problem or a failed request (the message never contains the token)."""


@dataclass
class Settings:
    url: str
    token: str = field(repr=False)
    project: str = DEFAULT_PROJECT


def read_env_file(path: Path) -> dict[str, str]:
    """KEY=VALUE lines of a .env file; comments and blank lines are skipped, quotes around a value are removed."""
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _ENV_LINE_RE.match(line)
        if not match:
            continue
        value = match.group(2)
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[match.group(1)] = value
    return values


def load_settings(env: Mapping[str, str] | None = None, env_file: Path | None = None, project: str | None = None) -> Settings:
    """Process environment first, then the .env file. Raises VikunjaError when the URL or the token is missing."""
    env = os.environ if env is None else env
    file_values = read_env_file(env_file) if env_file else {}

    def pick(name: str) -> str:
        return (env.get(name) or file_values.get(name) or "").strip()

    url, token = pick(ENV_URL), pick(ENV_TOKEN)
    missing = [name for name, value in ((ENV_URL, url), (ENV_TOKEN, token)) if not value]
    if missing:
        where = f" or in {env_file}" if env_file else ""
        raise VikunjaError(f"{', '.join(missing)} not set (set it in the environment{where})")
    return Settings(url=url, token=token, project=project or pick(ENV_PROJECT) or DEFAULT_PROJECT)


class VikunjaClient:
    def __init__(self, url: str, token: str, timeout: int = REQUEST_TIMEOUT_SECONDS):
        base = url.rstrip("/")
        self._base = base if base.endswith(API_PREFIX) else base + API_PREFIX
        self._token = token
        self._timeout = timeout

    def request(self, method: str, path: str, body: dict | None = None) -> tuple[object, Mapping[str, str]]:
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
            raise VikunjaError(f"{method} {path} -> HTTP {error.code}: {detail}") from None
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
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


# --- what a task should look like -------------------------------------------------------------


def render_description(entry: dict, stage_title: str | None) -> str:
    """The marker first, then the entry's facts. Plain <p> blocks, so the Vikunja editor keeps them."""
    status = entry["status"] + (f" since {entry['date']}" if entry["date"] else "")
    blocks = [f"{MARKER_PREFIX}{entry['id']}", f"<strong>Status:</strong> {escape(status)}"]
    if entry["stage"] is not None:
        stage = f"{entry['stage']}: {stage_title}" if stage_title else str(entry["stage"])
        blocks.append(f"<strong>Stage:</strong> {escape(stage)}")
    if entry["plan"]:
        progress = entry["planProgress"]
        suffix = f" ({progress['done']} / {progress['total']} done)" if progress else ""
        blocks.append(f"<strong>Plan:</strong> {escape(entry['plan'])}{escape(suffix)}")
    if entry["summary"]:
        blocks.append(escape(entry["summary"]))
    return "".join(f"<p>{block}</p>" for block in blocks)


def desired_labels(entry: dict) -> set[str]:
    labels = set()
    if entry["status"] != "open":
        labels.add(entry["status"])
    if entry["stage"] is not None:
        labels.add(f"stage {entry['stage']}")
    return labels


def _normalize_html(text: str) -> str:
    return _BETWEEN_TAGS_RE.sub("><", (text or "").strip())


def marker_of(task: dict) -> str | None:
    match = _MARKER_RE.search(re.sub(r"<[^>]*>", " ", task.get("description") or ""))
    return match.group(1) if match else None


# --- the plan: what has to change --------------------------------------------------------------


@dataclass
class Action:
    kind: str
    entry_id: str
    task: dict | None  # the listed task, None for a create
    title: str = ""
    description: str = ""
    labels: set[str] = field(default_factory=set)  # desired managed labels
    changes: list[str] = field(default_factory=list)  # what differs, for the report


def plan_actions(data: dict, tasks: list[dict]) -> tuple[list[Action], list[str]]:
    """Compare TODO.md entries with the project's tasks. Returns the actions and the warnings."""
    stage_titles = {stage["number"]: stage["title"] for stage in data["stages"]}
    warnings: list[str] = []
    by_marker: dict[str, dict] = {}
    for task in tasks:
        entry_id = marker_of(task)
        if entry_id is None:
            continue
        if entry_id in by_marker:
            warnings.append(f"more than one task carries todo-sync:id={entry_id}; using task {by_marker[entry_id]['id']}")
            continue
        by_marker[entry_id] = task

    actions: list[Action] = []
    wanted_ids = set()
    for entry in data["entries"]:
        wanted_ids.add(entry["id"])
        description = render_description(entry, stage_titles.get(entry["stage"]))
        labels = desired_labels(entry)
        task = by_marker.get(entry["id"])
        action = Action(CREATE, entry["id"], task, entry["title"], description, labels)
        if task is not None:
            current = {label["title"] for label in task.get("labels") or [] if _MANAGED_LABEL_RE.match(label["title"])}
            if task["title"] != entry["title"]:
                action.changes.append("title")
            if _normalize_html(task.get("description")) != _normalize_html(description):
                action.changes.append("description")
            if current != labels:
                action.changes.append("labels")
            if task.get("done"):
                action.kind = REOPEN
            else:
                action.kind = UPDATE if action.changes else UNCHANGED
        actions.append(action)

    for entry_id, task in by_marker.items():
        if entry_id not in wanted_ids and not task.get("done"):
            actions.append(Action(CLOSE, entry_id, task, task["title"]))
    return actions, warnings


# --- applying it -------------------------------------------------------------------------------


@dataclass
class Report:
    lines: list[str] = field(default_factory=list)
    counts: dict[str, int] = field(default_factory=lambda: {kind: 0 for kind in (CREATE, UPDATE, CLOSE, REOPEN, UNCHANGED)})
    warnings: list[str] = field(default_factory=list)

    def summary(self) -> str:
        return ", ".join(f"{count} {kind}" for kind, count in self.counts.items())


def _find_project(client: VikunjaClient, title: str, report: Report) -> dict | None:
    found = [p for p in client.get_all(f"/projects?s={urllib.parse.quote(title)}") if p.get("title") == title and not p.get("is_archived")]
    if len(found) > 1:
        report.warnings.append(f"{len(found)} projects are titled '{title}'; using project {found[0]['id']}")
    return found[0] if found else None


def _label_ids(client: VikunjaClient, titles: set[str], dry_run: bool) -> dict[str, int]:
    """Ids of the labels with these titles; the missing ones are created (not on a dry run)."""
    ids = {label["title"]: label["id"] for label in client.get_all("/labels")}
    for title in sorted(titles - set(ids)):
        if dry_run:
            continue
        created, _ = client.request("PUT", "/labels", {"title": title})
        ids[title] = created["id"]
    return ids


def _set_labels(client: VikunjaClient, task: dict | None, task_id: int, labels: set[str], ids: dict[str, int]) -> None:
    """Replace the managed labels of a task and keep the others."""
    foreign = [label["id"] for label in (task or {}).get("labels") or [] if not _MANAGED_LABEL_RE.match(label["title"])]
    wanted = sorted(ids[title] for title in labels if title in ids)
    client.request("POST", f"/tasks/{task_id}/labels/bulk", {"labels": [{"id": label_id} for label_id in foreign + wanted]})


def sync(client: VikunjaClient, data: dict, project_title: str, dry_run: bool = False) -> Report:
    report = Report()
    project = _find_project(client, project_title, report)
    if project is None:
        report.lines.append(f"project '{project_title}': {'would create' if dry_run else 'create'}")
        if not dry_run:
            project, _ = client.request("PUT", "/projects", {"title": project_title})
    tasks: list[dict] = []
    if project is not None:
        task_filter = urllib.parse.quote(f"project_id = {project['id']}")
        tasks = client.get_all(f"/tasks?filter={task_filter}")

    actions, warnings = plan_actions(data, tasks)
    report.warnings.extend(warnings)
    needed_labels = set().union(*(action.labels for action in actions)) if actions else set()
    ids = _label_ids(client, needed_labels, dry_run) if needed_labels else {}

    for action in actions:
        report.counts[action.kind] += 1
        if action.kind != UNCHANGED:
            detail = f" ({', '.join(action.changes)})" if action.changes and action.kind != CREATE else ""
            report.lines.append(f"{action.kind:<7} {action.entry_id}{detail}")
        if dry_run or action.kind == UNCHANGED:
            continue
        if action.kind == CREATE:
            created, _ = client.request(
                "PUT", f"/projects/{project['id']}/tasks", {"title": action.title, "description": action.description}
            )
            if action.labels:
                _set_labels(client, None, created["id"], action.labels, ids)
            continue
        # read-modify-write: the listed task goes back whole with only the owned fields changed
        # (an UPDATE is never a done task, so only CLOSE leaves it done)
        task = dict(action.task)
        task["done"] = action.kind == CLOSE
        if action.kind != CLOSE:
            task["title"], task["description"] = action.title, action.description
        client.request("POST", f"/tasks/{task['id']}", task)
        if "labels" in action.changes:
            _set_labels(client, action.task, task["id"], action.labels, ids)
    return report
