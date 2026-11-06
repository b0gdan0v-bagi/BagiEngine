#!/usr/bin/env python3
"""
The text side of the todo store, shared by the CLI (todo.py) and the service (service/).

- The old entry-file format (front matter, `# Title`, body) and roadmap.md, read only by `todo.py import`.
- A plan's checklist (`- [ ]` / `- [x]` outside code fences) and its progress.
- The one-line summary of an entry (the `**What is missing**` / `**What is wrong**` paragraph).
- Markdown to HTML (the description of a Vikunja task) and back (an edit a person made there).

Python 3.10+, standard library only: the CLI has no dependencies.
"""

import re
from datetime import date
from html import escape
from html.parser import HTMLParser

STATUSES = ("open", "in progress", "review", "deferred", "closed")
FILE_STATUSES = ("open", "in progress", "review", "deferred")

README_FILE = "README.md"
ROADMAP_FILE = "roadmap.md"
FRONT_MATTER_DELIMITER = "---"
FRONT_MATTER_KEYS = ("status", "stage", "plan", "id")
SLUG_MAX_LENGTH = 60
SUMMARY_MAX_LENGTH = 300

ID_RE = re.compile(r"^[\w-]+$")
PLAN_NAME_RE = re.compile(r"^[^/\\\s`'\"]+\.md$")
_FENCE_RE = re.compile(r"^\s*(```|~~~)")
_TITLE_RE = re.compile(r"^# (.+?)\s*$")
_STAGE_RE = re.compile(r"^(\d+)\.\s+\*\*(.+?)\*\*")
_STATUS_KEYWORD_RE = re.compile(r"^(" + "|".join(re.escape(status) for status in FILE_STATUSES) + r")\b", re.IGNORECASE)
_DATE_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
_CHECKBOX_RE = re.compile(r"^\s*[-*]\s+\[([ xX])\]\s*(.*?)\s*$")
_HEADING_RE = re.compile(r"^#{1,6}\s+(.+?)\s*$")
_SUMMARY_LABEL_RE = re.compile(r"^\*\*What is (?:missing|wrong)(?::\*\*|\*\*)\s*(.*)$")


class ParseError(Exception):
    """A text cannot be turned into an entry (a message for the person who wrote it)."""


def slugify(title: str) -> str:
    """Lowercase, runs of non-word characters become '-', cut at SLUG_MAX_LENGTH on a word boundary."""
    slug = re.sub(r"[\W_]+", "-", title.lower()).strip("-")
    if len(slug) > SLUG_MAX_LENGTH:
        cut = slug[:SLUG_MAX_LENGTH]
        if slug[SLUG_MAX_LENGTH] != "-" and "-" in cut:
            cut = cut.rsplit("-", 1)[0]
        slug = cut.strip("-")
    return slug


# --- summary and checklist ---------------------------------------------------------------------


def _plain_text(markdown: str) -> str:
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", markdown)
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = text.replace("`", "")
    return re.sub(r"\s+", " ", text).strip()


def _truncate(text: str, limit: int = SUMMARY_MAX_LENGTH) -> str:
    if len(text) <= limit:
        return text
    cut = text[:limit].rsplit(" ", 1)[0]
    return cut.rstrip(" ,;:") + "…"


def extract_summary(body: str) -> str:
    """The lead of the 'What is missing / wrong' paragraph, as plain text ("" when there is none)."""
    lines = body.splitlines()
    for index, line in enumerate(lines):
        match = _SUMMARY_LABEL_RE.match(line)
        if not match:
            continue
        rest = re.sub(r"^\s*\([^)]*\)", "", match.group(1)).lstrip(" :")
        if not rest.strip():
            rest = next((candidate for candidate in lines[index + 1 :] if candidate.strip()), "")
            rest = re.sub(r"^\s*(?:\d+\.|[-*])\s+", "", rest)
        text = _truncate(_plain_text(rest))
        return text[:1].upper() + text[1:]
    return ""


def read_checklist(text: str, fallback_title: str) -> dict | None:
    """The plan's title and, under each heading that has checkboxes, those checkboxes (nesting is flattened)."""
    title = None
    sections: list[dict] = []
    in_fence = False
    for line in text.splitlines():
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
    return {"title": title or fallback_title, "sections": sections} if sections else None


def checklist_progress(checklist: dict | None) -> dict | None:
    if checklist is None:
        return None
    items = [item for section in checklist["sections"] for item in section["items"]]
    return {"done": sum(1 for item in items if item["done"]), "total": len(items)}


# --- the old entry files (todo.py import) ------------------------------------------------------


def parse_stages(text: str) -> list[dict]:
    """The numbered `1. **Title**` lines of roadmap.md."""
    stages = []
    for line in text.splitlines():
        match = _STAGE_RE.match(line)
        if match:
            stages.append({"number": int(match.group(1)), "title": match.group(2).rstrip(": ")})
    return stages


def _read_front_matter(lines: list[str], where: str) -> tuple[dict[str, str], list[str]]:
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


_LEADING_DATE_RE = re.compile(r"^[,;]?\s*(?:since\s+|on\s+)?(\d{4}-\d{2}-\d{2})[.,:;]?\s*")


def parse_status(text: str, where: str) -> tuple[str, str | None, str]:
    """
    (keyword, date or None, the free text). The date is the first YYYY-MM-DD anywhere in the text, as the
    old parser read it; a leading `<date>`, `since <date>` or `, <date>.` is not repeated in the free text.
    """
    match = _STATUS_KEYWORD_RE.match(text)
    if not match:
        raise ParseError(f"{where}: status must start with one of {', '.join(FILE_STATUSES)}, got '{text}'")
    status = match.group(1).lower()
    rest = text[match.end() :].strip()
    found = _DATE_RE.search(rest)
    if not found:
        return status, None, rest
    try:
        status_date = date.fromisoformat(found.group(0)).isoformat()
    except ValueError:
        raise ParseError(f"{where}: '{found.group(0)}' in status is not a date") from None
    leading = _LEADING_DATE_RE.match(rest)
    if leading and leading.group(1) == found.group(0):
        rest = rest[leading.end() :].strip()
    return status, status_date, rest


_BRANCH_RE = re.compile(r"^branch (todo/[\w./-]+?)(?=[:,]|\s|$)[:,]?\s*")


def split_branch(note: str) -> tuple[str | None, str]:
    """`branch todo/x: text` of an old status line -> ("todo/x", "text")."""
    match = _BRANCH_RE.match(note)
    if not match:
        return None, note
    return match.group(1), note[match.end() :].strip()


def parse_entry_text(text: str, file_name: str) -> dict:
    """One old entry file as a record of the store: id, title, body, status, date, note, branch, stage, plan."""
    lines = text.splitlines()
    fields, lines = _read_front_matter(lines, file_name)
    title, body = _split_title(lines, file_name)
    status, status_date, note = "open", None, ""
    if "status" in fields:
        status, status_date, note = parse_status(fields["status"], file_name)
    branch, note = split_branch(note)
    stage = int(fields["stage"]) if fields.get("stage", "").isdigit() else None
    plan = fields.get("plan") or None
    if plan is not None and not PLAN_NAME_RE.match(plan):
        raise ParseError(f"{file_name}: plan '{plan}' must be a bare file name like 'my-plan.md'")
    entry_id = fields.get("id") or file_name.removesuffix(".md")
    if not ID_RE.match(entry_id):
        raise ParseError(f"{file_name}: id '{entry_id}' may hold only letters, digits, '_' and '-'")
    return {
        "id": entry_id,
        "title": title,
        "body": "\n".join(body).strip("\n") + "\n",
        "status": status,
        "statusDate": status_date,
        "statusNote": note,
        "branch": branch,
        "stage": stage,
        "plan": plan,
    }


# --- markdown to HTML (the description of a task) ----------------------------------------------

_LIST_ITEM_RE = re.compile(r"^(\s*)([-*]|\d+\.)\s+(.*)$")
_TASK_BOX_RE = re.compile(r"^\[([ xX])\]\s*(.*)$")


def inline_html(text: str) -> str:
    """Escaped text with `code`, **bold**, *italic* and [links](url) kept."""
    code_spans: list[str] = []

    def keep_code(match: re.Match) -> str:
        code_spans.append(f"<code>{escape(match.group(1))}</code>")
        return f"\x00{len(code_spans) - 1}\x00"

    text = re.sub(r"`([^`]+)`", keep_code, text)
    html = escape(text, quote=False)
    html = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: f'<a href="{escape(m.group(2))}">{m.group(1)}</a>', html)
    html = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", html)
    html = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", html)
    return re.sub(r"\x00(\d+)\x00", lambda m: code_spans[int(m.group(1))], html)


def markdown_to_html(markdown: str) -> str:
    """
    The subset of markdown the entries use: headings, paragraphs, bullet and numbered lists (nested by
    indentation, `[ ]` boxes as the editor's task list), code fences, `---` rules and inline marks.
    Tables and quotes come out as paragraphs. Plain <p> and list blocks, so the Vikunja editor keeps them.
    """
    out: list[str] = []
    paragraph: list[str] = []
    lists: list[tuple[int, str]] = []  # (indent, "ul" | "ol") per open list
    open_items: list[bool] = []
    lines = markdown.splitlines()
    index = 0

    def flush_paragraph() -> None:
        if paragraph:
            out.append(f"<p>{inline_html(' '.join(part.strip() for part in paragraph))}</p>")
            paragraph.clear()

    def close_lists(to_depth: int = 0) -> None:
        while len(lists) > to_depth:
            if open_items and open_items[-1]:
                out.append("</li>")
            open_items.pop()
            out.append(f"</{lists.pop()[1]}>")

    while index < len(lines):
        line = lines[index]
        if _FENCE_RE.match(line):
            flush_paragraph()
            close_lists()
            code: list[str] = []
            index += 1
            while index < len(lines) and not _FENCE_RE.match(lines[index]):
                code.append(lines[index])
                index += 1
            out.append(f"<pre><code>{escape(chr(10).join(code))}</code></pre>")
            index += 1
            continue
        if not line.strip():
            flush_paragraph()
            index += 1
            continue
        heading = re.match(r"^(#{1,6})\s+(.+?)\s*$", line)
        if heading:
            flush_paragraph()
            close_lists()
            level = len(heading.group(1))
            out.append(f"<h{level}>{inline_html(heading.group(2))}</h{level}>")
            index += 1
            continue
        if re.match(r"^\s*(-{3,}|\*{3,})\s*$", line):
            flush_paragraph()
            close_lists()
            out.append("<hr>")
            index += 1
            continue
        item = _LIST_ITEM_RE.match(line)
        if item:
            flush_paragraph()
            indent = len(item.group(1).expandtabs(4))
            kind = "ol" if item.group(2)[0].isdigit() else "ul"
            text = item.group(3)
            box = _TASK_BOX_RE.match(text) if kind == "ul" else None
            while lists and lists[-1][0] > indent:
                close_lists(len(lists) - 1)
            if lists and lists[-1][0] == indent and lists[-1][1] != kind:
                close_lists(len(lists) - 1)
            if not lists or lists[-1][0] < indent:
                task_list = ' data-type="taskList"' if box else ""
                number = int(item.group(2)[:-1]) if kind == "ol" else 1
                start = f' start="{number}"' if number != 1 else ""
                out.append(f"<{kind}{task_list}{start}>")
                lists.append((indent, kind))
                open_items.append(False)
            elif open_items[-1]:
                out.append("</li>")
            if box:
                done = box.group(1) != " "
                checked = ' checked="checked"' if done else ""
                out.append(
                    f'<li data-checked="{"true" if done else "false"}" data-type="taskItem"><label><input type="checkbox"{checked}>'
                    f"<span></span></label><div><p>{inline_html(box.group(2))}</p></div>"
                )
            else:
                out.append(f"<li><p>{inline_html(text)}</p>")
            open_items[-1] = True
            index += 1
            continue
        if lists and line.startswith(" ") and not paragraph:
            # a continuation line of a list item
            out.append(f"<p>{inline_html(line.strip())}</p>")
            index += 1
            continue
        close_lists()
        paragraph.append(line)
        index += 1
    flush_paragraph()
    close_lists()
    return "".join(out)


# --- HTML to markdown (an edit made in Vikunja) ------------------------------------------------

_BLOCK_TAGS = {"p", "div", "ul", "ol", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6", "pre"}
_HEADING_PREFIX = {f"h{level}": "#" * level + " " for level in range(1, 7)}
_BLANK_RUNS_RE = re.compile(r"\n{3,}")


class _HtmlToMarkdown(HTMLParser):
    """The small subset of HTML that the Vikunja editor (and markdown_to_html) produces, as markdown."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self._out: list[str] = []
        self._lists: list[str] = []
        self._counters: list[int] = []
        self._links: list[str | None] = []
        self._in_pre = False
        self._in_label = 0

    def _text(self) -> str:
        return "".join(self._out)

    def _block_break(self) -> None:
        text = self._text()
        if not text or text.endswith("\n\n"):
            return
        # inside a list item a paragraph is a line of the item, not a new block
        if self._lists and text.endswith(("- ", "[ ] ", "[x] ", ". ")):
            return
        self._out.append("\n" if text.endswith("\n") else ("\n" if self._lists else "\n\n"))

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = dict(attrs)
        if tag == "label":
            self._in_label += 1
        elif tag in ("ul", "ol"):
            if self._text() and not self._text().endswith("\n"):
                self._out.append("\n")
            if not self._lists:
                self._block_break()
            self._lists.append(tag)
            start = attributes.get("start") or "1"
            self._counters.append(int(start) - 1 if start.isdigit() else 0)
        elif tag == "li":
            if self._text() and not self._text().endswith("\n"):
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
        elif tag == "hr":
            self._block_break()
            self._out.append("---")
            self._block_break()
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
        if tag == "label":
            self._in_label = max(self._in_label - 1, 0)
        elif tag in ("ul", "ol"):
            if self._lists:
                self._lists.pop()
                self._counters.pop()
            if not self._text().endswith("\n"):
                self._out.append("\n")
            if not self._lists:
                self._block_break()
        elif tag == "li":
            if not self._text().endswith("\n"):
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
        if self._in_label:
            return
        if self._in_pre:
            self._out.append(data)
            return
        text = re.sub(r"\s+", " ", data)
        if self._text().endswith(("\n", "- ", "] ", ". ")) and not self._text().endswith("\n\n"):
            text = text.lstrip()
        if text.strip() or not self._text().endswith("\n"):
            self._out.append(text)

    def text(self) -> str:
        lines = [line.rstrip() for line in self._text().splitlines()]
        return _BLANK_RUNS_RE.sub("\n\n", "\n".join(lines)).strip()


def html_to_markdown(html: str) -> str:
    """The description of a task as markdown text. A description that is plain text comes back as it is."""
    parser = _HtmlToMarkdown()
    parser.feed(html or "")
    parser.close()
    return parser.text()
