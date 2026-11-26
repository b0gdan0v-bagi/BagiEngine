#!/usr/bin/env python3
"""Merges the profiles of a coverage build and writes which lines of the engine ran.

Build with the preset `macos-coverage` (`ENGINE_COVERAGE=ON`, cmake/BagiFuzz.cmake), then from the root of the repository:

    python3 CI/scripts/coverage_report.py --root . --reset    # delete the profiles of earlier runs
    ctest --preset macos-coverage                             # and / or a QA replay, a smoke, an executable by hand
    python3 CI/scripts/coverage_report.py --root .            # in a game: engine/CI/scripts/coverage_report.py

An instrumented executable writes a profile into `<build>/profraw` when it exits; a process that is killed or crashes writes none.
The report takes every executable and shared library of `<build>/bin` that holds a coverage mapping, so a game needs no list. It
writes `coverage.profdata`, `summary.txt` (a line per source file) and `html/index.html` into `--out`, which must be outside every git
work tree. A source that no instrumented executable links is absent from the report, not shown as not covered. Left out by default:
`external/`, the vendored modules of `src/Modules/`, generated code, the build folder and (without `--include-tests`) every `tests/`.

Exit codes: 0 the report is written, 1 an llvm tool failed, 2 a bad argument, 77 nothing to report (no profile, no instrumented
executable or no llvm tools). Standard library only.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_PRESET = "macos-coverage"
VENDORED_MODULES = ("EABase", "RmlUi", "dr_libs", "fmt", "imgui", "pugixml", "stb", "tracy")
PROFILE_FOLDER = "profraw"
PROFILE_PATTERN = "*.profraw"
# The first bytes of a Mach-O (64 bit, 32 bit, universal), an ELF and a PE file
IMAGE_MAGICS = (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\x7fELF", b"MZ")
# The name of the section that holds the coverage mapping: Mach-O and ELF, PE
COVERAGE_MARKS = (b"__llvm_covmap", b".lcovmap")
SCAN_CHUNK = 1 << 20
EXIT_DONE = 0
EXIT_TOOL_FAILED = 1
EXIT_BAD_ARGUMENT = 2
EXIT_NOTHING = 77


class Stop(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code


def escape_regex(text: str) -> str:
    """`text` as a literal in a POSIX extended regular expression (llvm) and in a Python one."""
    return re.sub(r"([.^$*+?()\[\]{}|\\])", r"\\\1", text)


def spellings(path: Path) -> list[str]:
    """The absolute path as given and with its links resolved: the compiler records the one it was configured with."""
    return list(dict.fromkeys([path.absolute().as_posix(), path.resolve().as_posix()]))


def ignore_patterns(root: Path, build: Path, include_tests: bool) -> list[str]:
    """The regular expressions of the source files left out. The folder names count only below `root`, so a checkout that lies under
    a folder called `tests` or `external` is not dropped whole."""
    folders = ["external", "Generated", f"src/Modules/({'|'.join(VENDORED_MODULES)})"]
    if not include_tests:
        folders.append("tests")
    patterns = [f"^{escape_regex(spelled)}/(.*/)?({'|'.join(folders)})/" for spelled in spellings(root)]
    patterns += [f"^{escape_regex(spelled)}/" for spelled in spellings(build)]
    return patterns


def git_work_tree_of(path: Path) -> Path | None:
    resolved = path.resolve()
    return next((folder for folder in (resolved, *resolved.parents) if (folder / ".git").exists()), None)


def default_out(root: Path, build: Path) -> Path:
    return Path(tempfile.gettempdir()) / "bagi-coverage" / f"{root.resolve().name}-{build.resolve().name}"


def find_profiles(sources: list[Path]) -> list[Path]:
    """The profile files named by `sources`: a file as it is, a folder searched for `*.profraw`."""
    found: list[Path] = []
    for source in sources:
        if source.is_dir():
            found += sorted(path for path in source.rglob(PROFILE_PATTERN) if path.is_file())
        elif source.is_file():
            found.append(source)
    return list(dict.fromkeys(found))


def holds_coverage(path: Path) -> bool:
    """Whether `path` is an executable or a shared library with a coverage mapping."""
    overlap = max(len(mark) for mark in COVERAGE_MARKS) - 1
    with path.open("rb") as handle:
        if not handle.read(len(IMAGE_MAGICS[0])).startswith(IMAGE_MAGICS):
            return False
        handle.seek(0)
        tail = b""
        while chunk := handle.read(SCAN_CHUNK):
            window = tail + chunk
            if any(mark in window for mark in COVERAGE_MARKS):
                return True
            tail = window[-overlap:]
    return False


def find_objects(folder: Path) -> list[Path]:
    """The instrumented images directly in `folder`, one per real file (a versioned library has symbolic links beside it)."""
    if not folder.is_dir():
        return []
    images = dict.fromkeys(path.resolve() for path in sorted(folder.iterdir()) if path.is_file())
    return [path for path in images if holds_coverage(path)]


def find_tool(name: str) -> str | None:
    # xcrun first on macOS: the profile format follows the compiler, so the tools must be those of Apple clang
    if sys.platform == "darwin" and shutil.which("xcrun"):
        found = subprocess.run(["xcrun", "--find", name], capture_output=True, text=True, check=False)
        if found.returncode == 0 and found.stdout.strip():
            return found.stdout.strip()
    return shutil.which(name)


def tool(name: str, explicit: str | None) -> str:
    if explicit is not None:
        path = shutil.which(explicit)
        if path is None:
            raise Stop(EXIT_BAD_ARGUMENT, f"--{name}: no such program: {explicit}")
        return path
    path = find_tool(name)
    if path is None:
        raise Stop(EXIT_NOTHING, f"{name} not found (Xcode command line tools on macOS, else on PATH; or --{name})")
    return path


def run_tool(command: list[str]) -> str:
    """The standard output of an llvm tool; its warnings go to standard error."""
    result = subprocess.run(command, capture_output=True, text=True, errors="replace", check=False)
    if result.returncode != 0:
        raise Stop(EXIT_TOOL_FAILED, f"{Path(command[0]).name} failed (exit {result.returncode}):\n{result.stdout}{result.stderr}")
    sys.stderr.write(result.stderr)
    return result.stdout


def reset(build: Path) -> int:
    profiles = find_profiles([build / PROFILE_FOLDER])
    for path in profiles:
        path.unlink()
    print(f"deleted {len(profiles)} profiles under {build / PROFILE_FOLDER}")
    return EXIT_DONE


def report(args: argparse.Namespace, root: Path, build: Path) -> int:
    out = Path(args.out) if args.out else default_out(root, build)
    work_tree = git_work_tree_of(out)
    if work_tree is not None:
        raise Stop(EXIT_BAD_ARGUMENT, f"--out {out} is inside the git work tree {work_tree}: name a folder outside")
    if out.exists() and not out.is_dir():
        raise Stop(EXIT_BAD_ARGUMENT, f"--out {out} is not a folder")
    for path in [Path(text) for text in (args.profraw or [])]:
        if not path.exists():
            raise Stop(EXIT_BAD_ARGUMENT, f"--profraw {path} does not exist")
    for path in [Path(text) for text in (args.object or [])]:
        if not path.is_file():
            raise Stop(EXIT_BAD_ARGUMENT, f"--object {path} is not a file")
    for pattern in args.ignore:
        try:
            re.compile(pattern)
        except re.error as error:
            raise Stop(EXIT_BAD_ARGUMENT, f"--ignore {pattern}: {error}") from error

    profile_sources = [Path(text) for text in args.profraw] if args.profraw else [build / PROFILE_FOLDER]
    profiles = find_profiles(profile_sources)
    if not profiles:
        raise Stop(EXIT_NOTHING, "no profile under " + ", ".join(str(path) for path in profile_sources) +
                   " (run an executable of the coverage build first)")
    objects = [Path(text) for text in args.object] if args.object else find_objects(build / "bin")
    if not objects:
        raise Stop(EXIT_NOTHING, f"no executable with a coverage mapping in {build / 'bin'} (build the preset {DEFAULT_PRESET})")
    profdata_tool = tool("llvm-profdata", args.llvm_profdata)
    cov_tool = tool("llvm-cov", args.llvm_cov)

    newest = max(path.stat().st_mtime for path in objects)
    stale = sum(1 for path in profiles if path.stat().st_mtime < newest)
    if stale:
        print(f"warning: {stale} of {len(profiles)} profiles are older than an executable: they may be of an earlier build "
              "(--reset, then run again)", file=sys.stderr)

    out.mkdir(parents=True, exist_ok=True)
    summary = out / "summary.txt"
    html = out / "html"
    profdata = out / "coverage.profdata"
    # Only what an earlier run of this script wrote: pages of a source that is gone would stay
    if profdata.is_file() and (html / "index.html").is_file():
        shutil.rmtree(html)
    summary.unlink(missing_ok=True)

    listing = out / "profraw-files.txt"
    # "<weight>,<file>": without a weight llvm-profdata takes a comma or a leading '#' of a path for its own syntax
    listing.write_text("".join(f"1,{path.absolute()}\n" for path in profiles), encoding="utf-8")
    # failure-mode=all: one profile cut short by a dying process does not void the others
    run_tool([profdata_tool, "merge", "-sparse", "--failure-mode=all", f"--input-files={listing}", "-o", str(profdata)])

    common = [str(objects[0])]
    for path in objects[1:]:
        common += ["-object", str(path)]
    common.append(f"-instr-profile={profdata}")
    common += [f"-ignore-filename-regex={pattern}" for pattern in ignore_patterns(root, build, args.include_tests) + args.ignore]
    text = run_tool([cov_tool, "report", *common])
    summary.write_text(text, encoding="utf-8")
    run_tool([cov_tool, "show", "-format=html", f"-output-dir={html}", *common])

    lines = text.splitlines()
    print(f"{len(profiles)} profiles, {len(objects)} executables and libraries")
    for line in lines[:1] + [line for line in lines if line.startswith("TOTAL")]:
        print(line)
    print(f"summary: {summary}")
    print(f"html: {html / 'index.html'}")
    return EXIT_DONE


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Coverage report of a build with ENGINE_COVERAGE.")
    parser.add_argument("--root", default=".", help="the repository whose sources are reported (default .)")
    parser.add_argument("--build", help=f"the build folder (default <root>/build/{DEFAULT_PRESET})")
    parser.add_argument("--out", help="the report folder, outside every git work tree (default under the system temp folder)")
    parser.add_argument("--profraw", action="append", help="a profile file or a folder of them, repeatable (default <build>/profraw)")
    parser.add_argument("--object", action="append", help="an instrumented executable, repeatable (default: those in <build>/bin)")
    parser.add_argument("--ignore", action="append", default=[], help="a regular expression of source paths to leave out, repeatable")
    parser.add_argument("--include-tests", action="store_true", help="report the files under tests/ too")
    parser.add_argument("--reset", action="store_true", help="delete the profiles of the build and exit")
    parser.add_argument("--llvm-profdata", help="the llvm-profdata to use (default: xcrun on macOS, else PATH)")
    parser.add_argument("--llvm-cov", help="the llvm-cov to use (default: xcrun on macOS, else PATH)")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return EXIT_BAD_ARGUMENT if error.code else EXIT_DONE
    root = Path(args.root)
    build = Path(args.build) if args.build else root / "build" / DEFAULT_PRESET
    try:
        if not root.is_dir():
            raise Stop(EXIT_BAD_ARGUMENT, f"--root {root} is not a folder")
        if args.build and not build.is_dir():
            raise Stop(EXIT_BAD_ARGUMENT, f"--build {build} is not a folder")
        return reset(build) if args.reset else report(args, root, build)
    except Stop as stop:
        print(f"error: {stop}", file=sys.stderr)
        return stop.code


if __name__ == "__main__":
    sys.exit(main())
