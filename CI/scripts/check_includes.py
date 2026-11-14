#!/usr/bin/env python3
"""Holds the include style: `#include "..."` only for a source file including its own header.

A quoted include is looked up first in the folder of the including file, so one header gets two spellings and the include roots are
bypassed. Every include is `<...>` from an include root (`<BECore/...>`, `<Games/...>`, a test helper as `<BECore/Rooms/TestClient.h>`),
except one case: a `.cpp .cc .cxx .c .mm .m` file including the header of the same stem without a folder (`Foo.cpp` -> `"Foo.h"`).
`#import "..."` follows the same rule. `FooTest.cpp` with `"Foo.h"` is not the own-header case.

    python3 engine/CI/scripts/check_includes.py --root .          (a game)
    python3 CI/scripts/check_includes.py --root .                 (the engine)

Scanned: `.h .hpp .hh .hxx .inl .c .cc .cpp .cxx .m .mm`. Skipped: the folders `.git .venv build external node_modules __pycache__`, a
folder with a `.git` entry below the root (a submodule checks itself), a vendored module (`src/Modules/<m>` with no CLAUDE.md, as in
check_third_party_notices.py), a file with `Vendored` in its first lines, and every `--exclude <path relative to root>` (repeatable; a
folder or a file, for a game's own vendored code). Comments are skipped; a raw string holding an include at the start of a line is a
known blind spot (it is reported).

Exit code 0 when no finding, 1 with one line per finding and a last line with the count, 2 on a bad argument. Standard library only.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

SCAN_SUFFIXES = {".h", ".hpp", ".hh", ".hxx", ".inl", ".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx", ".c", ".mm", ".m"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh", ".hxx"}
SKIP_DIRS = {".git", ".venv", "build", "external", "node_modules", "__pycache__"}
VENDORED_MARK = re.compile(r"\bVendored\b")
VENDORED_HEAD_LINES = 5
QUOTED_INCLUDE = re.compile(r'^\s*#\s*(?:include|import)\s+"([^"]*)"')


def strip_comments(line: str, in_block: bool) -> tuple[str, bool]:
    """The code of `line` without comments, and whether a block comment is still open at its end."""
    out = []
    i = 0
    in_string = False
    while i < len(line):
        two = line[i : i + 2]
        if in_block:
            if two == "*/":
                in_block = False
                i += 2
            else:
                i += 1
            continue
        char = line[i]
        if in_string:
            out.append(char)
            if char == "\\" and i + 1 < len(line):
                out.append(line[i + 1])
                i += 1
            elif char == '"':
                in_string = False
        elif two == "/*":
            in_block = True
            i += 1
        elif two == "//":
            break
        else:
            out.append(char)
            in_string = char == '"'
        i += 1
    return "".join(out), in_block


def is_own_header(path: Path, quoted: str) -> bool:
    """`Foo.cpp` including `"Foo.h"`: a source file, no folder in the quotes, a header suffix, the same stem."""
    header = Path(quoted)
    return (
        path.suffix in SOURCE_SUFFIXES
        and "/" not in quoted
        and header.suffix in HEADER_SUFFIXES
        and header.stem == path.stem
    )


def is_vendored_file(path: Path) -> bool:
    with path.open(encoding="utf-8", errors="replace") as handle:
        for _ in range(VENDORED_HEAD_LINES):
            if VENDORED_MARK.search(handle.readline()):
                return True
    return False


def scan_file(path: Path) -> list[tuple[int, str]]:
    """(line number, quoted text) of every quoted include of `path` that is not its own header."""
    found = []
    in_block = False
    with path.open(encoding="utf-8", errors="replace") as handle:
        for number, line in enumerate(handle, 1):
            code, in_block = strip_comments(line, in_block)
            match = QUOTED_INCLUDE.match(code)
            if match and not is_own_header(path, match.group(1)):
                found.append((number, match.group(1)))
    return found


def vendored_modules(root: Path) -> set[Path]:
    modules = root / "src" / "Modules"
    if not modules.is_dir():
        return set()
    return {m for m in modules.iterdir() if m.is_dir() and not (m / "CLAUDE.md").exists()}


def check(root: Path, excludes: list[str]) -> list[str]:
    skipped = {(root / e).resolve() for e in excludes} | {m.resolve() for m in vendored_modules(root)}
    problems = []
    for folder, dirs, files in os.walk(root):
        here = Path(folder)
        dirs[:] = sorted(
            d
            for d in dirs
            if d not in SKIP_DIRS and not (here / d / ".git").exists() and (here / d).resolve() not in skipped
        )
        for name in sorted(files):
            path = here / name
            if path.suffix not in SCAN_SUFFIXES or path.resolve() in skipped or is_vendored_file(path):
                continue
            relative = path.relative_to(root).as_posix()
            for number, quoted in scan_file(path):
                problems.append(f'{relative}:{number}: #include "{quoted}": use <...> from an include root')
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", type=Path, help="the repository root")
    parser.add_argument("--exclude", action="append", default=[], help="a folder or file relative to the root (repeatable)")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return 2 if error.code else 0
    if not args.root.is_dir():
        print(f"--root {args.root} is not a folder", file=sys.stderr)
        return 2
    problems = check(args.root, args.exclude)
    for problem in problems:
        print(problem)
    if problems:
        print(f"{len(problems)} quoted include(s) to convert")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
