#!/usr/bin/env python3
"""Holds the rules of `PoolString` constants (`src/Modules/BECore/PoolString/CLAUDE.md`, "Constants in code").

A text that is known when the code is written is a constant: `"text"_intern` is a ready entry, no hashing and no copy at run time, and a
named one (`static constexpr auto name = "text"_intern;` in a structure) is one place for one text. The four rules, by id:

- `literal`: a text written as a literal in a call of `PoolString::Intern(` or `PoolString::Find(` (the qualifier is required, so
  `storage.Intern("x")` is another function): the literal itself, `"a" "b"`, `"x"sv`, `StringView{"x"}` (also `string_view`,
  `eastl::` / `std::` ones, `String`, `std::string`, braces or parentheses), or a ternary of two such. The call with a plain literal does
  not compile any more; this finds the spellings that still do. In a test file `Find(StringView{"x"})` is allowed: it asks "was it ever
  interned", which `"x"_intern` would answer by interning it.
- `duplicate`: the same text through `"..."_intern` in two or more files (a test file does not count): one text, one place, in a header
  both files include.
- `loose`: more than two loose `_intern` in one source file (a header is the next rule). A named `_intern` is the initializer of a
  `constexpr` (`constexpr auto k = "a"_intern;`, `constexpr PoolString k{"a"_intern};`, the literal on the next line too) and is not
  counted; every other one is loose.
- `header`: a loose `_intern` in a header (`.h .hpp .hh .hxx`), because it is a one-off that every includer carries.

A test file is one whose path has a `tests` folder (`tests/...`, `tools/stress/tests/...`); the rules `duplicate`, `loose` and
`header` do not apply to it, the rule `literal` does.

    python3 engine/CI/scripts/check_pool_strings.py --root . --allowlist <its file> [--exclude <own vendored>]    (a game)
    python3 CI/scripts/check_pool_strings.py --root . --allowlist CI/pool_strings_allowlist.txt                  (the engine)

Scanned and skipped: the files and folders of `check_includes.py` (`iter_files`: the headers and sources, not `.git .venv build external
node_modules __pycache__`, a folder with a `.git` entry, a vendored module, a file marked `Vendored`, every `--exclude <path relative to
root>`). The text is scanned whole with its comments blanked, so a call or an initializer may continue on the next line and a comment
holds no hit. Known blind spot: a `//` inside a raw string cuts the line (the comments are blanked line by line).

`--allowlist <file>` (repeatable, no default so a game's run is its own) holds one row per line, `<rule> | <target> | <reason>`, with
`#` comments and blank lines. The target is a path glob relative to `--root` (`fnmatch`, `*` crosses `/`) for `literal`, `loose` and
`header`, and the text itself for `duplicate`. A finding with a matching row is dropped. A row with no reason, an unknown rule or fewer
than three fields is a bad argument, and a row that matched nothing is a finding (`stale allowlist row`): the list does not outlive its
need.

Output: `path:line: [rule] message` per finding and a last line with the count. Exit code 0 when clean, 1 with findings, 2 on a bad
argument or allowlist. Standard library only.
"""

from __future__ import annotations

import argparse
import bisect
import fnmatch
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_includes  # noqa: E402

EXIT_OK, EXIT_FINDINGS, EXIT_ERROR = 0, 1, 2

MAX_LOOSE_PER_FILE = 2
RULES = ("literal", "duplicate", "loose", "header")
TESTS_FOLDER = "tests"
HEADER_SUFFIXES = {".h", ".hpp", ".hh", ".hxx"}
CONSTANTS_DOC = 'PoolString/CLAUDE.md, "Constants in code"'

# How far back from a literal to look for the `constexpr` that makes its `_intern` a named constant.
NAMED_WINDOW = 300
RAW_DELIMITER_MAX = 16

CALL = re.compile(r"(?<![\w:])(?:::)?(?:BECore::)?PoolString::(?P<fn>Intern|Find)\s*\(")
# On text whose literals are masked: the content of a literal is `x` characters, the quotes stay.
LITERAL = re.compile(r'(?:(?:u8|u|U|L)?R?"[^"]*"\s*)+(?:sv)?')
VIEW_NAMES = ("StringView", "string_view", "eastl::string_view", "std::string_view")
OWNING_NAMES = ("String", "std::string")
WRAPPER = re.compile(r"(" + "|".join(re.escape(n) for n in (*VIEW_NAMES, *OWNING_NAMES)) + r")\s*(?=[({])")
NAMED_INTERN = re.compile(r"\bconstexpr\b[^;{}()]*\w+\s*(?:=|\{)\s*$")
WORD_CHAR = re.compile(r"\w")
OPENERS = {"(": ")", "{": "}", "[": "]"}


class AllowlistError(ValueError):
    """A row of an allowlist the script cannot use, or an allowlist it cannot read."""


@dataclass
class Row:
    rule: str
    target: str
    path: str
    line: int
    used: bool = False


@dataclass
class Finding:
    rule: str
    path: str
    line: int
    message: str
    key: str  # what an allowlist row names: the path, or the text of a `duplicate`


def parse_allowlist(path: Path) -> list[Row]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        raise AllowlistError(f"cannot read the allowlist {path}: {error.strerror}") from error
    rows = []
    for number, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = [f.strip() for f in line.split("|", 2)]
        if len(fields) < 3 or not all(fields):
            raise AllowlistError(f"{path}:{number}: a row is `<rule> | <target> | <reason>`, each part non-empty: {line}")
        if fields[0] not in RULES:
            raise AllowlistError(f"{path}:{number}: unknown rule '{fields[0]}' (one of {', '.join(RULES)})")
        rows.append(Row(fields[0], fields[1], str(path), number))
    return rows


def raw_string_end(text: str, quote: int) -> int:
    """The index of the closing quote of the raw string whose opening quote is at `quote`, or -1 when it is no raw string."""
    prefix = quote - 1
    if prefix < 0 or text[prefix] != "R":
        return -1
    before = prefix - 1
    if text[max(before - 1, 0) : before + 1] == "u8":
        before -= 2
    elif before >= 0 and text[before] in "uUL":
        before -= 1
    if before >= 0 and (text[before].isalnum() or text[before] == "_"):
        return -1
    paren = text.find("(", quote + 1, quote + 2 + RAW_DELIMITER_MAX)
    if paren < 0:
        return -1
    closing = ")" + text[quote + 1 : paren] + '"'
    end = text.find(closing, paren + 1)
    return -1 if end < 0 else end + len(closing) - 1


def mask_literals(text: str) -> tuple[str, list[tuple[int, int]]]:
    """`text` with the content of every string and char literal replaced by `x` (newlines kept, quotes kept), and the (open, close)
    quote indexes of the string literals. What is left holds no bracket, comma or word that was inside a literal."""
    out = list(text)
    spans: list[tuple[int, int]] = []
    length = len(text)
    i = 0
    while i < length:
        char = text[i]
        if char == '"':
            end = raw_string_end(text, i)
            if end < 0:
                end = i + 1
                while end < length and text[end] not in '"\n':
                    end += 2 if text[end] == "\\" else 1
                if end >= length or text[end] != '"':
                    i += 1
                    continue
            spans.append((i, end))
            out[i + 1 : end] = ["\n" if c == "\n" else "x" for c in text[i + 1 : end]]
            i = end + 1
        elif char == "'" and not (i > 0 and text[i - 1].isalnum()):  # `1'000` is a digit separator
            end = i + 1
            while end < length and text[end] not in "'\n":
                end += 2 if text[end] == "\\" else 1
            if end < length and text[end] == "'":
                out[i + 1 : end] = ["x"] * (end - i - 1)
                i = end + 1
            else:
                i += 1
        else:
            i += 1
    return "".join(out), spans


def blank_comments(text: str) -> str:
    """`text` with the `//` and `/* */` comments removed line by line (check_includes.strip_comments); line numbers survive."""
    in_block = False
    lines = []
    for line in text.splitlines():
        code, in_block = check_includes.strip_comments(line, in_block)
        lines.append(code)
    return "\n".join(lines)


def group_end(text: str, opener: int) -> Optional[int]:
    """The index of the bracket that closes the one at `opener` (any of `({[`), or None when it never closes."""
    depth = 0
    for i in range(opener, len(text)):
        char = text[i]
        if char in OPENERS:
            depth += 1
        elif char in ")}]":
            depth -= 1
            if depth == 0:
                return i
    return None


def split_ternary(arg: str) -> Optional[tuple[str, str]]:
    """The two branches of a top-level `c ? a : b`, or None. A nested `?` of the first branch takes its own `:`."""
    depth = 0
    question = -1
    nested = 0
    i = 0
    while i < len(arg):
        char = arg[i]
        if char in OPENERS:
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif depth == 0 and char == ":" and arg[i + 1 : i + 2] == ":":
            i += 1
        elif depth == 0 and char == "?":
            if question < 0:
                question = i
            else:
                nested += 1
        elif depth == 0 and char == ":" and question >= 0:
            if nested == 0:
                return arg[question + 1 : i], arg[i + 1 :]
            nested -= 1
        i += 1
    return None


def unwrap(arg: str) -> Optional[tuple[str, str]]:
    """(the type name, the argument) of `StringView{"x"}` and its kin when the whole `arg` is that call, else None."""
    match = WRAPPER.match(arg)
    if not match:
        return None
    opener = match.end()
    if group_end(arg, opener) != len(arg) - 1:
        return None
    return match.group(1), arg[opener + 1 : -1]


def is_literal_arg(arg: str) -> bool:
    """Whether `arg` (masked text) is a literal text: a literal, a string type applied to one, or a ternary of two."""
    arg = arg.strip()
    branches = split_ternary(arg)
    if branches:
        return is_literal_arg(branches[0]) and is_literal_arg(branches[1])
    if LITERAL.fullmatch(arg):
        return True
    wrapped = unwrap(arg)
    return bool(wrapped) and is_literal_arg(wrapped[1])


def is_view_of_literal(arg: str) -> bool:
    wrapped = unwrap(arg.strip())
    return bool(wrapped) and wrapped[0] in VIEW_NAMES and is_literal_arg(wrapped[1])


def is_test_path(relative: Path) -> bool:
    return TESTS_FOLDER in relative.parts[:-1]


@dataclass
class Intern:
    line: int
    text: str
    named: bool


def scan_text(relative: str, text: str, is_test: bool) -> tuple[list[Finding], list[Intern]]:
    """The findings of the rule `literal` and every `"..."_intern` of one file's text (comments already blanked)."""
    masked, spans = mask_literals(text)
    newlines = [i for i, c in enumerate(text) if c == "\n"]

    def line_of(index: int) -> int:
        return bisect.bisect_left(newlines, index) + 1

    findings = []
    for match in CALL.finditer(masked):
        opener = match.end() - 1
        closer = group_end(masked, opener)
        if closer is None:
            continue
        arg = masked[opener + 1 : closer]
        if not is_literal_arg(arg):
            continue
        function = match.group("fn")
        if is_test and function == "Find" and is_view_of_literal(arg):
            continue
        advice = f'PoolString::{function} of a literal text: name it as a constant, "text"_intern ({CONSTANTS_DOC})'
        if is_test and function == "Find":
            advice = 'PoolString::Find of a literal text: ask with Find(StringView{"text"}), "text"_intern would intern it'
        findings.append(Finding("literal", relative, line_of(match.start()), advice, relative))

    interns = []
    suffix = "_intern"
    for opener, closer in spans:
        after = closer + 1 + len(suffix)
        if not text.startswith(suffix, closer + 1) or WORD_CHAR.match(text[after : after + 1]):
            continue
        before = masked[max(0, opener - NAMED_WINDOW) : opener]
        if before.rstrip().endswith("operator"):  # the definition `operator""_intern`
            continue
        interns.append(Intern(line_of(opener), text[opener + 1 : closer], bool(NAMED_INTERN.search(before))))
    return findings, interns


def scan_file(path: Path, relative: Path) -> tuple[list[Finding], list[Intern]]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    if "_intern" not in raw and "PoolString::" not in raw:
        return [], []
    return scan_text(relative.as_posix(), blank_comments(raw), is_test_path(relative))


def check(root: Path, excludes: list[str], allowlists: list[Path]) -> list[str]:
    """One line per finding, `path:line: [rule] message`; raises AllowlistError for an allowlist it cannot use."""
    rows = [row for allowlist in allowlists for row in parse_allowlist(allowlist)]
    findings: list[Finding] = []
    places: dict[str, list[tuple[str, int]]] = {}
    for path in check_includes.iter_files(root, excludes, check_includes.SCAN_SUFFIXES):
        relative = path.relative_to(root)
        name = relative.as_posix()
        file_findings, interns = scan_file(path, relative)
        findings += file_findings
        if is_test_path(relative):
            continue
        seen = set()
        for intern in interns:
            if intern.text not in seen:
                seen.add(intern.text)
                places.setdefault(intern.text, []).append((name, intern.line))
        loose = [i for i in interns if not i.named]
        if path.suffix in HEADER_SUFFIXES:
            for intern in loose:
                findings.append(Finding("header", name, intern.line, f'loose "{intern.text}"_intern in a header: name it in a structure ({CONSTANTS_DOC})', name))
        elif len(loose) > MAX_LOOSE_PER_FILE:
            lines = ", ".join(str(i.line) for i in loose)
            findings.append(Finding("loose", name, loose[MAX_LOOSE_PER_FILE].line, f"{len(loose)} loose _intern in one file, at most {MAX_LOOSE_PER_FILE} (lines {lines}): name them in a `struct Ids` ({CONSTANTS_DOC})", name))
    for text, where in places.items():
        if len(where) > 1:
            first = where[0]
            others = ", ".join(f"{p}:{n}" for p, n in where)
            findings.append(Finding("duplicate", first[0], first[1], f'"{text}"_intern is written in {len(where)} files ({others}): one text, one named constant in a shared header', text))

    kept = []
    for finding in findings:
        matching = [r for r in rows if r.rule == finding.rule and (r.target == finding.key if finding.rule == "duplicate" else fnmatch.fnmatchcase(finding.key, r.target))]
        for row in matching:
            row.used = True
        if not matching:
            kept.append(finding)
    problems = [f"{f.path}:{f.line}: [{f.rule}] {f.message}" for f in sorted(kept, key=lambda f: (f.path, f.line, f.rule))]
    for row in rows:
        if not row.used:
            problems.append(f"{row.path}:{row.line}: [allowlist] stale allowlist row `{row.rule} | {row.target}` matched nothing: delete it")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", type=Path, help="the repository root")
    parser.add_argument("--exclude", action="append", default=[], help="a folder or file relative to the root (repeatable)")
    parser.add_argument("--allowlist", action="append", default=[], type=Path, help="a file of `<rule> | <target> | <reason>` rows (repeatable)")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return EXIT_ERROR if error.code else EXIT_OK
    if not args.root.is_dir():
        print(f"--root {args.root} is not a folder", file=sys.stderr)
        return EXIT_ERROR
    try:
        problems = check(args.root, args.exclude, args.allowlist)
    except AllowlistError as error:
        print(error, file=sys.stderr)
        return EXIT_ERROR
    for problem in problems:
        print(problem)
    if problems:
        print(f"{len(problems)} pool string finding(s) to fix")
    return EXIT_FINDINGS if problems else EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
