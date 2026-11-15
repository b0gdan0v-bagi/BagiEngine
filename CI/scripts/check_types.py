#!/usr/bin/env python3
"""Holds the type rules of the engine: no raw `std::` / `eastl::` type that the type layer replaces, and no allocating standard call.

The code spells the `BECore` names (`String`, `Vector`, `Optional`, ... : `src/Modules/BECore/Types/CLAUDE.md`), `Format` and `ToString`
(`Format/CLAUDE.md`), not the types and calls they stand for. A real boundary (a standard or third-party API) says why on its line with
`//-NoStd: <why>` (no space after `//`; on the line itself, or alone on the line before it).

    python3 engine/CI/scripts/check_types.py --root . --exclude <own vendored> [--baseline <file>]    (a game)
    python3 CI/scripts/check_types.py --root . --baseline CI/types_baseline.txt                       (the engine)

Scanned: `.h .hpp .hh .hxx .inl .cpp .cc .cxx .mm`, on the text with comments and string literals blanked (`nostd.mask`). The marker and
the skips are the ones of `tools/codemod/codemod.py` (`CI/scripts/nostd.py`): the vendored zones, `external`, `build*`, the type layer
(`BECore/Types`), the format layer (`BECore/Format`), the reflection traits and the twin tests of the aliases. Also skipped: a folder
with a `.git` entry below the root (a game's `engine/` submodule checks itself) and every `--exclude <path relative to root>`
(repeatable; a folder or a file, for a game's own vendored code).

The rules are the table `RULES` below: one line is one rule, and the companions that stay `std::` (`std::visit`, `std::get`,
`std::monostate`, `std::nullopt`, `std::tuple_size_v`, ...) are in no rule. The threading rules are `lock` (`std::mutex`, the guards and
the condition variables) and `thread` (`std::thread`, `std::jthread`; `std::thread::id`, `std::this_thread` and `std::once_flag` are no
hit): the engine's own are in `BECore/Threading` (`Threading/CLAUDE.md`). A rule may be allowed in folders: `tests` allows `regex`,
`stringstream`, `include-stream`, `lock` and `thread`, and `BECore/Threading`, the layer that wraps the raw locks, allows `lock` and
`thread` (a folder matches when its segments are a run of the file's path from the root, so `tests` is also `tools/stress/tests`);
`--allow <folder>:<rule>[,<rule>]` (repeatable) allows more in one root.

`--baseline <file>` lists what a root has not migrated yet, one `<path> <rule id> <count>` per line (sorted, `#` comments). A count above
the baseline is `new` (every hit of that file and rule is printed), a count below it, or a path with no hits, is `fixed` and fails until
`--update-baseline` rewrites the file, so the list only shrinks: a change that adds a line or raises a count is a finding. Without
`--baseline` every hit is new. `--update-baseline` keeps the comment lines at the head of an existing file.

Output: one line `path:line: [rule id] name: advice` per new hit, `fixed: ...` lines, then a summary. Exit code 0 when clean, 1 with
findings, 2 on a bad argument. Standard library only.
"""

from __future__ import annotations

import argparse
import bisect
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nostd import SOURCE_SUFFIXES, exempt_lines, mask, walk  # noqa: E402

EXIT_OK, EXIT_FINDINGS, EXIT_ERROR = 0, 1, 2

SCAN_SUFFIXES = SOURCE_SUFFIXES | {".hh", ".hxx", ".cc", ".cxx", ".mm"}
DEFAULT_BASELINE_HEADER = (
    "# Type rules not migrated yet (check_types.py): <path> <rule id> <count>. A fix removes or lowers lines, nothing is added or raised.\n"
    "# Regenerate: python3 <engine>/CI/scripts/check_types.py --root . --baseline {path} --update-baseline\n"
)
TESTS_FOLDER = "tests"
NAMESPACES = "(?:std|eastl)"


@dataclass(frozen=True)
class Rule:
    """One rule. `regex` has a group `name` (the matched text, printed); `folders_allowed` are folders where the rule does not apply.
    Several rules may share one `id` (the type rules do): they count and are allowed together."""

    id: str
    regex: "re.Pattern[str]"
    advice: str
    folders_allowed: Tuple[str, ...] = ()


def qualified(names: str, namespaces: str = NAMESPACES) -> "re.Pattern[str]":
    """`namespaces::name` for a regex alternation `names`, not part of a longer name (`std::tuple_size_v` is no `std::tuple`)."""
    return re.compile(r"(?<![\w:])(?:::)?(?P<name>" + namespaces + r"::(?:" + names + r"))\b")


# The types of the type layer: (the advice's alias, the `std::` / `eastl::` names it replaces)
TYPES: Tuple[Tuple[str, str], ...] = (
    ("String", "string"),
    ("StringView", "string_view"),
    ("`String` (UTF-8); there is no wide string alias", "wstring"),
    ("Vector", "vector"),
    ("SmallVector or FixedVector", "fixed_vector"),
    ("FixedString", "fixed_string"),
    ("Array", "array"),
    ("Span", "span"),
    ("HashMap", "unordered_map|hash_map"),
    ("Map", "map"),
    ("Set", "set"),
    ("HashSet", "unordered_set|hash_set"),
    ("Optional", "optional"),
    ("UniquePtr", "unique_ptr"),
    ("SharedPtr", "shared_ptr"),
    ("MakeUnique", "make_unique"),
    ("MakeShared", "make_shared"),
    ("Pair", "pair"),
    ("MakePair", "make_pair"),
    ("Variant", "variant"),
    ("Tuple", "tuple"),
)
TYPE_ADVICE = "use the `{alias}` of BECore/Types (Types/CLAUDE.md) or mark `//-NoStd: <why>`"
MARK_ADVICE = "mark a real boundary `//-NoStd: <why>`"
# The threading names: (the advice, the `std::` names it covers). A test keeps raw locks and threads, and `BECore/Threading` wraps them.
LOCKS: Tuple[Tuple[str, str], ...] = (
    ("use `Mutex` (Threading/CLAUDE.md), or ", "mutex|recursive_mutex|timed_mutex|recursive_timed_mutex"),
    ("use `SharedMutex` (Threading/CLAUDE.md), or ", "shared_mutex|shared_timed_mutex"),
    ("use `LockGuard` (Threading/CLAUDE.md), or ", "lock_guard"),
    ("use `UniqueLock` (Threading/CLAUDE.md), or ", "unique_lock"),
    ("use `SharedLock` (Threading/CLAUDE.md), or ", "shared_lock"),
    ("use `LockGuard`, or a std guard over the engine's lock types (Threading/CLAUDE.md), or ", "scoped_lock"),
    ("use `ConditionVariable` (Threading/CLAUDE.md), or ", "condition_variable|condition_variable_any"),
)
LOCK_FOLDERS = (TESTS_FOLDER, "BECore/Threading")

RULES: Tuple[Rule, ...] = (
    # the types the type layer replaces
    *(Rule("type", qualified(names), TYPE_ADVICE.format(alias=alias)) for alias, names in TYPES),
    # no alias yet (`callables-move-only` makes the type)
    Rule("function", qualified("function"), "no alias exists yet (entry `callables-move-only`); " + MARK_ADVICE),
    # allocating calls
    Rule("to_string", qualified("to_string"), "use `ToString` or `Format` (Format/CLAUDE.md), or " + MARK_ADVICE),
    Rule("sto", qualified(r"sto[ifdlu]+"), "parses through a temporary `std::string`; " + MARK_ADVICE),
    Rule("regex", qualified(r"(?:basic_)?regex\w*|sregex\w*|cregex\w*|smatch|cmatch|match_results"), "`std::regex` allocates; " + MARK_ADVICE, (TESTS_FOLDER,)),
    Rule("stringstream", qualified(r"(?:basic_)?[io]?stringstream"), "use `Format` / `AppendFormat`, or " + MARK_ADVICE, (TESTS_FOLDER,)),
    Rule("fstream", qualified(r"(?:basic_)?[io]?fstream"), "files go through the file layer (entry `engine-file-layer`); " + MARK_ADVICE),
    Rule("getline", qualified("getline"), "reads into a `std::string`; " + MARK_ADVICE),
    Rule("async", qualified("async"), "use the `TaskManager` (TaskSystem/CLAUDE.md), or " + MARK_ADVICE),
    Rule("format", qualified("v?format", "std"), "use `Format` (Format/CLAUDE.md), or " + MARK_ADVICE),
    Rule("stable-sort", qualified("stable_sort|inplace_merge|stable_partition"), "allocates a buffer; " + MARK_ADVICE),
    Rule("directory-iterator", re.compile(r"(?<![\w:])(?:::)?(?P<name>std::filesystem::(?:recursive_)?directory_iterator)\b"), "allocates per entry; " + MARK_ADVICE),
    Rule("fmt", re.compile(r"(?<![\w:])(?:::)?(?P<name>fmt::)(?=\w)"), "use the name of the format layer (Format/CLAUDE.md), or " + MARK_ADVICE),
    # threading primitives: the engine's own are in BECore/Threading (Threading/CLAUDE.md)
    *(Rule("lock", qualified(names, "std"), advice + MARK_ADVICE, LOCK_FOLDERS) for advice, names in LOCKS),
    Rule(
        "thread",
        qualified(r"j?thread(?!::id\b)", "std"),
        "use `Thread(name, body)` or `Thread::HardwareConcurrency()` (Threading/CLAUDE.md), or " + MARK_ADVICE,
        LOCK_FOLDERS,
    ),
    # the stream headers
    Rule(
        "include-stream",
        re.compile(r"^[ \t]*#[ \t]*include[ \t]*(?P<name><(?:fstream|sstream|iostream)>)", re.MULTILINE),
        "streams allocate and bypass the file layer; " + MARK_ADVICE,
        (TESTS_FOLDER,),
    ),
)
RULE_IDS = frozenset(rule.id for rule in RULES)


@dataclass(frozen=True)
class Hit:
    path: str
    line: int
    rule: str
    name: str
    advice: str

    def text(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.name}: {self.advice}"


@dataclass
class Result:
    checked: int = 0
    hits: List[Hit] = field(default_factory=list)  # every hit that is not exempt and not allowed
    new: List[Hit] = field(default_factory=list)  # the hits of the (file, rule) pairs above the baseline
    fixed: List[str] = field(default_factory=list)  # the baseline lines that are above the tree now

    @property
    def findings(self) -> List[str]:
        return [hit.text() for hit in self.new] + self.fixed

    def counts(self) -> Dict[Tuple[str, str], int]:
        counts: Dict[Tuple[str, str], int] = {}
        for hit in self.hits:
            key = (hit.path, hit.rule)
            counts[key] = counts.get(key, 0) + 1
        return counts


def folder_matches(folder: str, relative: str) -> bool:
    """`folder` (`tests`, `tools/stress/tests`) is a run of the folder segments of `relative`, the path from the checked root."""
    wanted = folder.strip("/").split("/")
    segments = relative.split("/")[:-1]
    return any(segments[i : i + len(wanted)] == wanted for i in range(len(segments) - len(wanted) + 1))


def effective_allowed(allow: Dict[str, Tuple[str, ...]]) -> Dict[str, Tuple[str, ...]]:
    """The folders per rule id: the rules' own, then the `--allow` ones of this root."""
    allowed: Dict[str, Tuple[str, ...]] = {}
    for rule in RULES:
        allowed[rule.id] = tuple(dict.fromkeys(allowed.get(rule.id, ()) + rule.folders_allowed + allow.get(rule.id, ())))
    return allowed


def scan_source(source: str, relative: str, allowed: Dict[str, Tuple[str, ...]]) -> List[Hit]:
    masked = mask(source)
    exempt = exempt_lines(source)
    starts = [0] + [m.end() for m in re.finditer(r"\n", source)]
    hits = []
    for rule in RULES:
        if any(folder_matches(folder, relative) for folder in allowed[rule.id]):
            continue
        for match in rule.regex.finditer(masked):
            line = bisect.bisect_right(starts, match.start("name"))
            if line not in exempt:
                hits.append(Hit(relative, line, rule.id, match.group("name"), rule.advice))
    hits.sort(key=lambda hit: (hit.line, hit.rule))
    return hits


def scan(root: Path, excludes: Sequence[str] = (), allow: Optional[Dict[str, Tuple[str, ...]]] = None) -> Result:
    allowed = effective_allowed(allow or {})
    result = Result()
    for path in walk(root, SCAN_SUFFIXES, excludes, skip_submodules=True):
        result.checked += 1
        relative = path.relative_to(root).as_posix()
        source = path.read_text(encoding="utf-8", errors="replace")
        result.hits.extend(scan_source(source, relative, allowed))
    return result


def check(
    root: Path,
    excludes: Sequence[str] = (),
    allow: Optional[Dict[str, Tuple[str, ...]]] = None,
    baseline: Optional[Dict[Tuple[str, str], int]] = None,
) -> Result:
    """Scans `root` and sets `new` and `fixed` against `baseline` (`(path, rule id)` to count; none: every hit is new)."""
    result = scan(root, excludes, allow)
    baseline = baseline or {}
    counts = result.counts()
    over = {key for key, count in counts.items() if count > baseline.get(key, 0)}
    result.new = [hit for hit in result.hits if (hit.path, hit.rule) in over]
    for key in sorted(baseline):
        now = counts.get(key, 0)
        if now < baseline[key]:
            result.fixed.append(f"fixed: {key[0]} [{key[1]}]: baseline {baseline[key]}, now {now}")
    return result


def read_baseline(path: Optional[Path]) -> Dict[Tuple[str, str], int]:
    baseline: Dict[Tuple[str, str], int] = {}
    if path is None or not path.exists():
        return baseline
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 3 or not parts[2].isdigit() or parts[1] not in RULE_IDS:
            raise ValueError(f"{path}:{number}: expected '<path> <rule id> <count>' with a known rule id")
        baseline[(parts[0], parts[1])] = int(parts[2])
    return baseline


def write_baseline(path: Path, counts: Dict[Tuple[str, str], int]) -> None:
    header = DEFAULT_BASELINE_HEADER.format(path=path)
    if path.exists():  # keep the comment block the owner wrote at the head
        kept = []
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.startswith("#"):
                break
            kept.append(line)
        if kept:
            header = "".join(line + "\n" for line in kept)
    body = "".join(f"{p} {rule} {count}\n" for (p, rule), count in sorted(counts.items()))
    path.write_text(header + body, encoding="utf-8")


def parse_allow(values: Sequence[str]) -> Dict[str, Tuple[str, ...]]:
    """`<folder>:<rule>[,<rule>]` entries as {rule id: folders}; ValueError on a bad entry."""
    allow: Dict[str, Tuple[str, ...]] = {}
    for value in values:
        folder, _, rules = value.partition(":")
        ids = [name for name in rules.split(",") if name]
        unknown = [name for name in ids if name not in RULE_IDS]
        if not folder.strip("/") or not ids or unknown:
            raise ValueError(f"--allow {value!r}: expected <folder>:<rule>[,<rule>] with rules from {', '.join(sorted(RULE_IDS))}")
        for name in ids:
            allow[name] = allow.get(name, ()) + (folder.strip("/"),)
    return allow


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", type=Path, help="the repository root")
    parser.add_argument("--exclude", action="append", default=[], help="a folder or file relative to the root (repeatable)")
    parser.add_argument("--allow", action="append", default=[], help="<folder>:<rule>[,<rule>]: allow rules in a folder (repeatable)")
    parser.add_argument("--baseline", type=Path, help="the file of the hits known and not migrated yet")
    parser.add_argument("--update-baseline", action="store_true", help="rewrite --baseline with the current hits")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return EXIT_ERROR if error.code else EXIT_OK
    if not args.root.is_dir():
        print(f"--root {args.root} is not a folder", file=sys.stderr)
        return EXIT_ERROR
    if args.update_baseline and args.baseline is None:
        print("--update-baseline needs --baseline", file=sys.stderr)
        return EXIT_ERROR
    try:
        allow = parse_allow(args.allow)
        baseline = {} if args.update_baseline else read_baseline(args.baseline)
    except ValueError as error:
        print(error, file=sys.stderr)
        return EXIT_ERROR
    result = check(args.root, args.exclude, allow, baseline)
    if args.update_baseline:
        write_baseline(args.baseline, result.counts())
        print(f"baseline {args.baseline} rewritten: {len(result.hits)} hits in {len(result.counts())} lines, {result.checked} files checked")
        return EXIT_OK
    for line in result.findings:
        print(line)
    if result.fixed:
        print("a baseline line is above the tree now: lower or remove it (--update-baseline)")
    print(
        f"checked {result.checked} files, hits {len(result.hits)}, baselined {len(result.hits) - len(result.new)}, "
        f"new {len(result.new)}, fixed {len(result.fixed)}"
    )
    return EXIT_FINDINGS if result.findings else EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
