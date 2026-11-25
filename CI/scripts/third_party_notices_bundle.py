#!/usr/bin/env python3
"""Writes the third-party notices file of a game's release build from the tables of THIRD_PARTY_NOTICES.md.

`build` reads the engine's table and, when given, a game's own file of the same format (its paths are relative to the game root), drops
the rows whose "In a game" cell starts with `build only` and writes one text file: the list of the components with version, licence
and the condition of the row, then every distinct licence text once. A licence that lives in a source file is carried as an excerpt
(the last block comment for `(end of file)`, the leading comment for `(its header)`). A row without a licence file is listed with a
line saying so; a missing licence file is an error, except inside a submodule that is not checked out. The file is written only when
its content changed, so a build that finds nothing new does not touch it.

`verify` checks a bundle against the tables: every path that is not build only is in it, and so is the first line of each licence
text. Both exit 0, or 1 with one line per problem; `verify --skip-missing` exits 77 when there is no bundle yet.

    python3 CI/scripts/third_party_notices_bundle.py build --root <engine> --out <file> [--game-notices <file> [--game-root <dir>]]
    python3 CI/scripts/third_party_notices_bundle.py verify --root <engine> --bundle <file> [--game-notices <file> [--game-root <dir>]]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_third_party_notices import CELL_PATH, NOTICES, ROW, in_missing_submodule  # noqa: E402

CELLS_PER_ROW = 6
PATH_CELL, COMPONENT_CELL, VERSION_CELL, LICENCE_CELL, LICENCE_FILE_CELL, CONDITION_CELL = range(CELLS_PER_ROW)
BUILD_ONLY = "build only"
EXCERPT_NOTE = re.compile(r"\((end of file|its header)\)")
END_OF_FILE = "end of file"
# A source file's licence text is a bounded excerpt: stb_image.h is thousands of lines, the licence is its last comment.
EXCERPT_MAX_LINES = 120
# An end-of-file licence in a file whose last comment cannot be found is its last lines.
TAIL_FALLBACK_LINES = 60
COMMENT_OPEN = "/*"
COMMENT_CLOSE = "*/"
LINE_COMMENT = "//"
SKIPPED = 77
RULE = "=" * 78
SUB_RULE = "-" * 78
NO_LICENCE_FILE = "no licence file in the folder (the owner has to add one)"
NOT_CHECKED_OUT = "licence file not available: the submodule is not checked out in this build"


@dataclass(frozen=True)
class Row:
    path: str
    component: str
    version: str
    licence: str
    licence_file: str | None
    excerpt: str | None
    condition: str
    root: Path

    @property
    def build_only(self) -> bool:
        return self.condition.lower().startswith(BUILD_ONLY)


@dataclass
class Licence:
    key: str
    text: str | None
    note: str | None
    users: list[Row]


def read_table(text: str, root: Path, source: str) -> tuple[list[Row], list[str]]:
    """Every row of the tables of `text` with its six cells; a row with another count of cells is a problem."""
    rows, problems = [], []
    for line in text.splitlines():
        match = ROW.match(line)
        if not match:
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != CELLS_PER_ROW:
            problems.append(f"{source}: {match.group(1)}: {len(cells)} cells, expected {CELLS_PER_ROW}")
            continue
        found = CELL_PATH.search(cells[LICENCE_FILE_CELL])
        note = EXCERPT_NOTE.search(cells[LICENCE_FILE_CELL])
        rows.append(Row(match.group(1), cells[COMPONENT_CELL], cells[VERSION_CELL], cells[LICENCE_CELL],
                        found.group(1) if found else None, note.group(1) if note else None, cells[CONDITION_CELL], root))
    return rows, problems


def read_all(root: Path, game_notices: Path | None, game_root: Path | None) -> tuple[list[Row], list[str]]:
    """The rows of the engine's table and of the game's; the game's licence files lie under `game_root`."""
    notices = root / NOTICES
    if not notices.is_file():
        return [], [f"{NOTICES} is missing in {root}"]
    rows, problems = read_table(notices.read_text(encoding="utf-8"), root, NOTICES)
    if game_notices is not None:
        if not game_notices.is_file():
            return rows, problems + [f"{game_notices}: the game's notices file does not exist"]
        game_rows, game_problems = read_table(game_notices.read_text(encoding="utf-8"), game_root or game_notices.parent,
                                              str(game_notices))
        rows += game_rows
        problems += game_problems
    return rows, problems


def leading_comment(lines: list[str]) -> list[str]:
    """The comment at the top of a file, with the `//` marks taken off."""
    if lines and lines[0].lstrip().startswith(COMMENT_OPEN):
        for index, line in enumerate(lines):
            if COMMENT_CLOSE in line:
                return lines[: index + 1]
        return lines
    block = []
    for line in lines:
        if not line.startswith(LINE_COMMENT):
            break
        block.append(line[len(LINE_COMMENT):].removeprefix(" "))
    return block


def trailing_comment(lines: list[str]) -> list[str]:
    """The last block comment of a file: from the last line that is only `/*`."""
    for index in range(len(lines) - 1, -1, -1):
        if lines[index].strip() == COMMENT_OPEN:
            return lines[index:]
    return lines[-TAIL_FALLBACK_LINES:]


def excerpt_of(path: Path, kind: str) -> str:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    while lines and not lines[-1].strip():
        lines.pop()
    block = trailing_comment(lines) if kind == END_OF_FILE else leading_comment(lines)
    return "\n".join(block[:EXCERPT_MAX_LINES]).rstrip()


def licence_of(row: Row) -> tuple[str | None, str | None, str | None]:
    """(key, text, note) of the row's licence: the key groups the rows that share a text, a note says why there is no text."""
    if row.licence_file is None:
        return None, None, NO_LICENCE_FILE
    key = row.licence_file if row.excerpt is None else f"{row.licence_file} ({row.excerpt})"
    path = row.root / row.licence_file
    if path.is_file():
        text = path.read_text(encoding="utf-8", errors="replace").strip("\n") if row.excerpt is None else excerpt_of(path, row.excerpt)
        return key, text, None
    if in_missing_submodule(row.root, row.licence_file):
        return key, None, NOT_CHECKED_OUT
    return key, None, f"the licence file {row.licence_file} does not exist"


def collect(rows: list[Row]) -> tuple[list[Licence], list[str]]:
    """The licence texts of the rows that ship, each once; the rows without any are in `users` of a keyless entry."""
    licences: dict[str | None, Licence] = {}
    problems = []
    for row in rows:
        if row.build_only:
            continue
        key, text, note = licence_of(row)
        if text is None and key is not None and note != NOT_CHECKED_OUT:
            problems.append(f"{row.path}: {note}")
        entry = licences.setdefault(key, Licence(key or "", text, note, []))
        entry.users.append(row)
    return list(licences.values()), problems


def first_line(text: str) -> str:
    """The first line with a letter in it: what `verify` looks for in a bundle."""
    for line in text.splitlines():
        if re.search(r"[A-Za-z]", line):
            return line.strip()
    return ""


def render(rows: list[Row], licences: list[Licence]) -> str:
    shipped = [row for row in rows if not row.build_only]
    out = [
        "THIRD-PARTY NOTICES",
        "",
        "This software contains the components below. Each keeps its own licence; the texts follow the list.",
        "",
        RULE,
        "Components",
        RULE,
        "",
    ]
    for row in shipped:
        out.append(f"* {row.component}")
        out.append(f"    path: {row.path}")
        out.append(f"    version: {row.version}; licence: {row.licence}; included: {row.condition}")
    out += ["", RULE, "Licence texts", RULE]
    for licence in licences:
        out += ["", SUB_RULE]
        out.append(f"Licence file: {licence.key}" if licence.key else "No licence file")
        out.append("Used by: " + "; ".join(user.component for user in licence.users))
        out += [SUB_RULE, ""]
        out.append(licence.text if licence.text is not None else f"({licence.note})")
    return "\n".join(out) + "\n"


def build(root: Path, out: Path, game_notices: Path | None, game_root: Path | None) -> list[str]:
    rows, problems = read_all(root, game_notices, game_root)
    licences, more = collect(rows)
    problems += more
    if problems:
        return problems
    text = render(rows, licences)
    if out.is_file() and out.read_text(encoding="utf-8") == text:
        return []
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    return []


def verify(root: Path, bundle: Path, game_notices: Path | None, game_root: Path | None) -> list[str]:
    if not bundle.is_file():
        return [f"{bundle}: the bundle does not exist"]
    text = bundle.read_text(encoding="utf-8")
    rows, problems = read_all(root, game_notices, game_root)
    licences, more = collect(rows)
    problems += more
    for row in rows:
        if not row.build_only and f"path: {row.path}\n" not in text:
            problems.append(f"{row.path}: not in {bundle.name}")
    for licence in licences:
        if licence.text is not None and first_line(licence.text) not in text:
            problems.append(f"{licence.key}: the licence text is not in {bundle.name}")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("build", "verify"))
    parser.add_argument("--root", default=Path(__file__).resolve().parents[2], type=Path, help="the engine folder")
    parser.add_argument("--out", type=Path, help="build: the file to write")
    parser.add_argument("--bundle", type=Path, help="verify: the file to check")
    parser.add_argument("--skip-missing", action="store_true", help="verify: exit 77 (a ctest skip) when the bundle was not built")
    parser.add_argument("--game-notices", type=Path, help="the game's own table, the format of THIRD_PARTY_NOTICES.md")
    parser.add_argument("--game-root", type=Path, help="the folder the game's paths are relative to (default: where its table is)")
    args = parser.parse_args(argv)
    target = args.out if args.command == "build" else args.bundle
    if target is None:
        parser.error("build needs --out, verify needs --bundle")
    if args.skip_missing and not target.is_file():
        print(f"{target}: not built, skipped")
        return SKIPPED
    if args.command == "build":
        problems = build(args.root, target, args.game_notices, args.game_root)
    else:
        problems = verify(args.root, target, args.game_notices, args.game_root)
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
