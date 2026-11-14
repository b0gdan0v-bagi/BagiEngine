#!/usr/bin/env python3
"""Moves the `eastl::` and `std::` type names of C++ sources to the BECore type layer (`BECore/Types/`).

    python3 tools/codemod/codemod.py --root <folder> --family <optionals|views|strings|containers|maps|utility|all>
                                     [--dry-run] [--fail-on-change] [--report]

It walks `*.h *.hpp *.inl *.cpp` under `--root`, skips the vendored zones, the type layer and the reflection traits, and works on code
only: comments and string literals are blanked for matching and kept as they were. A line that must keep a `std::` type carries
`//-NoStd: <why>` on it or on the line before; the tool never touches such a line. A second run changes nothing. Standard library only.
`CLAUDE.md` has the contract and the rules of each family.

Exit code: 0 ok, 1 `--fail-on-change` found something to change, 2 an error (a bad root, a file that is not UTF-8).
"""

from __future__ import annotations

import argparse
import bisect
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Sequence, Set, Tuple

EXIT_OK, EXIT_CHANGES, EXIT_ERROR = 0, 1, 2

SOURCE_SUFFIXES = {".h", ".hpp", ".inl", ".cpp"}
FAMILIES = ("optionals", "views", "strings", "containers", "maps", "utility")
# Folders never walked, at any depth (the vendored zones of the engine and the build and tool folders)
SKIPPED_FOLDERS = {"EABase", "EnTT", "fmt", "imgui", "nlohmann", "pugixml", "RmlUi", "stb", "tracy", "external", ".venv", ".git", "node_modules", "__pycache__"}
SKIPPED_FOLDER_PREFIXES = ("build",)
# Paths (relative to the root, `/` separated) that are skipped: a folder prefix as a segment run, or a file
SKIPPED_PATH_RUNS = ("src/Widgets/Icons", "BECore/Types")
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
EASTL_OPTIONAL_HEADER = re.compile(r"^[ \t]*#[ \t]*include[ \t]*<EASTL/optional\.h>[ \t]*\r?\n", re.MULTILINE)
EASTL_OPTIONAL_LEFT = re.compile(r"(?<![\w:])(?:::)?eastl::(?:optional|nullopt|nullopt_t|make_optional|in_place|in_place_t|in_place_type|in_place_index|bad_optional_access)\b")
# `//-NoStd: reason` exactly (no space after `//`): the old `// std: reason` and an ordinary comment that starts with a `std::` name are no markers
STD_MARKER = re.compile(r"//-NoStd:")
MAX_PASSES = 8

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


# ---- scanning -----------------------------------------------------------------------------------------------------------------

_OPENERS = "([{"
_CLOSERS = ")]}"


def match_close(masked: str, open_pos: int) -> int:
    """The index of the bracket that closes `([{` at `open_pos` (angle brackets are not counted), -1 when there is none."""
    depth = 0
    for i in range(open_pos, len(masked)):
        ch = masked[i]
        if ch in _OPENERS:
            depth += 1
        elif ch in _CLOSERS:
            depth -= 1
            if depth == 0:
                return i
    return -1


def split_args(masked: str, start: int, end: int, angles: bool) -> List[Tuple[int, int]]:
    """The top-level arguments between `start` and `end` as (start, end) ranges stripped of blanks. With `angles` the commas inside
    `<>` do not split (template arguments); without, only `()[]{}` nest (call arguments, where `<` may be a comparison)."""
    ranges: List[Tuple[int, int]] = []
    depth = 0
    angle = 0
    begin = start
    i = start
    while i < end:
        ch = masked[i]
        if ch in _OPENERS:
            depth += 1
        elif ch in _CLOSERS:
            depth -= 1
        elif angles and depth == 0:
            if ch == "<":
                if masked.startswith("<<", i):
                    i += 1
                else:
                    angle += 1
            elif ch == ">" and (i == 0 or masked[i - 1] != "-"):
                angle -= 1
            elif ch == "," and angle == 0:
                ranges.append((begin, i))
                begin = i + 1
        elif not angles and ch == "," and depth == 0:
            ranges.append((begin, i))
            begin = i + 1
        i += 1
    ranges.append((begin, end))
    stripped = []
    for a, b in ranges:
        while a < b and masked[a].isspace():
            a += 1
        while b > a and masked[b - 1].isspace():
            b -= 1
        stripped.append((a, b))
    return stripped


def template_args(masked: str, lt: int) -> Optional[Tuple[List[Tuple[int, int]], int]]:
    """The arguments of the template argument list opened by the `<` at `lt` and the index of its closing `>`; None when it does not
    close before a `;` (it was a comparison)."""
    depth = 0
    angle = 1
    i = lt + 1
    n = len(masked)
    while i < n:
        ch = masked[i]
        if ch in _OPENERS:
            depth += 1
        elif ch in _CLOSERS:
            depth -= 1
            if depth < 0:
                return None
        elif depth == 0:
            if ch == ";":
                return None
            if ch == "<":
                if masked.startswith("<<", i):
                    i += 1
                else:
                    angle += 1
            elif ch == ">" and masked[i - 1] != "-":
                angle -= 1
                if angle == 0:
                    return split_args(masked, lt + 1, i, angles=True), i
        i += 1
    return None


def skip_blanks(masked: str, pos: int) -> int:
    while pos < len(masked) and masked[pos].isspace():
        pos += 1
    return pos


# ---- the rules ----------------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Name:
    """One `eastl::` / `std::` name that becomes an alias."""

    family: str
    source: str
    alias: str
    max_args: Optional[int] = None  # more template arguments than this (an allocator, ...): reported, left
    template: bool = True  # a class template: a name with no `<` before `{`, `(` or a variable is CTAD (reported, left)
    pattern: "re.Pattern[str]" = field(init=False, compare=False, repr=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "pattern", qualified(self.source))


def qualified(name: str) -> "re.Pattern[str]":
    return re.compile(r"(?<![\w:])(?:::)?" + re.escape(name) + r"(?!\w)")


NAMES: Tuple[Name, ...] = (
    Name("optionals", "eastl::optional", "Optional", max_args=1),
    Name("optionals", "std::optional", "Optional", max_args=1),
    Name("views", "eastl::string_view", "StringView", template=False),
    Name("views", "std::string_view", "StringView", template=False),
    Name("strings", "eastl::string", "String", template=False),
    Name("strings", "std::string", "String", template=False),
    Name("containers", "eastl::vector", "Vector", max_args=1),
    Name("containers", "std::vector", "Vector", max_args=1),
    Name("containers", "eastl::array", "Array", max_args=2),
    Name("containers", "std::array", "Array", max_args=2),
    Name("containers", "eastl::span", "Span", max_args=2),
    Name("containers", "eastl::unique_ptr", "UniquePtr", max_args=2),
    Name("containers", "std::unique_ptr", "UniquePtr", max_args=2),
    Name("maps", "eastl::hash_map", "HashMap", max_args=4),
    Name("maps", "eastl::unordered_map", "HashMap", max_args=4),
    Name("maps", "std::unordered_map", "HashMap", max_args=4),
    Name("maps", "eastl::map", "Map", max_args=3),
    Name("maps", "std::map", "Map", max_args=3),
    Name("maps", "eastl::fixed_hash_map", "FixedHashMap", max_args=7),
    Name("maps", "eastl::set", "Set", max_args=2),
    Name("maps", "std::set", "Set", max_args=2),
    Name("maps", "eastl::hash_set", "HashSet", max_args=3),
    Name("maps", "eastl::unordered_set", "HashSet", max_args=3),
    Name("maps", "std::unordered_set", "HashSet", max_args=3),
    Name("utility", "eastl::pair", "Pair", max_args=2),
    Name("utility", "std::pair", "Pair", max_args=2),
    Name("utility", "eastl::make_pair", "MakePair", template=False),
    Name("utility", "std::make_pair", "MakePair", template=False),
    Name("utility", "eastl::variant", "Variant"),
    Name("utility", "std::variant", "Variant"),
    Name("utility", "std::tuple", "Tuple"),
    Name("utility", "eastl::shared_ptr", "SharedPtr", max_args=1),
    Name("utility", "std::shared_ptr", "SharedPtr", max_args=1),
    Name("utility", "eastl::make_shared", "MakeShared", template=False),
    Name("utility", "std::make_shared", "MakeShared", template=False),
    Name("utility", "eastl::make_unique", "MakeUnique", template=False),
    Name("utility", "std::make_unique", "MakeUnique", template=False),
)

# `eastl::nullopt` and friends: the optional is `std::optional`, so its companions are the standard ones.
OPTIONAL_COMPANIONS = (
    ("eastl::nullopt_t", "std::nullopt_t"),
    ("eastl::nullopt", "std::nullopt"),
    ("eastl::make_optional", "std::make_optional"),
    ("eastl::in_place_t", "std::in_place_t"),
    ("eastl::in_place", "std::in_place"),
)

# The `std::` companions of an `eastl::variant`: `Variant` is `std::variant`, so a file that had one moves these by hand
EASTL_VARIANT = qualified("eastl::variant")
EASTL_VARIANT_COMPANIONS = re.compile(r"(?<![\w:])(?:::)?eastl::(?:get|get_if|visit|holds_alternative|monostate|variant_size|variant_alternative|bad_variant_access)(?!\w)")
# What follows a class template name written without arguments in a declaration or a call: `{`, `(`, or a variable and one of those or `=`
CTAD_AFTER = re.compile(r"\s*(?:[{(]|\w+\s*[{(=](?!=))")
FIXED_VECTOR = qualified("eastl::fixed_vector")
FIXED_STRING = qualified("eastl::fixed_string")
TO_STRING_EASTL = qualified("eastl::to_string")
STD_TO_STRING = qualified("std::to_string")
STRING_CALL = re.compile(r"(?<![\w:])(?:::)?eastl::string\s*[({]\s*std::to_string\s*\(")
VIEW_CALL = re.compile(r"(?<![\w:])(?:::)?(eastl|std)::string_view\s*[({]")
LOOKUP_TEMPORARY = re.compile(r"\.\s*(?:find|contains|count|at)\s*\(\s*(?:(?:eastl|std|BECore)::)?(?:string|String)\s*[({]")
BECORE_NAMESPACE = re.compile(r"\bnamespace\s+BECore\b(?:\s*::\s*\w+)*\s*\{")
USING_BECORE = re.compile(r"\busing\s+namespace\s+BECore\s*;")


@dataclass
class Edit:
    start: int
    end: int
    text: str
    rule: str


@dataclass
class Note:
    line: int
    rule: str
    message: str


class Pass:
    """One look at one file: the edits and notes its rules find in the current text."""

    def __init__(self, source: str, families: Set[str], had_eastl_variant: bool = False) -> None:
        self.source = source
        self.masked = mask(source)
        self.families = families
        self.had_eastl_variant = had_eastl_variant
        self.edits: List[Edit] = []
        self.notes: List[Note] = []
        self._line_starts = [0] + [m.end() for m in re.finditer(r"\n", source)]
        self._exempt_lines = self._find_exempt_lines()
        self._becore_ranges = self._find_becore_ranges()
        self._using_becore = [m.start() for m in USING_BECORE.finditer(self.masked)]

    # -- helpers --

    def line_of(self, pos: int) -> int:
        return bisect.bisect_right(self._line_starts, pos)  # 1-based

    def _find_exempt_lines(self) -> Set[int]:
        exempt: Set[int] = set()
        for index, line in enumerate(self.source.split("\n"), start=1):
            if STD_MARKER.search(line):
                exempt.add(index)
                if line.lstrip().startswith("//"):  # a marker line of its own covers the next line; one after code covers its own line only
                    exempt.add(index + 1)
        return exempt

    def exempt(self, pos: int) -> bool:
        return self.line_of(pos) in self._exempt_lines

    def _find_becore_ranges(self) -> List[Tuple[int, int]]:
        ranges = []
        for m in BECORE_NAMESPACE.finditer(self.masked):
            close = match_close(self.masked, m.end() - 1)
            ranges.append((m.end(), close if close >= 0 else len(self.masked)))
        return ranges

    def alias(self, name: str, pos: int) -> str:
        """`name`, or `BECore::name` where no `namespace BECore` is open at `pos` and `using namespace BECore` has not been seen."""
        if any(a <= pos < b for a, b in self._becore_ranges):
            return name
        if any(u < pos for u in self._using_becore):
            return name
        return "BECore::" + name

    def replace(self, match: re.Match, text: str, rule: str) -> None:
        self.edits.append(Edit(match.start(), match.end(), text, rule))

    def note(self, pos: int, rule: str, message: str) -> None:
        self.notes.append(Note(self.line_of(pos), rule, message))

    def arguments_after(self, pos: int) -> Optional[Tuple[List[Tuple[int, int]], int, int]]:
        """The template arguments that follow the name ending at `pos`: (ranges, index of `<`, index of `>`); None without a list."""
        lt = skip_blanks(self.masked, pos)
        if lt >= len(self.masked) or self.masked[lt] != "<":
            return None
        found = template_args(self.masked, lt)
        if found is None:
            return None
        return found[0], lt, found[1]

    def text(self, span: Tuple[int, int]) -> str:
        return self.source[span[0]:span[1]]

    # -- the rules --

    def collect(self) -> None:
        if "optionals" in self.families:
            self._optional_companions()
            self._optional_header()
        if "views" in self.families:
            self._view_shapes()
        if "strings" in self.families:
            self._string_shapes()
            self._fixed_string()
            self._to_string()
        if "containers" in self.families:
            self._fixed_vector()
        if "maps" in self.families:
            self._lookup_temporaries()
        if "utility" in self.families and self.had_eastl_variant:
            self._variant_companions()
        for name in NAMES:
            if name.family in self.families:
                self._simple(name)

    def _simple(self, name: Name) -> None:
        for m in name.pattern.finditer(self.masked):
            if self.exempt(m.start()):
                continue
            rule = f"{name.source} -> {name.alias}"
            found = self.arguments_after(m.end())
            if found is None and name.template and CTAD_AFTER.match(self.masked, m.end()):
                self.note(m.start(), rule, f"{name.source} without template arguments (CTAD): left, write the arguments")
                continue
            if found is not None and name.max_args is not None:
                ranges = found[0]
                if len(ranges) > name.max_args:
                    self.note(m.start(), rule, f"{name.source} with {len(ranges)} template arguments (an allocator?): left")
                    continue
                if name.alias == "Optional" and self.text(ranges[0]).endswith("&"):
                    self.note(m.start(), rule, f"{name.source} of a reference: left")
                    continue
            self.replace(m, self.alias(name.alias, m.start()), rule)

    def _variant_companions(self) -> None:
        for m in EASTL_VARIANT_COMPANIONS.finditer(self.masked):
            if not self.exempt(m.start()):
                self.note(m.start(), "eastl::variant companion", f"{m.group(0).strip(':')} of an eastl::variant (now std::variant): the std:: one, by hand")

    def _optional_companions(self) -> None:
        for source, target in OPTIONAL_COMPANIONS:
            for m in qualified(source).finditer(self.masked):
                if not self.exempt(m.start()):
                    self.replace(m, target, f"{source} -> {target}")

    def _optional_header(self) -> None:
        header = EASTL_OPTIONAL_HEADER.search(self.source)
        if header and not EASTL_OPTIONAL_LEFT.search(self.masked):
            self.edits.append(Edit(header.start(), header.end(), "", "#include <EASTL/optional.h> dropped"))

    def _view_shapes(self) -> None:
        """`std::string_view{X.data(), X.size()}` -> `ToStd(X)`, `eastl::string_view(X.data(), X.size())` -> `FromStd(X)`."""
        for m in VIEW_CALL.finditer(self.masked):
            if self.exempt(m.start()):
                continue
            open_pos = m.end() - 1
            close = match_close(self.masked, open_pos)
            if close < 0:
                continue
            ranges = split_args(self.masked, open_pos + 1, close, angles=False)
            if len(ranges) != 2:
                continue
            first = re.fullmatch(r"(.+)\.data\(\)", self.text(ranges[0]), re.DOTALL)
            second = re.fullmatch(r"(.+)\.size\(\)", self.text(ranges[1]), re.DOTALL)
            if not first or not second or first.group(1).strip() != second.group(1).strip():
                continue
            function = "ToStd" if m.group(1) == "std" else "FromStd"
            source = f"{m.group(1)}::string_view(X.data(), X.size()) -> {function}(X)"
            self.edits.append(Edit(m.start(), close + 1, f"{self.alias(function, m.start())}({first.group(1).strip()})", source))

    def _string_shapes(self) -> None:
        """`eastl::string(std::to_string(X).c_str())` -> `ToString(X)`."""
        for m in STRING_CALL.finditer(self.masked):
            if self.exempt(m.start()):
                continue
            inner_open = m.end() - 1
            inner_close = match_close(self.masked, inner_open)
            outer_open = skip_blanks(self.masked, m.start() + self.masked[m.start():].index("string") + len("string"))
            outer_close = match_close(self.masked, outer_open) if outer_open < len(self.masked) else -1
            if inner_close < 0 or outer_close < 0:
                continue
            tail = re.match(r"\s*\.\s*c_str\s*\(\s*\)\s*", self.masked[inner_close + 1:outer_close])
            if not tail or inner_close + 1 + tail.end() != outer_close:
                continue
            argument = self.source[inner_open + 1:inner_close].strip()
            self.edits.append(Edit(m.start(), outer_close + 1, f"{self.alias('ToString', m.start())}({argument})", "eastl::string(std::to_string(x).c_str()) -> ToString(x)"))

    def _to_string(self) -> None:
        for m in TO_STRING_EASTL.finditer(self.masked):
            if not self.exempt(m.start()):
                self.replace(m, self.alias("ToString", m.start()), "eastl::to_string -> ToString")
        shapes = [(e.start, e.end) for e in self.edits if e.rule.startswith("eastl::string(std::to_string")]
        for m in STD_TO_STRING.finditer(self.masked):
            if self.exempt(m.start()) or any(a <= m.start() < b for a, b in shapes):
                continue
            self.note(m.start(), "std::to_string", "std::to_string left: a String made from it moves to ToString by hand")

    def _fixed_string(self) -> None:
        """`eastl::fixed_string<char, N[, Overflow]>` -> `FixedString<N[, Overflow]>`."""
        for m in FIXED_STRING.finditer(self.masked):
            if self.exempt(m.start()):
                continue
            rule = "eastl::fixed_string<char, N> -> FixedString<N>"
            found = self.arguments_after(m.end())
            if found is None:
                self.note(m.start(), rule, "eastl::fixed_string without template arguments: left")
                continue
            ranges, lt, _ = found
            if len(ranges) not in (2, 3) or self.text(ranges[0]) != "char":
                self.note(m.start(), rule, "eastl::fixed_string that is not <char, N[, Overflow]> (an allocator?): left")
                continue
            self.replace(m, self.alias("FixedString", m.start()), rule)
            self.edits.append(Edit(lt + 1, ranges[1][0], "", rule))

    def _fixed_vector(self) -> None:
        """`eastl::fixed_vector<T, N>` and `<T, N, true>` -> `SmallVector<T, N>`, `<T, N, false>` -> `FixedVector<T, N>`."""
        for m in FIXED_VECTOR.finditer(self.masked):
            if self.exempt(m.start()):
                continue
            rule = "eastl::fixed_vector -> SmallVector / FixedVector"
            found = self.arguments_after(m.end())
            if found is None:
                self.note(m.start(), rule, "eastl::fixed_vector without template arguments: left")
                continue
            ranges, _, _ = found
            overflow = self.text(ranges[2]) if len(ranges) == 3 else "true"
            if len(ranges) not in (2, 3) or overflow not in ("true", "false"):
                self.note(m.start(), rule, "eastl::fixed_vector with an allocator or an overflow that is not true / false: left")
                continue
            alias = "SmallVector" if overflow == "true" else "FixedVector"
            self.replace(m, self.alias(alias, m.start()), rule)
            if len(ranges) == 3:
                self.edits.append(Edit(ranges[1][1], ranges[2][1], "", rule))

    def _lookup_temporaries(self) -> None:
        for m in LOOKUP_TEMPORARY.finditer(self.masked):
            if not self.exempt(m.start()):
                self.note(m.start(), "map lookup", "a lookup that builds a temporary string: a map of String keys takes the view or the literal")


def resolve(edits: Sequence[Edit]) -> List[Edit]:
    """The edits in order of position; an edit inside an earlier one (longer first at the same start) waits for the next pass."""
    chosen: List[Edit] = []
    last_end = -1
    for edit in sorted(edits, key=lambda e: (e.start, -e.end)):
        if edit.start >= last_end:
            chosen.append(edit)
            last_end = edit.end
    return chosen


def apply(source: str, edits: Sequence[Edit]) -> str:
    out: List[str] = []
    pos = 0
    for edit in edits:
        out.append(source[pos:edit.start])
        out.append(edit.text)
        pos = edit.end
    out.append(source[pos:])
    return "".join(out)


def transform(source: str, families: Set[str]) -> Tuple[str, List[str], List[Note]]:
    """The source with the families applied until nothing is left to change, the rule of each replacement, the notes of the result."""
    applied: List[str] = []
    had_eastl_variant = "utility" in families and EASTL_VARIANT.search(mask(source)) is not None
    for _ in range(MAX_PASSES):
        current = Pass(source, families, had_eastl_variant)
        current.collect()
        edits = resolve(current.edits)
        if not edits:
            return source, applied, current.notes
        source = apply(source, edits)
        applied.extend(edit.rule for edit in edits)
    raise RuntimeError("the rules did not settle")


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


def walk(root: Path) -> Iterator[Path]:
    repository = repository_of(root)
    for folder, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIPPED_FOLDERS and not d.startswith(SKIPPED_FOLDER_PREFIXES))
        for name in sorted(files):
            path = Path(folder) / name
            if path.suffix in SOURCE_SUFFIXES and not is_skipped_file(path.resolve().relative_to(repository).as_posix()):
                yield path


@dataclass
class RuleStats:
    files: Set[str] = field(default_factory=set)
    replacements: int = 0


@dataclass
class Result:
    rules: Dict[str, RuleStats] = field(default_factory=dict)
    notes: List[Tuple[str, Note]] = field(default_factory=list)
    changed_files: List[str] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)

    @property
    def replacements(self) -> int:
        return sum(stats.replacements for stats in self.rules.values())


def run(root: Path, families: Sequence[str], write: bool) -> Result:
    chosen = set(families)
    result = Result()
    for path in walk(root):
        relative = path.relative_to(root).as_posix()
        try:
            with open(path, encoding="utf-8", newline="") as stream:
                source = stream.read()
        except (UnicodeDecodeError, OSError) as error:
            result.errors.append(f"{relative}: {error}")
            continue
        updated, applied, notes = transform(source, chosen)
        for rule in applied:
            stats = result.rules.setdefault(rule, RuleStats())
            stats.files.add(relative)
            stats.replacements += 1
        result.notes.extend((relative, note) for note in notes)
        if updated != source:
            result.changed_files.append(relative)
            if write:
                with open(path, "w", encoding="utf-8", newline="") as stream:
                    stream.write(updated)
    return result


def print_result(result: Result, dry_run: bool, report: bool) -> None:
    verb = "would change" if dry_run else "changed"
    for rule in sorted(result.rules):
        stats = result.rules[rule]
        print(f"{rule}: {len(stats.files)} files, {stats.replacements} replacements")
    print(f"{verb} {len(result.changed_files)} files, {result.replacements} replacements; {len(result.notes)} notes" + ("" if report else " (--report lists them)"))
    if report:
        for relative, note in sorted(result.notes, key=lambda item: (item[0], item[1].line)):
            print(f"{relative}:{note.line}: [{note.rule}] {note.message}")
    for error in result.errors:
        print(f"error: {error}", file=sys.stderr)


def parse_families(text: str) -> List[str]:
    names = [name.strip() for name in text.split(",") if name.strip()]
    if names == ["all"]:
        return list(FAMILIES)
    unknown = [name for name in names if name not in FAMILIES]
    if unknown or not names:
        raise argparse.ArgumentTypeError(f"family: {', '.join(FAMILIES)}, all (a comma list too); not {text!r}")
    return names


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Moves eastl:: and std:: type names to the BECore type layer.")
    parser.add_argument("--root", default=".", help="the folder to walk (the engine: .; a game: its src and its tests)")
    parser.add_argument("--family", required=True, type=parse_families, help="optionals, views, strings, containers, maps, utility, all (or a comma list)")
    parser.add_argument("--dry-run", action="store_true", help="write nothing, print what would change")
    parser.add_argument("--fail-on-change", action="store_true", help="write nothing; exit 1 when anything would change (the check that a family is done)")
    parser.add_argument("--report", action="store_true", help="list the notes: what the tool found and left")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve()
    if not root.is_dir():
        print(f"error: {args.root} is not a folder", file=sys.stderr)
        return EXIT_ERROR
    write = not (args.dry_run or args.fail_on_change)
    result = run(root, args.family, write)
    print_result(result, dry_run=not write, report=args.report)
    if result.errors:
        return EXIT_ERROR
    if args.fail_on_change and result.replacements:
        return EXIT_CHANGES
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
