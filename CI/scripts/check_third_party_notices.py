#!/usr/bin/env python3
"""Holds THIRD_PARTY_NOTICES.md to the tree of the engine.

Every folder of external/, every module of src/Modules/ without a CLAUDE.md of its own (the engine's own modules have one), every font
of assets/fonts/ and every source file under src/ that calls itself vendored in its first lines must have a row of the notices; every
row's path and licence file must exist. Exit 0 when all hold, 1 with one line per problem.

    python3 CI/scripts/check_third_party_notices.py [--root <engine folder>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

NOTICES = "THIRD_PARTY_NOTICES.md"
FONT_SUFFIXES = {".ttf", ".otf", ".woff", ".woff2"}
SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".inl"}
# A vendored file says so in its first lines ("Vendored: do not edit by hand.")
VENDORED_MARK = re.compile(r"\bVendored\b")
VENDORED_HEAD_LINES = 5
# A table row: the first cell is a path in backticks; the licence file is the fifth cell, a path in backticks (a note may follow it)
ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|")
CELL_PATH = re.compile(r"`([^`]+)`")
LICENCE_CELL = 4


def read_rows(text: str) -> list[tuple[str, str | None]]:
    """(path, licence file or None) of every table row whose first cell is a path."""
    rows = []
    for line in text.splitlines():
        match = ROW.match(line)
        if not match:
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        licence = None
        if len(cells) > LICENCE_CELL:
            found = CELL_PATH.search(cells[LICENCE_CELL])
            licence = found.group(1) if found else None
        rows.append((match.group(1), licence))
    return rows


def required_paths(root: Path) -> list[str]:
    """The paths that must have a row."""
    required = []
    external = root / "external"
    if external.is_dir():
        required += [f"external/{d.name}" for d in sorted(external.iterdir()) if d.is_dir()]
    modules = root / "src" / "Modules"
    if modules.is_dir():
        for module in sorted(modules.iterdir()):
            if module.is_dir() and not (module / "CLAUDE.md").exists():
                required.append(f"src/Modules/{module.name}")
    fonts = root / "assets" / "fonts"
    if fonts.is_dir():
        required += [f"assets/fonts/{f.name}" for f in sorted(fonts.iterdir()) if f.suffix.lower() in FONT_SUFFIXES]
    src = root / "src"
    if src.is_dir():
        vendored_modules = {p for p in required if p.startswith("src/Modules/")}
        for path in sorted(src.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            relative = path.relative_to(root).as_posix()
            if any(relative.startswith(module + "/") for module in vendored_modules):
                continue
            with path.open(encoding="utf-8", errors="replace") as handle:
                head = [handle.readline() for _ in range(VENDORED_HEAD_LINES)]
            if any(VENDORED_MARK.search(line) for line in head):
                required.append(relative)
    return required


def check(root: Path) -> list[str]:
    notices = root / NOTICES
    if not notices.is_file():
        return [f"{NOTICES} is missing"]
    rows = read_rows(notices.read_text(encoding="utf-8"))
    problems = []
    listed = {path for path, _ in rows}
    for path in required_paths(root):
        # A folder is covered by its own row or by rows of folders inside it (EABase lists EABase and EASTL)
        if path not in listed and not any(other.startswith(path + "/") for other in listed):
            problems.append(f"{path}: no row in {NOTICES}")
    for path, licence in rows:
        if not (root / path).exists():
            problems.append(f"{path}: the row's path does not exist")
        if licence is not None and not (root / licence).is_file():
            problems.append(f"{path}: the licence file {licence} does not exist")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=Path(__file__).resolve().parents[2], type=Path, help="the engine folder")
    args = parser.parse_args(argv)
    problems = check(args.root)
    for problem in problems:
        print(problem)
    if not problems:
        print(f"{NOTICES}: ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
