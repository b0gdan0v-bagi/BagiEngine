"""The one definition of the `//-NoStd: <why>` marker, of the masking of comments and literals, and of the files and folders that the
type rules skip. `tools/codemod/codemod.py` (moves `eastl::` / `std::` names to the type layer) and `CI/scripts/check_types.py` (holds
that nobody writes them again) both import this module, so the two tools cannot disagree on what a marker covers or what is skipped.

Standard library only.
"""

from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Iterator, Sequence, Set

SOURCE_SUFFIXES = {".h", ".hpp", ".inl", ".cpp"}
# Folders never walked, at any depth (the vendored zones of the engine and the build and tool folders)
SKIPPED_FOLDERS = {"EABase", "fmt", "imgui", "pugixml", "RmlUi", "stb", "tracy", "external", ".venv", ".git", "node_modules", "__pycache__"}
SKIPPED_FOLDER_PREFIXES = ("build",)
# Paths (relative to the root, `/` separated) that are skipped: a folder prefix as a segment run, or a file
SKIPPED_PATH_RUNS = ("src/Widgets/Icons", "BECore/Types", "BECore/Format")
# The files that name an `eastl::` / `std::` type on purpose (the type layer is above): the reflection traits and the tests that
# hold the aliases against the types they stand for
SKIPPED_FILES = (
    "pch.h",
    "Reflection/DataAccessor.h",
    "Reflection/SerializationTraits.h",
    "Reflection/PropertyVisitorDispatch.h",
    "tests/BECore/TypesTest.cpp",
    "tests/BECore/ReflectionAliasesTest.cpp",
    "tests/BECore/ReflectionAliasesTest.h",
)
# `//-NoStd: reason` exactly (no space after `//`): the old `// std: reason` and an ordinary comment that starts with a `std::` name are no markers
STD_MARKER = re.compile(r"//-NoStd:")

# ---- blanking comments and literals ------------------------------------------------------------------------------------------

# One alternative per kind, leftmost wins. A number is consumed whole so that the `'` of `1'000` is no character literal.
_MASK = re.compile(
    r"""
      (?P<line>//(?:[^\n\\]|\\.|\\\n)*)
    | (?P<block>/\*.*?(?:\*/|\Z))
    | (?P<raw>(?:u8|u|U|L)?R"(?P<delim>[^()\\\s"]{0,16})\(.*?\)(?P=delim)")
    | (?P<str>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.|\\\n)*")
    | (?P<num>\b\d(?:[\w']|(?<=[eEpP])[+-])*)
    | (?P<chr>(?:u8|u|U|L)?'(?:[^'\\\n]|\\.)*')
    """,
    re.VERBOSE | re.DOTALL,
)


def _blank(chunk: str) -> str:
    return "".join(ch if ch == "\n" else " " for ch in chunk)


def mask(source: str) -> str:
    """The source with comments and the insides of string and character literals blanked: same length, same newlines."""

    def blank(m: re.Match) -> str:
        text = m.group(0)
        kind = m.lastgroup
        if kind == "num":
            return text
        if kind in ("line", "block"):
            return _blank(text)
        if kind == "raw":
            open_end = text.index("(") + 1
            close_start = len(text) - len(m.group("delim")) - 2
            return text[:open_end] + _blank(text[open_end:close_start + 1]) + text[close_start + 1:]
        quote = text.index(text[-1])  # the first quote of the literal (after a prefix)
        return text[:quote + 1] + _blank(text[quote + 1:-1]) + text[-1]

    return _MASK.sub(blank, source)


def exempt_lines(source: str) -> Set[int]:
    """The 1-based lines that a `//-NoStd:` marker covers: a marker after code covers its own line; a line that holds only the comment
    covers the next line too."""
    exempt: Set[int] = set()
    for index, line in enumerate(source.split("\n"), start=1):
        if STD_MARKER.search(line):
            exempt.add(index)
            if line.lstrip().startswith("//"):
                exempt.add(index + 1)
    return exempt


# ---- the walk -----------------------------------------------------------------------------------------------------------------


def repository_of(root: Path) -> Path:
    """The nearest folder at or above `root` with a `.git` (a folder, or a file in a submodule); `root` itself when there is none."""
    resolved = root.resolve()
    for folder in (resolved, *resolved.parents):
        if (folder / ".git").exists():
            return folder
    return resolved


def is_skipped_file(relative: str) -> bool:
    """`relative` is the path of the file from its repository: `--root tests` must skip the twin tests too, and a folder above the
    checkout must not skip anything."""
    padded = "/" + relative.lstrip("/")
    if any(padded.endswith("/" + name) for name in SKIPPED_FILES):
        return True
    return any(("/" + run + "/") in padded or padded.endswith("/" + run) for run in SKIPPED_PATH_RUNS)


def path_in_repository(path: Path, repository: Path) -> str:
    """The path of `path` from `repository`, symlinks resolved; a link that leaves the repository keeps the path it was found under."""
    try:
        return path.resolve().relative_to(repository).as_posix()
    except ValueError:
        return Path(os.path.abspath(path)).relative_to(repository).as_posix()


def walk(
    root: Path,
    suffixes: Set[str] = SOURCE_SUFFIXES,
    excludes: Sequence[str] = (),
    skip_submodules: bool = False,
) -> Iterator[Path]:
    """The source files under `root` that the type rules look at, sorted folder by folder.

    Skips the folders of SKIPPED_FOLDERS and SKIPPED_FOLDER_PREFIXES, the files of `is_skipped_file`, every `excludes` entry (a path
    relative to `root`, a folder or a file) and, with `skip_submodules`, a folder with a `.git` entry below `root` (a submodule checks
    itself)."""
    repository = repository_of(root)
    skipped = {(root / e).resolve() for e in excludes}
    for folder, dirs, files in os.walk(root):
        here = Path(folder)
        dirs[:] = sorted(
            d
            for d in dirs
            if d not in SKIPPED_FOLDERS
            and not d.startswith(SKIPPED_FOLDER_PREFIXES)
            and not (skip_submodules and (here / d / ".git").exists())
            and (here / d).resolve() not in skipped
        )
        for name in sorted(files):
            path = here / name
            if path.suffix in suffixes and path.resolve() not in skipped and not is_skipped_file(path_in_repository(path, repository)):
                yield path
