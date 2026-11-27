#!/usr/bin/env python3
"""Check the pages of docs/guide/ against the repository: every snippet, command, link and picture.

    python3 tools/docs/check_guide.py [--root DIR]

Exit 0 when clean (also when docs/guide/ has no page: a game that carries the engine), 1 with one line per
finding, `<page>:<line>: <message>`. Standard library only, Python 3.10+. The rules are in tools/docs/CLAUDE.md.
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import sys
from pathlib import Path
from typing import NamedTuple

GUIDE_DIR = "docs/guide"
INDEX = "README.md"
MANIFEST = "tools/docs/guide_shots.json"
# Names that are not part of the tree a reader gets by cloning: a command or a path under them is not checked.
NOT_IN_TREE = {"build", ".venv"}
PLACEHOLDER_MARKS = ("<", "*", "{", "...", "$")

MARKER_RE = re.compile(r"^<!--\s*(.*?)\s*-->$")
LINK_RE = re.compile(r"!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
CODE_SPAN_RE = re.compile(r"`([^`\n]+)`")
URL_SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")


class Finding(NamedTuple):
    page: str
    line: int
    message: str

    def __str__(self) -> str:
        return f"{self.page}:{self.line}: {self.message}"


class Fence(NamedTuple):
    line: int  # 1-based line of the opening fence
    body: list[str]
    marker_line: str | None  # the nearest non-blank line before the fence


def scan_fences(lines: list[str]) -> tuple[list[Fence], set[int]]:
    """The fenced blocks of a page and the 0-based indexes of the lines inside or on a fence."""
    fences: list[Fence] = []
    covered: set[int] = set()
    index = 0
    while index < len(lines):
        stripped = lines[index].lstrip()
        opener = re.match(r"(`{3,}|~{3,})", stripped)
        if not opener:
            index += 1
            continue
        mark = opener.group(1)
        previous = next((lines[i].strip() for i in range(index - 1, -1, -1) if lines[i].strip()), None)
        end = index + 1
        while end < len(lines):
            close = lines[end].strip()
            if close.startswith(mark[0] * len(mark)) and not close.strip(mark[0]):
                break
            end += 1
        fences.append(Fence(index + 1, lines[index + 1 : end], previous))
        covered.update(range(index, min(end, len(lines) - 1) + 1))
        index = end + 1
    return fences, covered


def top_level_entries(root: Path) -> set[str]:
    return {child.name for child in root.iterdir()} - NOT_IN_TREE


def preset_names(root: Path) -> set[str]:
    presets = root / "CMakePresets.json"
    if not presets.is_file():
        return set()
    data = json.loads(presets.read_text(encoding="utf-8"))
    names: set[str] = set()
    for key in ("configurePresets", "buildPresets", "testPresets"):
        names.update(entry["name"] for entry in data.get(key, []) if "name" in entry)
    return names


def is_placeholder(token: str) -> bool:
    return any(mark in token for mark in PLACEHOLDER_MARKS)


def stripped_nonblank(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines() if line.strip()]


def check_excerpt(root: Path, relative: str, fence: Fence, page: str) -> list[Finding]:
    source = root / relative
    if not source.is_file():
        return [Finding(page, fence.line, f"source file does not exist: {relative}")]
    haystack = stripped_nonblank(source.read_text(encoding="utf-8", errors="replace"))
    segments: list[list[str]] = [[]]
    for line in fence.body:
        if line.strip() == "...":
            segments.append([])
        elif line.strip():
            segments[-1].append(line.strip())
    position = 0
    for number, segment in enumerate(s for s in segments if s):
        found = next(
            (
                start
                for start in range(position, len(haystack) - len(segment) + 1)
                if haystack[start : start + len(segment)] == segment
            ),
            None,
        )
        if found is None:
            return [
                Finding(
                    page,
                    fence.line,
                    f"excerpt segment {number + 1} is not in {relative} (or is out of order): {segment[0]!r}",
                )
            ]
        position = found + len(segment)
    return []


def check_command(root: Path, fence: Fence, page: str, entries: set[str], presets: set[str]) -> list[Finding]:
    text = "\n".join(fence.body)
    try:
        tokens = shlex.split(text, comments=True)
    except ValueError:
        tokens = text.split()
    findings: list[Finding] = []
    for position, token in enumerate(tokens):
        value = token.split("=", 1)[1] if token.startswith("--") and "=" in token else token
        if value.startswith("./"):
            value = value[2:]
        flag = token.split("=", 1)[0] if token.startswith("--") and "=" in token else None
        if flag in ("--preset", "--build") or (flag is None and token in ("--preset", "--build", "--test-dir")):
            if flag is None:
                if position + 1 >= len(tokens):
                    continue
                value = tokens[position + 1]
                flag = token
            findings.extend(check_preset_argument(flag, value, fence, page, presets))
            continue
        if value.startswith("-") or is_placeholder(value) or not value:
            continue
        first = value.split("/", 1)[0]
        if first in entries and not (root / value).exists():
            findings.append(Finding(page, fence.line, f"command names a path that does not exist: {value}"))
    return findings


def check_preset_argument(flag: str, value: str, fence: Fence, page: str, presets: set[str]) -> list[Finding]:
    if value.startswith("-") or is_placeholder(value):
        return []
    if flag == "--test-dir":
        if not value.startswith("build/"):
            return []
        value = value[len("build/") :].split("/", 1)[0]
    elif flag == "--build" and value.startswith("build/"):
        value = value[len("build/") :].split("/", 1)[0]
    if value not in presets:
        return [Finding(page, fence.line, f"{flag} names {value!r}, which is not a preset of CMakePresets.json")]
    return []


def check_fences(
    root: Path, page: str, fences: list[Fence], entries: set[str], presets: set[str]
) -> tuple[list[Finding], int]:
    findings: list[Finding] = []
    illustrations = 0
    for fence in fences:
        marker = MARKER_RE.match(fence.marker_line) if fence.marker_line else None
        body = marker.group(1) if marker else None
        if body is None:
            findings.append(
                Finding(page, fence.line, "fenced block without a marker (source, command or illustration) before it")
            )
        elif body.startswith("source:"):
            relative = body[len("source:") :].strip()
            if relative:
                findings.extend(check_excerpt(root, relative, fence, page))
            else:
                findings.append(Finding(page, fence.line, "source marker without a path"))
        elif body == "command":
            findings.extend(check_command(root, fence, page, entries, presets))
        elif body.startswith("illustration:"):
            illustrations += 1
            if not body[len("illustration:") :].strip():
                findings.append(Finding(page, fence.line, "illustration marker without a reason"))
        else:
            findings.append(Finding(page, fence.line, f"unknown marker: {body!r}"))
    return findings, illustrations


def link_targets(lines: list[str], covered: set[int]) -> list[tuple[int, str]]:
    found: list[tuple[int, str]] = []
    for index, line in enumerate(lines):
        if index in covered:
            continue
        without_code = CODE_SPAN_RE.sub("", line)
        found.extend((index + 1, match.group(1)) for match in LINK_RE.finditer(without_code))
    return found


def resolve_link(root: Path, page_path: Path, target: str) -> Path | None:
    """The file a relative link names, or None for an external link and a bare anchor."""
    if URL_SCHEME_RE.match(target) or target.startswith("#"):
        return None
    target = re.split(r"[#?]", target, maxsplit=1)[0]
    if not target:
        return None
    base = root if target.startswith("/") else page_path.parent
    return (base / target.lstrip("/")).resolve()


def check_links(root: Path, page: str, page_path: Path, links: list[tuple[int, str]]) -> list[Finding]:
    findings = []
    for line, target in links:
        resolved = resolve_link(root, page_path, target)
        if resolved is not None and not resolved.exists():
            findings.append(Finding(page, line, f"link target does not exist: {target}"))
    return findings


def path_word(word: str) -> str:
    word = word.strip(",.;:)(")
    word = re.sub(r":\d+(-\d+)?$", "", word)
    return word.split("#", 1)[0]


def looks_like_path(word: str) -> bool:
    if "/" not in word:
        return False
    if word.endswith("/"):
        return True
    return re.search(r"\.[A-Za-z0-9]+$", word.rsplit("/", 1)[1]) is not None


def check_backticked_paths(
    root: Path, page: str, lines: list[str], covered: set[int], entries: set[str]
) -> list[Finding]:
    findings = []
    for index, line in enumerate(lines):
        if index in covered:
            continue
        for span in CODE_SPAN_RE.finditer(line):
            for raw in span.group(1).split():
                word = path_word(raw)
                if not word or is_placeholder(word) or not looks_like_path(word):
                    continue
                if word.split("/", 1)[0] in entries and not (root / word).exists():
                    findings.append(Finding(page, index + 1, f"path in backticks does not exist: {word}"))
    return findings


def manifest_names(root: Path) -> tuple[set[str], str | None]:
    manifest = root / MANIFEST
    if not manifest.is_file():
        return set(), f"{MANIFEST} does not exist"
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
        return {row["name"] for row in data["shots"]}, None
    except (ValueError, KeyError, TypeError) as error:
        return set(), f"{MANIFEST} is not a {{\"shots\": [{{\"name\": ...}}]}} manifest ({error})"


def check_tree(root: Path, guide: Path, pages: list[Path]) -> list[Finding]:
    findings: list[Finding] = []
    index = guide / INDEX
    index_page = f"{GUIDE_DIR}/{INDEX}"
    if not index.is_file():
        findings.append(Finding(index_page, 1, "the index page does not exist"))
    else:
        index_lines = index.read_text(encoding="utf-8").splitlines()
        _, covered = scan_fences(index_lines)
        linked = {
            resolved
            for _, target in link_targets(index_lines, covered)
            if (resolved := resolve_link(root, index, target)) is not None
        }
        for page in pages:
            if page != index and page.resolve() not in linked:
                findings.append(
                    Finding(index_page, 1, f"the index does not link {page.relative_to(root).as_posix()}")
                )

    pictures = sorted((guide / "img").glob("*.png")) if (guide / "img").is_dir() else []
    if pictures:
        names, problem = manifest_names(root)
        for picture in pictures:
            if picture.stem not in names:
                reason = problem or f"{MANIFEST} has no row {picture.stem!r}"
                findings.append(
                    Finding(picture.relative_to(root).as_posix(), 1, f"a picture that cannot be taken again: {reason}")
                )

    for stray in sorted(guide.rglob("CLAUDE.md")):
        findings.append(
            Finding(stray.relative_to(root).as_posix(), 1, "the guide has no CLAUDE.md (it stays outside the agents' context)")
        )

    readme = root / "README.md"
    if not readme.is_file() or f"]({GUIDE_DIR}/{INDEX}" not in readme.read_text(encoding="utf-8"):
        findings.append(Finding("README.md", 1, f"the repository README does not link {GUIDE_DIR}/{INDEX}"))
    return findings


def run(root: Path) -> tuple[list[Finding], int, bool]:
    """The findings, the number of illustration blocks and whether docs/guide/ exists."""
    guide = root / GUIDE_DIR
    pages = sorted(guide.rglob("*.md")) if guide.is_dir() else []
    if not pages:
        return [], 0, False  # pictures alone (the picture tool ran before the first page) are no guide yet
    entries = top_level_entries(root)
    presets = preset_names(root)
    findings = check_tree(root, guide, pages)
    illustrations = 0
    for page_path in pages:
        page = page_path.relative_to(root).as_posix()
        lines = page_path.read_text(encoding="utf-8").splitlines()
        fences, covered = scan_fences(lines)
        fence_findings, count = check_fences(root, page, fences, entries, presets)
        illustrations += count
        findings.extend(fence_findings)
        findings.extend(check_links(root, page, page_path, link_targets(lines, covered)))
        findings.extend(check_backticked_paths(root, page, lines, covered, entries))
    findings.sort(key=lambda finding: (finding.page, finding.line, finding.message))
    return findings, illustrations, True


def check(root: Path) -> list[Finding]:
    return run(root)[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", help="the repository root")
    args = parser.parse_args()
    root = Path(args.root).resolve()

    findings, illustrations, present = run(root)
    if not present:
        print(f"no page under {GUIDE_DIR}/: nothing to check")
        return 0
    for finding in findings:
        print(finding)
    if findings:
        print(f"{len(findings)} finding(s) in {GUIDE_DIR}/", file=sys.stderr)
        return 1
    print(f"{GUIDE_DIR}/ is clean; {illustrations} illustration block(s) are not checked against a file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
