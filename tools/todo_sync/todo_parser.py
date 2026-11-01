#!/usr/bin/env python3
"""
Parse TODO.md into plain data.

The entry id is the slug of its "## " heading. Optional fields sit on their own lines:
  **Status:**  open | in progress | deferred, then a date YYYY-MM-DD and free text
  **Stage:**   the roadmap number
  **Plan:**    a path under .claude/plans/ (its "- [ ]" / "- [x]" lines give the progress)

An entry without these fields is open, has no stage and no plan. The legacy prose form
("in progress since 2026-11-01, plan in `.claude/plans/x.md`") is read too.

Only reads files. Python 3.10+, standard library only.
"""

import re
from datetime import date
from pathlib import Path

STATUSES = ("open", "in progress", "deferred")

ROADMAP_HEADING = "roadmap"
SLUG_MAX_LENGTH = 60
SUMMARY_MAX_LENGTH = 300

_HEADING_RE = re.compile(r"^## (.+?)\s*$")
_FENCE_RE = re.compile(r"^\s*(```|~~~)")
_FIELD_RE = re.compile(r"^\*\*(Status|Stage|Plan):\*\*\s*(.*?)\s*$")
_STAGE_RE = re.compile(r"^(\d+)\.\s+\*\*(.+?)\*\*")
_STATUS_KEYWORD_RE = re.compile(r"^(open|in progress|deferred)\b", re.IGNORECASE)
_DATE_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
_PLAN_PATH_RE = re.compile(r"\.claude/plans/[^\s`'\")]+\.md")
_CHECKBOX_RE = re.compile(r"^\s*[-*]\s+\[([ xX])\]")
_SUMMARY_LABEL_RE = re.compile(r"^\*\*What is (?:missing|wrong)(?::\*\*|\*\*)\s*(.*)$")


class ParseError(Exception):
    """TODO.md cannot be turned into entries (a message for the person who edits it)."""


def slugify(title: str) -> str:
    """Lowercase, runs of non-word characters become '-', cut at SLUG_MAX_LENGTH on a word boundary."""
    slug = re.sub(r"[\W_]+", "-", title.lower()).strip("-")
    if len(slug) > SLUG_MAX_LENGTH:
        cut = slug[:SLUG_MAX_LENGTH]
        if slug[SLUG_MAX_LENGTH] != "-" and "-" in cut:
            cut = cut.rsplit("-", 1)[0]
        slug = cut.strip("-")
    return slug


def _plain_text(markdown: str) -> str:
    """Strip the inline markdown that a one-line summary does not need."""
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", markdown)
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = text.replace("`", "")
    return re.sub(r"\s+", " ", text).strip()


def _truncate(text: str) -> str:
    if len(text) <= SUMMARY_MAX_LENGTH:
        return text
    cut = text[:SUMMARY_MAX_LENGTH].rsplit(" ", 1)[0]
    return cut.rstrip(" ,;:") + "…"


def _extract_summary(body: list[str]) -> str:
    """The lead of the 'What is missing / wrong' paragraph, as plain text."""
    for index, line in enumerate(body):
        match = _SUMMARY_LABEL_RE.match(line)
        if not match:
            continue
        rest = re.sub(r"^\s*\([^)]*\)", "", match.group(1)).lstrip(" :")
        if not rest.strip():
            rest = next((candidate for candidate in body[index + 1 :] if candidate.strip()), "")
            rest = re.sub(r"^\s*(?:\d+\.|[-*])\s+", "", rest)
        text = _truncate(_plain_text(rest))
        return text[:1].upper() + text[1:]
    return ""


def _parse_status(text: str, title: str) -> tuple[str, str | None]:
    match = _STATUS_KEYWORD_RE.match(text)
    if not match:
        raise ParseError(
            f"entry '{title}': **Status:** must start with one of {', '.join(STATUSES)}, got '{text}'"
        )
    status = match.group(1).lower()
    found = _DATE_RE.search(text)
    if not found:
        return status, None
    try:
        return status, date.fromisoformat(found.group(0)).isoformat()
    except ValueError:
        raise ParseError(f"entry '{title}': '{found.group(0)}' in **Status:** is not a date") from None


def _plan_progress(plan: str | None, root: Path, title: str, warnings: list[str]) -> dict | None:
    if plan is None:
        return None
    plan_file = root / plan
    if not plan_file.is_file():
        warnings.append(f"entry '{title}': plan file '{plan}' not found")
        return None
    done = total = 0
    for line in plan_file.read_text(encoding="utf-8", errors="replace").splitlines():
        match = _CHECKBOX_RE.match(line)
        if match:
            total += 1
            if match.group(1) != " ":
                done += 1
    return {"done": done, "total": total} if total else None


def _parse_stages(lines: list[str]) -> list[dict]:
    stages = []
    for line in lines:
        match = _STAGE_RE.match(line)
        if match:
            stages.append({"number": int(match.group(1)), "title": match.group(2).rstrip(": ")})
    return stages


def _split_sections(text: str) -> list[tuple[str, int, list[str]]]:
    """(heading, 1-based heading line, body lines) per '## ' section; headings inside code fences are ignored."""
    sections: list[tuple[str, int, list[str]]] = []
    in_fence = False
    for number, line in enumerate(text.splitlines(), start=1):
        if _FENCE_RE.match(line):
            in_fence = not in_fence
        heading = None if in_fence else _HEADING_RE.match(line)
        if heading:
            sections.append((heading.group(1), number, []))
        elif sections:
            sections[-1][2].append(line)
    return sections


def _parse_entry(title: str, line: int, body: list[str], root: Path, warnings: list[str]) -> dict:
    fields: dict[str, str] = {}
    in_fence = False
    for text_line in body:
        if _FENCE_RE.match(text_line):
            in_fence = not in_fence
        match = None if in_fence else _FIELD_RE.match(text_line)
        if match:
            fields.setdefault(match.group(1), match.group(2))

    status, status_date = "open", None
    if "Status" in fields:
        status, status_date = _parse_status(fields["Status"], title)

    stage = None
    if "Stage" in fields:
        if fields["Stage"].isdigit():
            stage = int(fields["Stage"])
        else:
            warnings.append(f"entry '{title}': **Stage:** '{fields['Stage']}' is not a number, ignored")

    plan_match = _PLAN_PATH_RE.search(fields.get("Plan") or fields.get("Status", ""))
    plan = plan_match.group(0) if plan_match else None
    if fields.get("Plan") and not plan:
        warnings.append(f"entry '{title}': **Plan:** '{fields['Plan']}' is not a path under .claude/plans/")

    return {
        "id": slugify(title),
        "title": title,
        "status": status,
        "date": status_date,
        "stage": stage,
        "plan": plan,
        "planProgress": _plan_progress(plan, root, title, warnings),
        "summary": _extract_summary(body),
        "line": line,
    }


def parse_todo(todo_path: Path, root: Path | None = None) -> dict:
    """
    Returns {"stages": [...], "entries": [...], "warnings": [...]}.
    Plan paths are resolved against root (default: the folder of the TODO file).
    Raises ParseError on an unreadable file, a bad status keyword or date, or a duplicate id.
    """
    root = root or todo_path.parent
    try:
        text = todo_path.read_text(encoding="utf-8")
    except OSError as error:
        raise ParseError(f"cannot read {todo_path}: {error}") from error

    warnings: list[str] = []
    stages: list[dict] = []
    entries: list[dict] = []
    seen_ids: dict[str, str] = {}

    for title, line, body in _split_sections(text):
        if title.strip().lower() == ROADMAP_HEADING:
            stages = _parse_stages(body)
            continue
        entry = _parse_entry(title, line, body, root, warnings)
        if not entry["id"]:
            raise ParseError(f"entry '{title}' (line {line}): the heading gives an empty id")
        if entry["id"] in seen_ids:
            raise ParseError(
                f"entries '{seen_ids[entry['id']]}' and '{title}' have the same id '{entry['id']}'"
            )
        seen_ids[entry["id"]] = title
        entries.append(entry)

    return {"stages": stages, "entries": entries, "warnings": warnings}
