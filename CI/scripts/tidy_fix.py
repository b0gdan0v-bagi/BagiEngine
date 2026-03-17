#!/usr/bin/env python3
"""
Run clang-tidy fixes on project sources (readability-braces-around-statements).

Modes:
  --changed  Only files with uncommitted changes (git).
  --all      All C/C++ sources under src/ and config/.

Only .cpp and .c files are passed to clang-tidy directly; fixes in included
headers are applied automatically by clang-tidy when processing the .cpp unit.

Requires:
  - clang-tidy on PATH or LIBCLANG_PATH pointing to LLVM bin directory.
  - compile_commands.json in a build/ subdirectory (or specify --build-dir).

After running, apply clang-format to keep consistent style:
  python CI/scripts/format_sources.py --changed
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

_script_dir = Path(__file__).resolve().parent
if str(_script_dir) not in sys.path:
    sys.path.insert(0, str(_script_dir))

from format_sources import load_ignore_patterns  # noqa: E402

SOURCE_EXTENSIONS = {".cpp", ".c"}


def is_excluded(rel_path: str, ignore_patterns: list[str]) -> bool:
    norm = rel_path.replace("\\", "/")
    for p in ignore_patterns:
        p_norm = p.rstrip("/")
        if norm == p_norm or norm.startswith(p_norm + "/"):
            return True
    return False


def find_clang_tidy(project_root: Path) -> Path | None:
    import shutil

    exe = shutil.which("clang-tidy")
    if exe:
        return Path(exe)

    libclang_path = os.environ.get("LIBCLANG_PATH")
    if libclang_path:
        base = Path(libclang_path)
        if sys.platform == "win32":
            candidates = [base / "clang-tidy.exe", base / "bin" / "clang-tidy.exe"]
        else:
            candidates = [base / "clang-tidy", base / "bin" / "clang-tidy"]
        for p in candidates:
            if p.exists():
                return p

    return None


def find_compile_commands(project_root: Path, build_dir_arg: str | None) -> Path | None:
    if build_dir_arg:
        candidate = Path(build_dir_arg)
        if not candidate.is_absolute():
            candidate = project_root / candidate
        db = candidate / "compile_commands.json"
        return db if db.exists() else None

    build_root = project_root / "build"
    if not build_root.is_dir():
        return None
    for child in sorted(build_root.iterdir()):
        if child.is_dir():
            db = child / "compile_commands.json"
            if db.exists():
                return db
    return None


def get_changed_files(project_root: Path, ignore_patterns: list[str]) -> list[Path]:
    try:
        out = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            cwd=project_root,
            capture_output=True,
            text=True,
            timeout=30,
        )
        if out.returncode != 0:
            return []
        names = set(out.stdout.strip().splitlines()) if out.stdout.strip() else set()
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return []

    result = []
    for name in names:
        if not name:
            continue
        p = Path(name)
        if p.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        if is_excluded(name.replace("\\", "/"), ignore_patterns):
            continue
        result.append(project_root / p)
    return result


def get_all_source_files(project_root: Path, ignore_patterns: list[str]) -> list[Path]:
    result = []
    for top in ("src", "config"):
        top_path = project_root / top
        if not top_path.is_dir():
            continue
        for path in top_path.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix.lower() not in SOURCE_EXTENSIONS:
                continue
            try:
                rel = path.relative_to(project_root)
            except ValueError:
                continue
            if is_excluded(str(rel).replace("\\", "/"), ignore_patterns):
                continue
            result.append(path)
    return sorted(result)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run clang-tidy fixes on project sources."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--changed", action="store_true", help="Process only changed files (git)")
    group.add_argument("--all", action="store_true", help="Process all sources under src/ and config/")
    parser.add_argument(
        "--build-dir",
        metavar="PATH",
        help="Path to directory containing compile_commands.json (default: auto-discover build/*/)",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent.parent

    clang_tidy = find_clang_tidy(project_root)
    if not clang_tidy:
        print(
            "Error: clang-tidy not found. Add it to PATH or set LIBCLANG_PATH to the LLVM bin directory.",
            file=sys.stderr,
        )
        return 1

    compile_commands_db = find_compile_commands(project_root, args.build_dir)
    if not compile_commands_db:
        hint = (
            f"  Looked in: {args.build_dir}" if args.build_dir else "  Looked in: build/*/"
        )
        print(
            "Error: compile_commands.json not found.\n"
            f"{hint}\n"
            "  Run CMake configuration first (e.g. via the launcher or cmake -B build/...).",
            file=sys.stderr,
        )
        return 1

    build_dir = compile_commands_db.parent
    print(f"Using compile_commands.json: {compile_commands_db.relative_to(project_root)}")

    ignore_patterns = load_ignore_patterns(project_root)
    if args.changed:
        files = get_changed_files(project_root, ignore_patterns)
        print(f"Processing {len(files)} changed file(s)...")
    else:
        files = get_all_source_files(project_root, ignore_patterns)
        print(f"Processing {len(files)} file(s)...")

    if not files:
        print("No files to process.")
        return 0

    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    failed = []
    for path in files:
        rel = path.relative_to(project_root)
        try:
            result = subprocess.run(
                [str(clang_tidy), "-p", str(build_dir), str(path), "--fix"],
                cwd=project_root,
                capture_output=True,
                text=True,
                timeout=60,
                creationflags=creationflags,
            )
            if result.returncode not in (0, 1):
                failed.append((str(rel), result.stderr.strip()))
                print(f"Error: {rel}", file=sys.stderr)
                if result.stderr.strip():
                    print(f"  {result.stderr.strip()}", file=sys.stderr)
            else:
                print(str(rel))
        except (subprocess.TimeoutExpired, Exception) as e:
            failed.append((str(rel), str(e)))
            print(f"Error: {rel} - {e}", file=sys.stderr)

    print()
    if failed:
        print(f"Failed: {len(failed)} file(s).", file=sys.stderr)
        return 1

    print("Done.")
    print("Reminder: run 'python CI/scripts/format_sources.py --changed' to reformat after tidy fixes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
