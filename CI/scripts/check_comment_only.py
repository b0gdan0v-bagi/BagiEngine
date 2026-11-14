#!/usr/bin/env python3
"""Proves that a change only touched comments: each changed C-family file is the same, token by token, without its comments.

    python3 CI/scripts/check_comment_only.py --base <rev> --root <R> [paths...]
    python3 engine/CI/scripts/check_comment_only.py --base games/mahjong --root engine       (from a game checkout)

Compared: every file of `.h .hpp .inl .cpp .mm .c` that differs between the merge-base of `<rev>` and `HEAD` (so a base branch that moved on
after the work branch was cut does not count; read with `git show`, git runs in `--root`) and the working tree, committed or not, untracked
files included; with `paths` (files or folders relative to `--root`) only
those, otherwise all. Each side is read with the comments (`//`, `/* */`, a `\\` continuation extends a `//` comment) removed and the
whitespace collapsed to tokens, so `clang-format` re-wrapping and the removal of whole comments pass; a changed token, a renamed
identifier, a removed line of code or an added or deleted file fail. A string, a character or a raw string literal is code, comment signs and
whitespace inside it included (`"x  y"` and `"x y"` differ). These markers are code too and stay in the compared text, so they cannot be cut or edited by a comment cleanup: `//-NoStd:`,
`NOLINT` (also `NOLINTNEXTLINE`, `NOLINTBEGIN`), `clang-format off` / `on`. A preprocessor line ends at its own line end, so `#define X 1`
and `#define X` / `1` differ.
Not compared: Python, CMake, Markdown and every other file; a file that changed in a way no comment explains is a finding all the same.

Output: `path:line: <what differs>` per file (the line is in the working-tree file; for a missing token the last line of it), then a summary.
Exit code 0 when every file is the same without comments, 1 with findings, 2 on a bad argument or a git failure. Standard library only.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import nostd  # noqa: E402

EXIT_OK, EXIT_FINDINGS, EXIT_ERROR = 0, 1, 2

SUFFIXES = (".h", ".hpp", ".inl", ".cpp", ".mm", ".c")
# A comment that holds one of these is read by a tool (`check_types.py`, clang-tidy, clang-format) and is code to this check
MARKER = re.compile(r"//-NoStd:|NOLINT|clang-format\s+(?:off|on)")
TOKEN = re.compile(r"\S+")
LITERAL_KINDS = ("raw", "str", "chr")
# The whitespace of a literal is shifted into this block; no source holds it, and `str.isspace` is false there
PRIVATE_USE = 0xF0000
# A logical line ends at a newline that no backslash continues
LOGICAL_LINE_END = re.compile(r"(?<!\\)\n|\Z")
DIRECTIVE_END = "\n"

Token = Tuple[str, int]  # the text and the 1-based line in the file it comes from


@dataclass
class Difference:
    path: str
    line: int
    what: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.what}"


def strip_comments(source: str) -> str:
    """The source with every comment that is no marker replaced by blanks (same length, newlines of a block comment included, so a
    preprocessor line that holds one stays one logical line). The whitespace inside a string, a character or a raw string literal is
    swapped for a private-use character of the same length, so that the literal stays one token and its spaces are compared."""

    def replace(m: re.Match) -> str:
        text = m.group(0)
        if m.lastgroup in ("line", "block"):
            return " " * len(text) if MARKER.search(text) is None else text
        if m.lastgroup in LITERAL_KINDS:
            return "".join(chr(PRIVATE_USE + ord(ch)) if ch.isspace() else ch for ch in text)
        return text

    # `nostd._MASK` is the scanner of `nostd.mask`: one alternative per kind (comments, raw and plain strings, characters, numbers),
    # leftmost wins, so a `//` inside a string is no comment
    return nostd._MASK.sub(replace, source)


def tokens(source: str) -> List[Token]:
    """The tokens of the source without its comments: words split at whitespace, each with its line; a preprocessor line ends in a
    `DIRECTIVE_END` token so that a join of two lines shows."""
    text = strip_comments(source)
    result: List[Token] = []
    position = 0
    for end in LOGICAL_LINE_END.finditer(text):
        segment = text[position:end.start()]
        for word in TOKEN.finditer(segment):
            offset = position + word.start()
            result.append((word.group(0), source.count("\n", 0, offset) + 1))
        if segment.lstrip().startswith("#"):
            result.append((DIRECTIVE_END, source.count("\n", 0, end.start()) + 1))
        position = end.end()
    return result


def first_difference(old: str, new: str) -> Optional[Tuple[int, str]]:
    """None when the two sources are the same without comments, otherwise the line in `new` and what differs there."""
    old_tokens, new_tokens = tokens(old), tokens(new)
    for index in range(min(len(old_tokens), len(new_tokens))):
        if old_tokens[index][0] != new_tokens[index][0]:
            return new_tokens[index][1], f"code differs: `{show(old_tokens[index][0])}` before, `{show(new_tokens[index][0])}` now"
    last_line = new.count("\n") + 1
    if len(old_tokens) > len(new_tokens):
        return (new_tokens[-1][1] if new_tokens else last_line), f"code removed: `{show(old_tokens[len(new_tokens)][0])}` and what follows is gone"
    if len(new_tokens) > len(old_tokens):
        return new_tokens[len(old_tokens)][1], f"code added: `{show(new_tokens[len(old_tokens)][0])}` has no counterpart before"
    return None


def show(token: str) -> str:
    if token == DIRECTIVE_END:
        return "end of the preprocessor line"
    return "".join(chr(ord(ch) - PRIVATE_USE) if ord(ch) >= PRIVATE_USE else ch for ch in token)


def git(root: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=False)


def merge_base(root: Path, base: str) -> str:
    """The commit where the work branch left `base`: the files the branch changed are compared with that, not with a base that moved on."""
    done = git(root, "merge-base", base, "HEAD")
    if done.returncode != 0:
        raise RuntimeError(done.stderr.decode(errors="replace").strip() or f"git merge-base {base} HEAD failed")
    return done.stdout.decode(errors="replace").strip()


def changed_files(root: Path, base: str, paths: Sequence[str]) -> List[str]:
    """The C-family files that differ from `base` in the working tree, relative to `root`, untracked ones included."""
    spec = ["--", *paths] if paths else []
    names = set()
    for args in (("diff", "--name-only", "--no-renames", "--relative", base), ("ls-files", "--others", "--exclude-standard")):
        done = git(root, *args, *spec)
        if done.returncode != 0:
            raise RuntimeError(done.stderr.decode(errors="replace").strip() or f"git {args[0]} failed")
        names.update(line for line in done.stdout.decode(errors="replace").splitlines() if line.endswith(SUFFIXES))
    return sorted(names)


def check(root: Path, base: str, paths: Sequence[str] = ()) -> Tuple[List[Difference], int]:
    """The differences that are more than comments, and the number of files looked at."""
    base = merge_base(root, base)
    files = changed_files(root, base, paths)
    differences: List[Difference] = []
    for name in files:
        shown = git(root, "show", f"{base}:./{name}")
        file = root / name
        if shown.returncode != 0:
            differences.append(Difference(name, 1, "file added (it has no counterpart in the base)"))
            continue
        if not file.is_file():
            differences.append(Difference(name, 1, "file deleted"))
            continue
        found = first_difference(shown.stdout.decode(errors="replace"), file.read_bytes().decode(errors="replace"))
        if found is not None:
            differences.append(Difference(name, found[0], found[1]))
    return differences, len(files)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0], epilog=__doc__.split("\n\n", 1)[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", required=True, help="the git revision to compare with (a branch, a tag, a hash)")
    parser.add_argument("--root", default=".", type=Path, help="the repository (or a folder of it) that git runs in")
    parser.add_argument("paths", nargs="*", help="files or folders relative to --root; default: every changed C-family file")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return EXIT_ERROR if error.code else EXIT_OK
    if not args.root.is_dir():
        print(f"--root {args.root} is not a folder", file=sys.stderr)
        return EXIT_ERROR
    try:
        differences, count = check(args.root, args.base, args.paths)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return EXIT_ERROR
    for difference in differences:
        print(difference)
    print(f"checked {count} files against {args.base}, not only comments: {len(differences)}")
    return EXIT_FINDINGS if differences else EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
