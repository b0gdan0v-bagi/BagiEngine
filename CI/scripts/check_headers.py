#!/usr/bin/env python3
"""Checks that every header compiles alone: it includes what it uses and does not rely on the precompiled header (PCH).

    python3 CI/scripts/check_headers.py --root . --build build/macos-debug [--exclude <path>]... [--jobs N]              (the engine)
    python3 engine/CI/scripts/check_headers.py --root . --build build/macos-debug --exclude engine [--baseline <file>]  (a game)
    ... --baseline <file> --update-baseline                                  (a game: rewrite its baseline with the current failures)

Each header of `--root` is compiled as `<compiler> -fsyntax-only -x c++-header <flags> <header>` with no `-include` and no
`-include-pch` (the `-Xclang -include ...` pairs of CMake's force-include and PCH go too). The compiler and the flags (`-D -I -isystem
-idirafter -iquote -F -isysroot -arch -std= -f... -m... -target -pthread`) come from `<build>/compile_commands.json`: the entry of the C++
translation unit nearest to the header (the longest common folder prefix; the first one when none shares a folder), so the include
paths of a module and of `tests/` are right. A header that fails is reported as `path: first error line`. `#pragma once` in a header
compiled as the main file is a warning here and is silenced.

Scanned: `.h .hpp .hh .hxx` below `--root`, walking the way check_includes.py does (skipped: the folders `.git .venv build external
node_modules __pycache__`, a submodule, a vendored module, a file with `Vendored` in its first lines, every `--exclude`). A header that is
a fragment by design (included inside another file, an X-macro list, a generated one) goes to `--exclude`, with the reason in the build
file that passes it; the baseline is for headers that are to be fixed.

Every failing header fails the check: the engine has no baseline. A game may keep one while it fixes its headers (`--baseline`, one
path relative to `--root` per line, `#` starts a comment): it is a ratchet, the headers known to fail. Exit code 0 when the failing set
equals it; 1 when a header fails that is not in it (line `new: <path>: <error>`) or a listed one passes (line `fixed: <path>`: remove it
from the baseline, or run `--update-baseline`), so the list only shrinks; 2 on a bad argument; 77 (skip) when
`<build>/compile_commands.json` is missing or the compiler is MSVC `cl` (the check is for clang and gcc). `--update-baseline` rewrites
the baseline file (sorted) with the current failures and exits 0. Without `--baseline` every failing header is `new`. The last
line holds the counts: checked, failing, new, fixed. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_includes  # noqa: E402

EXIT_SKIP = 77
CPP_SOURCE_SUFFIXES = check_includes.SOURCE_SUFFIXES - {".c", ".m"}
MSVC_COMPILERS = {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}
ALWAYS_FLAGS = ["-fsyntax-only", "-x", "c++-header", "-Wno-pragma-once-outside-header", "-w"]
# Flags kept from a translation unit; the ones with a separate value take the next argument along.
KEEP_PREFIXES = ("-D", "-I", "-F", "-idirafter", "-iquote", "-isystem", "-isysroot", "-std=", "-arch", "-f", "-m", "-target", "--target")
KEEP_EXACT = {"-pthread"}
SEPARATE_VALUE = {"-D", "-I", "-F", "-idirafter", "-iquote", "-isystem", "-isysroot", "-arch", "-target", "--target"}
DROP_WITH_VALUE = {"-Xclang", "-Xpreprocessor", "-include", "-include-pch", "-o", "-MT", "-MF", "-MQ", "-x", "-imacros"}
OUTPUT_LINES = 3

Compile = Callable[[str, list[str], Path, Path], "str | None"]


def split_windows_command(command: str) -> list[str]:
    """Splits a command line as the C runtime of Windows does (CommandLineToArgvW): a backslash is literal unless it comes before a
    quote, so C:\\PROGRA~2\\LLVM\\bin\\clang++.exe stays whole, which POSIX shlex would eat as escapes."""
    arguments: list[str] = []
    current: list[str] = []
    in_token = in_quotes = False
    i = 0
    while i < len(command):
        char = command[i]
        if char == "\\":
            run = 0
            while i < len(command) and command[i] == "\\":
                run += 1
                i += 1
            if i < len(command) and command[i] == '"':
                current.append("\\" * (run // 2))
                if run % 2:
                    current.append('"')
                    i += 1
            else:
                current.append("\\" * run)
            in_token = True
            continue
        if char == '"':
            in_quotes = not in_quotes
            in_token = True
        elif char in " \t" and not in_quotes:
            if in_token:
                arguments.append("".join(current))
                current, in_token = [], False
        else:
            current.append(char)
            in_token = True
        i += 1
    if in_token:
        arguments.append("".join(current))
    return arguments


def split_command(entry: dict, windows: bool = os.name == "nt") -> list[str]:
    if "arguments" in entry:
        return list(entry["arguments"])
    if windows:
        return split_windows_command(entry["command"])
    return shlex.split(entry["command"])


def keep_flags(arguments: list[str]) -> list[str]:
    """The compile flags of one command that a standalone header needs; the compiler, the input, the output and PCH flags go."""
    kept: list[str] = []
    i = 1
    while i < len(arguments):
        argument = arguments[i]
        i += 1
        if argument in DROP_WITH_VALUE:
            i += 1
        elif argument in SEPARATE_VALUE:
            kept.append(argument)
            if i < len(arguments):
                kept.append(arguments[i])
            i += 1
        elif argument in KEEP_EXACT or (argument.startswith(KEEP_PREFIXES) and not argument.startswith("-include")):
            kept.append(argument)
    return kept


class Flags:
    """The compiler and flags of the translation unit nearest to a header, read from `compile_commands.json`."""

    def __init__(self, entries: list[dict]) -> None:
        self.by_folder: dict[Path, tuple[str, list[str], Path]] = {}
        self.fallback: tuple[str, list[str], Path] | None = None
        for entry in entries:
            arguments = split_command(entry)
            source = Path(entry["file"])
            if source.suffix not in CPP_SOURCE_SUFFIXES or "-x" in arguments or "cmake_pch" in source.name:
                continue
            if not source.is_absolute():
                source = Path(entry.get("directory", ".")) / source
            value = (arguments[0], keep_flags(arguments), Path(entry.get("directory", ".")))
            self.by_folder.setdefault(source.parent.resolve(), value)
            self.fallback = self.fallback or value

    def compiler(self) -> str | None:
        return self.fallback[0] if self.fallback else None

    def nearest(self, header: Path) -> tuple[str, list[str], Path] | None:
        """The compiler, flags and working folder of the entry sharing the longest folder prefix with `header`."""
        folder = header.resolve().parent
        for candidate in (folder, *folder.parents):
            best = None
            for source_folder, value in self.by_folder.items():
                if source_folder == candidate or candidate in source_folder.parents:
                    best = value
                    break
            if best:
                return best
        return self.fallback


def first_error(output: str, code: int) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    for line in lines:
        if "error:" in line:
            return line
    return lines[0] if lines else f"exit code {code}"


def compile_header(compiler: str, flags: list[str], header: Path, cwd: Path) -> str | None:
    """None when `header` compiles alone, else the first error line."""
    command = [compiler, *ALWAYS_FLAGS, *flags, str(header)]
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    if result.returncode == 0:
        return None
    return first_error(result.stderr or result.stdout, result.returncode)


def find_headers(root: Path, excludes: list[str]) -> list[Path]:
    return list(check_includes.iter_files(root, excludes, check_includes.HEADER_SUFFIXES))


def run_checks(root: Path, headers: list[Path], flags: Flags, jobs: int, compile_fn: Compile) -> dict[str, str]:
    """{path relative to root: first error line} of the headers that fail."""

    def one(header: Path) -> tuple[str, str | None]:
        compiler, header_flags, cwd = flags.nearest(header)
        return header.relative_to(root).as_posix(), compile_fn(compiler, header_flags, header.resolve(), cwd)

    with ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        results = list(pool.map(one, headers))
    return {path: error for path, error in results if error is not None}


def read_baseline(path: Path | None) -> set[str]:
    if path is None or not path.exists():
        return set()
    entries = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.add(line)
    return entries


def write_baseline(path: Path, failing: set[str]) -> None:
    header = (
        "# Headers that do not compile alone (check_headers.py). A fix removes lines, nothing is added.\n"
        f"# Regenerate: python3 <engine>/CI/scripts/check_headers.py --root . --build build/<p> --baseline {path} --update-baseline\n"
    )
    path.write_text(header + "".join(f"{entry}\n" for entry in sorted(failing)), encoding="utf-8")


def compare(failing: dict[str, str], baseline: set[str], checked: set[str]) -> tuple[list[str], list[str]]:
    """(lines `new: ...`, lines `fixed: ...`); a baseline entry that is no scanned header counts as fixed."""
    new = [f"new: {path}: {failing[path]}" for path in sorted(failing) if path not in baseline]
    fixed = [
        f"fixed: {path}" + ("" if path in checked else " (not a scanned header any more)")
        for path in sorted(baseline)
        if path not in failing
    ]
    return new, fixed


def main(argv: list[str], compile_fn: Compile = compile_header) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", type=Path, help="the repository root")
    parser.add_argument("--build", required=True, type=Path, help="a build folder with compile_commands.json")
    parser.add_argument("--baseline", type=Path, help="the file of the headers known to fail")
    parser.add_argument("--exclude", action="append", default=[], help="a folder or file relative to the root (repeatable)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="compiles at once")
    parser.add_argument("--update-baseline", action="store_true", help="rewrite --baseline with the current failures")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return 2 if error.code else 0
    if not args.root.is_dir():
        print(f"--root {args.root} is not a folder", file=sys.stderr)
        return 2
    if args.update_baseline and args.baseline is None:
        print("--update-baseline needs --baseline", file=sys.stderr)
        return 2
    commands = args.build / "compile_commands.json"
    if not commands.is_file():
        print(f"skipped: {commands} is missing (configure the build first)")
        return EXIT_SKIP
    flags = Flags(json.loads(commands.read_text(encoding="utf-8")))
    compiler = flags.compiler()
    if compiler is None:
        print(f"skipped: no C++ translation unit in {commands}")
        return EXIT_SKIP
    if Path(compiler).name.lower() in MSVC_COMPILERS:
        print(f"skipped: the compiler {compiler} is not clang or gcc")
        return EXIT_SKIP

    root = args.root.resolve()
    headers = find_headers(root, args.exclude)
    failing = run_checks(root, headers, flags, args.jobs, compile_fn)
    checked = {header.relative_to(root).as_posix() for header in headers}
    if args.update_baseline:
        write_baseline(args.baseline, set(failing))
        print(f"baseline {args.baseline} rewritten: {len(failing)} failing of {len(headers)} checked")
        return 0
    new, fixed = compare(failing, read_baseline(args.baseline), checked)
    for line in new + fixed:
        print(line)
    if fixed:
        print("a header of the baseline compiles alone now: remove it from the baseline (--update-baseline)")
    print(f"checked {len(headers)}, failing {len(failing)}, new {len(new)}, fixed {len(fixed)}")
    return 1 if new or fixed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
