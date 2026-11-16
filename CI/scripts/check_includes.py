#!/usr/bin/env python3
"""Holds the include style: `#include "..."` only for a source file including its own header; `<windows.h>` only in a source file.

A quoted include is looked up first in the folder of the including file, so one header gets two spellings and the include roots are
bypassed. Every include is `<...>` from an include root (`<BECore/...>`, `<Games/...>`, a test helper as `<BECore/Rooms/TestClient.h>`),
except one case: a `.cpp .cc .cxx .c .mm .m` file including the header of the same stem without a folder (`Foo.cpp` -> `"Foo.h"`).
`#import "..."` follows the same rule. `FooTest.cpp` with `"Foo.h"` is not the own-header case.

`<windows.h>` and `<winsock2.h>` (any case) define the macros `min` and `max` unless `NOMINMAX` is defined first, and those macros
break every `std::numeric_limits<T>::max()` of a header that comes after them. So a header never includes them (the macros would reach
every includer), and a source file includes them only after a `#define NOMINMAX` of its own. The check reads the text only: it knows
these two names (not `<minwindef.h>`, `<ws2tcpip.h>` or a header that brings them in indirectly) and does not evaluate `#if`.

    python3 engine/CI/scripts/check_includes.py --root .          (a game)
    python3 CI/scripts/check_includes.py --root .                 (the engine)

Scanned: `.h .hpp .hh .hxx .inl .c .cc .cpp .cxx .m .mm`. Skipped: the folders `.git .venv build external node_modules __pycache__`, a
folder with a `.git` entry below the root (a submodule checks itself), a vendored module (`src/Modules/<m>` with no CLAUDE.md, as in
check_third_party_notices.py), a file with `Vendored` in its first lines, and every `--exclude <path relative to root>` (repeatable; a
folder or a file, for a game's own vendored code). Comments are skipped; a raw string holding an include at the start of a line is a
known blind spot (reported as a false finding).

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
WINDOWS_INCLUDE = re.compile(r"^\s*#\s*include\s*<((?:windows|winsock2)\.h)>", re.IGNORECASE)
NOMINMAX_DEFINE = re.compile(r"^\s*#\s*define\s+NOMINMAX\b")


def strip_comments(line: str, in_block: bool) -> tuple[str, bool]:
    """The code of `line` without comments, and whether a block comment is still open at its end."""
    out = []
    i = 0
    in_string = False
    in_char = False
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
        elif in_char:
            out.append(char)
            if char == "\\" and i + 1 < len(line):
                out.append(line[i + 1])
                i += 1
            elif char == "'":
                in_char = False
        elif two == "/*":
            in_block = True
            i += 1
        elif two == "//":
            break
        else:
            out.append(char)
            in_string = char == '"'
            in_char = char == "'"
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
    """(line number, message) of every include of `path` that breaks a rule of this script."""
    found = []
    in_block = False
    nominmax = False
    with path.open(encoding="utf-8", errors="replace") as handle:
        for number, line in enumerate(handle, 1):
            code, in_block = strip_comments(line, in_block)
            match = QUOTED_INCLUDE.match(code)
            if match and not is_own_header(path, match.group(1)):
                found.append((number, f'#include "{match.group(1)}": use <...> from an include root'))
            nominmax = nominmax or bool(NOMINMAX_DEFINE.match(code))
            windows = WINDOWS_INCLUDE.match(code)
            if windows and path.suffix not in SOURCE_SUFFIXES:
                found.append((number, f"#include <{windows.group(1)}> in a header: its macros reach every includer, include it in the source"))
            elif windows and not nominmax:
                found.append((number, f"#include <{windows.group(1)}> without #define NOMINMAX before it"))
    return found


def vendored_modules(root: Path) -> set[Path]:
    modules = root / "src" / "Modules"
    if not modules.is_dir():
        return set()
    return {m for m in modules.iterdir() if m.is_dir() and not (m / "CLAUDE.md").exists()}


def iter_files(root: Path, excludes: list[str], suffixes: set[str]):
    """The files of `root` with one of `suffixes` that the checks of this folder look at, sorted folder by folder.

    Skips the folders of SKIP_DIRS, a folder with a `.git` entry below `root`, a vendored module, a file with `Vendored` in its first
    lines and every `excludes` entry (a path relative to `root`, a folder or a file). check_headers.py walks the same way.
    """
    skipped = {(root / e).resolve() for e in excludes} | {m.resolve() for m in vendored_modules(root)}
    for folder, dirs, files in os.walk(root):
        here = Path(folder)
        dirs[:] = sorted(
            d
            for d in dirs
            if d not in SKIP_DIRS and not (here / d / ".git").exists() and (here / d).resolve() not in skipped
        )
        for name in sorted(files):
            path = here / name
            if path.suffix in suffixes and path.resolve() not in skipped and not is_vendored_file(path):
                yield path


def check(root: Path, excludes: list[str]) -> list[str]:
    problems = []
    for path in iter_files(root, excludes, SCAN_SUFFIXES):
        relative = path.relative_to(root).as_posix()
        for number, message in scan_file(path):
            problems.append(f"{relative}:{number}: {message}")
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
        print(f"{len(problems)} include(s) to fix")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
