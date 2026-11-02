#!/usr/bin/env python3
"""
Parse the todo/ folder into plain data.

One markdown file per entry. The entry id is the file name without ".md" (or the `id:` field),
the title is the first "# " line. Optional front matter (`key: value` lines between two `---`):
  status:  open | in progress | deferred, then a date YYYY-MM-DD and free text
  stage:   the roadmap number
  plan:    the NAME of a plan file in the local plans folder (its "- [ ]" / "- [x]" lines give the progress)
  id:      only to keep a task that was exported under another id

An entry without front matter is open, has no stage and no plan. README.md and roadmap.md are not
entries; the numbered list of roadmap.md gives the stages.

Only reads files. Python 3.10+, standard library only.
"""

import re
from datetime import date
from pathlib import Path

STATUSES = ("open", "in progress", "deferred")

README_FILE = "README.md"
ROADMAP_FILE = "roadmap.md"
FRONT_MATTER_DELIMITER = "---"
FRONT_MATTER_KEYS = ("status", "stage", "plan", "id")
SLUG_MAX_LENGTH = 60
SUMMARY_MAX_LENGTH = 300

_FENCE_RE = re.compile(r"^\s*(```|~~~)")
_TITLE_RE = re.compile(r"^# (.+?)\s*$")
_STAGE_RE = re.compile(r"^(\d+)\.\s+\*\*(.+?)\*\*")
_STATUS_KEYWORD_RE = re.compile(r"^(" + "|".join(re.escape(status) for status in STATUSES) + r")\b", re.IGNORECASE)
_DATE_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
_PLAN_NAME_RE = re.compile(r"^[^/\\\s`'\"]+\.md$")
_ID_RE = re.compile(r"^[\w-]+$")
_CHECKBOX_RE = re.compile(r"^\s*[-*]\s+\[([ xX])\]\s*(.*?)\s*$")
_HEADING_RE = re.compile(r"^#{1,6}\s+(.+?)\s*$")
_SUMMARY_LABEL_RE = re.compile(r"^\*\*What is (?:missing|wrong)(?::\*\*|\*\*)\s*(.*)$")


class ParseError(Exception):
    """An entry file cannot be turned into an entry (a message for the person who edits it)."""


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


def _parse_status(text: str, where: str) -> tuple[str, str | None]:
    match = _STATUS_KEYWORD_RE.match(text)
    if not match:
        raise ParseError(f"{where}: status must start with one of {', '.join(STATUSES)}, got '{text}'")
    status = match.group(1).lower()
    found = _DATE_RE.search(text)
    if not found:
        return status, None
    try:
        return status, date.fromisoformat(found.group(0)).isoformat()
    except ValueError:
        raise ParseError(f"{where}: '{found.group(0)}' in status is not a date") from None


def _read_checklist(plan_file: Path, plan: str) -> dict | None:
    """The plan's title and, under each heading that has checkboxes, those checkboxes (nesting is flattened)."""
    title = None
    sections: list[dict] = []
    in_fence = False
    for line in plan_file.read_text(encoding="utf-8", errors="replace").splitlines():
        if _FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        heading = _HEADING_RE.match(line)
        if heading:
            if title is None and line.startswith("# "):
                title = heading.group(1)
            sections.append({"heading": heading.group(1), "items": []})
            continue
        box = _CHECKBOX_RE.match(line)
        if box:
            if not sections:
                sections.append({"heading": None, "items": []})
            sections[-1]["items"].append({"done": box.group(1) != " ", "text": box.group(2)})
    sections = [section for section in sections if section["items"]]
    return {"title": title or plan, "sections": sections} if sections else None


def checklist_progress(checklist: dict | None) -> dict | None:
    if checklist is None:
        return None
    items = [item for section in checklist["sections"] for item in section["items"]]
    return {"done": sum(1 for item in items if item["done"]), "total": len(items)}


def _read_plan(plan: str | None, plans_dir: Path | None, where: str, notes: list[str], warnings: list[str]) -> tuple[bool, dict | None]:
    """(is the plan file here, its checklist). A plan that is not on this machine is a note; a finished one is a warning."""
    if plan is None:
        return False, None
    plan_file = plans_dir / plan if plans_dir else None
    if plan_file is None or not plan_file.is_file():
        notes.append(f"{where}: plan '{plan}' is not in {plans_dir}; the tracker copy, if any, stays as it is")
        return False, None
    checklist = _read_checklist(plan_file, plan)
    progress = checklist_progress(checklist)
    if progress and progress["done"] == progress["total"]:
        warnings.append(
            f"{where}: plan '{plan}' is complete ({progress['done']}/{progress['total']}): delete the file and the plan: line "
            "(the copy leaves the tracker at the next push), and delete the entry if the problem is solved"
        )
    return True, checklist


def _parse_stages(lines: list[str]) -> list[dict]:
    stages = []
    for line in lines:
        match = _STAGE_RE.match(line)
        if match:
            stages.append({"number": int(match.group(1)), "title": match.group(2).rstrip(": ")})
    return stages


def _read_front_matter(lines: list[str], where: str) -> tuple[dict[str, str], list[str]]:
    """(fields, the lines after the front matter). No front matter gives no fields."""
    if not lines or lines[0].strip() != FRONT_MATTER_DELIMITER:
        return {}, lines
    fields: dict[str, str] = {}
    for index in range(1, len(lines)):
        line = lines[index]
        if line.strip() == FRONT_MATTER_DELIMITER:
            return fields, lines[index + 1 :]
        if not line.strip():
            continue
        key, separator, value = line.partition(":")
        key = key.strip()
        if not separator:
            raise ParseError(f"{where}: front matter line '{line}' is not 'key: value'")
        if key not in FRONT_MATTER_KEYS:
            raise ParseError(f"{where}: unknown front matter key '{key}' (known: {', '.join(FRONT_MATTER_KEYS)})")
        if key in fields:
            raise ParseError(f"{where}: front matter key '{key}' is repeated")
        fields[key] = value.strip()
    raise ParseError(f"{where}: front matter is not closed by '{FRONT_MATTER_DELIMITER}'")


def _split_title(lines: list[str], where: str) -> tuple[str, list[str]]:
    """The first '# ' line outside code fences and the lines after it."""
    in_fence = False
    for index, line in enumerate(lines):
        if _FENCE_RE.match(line):
            in_fence = not in_fence
        match = None if in_fence else _TITLE_RE.match(line)
        if match:
            return match.group(1), lines[index + 1 :]
        if line.strip() and not in_fence:
            break
    raise ParseError(f"{where}: the entry must start with a '# Title' line")


def _parse_entry(path: Path, plans_dir: Path | None, notes: list[str], warnings: list[str]) -> dict:
    where = path.name
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ParseError(f"cannot read {path}: {error}") from error

    fields, lines = _read_front_matter(lines, where)
    title, body = _split_title(lines, where)

    status, status_date = "open", None
    if "status" in fields:
        status, status_date = _parse_status(fields["status"], where)

    stage = None
    if "stage" in fields:
        if fields["stage"].isdigit():
            stage = int(fields["stage"])
        else:
            warnings.append(f"{where}: stage '{fields['stage']}' is not a number, ignored")

    plan = fields.get("plan") or None
    if plan is not None and not _PLAN_NAME_RE.match(plan):
        raise ParseError(f"{where}: plan '{plan}' must be a bare file name like 'my-plan.md', no folder")

    plan_found, checklist = _read_plan(plan, plans_dir, where, notes, warnings)

    entry_id = fields.get("id") or path.stem
    if not _ID_RE.match(entry_id):
        raise ParseError(f"{where}: id '{entry_id}' may hold only letters, digits, '_' and '-'")

    return {
        "id": entry_id,
        "title": title,
        "status": status,
        "date": status_date,
        "stage": stage,
        "plan": plan,
        "planFound": plan_found,
        "planProgress": checklist_progress(checklist),
        "planChecklist": checklist,
        "summary": _extract_summary(body),
        "file": where,
    }


def parse_todo(todo_dir: Path, plans_dir: Path | None = None) -> dict:
    """
    Returns {"stages": [...], "entries": [...], "warnings": [...], "notes": [...]}.
    Plan names are looked up in plans_dir; without it no plan is read. A plan that is not found is a
    note (it lives on another machine), a plan with every box ticked is a warning (it is time to delete it).
    Raises ParseError on an unreadable folder, a malformed entry file, a bad status keyword or
    date, a bad plan name or a duplicate id.
    """
    if not todo_dir.is_dir():
        raise ParseError(f"{todo_dir} is not a folder")

    warnings: list[str] = []
    notes: list[str] = []
    stages: list[dict] = []
    entries: list[dict] = []
    seen_ids: dict[str, str] = {}

    roadmap = todo_dir / ROADMAP_FILE
    if roadmap.is_file():
        stages = _parse_stages(roadmap.read_text(encoding="utf-8").splitlines())

    for path in sorted(todo_dir.glob("*.md")):
        if path.name in (README_FILE, ROADMAP_FILE):
            continue
        entry = _parse_entry(path, plans_dir, notes, warnings)
        if entry["id"] in seen_ids:
            raise ParseError(f"{seen_ids[entry['id']]} and {path.name} have the same id '{entry['id']}'")
        seen_ids[entry["id"]] = path.name
        entries.append(entry)

    return {"stages": stages, "entries": entries, "warnings": warnings, "notes": notes}
