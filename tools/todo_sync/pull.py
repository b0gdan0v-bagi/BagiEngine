#!/usr/bin/env python3
"""
Bring what changed in Vikunja back into the todo/ files (the reverse of vikunja.sync).

Two things come back, and nothing else (title, description and labels of a managed task are
owned by push and overwritten by it):
  - a managed task that is done while its entry file still exists: the entry is finished, so with
    apply the file is deleted, but only if git tracks it and it has no uncommitted changes;
  - a task without the marker that is not done: adopted as a new entry file, and written back with
    the marker so the next push finds it unchanged instead of creating a duplicate.

Without apply nothing is written, in Vikunja or on disk. Standard library only.
"""

import re
import subprocess
from dataclasses import dataclass, field
from datetime import date
from html.parser import HTMLParser
from pathlib import Path

from todo_parser import README_FILE, ROADMAP_FILE, ParseError, parse_todo, slugify
from vikunja import Report, VikunjaClient, find_project, list_tasks, marker_of, render_description

_STAGE_LABEL_RE = re.compile(r"^stage (\d+)$")
_BLANK_RUNS_RE = re.compile(r"\n{3,}")
_MARKER_LINE_RE = re.compile(r"^todo-sync:id=\S+$")
GIT_TIMEOUT_SECONDS = 15

_BLOCK_TAGS = {"p", "div", "ul", "ol", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6", "pre"}
_HEADING_PREFIX = {"h1": "# ", "h2": "## ", "h3": "### ", "h4": "#### ", "h5": "##### ", "h6": "###### "}


class _HtmlToMarkdown(HTMLParser):
    """The small subset of HTML that the Vikunja editor produces, as markdown text."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self._out: list[str] = []
        self._lists: list[str] = []  # "ul" or "ol" per open list
        self._counters: list[int] = []
        self._links: list[str | None] = []
        self._in_pre = False

    def _block_break(self) -> None:
        text = "".join(self._out)
        if text and not text.endswith("\n\n"):
            self._out.append("\n" if text.endswith("\n") else "\n\n")

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = dict(attrs)
        if tag in ("ul", "ol"):
            self._block_break()
            self._lists.append(tag)
            self._counters.append(0)
        elif tag == "li":
            if "".join(self._out) and not "".join(self._out).endswith("\n"):
                self._out.append("\n")
            depth = max(len(self._lists) - 1, 0)
            checked = attributes.get("data-checked")
            if self._lists and self._lists[-1] == "ol":
                self._counters[-1] += 1
                bullet = f"{self._counters[-1]}. "
            else:
                bullet = "- "
            box = "" if checked is None else ("[x] " if checked == "true" else "[ ] ")
            self._out.append("  " * depth + bullet + box)
        elif tag in _BLOCK_TAGS:
            self._block_break()
            if tag in _HEADING_PREFIX:
                self._out.append(_HEADING_PREFIX[tag])
            if tag == "pre":
                self._out.append("```\n")
                self._in_pre = True
        elif tag == "br":
            self._out.append("\n")
        elif tag in ("strong", "b"):
            self._out.append("**")
        elif tag in ("em", "i"):
            self._out.append("*")
        elif tag == "code" and not self._in_pre:
            self._out.append("`")
        elif tag == "a":
            self._links.append(attributes.get("href"))
            self._out.append("[")

    def handle_endtag(self, tag: str) -> None:
        if tag in ("ul", "ol"):
            if self._lists:
                self._lists.pop()
                self._counters.pop()
            self._block_break()
        elif tag == "li":
            self._out.append("\n")
        elif tag == "pre":
            self._out.append("\n```")
            self._in_pre = False
            self._block_break()
        elif tag in _BLOCK_TAGS:
            self._block_break()
        elif tag in ("strong", "b"):
            self._out.append("**")
        elif tag in ("em", "i"):
            self._out.append("*")
        elif tag == "code" and not self._in_pre:
            self._out.append("`")
        elif tag == "a" and self._links:
            href = self._links.pop()
            self._out.append(f"]({href})" if href else "]")

    def handle_data(self, data: str) -> None:
        self._out.append(data if self._in_pre else re.sub(r"\s+", " ", data))

    def text(self) -> str:
        return _BLANK_RUNS_RE.sub("\n\n", "".join(self._out)).strip()


def html_to_markdown(html: str) -> str:
    """The description of a task as markdown text. A description that is plain text comes back as it is."""
    parser = _HtmlToMarkdown()
    parser.feed(html or "")
    parser.close()
    return parser.text()


@dataclass
class PullReport:
    lines: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    closed: int = 0
    adopted: int = 0

    def summary(self) -> str:
        return f"{self.closed} closed in Vikunja, {self.adopted} created in Vikunja"


# --- git ---------------------------------------------------------------------------------------


def _git(todo_dir: Path, *args: str) -> subprocess.CompletedProcess | None:
    try:
        return subprocess.run(
            ["git", "-C", str(todo_dir), *args], capture_output=True, text=True, timeout=GIT_TIMEOUT_SECONDS, check=False
        )
    except (OSError, subprocess.TimeoutExpired):
        return None


def deletion_blocker(todo_dir: Path, file_name: str) -> str | None:
    """Why this file must not be deleted, or None when git tracks it and it has no uncommitted changes."""
    tracked = _git(todo_dir, "ls-files", "--error-unmatch", "--", file_name)
    if tracked is None:
        return "git is not available"
    if tracked.returncode != 0:
        return "git does not track it"
    status = _git(todo_dir, "status", "--porcelain", "--", file_name)
    if status is None or status.returncode != 0:
        return "git status failed"
    if status.stdout.strip():
        return "it has uncommitted changes"
    return None


# --- a task becomes an entry -------------------------------------------------------------------


def _free_name(title: str, task_id: int, taken: set[str]) -> str:
    base = slugify(title) or f"task-{task_id}"
    name, suffix = base, 1
    while name in taken:
        suffix += 1
        name = f"{base}-{suffix}"
    return name


def entry_text(task: dict, today: date) -> str:
    """The content of the entry file for an unmanaged task: front matter from its labels, then the description."""
    labels = {label["title"] for label in task.get("labels") or []}
    front = []
    for status in ("in progress", "deferred"):
        if status in labels:
            front.append(f"status: {status} {today.isoformat()}")
            break
    stages = sorted(int(match.group(1)) for label in labels if (match := _STAGE_LABEL_RE.match(label)))
    if stages:
        front.append(f"stage: {stages[0]}")
    body = "\n".join(
        line for line in html_to_markdown(task.get("description") or "").splitlines() if not _MARKER_LINE_RE.match(line.strip())
    ).strip()
    parts = ["---", *front, "---"] if front else []
    parts += [f"# {task['title'].strip()}", ""]
    if body:
        parts += [body]
    return "\n".join(parts).rstrip() + "\n"


# --- the pull ----------------------------------------------------------------------------------


def pull(
    client: VikunjaClient, todo_dir: Path, plans_dir: Path | None, project_title: str, apply: bool = False, today: date | None = None
) -> PullReport:
    today = today or date.today()
    report = PullReport()
    sync_report = Report()
    project = find_project(client, project_title, sync_report)
    report.warnings.extend(sync_report.warnings)
    if project is None:
        report.warnings.append(f"project '{project_title}' does not exist in Vikunja; nothing to pull")
        return report
    tasks = list_tasks(client, project)
    data = parse_todo(todo_dir, plans_dir)
    stage_titles = {stage["number"]: stage["title"] for stage in data["stages"]}
    entries = {entry["id"]: entry for entry in data["entries"]}

    for task in tasks:
        entry_id = marker_of(task)
        entry = entries.get(entry_id) if entry_id else None
        if entry is None or not task.get("done"):
            continue
        report.closed += 1
        report.lines.append(f"closed   {entry['id']} ({entry['file']}): closed in Vikunja, entry still in the repository")
        if not apply:
            continue
        blocker = deletion_blocker(todo_dir, entry["file"])
        if blocker:
            report.warnings.append(f"kept {entry['file']}: {blocker}")
            continue
        (todo_dir / entry["file"]).unlink()
        report.lines.append(f"deleted  {entry['file']}")

    taken = {path.stem for path in todo_dir.glob("*.md")} | {Path(README_FILE).stem, Path(ROADMAP_FILE).stem} | set(entries)
    for task in tasks:
        if marker_of(task) is not None or task.get("done"):
            continue
        report.adopted += 1
        name = _free_name(task["title"], task["id"], taken)
        report.lines.append(f"created  {task['title']!r} -> {name}.md")
        if not apply:
            continue
        taken.add(name)
        target = todo_dir / f"{name}.md"
        target.write_text(entry_text(task, today), encoding="utf-8")
        try:
            adopted = next(entry for entry in parse_todo(todo_dir, plans_dir)["entries"] if entry["file"] == target.name)
        except ParseError as error:
            target.unlink()
            report.warnings.append(f"task {task['id']} '{task['title']}' not adopted: {error}")
            report.adopted -= 1
            continue
        description = render_description(adopted, stage_titles.get(adopted["stage"]))
        client.request("POST", f"/tasks/{task['id']}", {**task, "description": description})
    return report
