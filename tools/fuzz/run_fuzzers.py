#!/usr/bin/env python3
"""Runs the libFuzzer executables of the fuzz preset (BagiFuzz<Name>, tests/Fuzz/CLAUDE.md) for a fixed time each.

    python3 tools/fuzz/run_fuzzers.py --build-dir build/macos-fuzz --seconds 60 [--target BinaryDeserializer ...]

Each target runs over a fresh copy of its corpus folder (the work folder takes the inputs libFuzzer finds), then the corpus of the
repository. A crash, a timeout or an out-of-memory input is saved as a file in <build-dir>/fuzz-artifacts/, never in the repository.
Standard library only.

Exit code: 0 no finding, 1 a finding or a fuzzer that could not run, 77 no BagiFuzz executable (skipped, like the tests that need a build).
"""

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

EXIT_FINDING = 1
EXIT_SKIPPED = 77

EXECUTABLE_PREFIX = "BagiFuzz"
MAX_LEN = 65536  # kMaxFuzzInputSize of tests/Fuzz/FuzzMutator.h
RSS_LIMIT_MB = 2048
TIMEOUT_SECONDS = 10  # one input
LOG_TAIL_LINES = 12

# A row of FuzzTargetList() in tests/Fuzz/FuzzTargets.cpp: {"Name", &FuzzName, "corpus-folder"}
TARGET_ROW = re.compile(r'\{\s*"(?P<name>\w+)"\s*,\s*&\w+\s*,\s*"(?P<folder>[\w.-]+)"\s*\}')


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def read_target_table(root: Path) -> dict[str, str]:
    """Target name -> corpus folder name, read from the list the C++ side uses (one source of truth)."""
    source = (root / "tests" / "Fuzz" / "FuzzTargets.cpp").read_text(encoding="utf-8")
    return {match["name"]: match["folder"] for match in TARGET_ROW.finditer(source)}


def find_executables(build_dir: Path) -> dict[str, Path]:
    """Target name -> BagiFuzz<Name> executable in <build-dir>/bin (a .dSYM folder or any other file with a suffix is no executable)."""
    found: dict[str, Path] = {}
    bin_dir = build_dir / "bin"
    if not bin_dir.is_dir():
        return found
    for path in sorted(bin_dir.iterdir()):
        if path.name.startswith(EXECUTABLE_PREFIX) and path.is_file() and path.suffix in ("", ".exe"):
            found[path.stem[len(EXECUTABLE_PREFIX):]] = path
    return found


def build_command(executable: Path, seconds: int, work_dir: Path, corpus_dir: Path, artifact_prefix: Path) -> list[str]:
    return [
        str(executable),
        f"-max_total_time={seconds}",
        f"-max_len={MAX_LEN}",
        f"-artifact_prefix={artifact_prefix}",
        f"-rss_limit_mb={RSS_LIMIT_MB}",
        f"-timeout={TIMEOUT_SECONDS}",
        str(work_dir),
        str(corpus_dir),
    ]


def fresh_copy(corpus_dir: Path, work_dir: Path) -> None:
    if work_dir.exists():
        shutil.rmtree(work_dir)
    if corpus_dir.is_dir():
        shutil.copytree(corpus_dir, work_dir)
    else:
        work_dir.mkdir(parents=True)


def artifacts_of(artifact_dir: Path, name: str) -> set[Path]:
    if not artifact_dir.is_dir():
        return set()
    return {path for path in artifact_dir.iterdir() if path.name.startswith(f"{name}-")}


def tail(path: Path) -> str:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(lines[-LOG_TAIL_LINES:])


def minimize_hint(executable: Path, corpus_folder: str, artifact: Path) -> str:
    return (
        f"  minimize: {executable} -minimize_crash=1 -runs=100000 {artifact}\n"
        f"  then add the minimized file as tests/Fuzz/corpus/{corpus_folder}/fix-<what> together with the fix and a test next to the fixed code"
    )


def run_target(name: str, executable: Path, corpus_folder: str, args, root: Path, runner) -> bool:
    """Runs one fuzzer; True when it found nothing. Prints the one-line verdict."""
    build_dir = Path(args.build_dir)
    work_dir = build_dir / "fuzz-work" / name
    artifact_dir = build_dir / "fuzz-artifacts"
    log_path = build_dir / "fuzz-work" / f"{name}.log"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    corpus_dir = root / "tests" / "Fuzz" / "corpus" / corpus_folder
    fresh_copy(corpus_dir, work_dir)

    before = artifacts_of(artifact_dir, name)
    command = build_command(executable, args.seconds, work_dir, corpus_dir, artifact_dir / f"{name}-")
    started = time.monotonic()
    with open(log_path, "w", encoding="utf-8") as log:
        result = runner(command, stdout=log, stderr=subprocess.STDOUT, check=False)
    elapsed = time.monotonic() - started
    new = sorted(artifacts_of(artifact_dir, name) - before)

    if new:
        print(f"{name}: FINDING after {elapsed:.0f} s, saved {', '.join(str(path) for path in new)}")
        print(minimize_hint(executable, corpus_folder, new[0]))
        print(f"  log: {log_path}")
        return False
    if result.returncode != 0:
        print(f"{name}: FAILED, exit code {result.returncode} and no artifact; log {log_path}")
        print(tail(log_path))
        return False
    print(f"{name}: ok, {elapsed:.0f} s, no finding")
    return True


def main(argv=None, runner=subprocess.run) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build-dir", default="build/macos-fuzz", help="the build folder of the fuzz preset (default build/macos-fuzz)")
    parser.add_argument("--seconds", type=int, default=60, help="seconds per target (-max_total_time)")
    parser.add_argument("--target", action="append", default=[], metavar="NAME", help="only this target (BinaryDeserializer, ...); repeatable")
    parser.add_argument("--root", default=None, help="the repository root (default: the one of this script)")
    args = parser.parse_args(argv)

    root = Path(args.root) if args.root else repository_root()
    build_dir = Path(args.build_dir)
    executables = find_executables(build_dir)
    if not executables:
        print(f"No {EXECUTABLE_PREFIX}<Name> executable in {build_dir / 'bin'}: cmake --preset macos-fuzz && "
              f"cmake --build --preset macos-fuzz (.claude/rules/build-project.md, \"Fuzzing\")")
        return EXIT_SKIPPED

    table = read_target_table(root)
    wanted = args.target or sorted(executables)
    unknown = [name for name in wanted if name not in executables]
    if unknown:
        print(f"No executable for: {', '.join(unknown)} (built: {', '.join(sorted(executables))})")
        return EXIT_FINDING

    clean = True
    for name in wanted:
        folder = table.get(name)
        if folder is None:
            print(f"{name}: not in FuzzTargetList() of tests/Fuzz/FuzzTargets.cpp, so there is no corpus folder for it")
            clean = False
            continue
        clean = run_target(name, executables[name], folder, args, root, runner) and clean
    return 0 if clean else EXIT_FINDING


if __name__ == "__main__":
    sys.exit(main())
