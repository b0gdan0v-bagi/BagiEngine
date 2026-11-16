#!/usr/bin/env python3
"""
Measure whether two checkouts of the same commit share ccache hits (`.claude/rules/cmake-rules.md`, the ccache rows).

    python3 tools/ccache/ccache_share.py probe --work <scratch> --preset macos-debug \\
        --targets BECoreModule EventsModule [--cmake-arg -DX=Y ...] [--pch-only] [--show-diff] \\
        [--no-base-dir] [--user-config] [--expect-hit-rate 0.98] [--json <file>] [--wipe]

What it does. It copies the working tree of this engine (tracked files of the repository and its submodules, read from disk so
uncommitted edits count, plus the untracked files that git does not ignore) into two folders of different name and depth,
`<scratch>/one/engine` and `<scratch>/deeper/two/engine`, configures both with `-DENGINE_CCACHE_BASE_DIR=<scratch>` plus every
`--cmake-arg` (a later -D wins: `--cmake-arg -DENGINE_CCACHE_BASE_DIR=` tries no base_dir, `--no-base-dir` leaves the -D out),
and builds the targets in `one` (a cold build) and then in `two`. ccache runs with an isolated `CCACHE_DIR=<scratch>/ccache`, its
own config (only `max_size`; `--user-config` copies the owner's file into the scratch instead) and a log of its own per build:
the owner's cache is never read or written. Both folders sit under the scratch folder, which is refused inside a git repository.

How to read it. For every build the report shows the cacheable calls, hits (direct / preprocessed), misses, the wall time and
the size of the cache. The build in `two` is a copy of the first one at another path: every object that is a hit there
is shared, every miss depends on the path of the checkout (the list of missed objects is printed). The exit code is 1 when the
hit rate of `two` is below `--expect-hit-rate`, 2 when a step failed.

`--pch-only` builds only the precompiled header of `BECoreModule` in both folders (a minute, not a full build) and prints
whether the two `.pch` files are byte-equal, otherwise the number of differing bytes (`--show-diff`: the path strings that only one
of the two files has, the checkout root replaced). That is the quick test of a candidate fix: the PCH is the one object every other
object depends on. The byte count is inflated when the two root paths have different lengths (every offset after the first path
moves), so zero is the verdict that counts; the second build's line "missed objects" says whether ccache found the PCH of `one`.

The scratch folder stays after the run (look into its logs: `*-build.log`, `ccache-*.log`); `--wipe` removes a scratch folder
of an earlier probe (only one that holds the marker file this tool writes) before it starts.

Windows (`--preset windows-clang-debug`, Clang with Ninja): every cmake call runs through `CI/scripts/vs_env.py` (the Visual Studio
environment the Ninja presets leave to the caller), so the probe needs no developer prompt. Use a short scratch folder (`D:/cc/p1`):
the copy lives under `<scratch>/deeper/two/engine`, and the paths of the build inside SDL's tree reach the 260 characters of a
Windows path quickly (the probe warns when the scratch path is long). `-DENGINE_CCACHE_BASE_DIR` is passed with forward slashes, the
form CMake takes. A symlink the account may not make (`.venv`, a tracked link) becomes a directory junction or a plain copy; with
no `.venv` in the engine folder CMake takes the system Python (it needs jinja2).
Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path, PurePath

ENGINE_ROOT = Path(__file__).resolve().parents[2]
MARKER = ".ccache_share_probe"
CACHE_MAX_SIZE = "100G"  # only a cleanup threshold of the scratch cache: nothing may be evicted during a measurement
PCH_TARGET_SUFFIX = ".pch"
PCH_OWNER = "BECoreModule.dir"
DIFF_LIST_LIMIT = 12
MISS_LIST_LIMIT = 200
VS_ENV_SCRIPT = ENGINE_ROOT / "CI" / "scripts" / "vs_env.py"
WINDOWS_PATH_LIMIT = 260  # MAX_PATH: a compiler or a tool without long-path support fails on a longer file name
BUILD_PATH_LENGTH = 140  # the longest file name the build writes, relative to the engine folder (SDL_image's libwebp exports)

ROOT_MARK = b"<ROOT>"
# a file path as it sits in a PCH: absolute, relative with `../` or under the (replaced) checkout root; at least three
# components and an extension, so the random bytes of a binary file do not match
PATH_STRING = re.compile(rb"(?:(?:\.\./)+|/|<ROOT>/)[A-Za-z0-9_.+@~-]+(?:/[A-Za-z0-9_.+@~-]+){2,}\.[A-Za-z0-9]{1,5}")
STAT_LINE = re.compile(r"^([a-z_]+)\t(\d+)\s*$")
LOG_LINE = re.compile(r"^\[\S+\s+(\d+)\s*\] (.*)$")

# The ccache result counters that say what happened to one compile; everything else on a `Result:` line (local_storage_*,
# the *_miss details) repeats it.
RESULT_HIT_DIRECT = "direct_cache_hit"
RESULT_HIT_PREPROCESSED = "preprocessed_cache_hit"
RESULT_MISS = "cache_miss"
STORAGE_RESULTS = ("local_storage_", "remote_storage_", "direct_cache_miss", "preprocessed_cache_miss")


class ProbeError(Exception):
    pass


# --- the argument list ------------------------------------------------------------------------------------


def normalize_argv(argv: list[str]) -> list[str]:
    """`--cmake-arg -DX=Y` -> `--cmake-arg=-DX=Y`: argparse takes a value that starts with `-` for an option."""
    out: list[str] = []
    i = 0
    while i < len(argv):
        item = argv[i]
        if item == "--cmake-arg" and i + 1 < len(argv):
            out.append(f"--cmake-arg={argv[i + 1]}")
            i += 2
            continue
        out.append(item)
        i += 1
    return out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="ccache_share.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    probe = sub.add_parser("probe", help="copy the engine twice, build both with an isolated ccache, report the hits of the second")
    probe.add_argument("--work", required=True, type=Path, help="scratch folder (new or empty; never inside a repository)")
    probe.add_argument("--preset", default="macos-debug", help="configure and build preset (default macos-debug)")
    probe.add_argument("--targets", nargs="+", default=["BECoreModule", "EventsModule"], help="targets to build")
    probe.add_argument("--cmake-arg", action="append", default=[], help="extra argument of `cmake --preset` (use as --cmake-arg -DX=Y)")
    probe.add_argument("--no-base-dir", action="store_true", help="do not pass -DENGINE_CCACHE_BASE_DIR (baseline of the main checkout)")
    probe.add_argument("--pch-only", action="store_true", help="build only the PCH of BECoreModule in both folders and compare them")
    probe.add_argument("--show-diff", action="store_true", help="with --pch-only: print the path strings only one PCH has")
    probe.add_argument("--user-config", action="store_true", help="use a copy of the owner's ccache.conf instead of one with max_size only")
    probe.add_argument("--expect-hit-rate", type=float, default=None, help="exit 1 when the hit rate of the second build is below this")
    probe.add_argument("--json", type=Path, default=None, help="write the numbers to this file")
    probe.add_argument("--wipe", action="store_true", help="remove the scratch folder of an earlier probe first")
    probe.add_argument("--engine", type=Path, default=ENGINE_ROOT, help="the engine folder to copy (default: this one)")
    return parser


# --- the working tree --------------------------------------------------------------------------------------


def split_nul(data: bytes) -> list[str]:
    return [item.decode("utf-8", "surrogateescape") for item in data.split(b"\0") if item]


def working_tree_files(root: Path) -> list[str]:
    """The files of the working tree: tracked ones of the repository and its submodules, plus untracked, not ignored."""
    tracked = subprocess.run(["git", "ls-files", "-z", "--recurse-submodules"], cwd=root, check=True, capture_output=True).stdout
    # git refuses --others together with --recurse-submodules, so the untracked files are those of the top repository only
    untracked = subprocess.run(["git", "ls-files", "-z", "--others", "--exclude-standard"], cwd=root, check=True, capture_output=True).stdout
    seen: set[str] = set()
    files: list[str] = []
    for name in split_nul(tracked) + split_nul(untracked):
        if name not in seen:
            seen.add(name)
            files.append(name)
    return files


def copy_working_tree(root: Path, files: list[str], dest: Path) -> int:
    """Copy `files` (relative to `root`) to `dest`; a file deleted in the working tree is skipped. Returns the number copied."""
    copied = 0
    for name in files:
        src = root / name
        if not (src.is_symlink() or src.is_file()):
            continue
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if src.is_symlink():
            if not copy_symlink(src, target):
                continue
        else:
            shutil.copy2(src, target)
        copied += 1
    return copied


def copy_symlink(src: Path, target: Path) -> bool:
    """Make `target` a link like `src`; where that is refused (Windows without the privilege) copy a linked file, skip a folder."""
    try:
        target.symlink_to(os.readlink(src))
        return True
    except OSError:
        pass
    if src.is_file():
        shutil.copy2(src, target)
        return True
    print(f"skipped the link {src}: no symlink here and its target is not a file")
    return False


def link_directory(link: Path, target: Path) -> str:
    """Make `link` lead to the folder `target`: a symlink, else on Windows a junction (no privilege needed). Returns what was made,
    or "" when neither worked."""
    try:
        link.symlink_to(target, target_is_directory=True)
        return "symlink"
    except OSError:
        pass
    if is_windows():
        made = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True)
        if made.returncode == 0:
            return "junction"
    return ""


def is_windows() -> bool:
    return sys.platform == "win32"


def long_path_warning(copy_root: Path, limit: int = WINDOWS_PATH_LIMIT) -> str:
    """A warning when the longest file the build writes under `copy_root` would pass the Windows path limit; else ''."""
    estimate = len(str(copy_root)) + 1 + BUILD_PATH_LENGTH
    if estimate <= limit:
        return ""
    return f"warning: {copy_root} plus the longest build path is about {estimate} characters (Windows limit {limit}): use a shorter --work folder (e.g. D:/cc/p1)"


def inside_repository(path: Path) -> Path | None:
    """The nearest folder above `path` (or `path` itself) that holds a `.git`, if any."""
    probe = path.resolve()
    while True:
        if (probe / ".git").exists():
            return probe
        if probe.parent == probe:
            return None
        probe = probe.parent


# --- ccache numbers ----------------------------------------------------------------------------------------


def parse_print_stats(text: str) -> dict[str, int]:
    """`ccache --print-stats` (tab format): counter name -> value."""
    stats: dict[str, int] = {}
    for line in text.splitlines():
        match = STAT_LINE.match(line)
        if match:
            stats[match.group(1)] = int(match.group(2))
    return stats


@dataclass
class Summary:
    direct_hits: int = 0
    preprocessed_hits: int = 0
    misses: int = 0
    cache_size_kib: int = 0

    @property
    def hits(self) -> int:
        return self.direct_hits + self.preprocessed_hits

    @property
    def cacheable(self) -> int:
        return self.hits + self.misses

    @property
    def hit_rate(self) -> float:
        return self.hits / self.cacheable if self.cacheable else 0.0


def summarize_stats(stats: dict[str, int]) -> Summary:
    return Summary(
        direct_hits=stats.get(RESULT_HIT_DIRECT, 0),
        preprocessed_hits=stats.get(RESULT_HIT_PREPROCESSED, 0),
        misses=stats.get(RESULT_MISS, 0),
        cache_size_kib=stats.get("cache_size_kibibyte", 0),
    )


@dataclass
class Call:
    pid: str
    source: str = ""
    obj: str = ""
    outcome: str = ""  # direct_hit | preprocessed_hit | miss | the first other result counter


def parse_log(text: str) -> list[Call]:
    """The compiles of a ccache log (`CCACHE_LOGFILE`): one Call per `Command line:`, with its source, object and outcome."""
    calls: list[Call] = []
    open_calls: dict[str, Call] = {}
    for line in text.splitlines():
        match = LOG_LINE.match(line)
        if not match:
            continue
        pid, message = match.groups()
        if message.startswith("Command line:"):
            call = Call(pid)
            open_calls[pid] = call
            calls.append(call)
            continue
        call = open_calls.get(pid)
        if call is None:
            continue
        if message.startswith("Source file: "):
            call.source = message[len("Source file: ") :]
        elif message.startswith("Object file: "):
            call.obj = message[len("Object file: ") :]
        elif message.startswith("Result: "):
            result = message[len("Result: ") :].strip()
            if result == RESULT_HIT_DIRECT:
                call.outcome = "direct_hit"
            elif result == RESULT_HIT_PREPROCESSED:
                call.outcome = "preprocessed_hit"
            elif result == RESULT_MISS:
                call.outcome = "miss"
            elif not call.outcome and not result.startswith(STORAGE_RESULTS):
                call.outcome = result
    return calls


def missed(calls: list[Call]) -> list[Call]:
    return [call for call in calls if call.outcome == "miss"]


def other_outcomes(calls: list[Call]) -> dict[str, int]:
    """Calls that were neither a hit nor a miss (could not use the PCH, called for link, ...), by reason."""
    counts: dict[str, int] = {}
    for call in calls:
        if call.outcome and call.outcome not in ("direct_hit", "preprocessed_hit", "miss"):
            counts[call.outcome] = counts.get(call.outcome, 0) + 1
    return counts


# --- the PCH comparison ------------------------------------------------------------------------------------


def count_differing_bytes(a: bytes, b: bytes) -> int:
    """Positions where the two differ, plus the length difference."""
    common = min(len(a), len(b))
    if a[:common] == b[:common]:
        return abs(len(a) - len(b))
    diff = sum(1 for x, y in zip(a, b) if x != y)
    return diff + abs(len(a) - len(b))


def path_strings(data: bytes, root: str) -> set[bytes]:
    """Path strings of a binary file; the checkout root is replaced by `<ROOT>` so that only other differences show."""
    if root:
        data = data.replace(root.encode("utf-8"), ROOT_MARK)
    return {m.group(0) for m in PATH_STRING.finditer(data)}


def differing_paths(a: bytes, b: bytes, root_a: str = "", root_b: str = "", limit: int = DIFF_LIST_LIMIT) -> tuple[list[str], list[str]]:
    """Path strings that only `a` has and only `b` has, each file with its own checkout root replaced (sorted, at most `limit`)."""
    sa, sb = path_strings(a, root_a), path_strings(b, root_b)
    only_a = sorted(sa - sb)[:limit]
    only_b = sorted(sb - sa)[:limit]
    return [s.decode("utf-8", "replace") for s in only_a], [s.decode("utf-8", "replace") for s in only_b]


def count_occurrences(data: bytes, needle: str) -> int:
    return data.count(needle.encode("utf-8")) if needle else 0


# --- running the tools -------------------------------------------------------------------------------------


def probe_environment(scratch: Path) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith("CCACHE_") and k != "ENGINE_CCACHE_BASE_DIR"}
    env["CCACHE_DIR"] = str(scratch / "ccache")
    env["CCACHE_CONFIGPATH"] = str(scratch / "ccache.conf")
    return env


def write_ccache_config(scratch: Path, user_config: bool) -> None:
    config = scratch / "ccache.conf"
    if user_config:
        candidates = [os.environ.get("CCACHE_CONFIGPATH", ""), str(Path.home() / ".ccache" / "ccache.conf"), str(Path.home() / ".config" / "ccache" / "ccache.conf")]
        source = next((Path(c) for c in candidates if c and Path(c).is_file()), None)
        if source is None:
            raise ProbeError("--user-config: no ccache.conf found (CCACHE_CONFIGPATH, ~/.ccache, ~/.config/ccache)")
        lines = [line for line in source.read_text(encoding="utf-8", errors="replace").splitlines() if not line.strip().startswith("cache_dir")]
        config.write_text("\n".join(lines) + "\n", encoding="utf-8")
    else:
        config.write_text(f"max_size = {CACHE_MAX_SIZE}\n", encoding="utf-8")


def tool_command(cmd: list[str], windows: bool | None = None, vs_env: Path = VS_ENV_SCRIPT) -> list[str]:
    """The command for a cmake call: on Windows through `vs_env.py` (the x64 Visual Studio environment), elsewhere as it is."""
    if windows is None:
        windows = is_windows()
    return [sys.executable, str(vs_env), *cmd] if windows else cmd


def run(cmd: list[str], cwd: Path, env: dict[str, str], log: Path | None = None) -> str:
    """Run a command, stdout and stderr to `log` (or captured); raise ProbeError with the tail of the output on failure."""
    shown = " ".join(cmd)
    cmd = tool_command(cmd)
    if log is not None:
        with log.open("w", encoding="utf-8") as out:
            result = subprocess.run(cmd, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT)
        text = log.read_text(encoding="utf-8", errors="replace")
    else:
        result = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, errors="replace")
        text = result.stdout + result.stderr
    if result.returncode != 0:
        tail = "\n".join(text.splitlines()[-25:])
        raise ProbeError(f"`{shown}` in {cwd} failed ({result.returncode}); last lines:\n{tail}")
    return text


def ccache_stats(env: dict[str, str]) -> dict[str, int]:
    return parse_print_stats(subprocess.run(["ccache", "--print-stats"], env=env, capture_output=True, text=True, check=True).stdout)


@dataclass
class BuildResult:
    label: str
    folder: Path
    seconds: float
    summary: Summary
    calls: list[Call] = field(default_factory=list)


def configure(folder: Path, preset: str, args: list[str], env: dict[str, str], scratch: Path, label: str) -> None:
    run(["cmake", "--preset", preset, *args], folder, env, scratch / f"{label}-configure.log")


def build(folder: Path, preset: str, targets: list[str], env: dict[str, str], scratch: Path, label: str) -> BuildResult:
    log = scratch / f"ccache-{label}.log"
    env = {**env, "CCACHE_LOGFILE": str(log)}
    subprocess.run(["ccache", "-z"], env=env, check=True, capture_output=True)
    started = time.monotonic()
    run(["cmake", "--build", "--preset", preset, "--target", *targets], folder, env, scratch / f"{label}-build.log")
    seconds = time.monotonic() - started
    calls = parse_log(log.read_text(encoding="utf-8", errors="replace")) if log.exists() else []
    return BuildResult(label, folder, seconds, summarize_stats(ccache_stats(env)), calls)


def find_pch_targets(folder: Path, preset: str, env: dict[str, str]) -> list[str]:
    """The ninja targets of the PCH files of BECoreModule (relative to its build folder)."""
    text = run(["cmake", "--build", "--preset", preset, "--", "-t", "targets", "all"], folder, env)
    found = []
    for line in text.splitlines():
        name = line.split(":", 1)[0].strip()
        if PCH_OWNER in name and name.endswith(PCH_TARGET_SUFFIX):
            found.append(name)
    return found


# --- the report --------------------------------------------------------------------------------------------


def format_build(result: BuildResult, list_misses: bool) -> str:
    s = result.summary
    lines = [
        f"== build {result.label} ({result.folder}) ==",
        f"wall {result.seconds:.1f} s | cacheable {s.cacheable} | hits {s.hits} (direct {s.direct_hits}, preprocessed {s.preprocessed_hits})"
        f" | misses {s.misses} | hit rate {s.hit_rate * 100:.1f}% | cache {s.cache_size_kib / 1024:.1f} MiB",
    ]
    others = other_outcomes(result.calls)
    if others:
        lines.append("not cacheable or failed: " + ", ".join(f"{name} x{count}" for name, count in sorted(others.items())))
    if list_misses:
        misses = missed(result.calls)
        lines.append(f"missed objects ({len(misses)}):")
        for call in misses[:MISS_LIST_LIMIT]:
            lines.append(f"  {call.obj or call.source}")
        if len(misses) > MISS_LIST_LIMIT:
            lines.append(f"  ... and {len(misses) - MISS_LIST_LIMIT} more")
    return "\n".join(lines)


def build_json(result: BuildResult) -> dict:
    s = result.summary
    return {
        "label": result.label,
        "seconds": round(result.seconds, 1),
        "cacheable": s.cacheable,
        "hits": s.hits,
        "direct_hits": s.direct_hits,
        "preprocessed_hits": s.preprocessed_hits,
        "misses": s.misses,
        "hit_rate": round(s.hit_rate, 4),
        "cache_kib": s.cache_size_kib,
        "missed_objects": [call.obj or call.source for call in missed(result.calls)],
        "other": other_outcomes(result.calls),
    }


# --- the probe ---------------------------------------------------------------------------------------------


def prepare_scratch(work: Path, wipe: bool) -> None:
    repo = inside_repository(work.parent if not work.exists() else work)
    if repo is not None:
        raise ProbeError(f"the scratch folder {work} is inside the git repository {repo}: choose a folder outside every repository")
    if work.exists() and any(work.iterdir()):
        if wipe and (work / MARKER).exists():
            shutil.rmtree(work)
        else:
            raise ProbeError(f"{work} is not empty (an earlier probe? use --wipe, it removes folders that hold {MARKER}, or a new folder)")
    work.mkdir(parents=True, exist_ok=True)
    (work / MARKER).write_text("scratch folder of tools/ccache/ccache_share.py\n", encoding="utf-8")


def make_copies(engine: Path, scratch: Path) -> tuple[Path, Path]:
    files = working_tree_files(engine)
    one = scratch / "one" / "engine"
    two = scratch / "deeper" / "two" / "engine"
    for dest in (one, two):
        count = copy_working_tree(engine, files, dest)
        venv = engine / ".venv"  # git-ignored, so not copied: the reflection generator needs its jinja2
        if venv.is_dir():
            made = link_directory(dest / ".venv", venv)
            print(f".venv: {made or 'not linked (no symlink, no junction): CMake takes the system Python'}")
        print(f"copied {count} files to {dest}")
        warning = long_path_warning(dest) if is_windows() else ""
        if warning:
            print(warning)
    return one, two


def base_dir_argument(scratch: PurePath) -> str:
    """Forward slashes: the form CMake and ccache's base_dir take on every platform."""
    return f"-DENGINE_CCACHE_BASE_DIR={scratch.as_posix()}"


def cmake_arguments(args: argparse.Namespace, scratch: Path) -> list[str]:
    extra = [] if args.no_base_dir else [base_dir_argument(scratch)]
    return extra + list(args.cmake_arg)


def run_pch_only(args: argparse.Namespace, one: Path, two: Path, scratch: Path, env: dict[str, str]) -> int:
    configure_args = cmake_arguments(args, scratch)
    results = []
    pch_files: list[tuple[str, Path]] = []
    for label, folder in (("one", one), ("two", two)):
        configure(folder, args.preset, configure_args, env, scratch, label)
        targets = find_pch_targets(folder, args.preset, env)
        if not targets:
            raise ProbeError(f"no `*{PCH_TARGET_SUFFIX}` target of {PCH_OWNER} in {folder} (preset {args.preset}): is the PCH off?")
        results.append(build(folder, args.preset, targets, env, scratch, label))
        pch_files += [(label, folder / "build" / args.preset / name) for name in targets]
    one_files = sorted((p.relative_to(one / "build" / args.preset), p) for label, p in pch_files if label == "one")
    two_files = sorted((p.relative_to(two / "build" / args.preset), p) for label, p in pch_files if label == "two")
    report: dict = {"pch": []}
    for result in results:
        print(format_build(result, list_misses=result.label == "two"))
    for (rel_a, path_a), (rel_b, path_b) in zip(one_files, two_files):
        a, b = path_a.read_bytes(), path_b.read_bytes()
        diff = count_differing_bytes(a, b)
        roots = (count_occurrences(a, str(one)), count_occurrences(b, str(two)))
        verdict = "byte-equal" if diff == 0 else f"DIFFER: {diff} bytes"
        print(f"\n== PCH {rel_a} ==\n{verdict} | size {len(a)} / {len(b)} | checkout path inside: one {roots[0]}x, two {roots[1]}x")
        entry = {"file": str(rel_a), "equal": diff == 0, "differing_bytes": diff, "size_one": len(a), "size_two": len(b), "root_in_one": roots[0], "root_in_two": roots[1]}
        if diff and args.show_diff:
            only_one, only_two = differing_paths(a, b, str(one), str(two))
            print("paths only in one:\n  " + "\n  ".join(only_one or ["(none)"]))
            print("paths only in two:\n  " + "\n  ".join(only_two or ["(none)"]))
            entry["only_one"], entry["only_two"] = only_one, only_two
        report["pch"].append(entry)
    report["builds"] = [build_json(r) for r in results]
    write_json(args.json, report)
    return 0


def run_full(args: argparse.Namespace, one: Path, two: Path, scratch: Path, env: dict[str, str]) -> int:
    configure_args = cmake_arguments(args, scratch)
    results = []
    for label, folder in (("one", one), ("two", two)):
        configure(folder, args.preset, configure_args, env, scratch, label)
        result = build(folder, args.preset, args.targets, env, scratch, label)
        results.append(result)
        print(format_build(result, list_misses=label == "two"))
    second = results[1].summary
    write_json(args.json, {"preset": args.preset, "targets": args.targets, "cmake_args": configure_args, "builds": [build_json(r) for r in results]})
    if args.expect_hit_rate is not None and second.hit_rate < args.expect_hit_rate:
        print(f"\nFAIL: hit rate of two {second.hit_rate * 100:.1f}% is below {args.expect_hit_rate * 100:.1f}%")
        return 1
    return 0


def write_json(path: Path | None, data: dict) -> None:
    if path is not None:
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def probe(args: argparse.Namespace) -> int:
    if shutil.which("ccache") is None:
        raise ProbeError("ccache is not on the PATH")
    engine = args.engine.resolve()
    scratch = args.work.resolve()
    prepare_scratch(scratch, args.wipe)
    write_ccache_config(scratch, args.user_config)
    one, two = make_copies(engine, scratch)
    env = probe_environment(scratch)
    print(f"scratch {scratch}; ccache dir {scratch / 'ccache'}; preset {args.preset}; cmake args {cmake_arguments(args, scratch)}")
    if args.pch_only:
        return run_pch_only(args, one, two, scratch, env)
    return run_full(args, one, two, scratch, env)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(normalize_argv(sys.argv[1:] if argv is None else argv))
    try:
        if args.command == "probe":
            return probe(args)
    except ProbeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
