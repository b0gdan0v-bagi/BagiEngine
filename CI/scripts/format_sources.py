#!/usr/bin/env python3
"""
Apply .clang-format to project sources.

Modes:
  --changed  Only files with uncommitted changes (git).
  --all      All C/C++ sources under src/ and config/.

Ignore: .clang-format-ignore in project root (merged with built-in vendored paths).
Run from project root. Uses root .clang-format.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Extensions that clang-format should process
SOURCE_EXTENSIONS = {".cpp", ".c", ".h", ".hpp", ".cxx", ".hxx", ".cc"}

# Default ignore patterns (relative to project root, forward slashes). Vendored libs + external/build.
DEFAULT_IGNORE_PATTERNS = [
    "external/",
    "build/",
    ".venv/",
    "src/Modules/BECore",
    "src/Modules/EABase",
    "src/Modules/EnTT",
    "src/Modules/fmt",
    "src/Modules/imgui",
    "src/Modules/pugixml",
    "src/Modules/Vulkan",
]


def load_ignore_patterns(project_root: Path) -> list[str]:
    """Load ignore patterns: default list + lines from .clang-format-ignore (merged)."""
    patterns = list(DEFAULT_IGNORE_PATTERNS)
    ignore_file = project_root / ".clang-format-ignore"
    if ignore_file.exists():
        try:
            for line in ignore_file.read_text(encoding="utf-8", errors="replace").splitlines():
                line = line.split("#", 1)[0].strip()
                if line:
                    patterns.append(line.replace("\\", "/").rstrip("/"))
        except OSError:
            pass
    return patterns


def is_excluded(rel_path: str, ignore_patterns: list[str]) -> bool:
    """Return True if path should be excluded (any pattern matches prefix or equals path)."""
    norm = rel_path.replace("\\", "/")
    for p in ignore_patterns:
        p_norm = p.rstrip("/")
        if norm == p_norm or norm.startswith(p_norm + "/"):
            return True
    return False


def find_clang_format(project_root: Path) -> Path | None:
    """Locate clang-format: PATH first, then LIBCLANG_PATH."""
    import shutil

    exe = shutil.which("clang-format")
    if exe:
        return Path(exe)

    libclang_path = os.environ.get("LIBCLANG_PATH")
    if libclang_path:
        base = Path(libclang_path)
        if sys.platform == "win32":
            candidates = [base / "clang-format.exe", base / "bin" / "clang-format.exe"]
        else:
            candidates = [base / "clang-format", base / "bin" / "clang-format"]
        for p in candidates:
            if p.exists():
                return p

    return None


def get_changed_files(project_root: Path, ignore_patterns: list[str]) -> list[Path]:
    """Return list of changed (staged + unstaged) source files, respecting ignore."""
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
    """Return list of all source files under src/ and config/, respecting ignore."""
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
    parser = argparse.ArgumentParser(description="Apply .clang-format to project sources.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--changed", action="store_true", help="Format only changed files (git)")
    group.add_argument("--all", action="store_true", help="Format all sources under src/ and config/")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent.parent
    if not (project_root / ".clang-format").exists():
        print("Error: .clang-format not found in project root.", file=sys.stderr)
        return 1

    clang_format = find_clang_format(project_root)
    if not clang_format:
        print(
            "Error: clang-format not found. Add it to PATH or set LIBCLANG_PATH to LLVM bin directory.",
            file=sys.stderr,
        )
        return 1

    ignore_patterns = load_ignore_patterns(project_root)
    if args.changed:
        files = get_changed_files(project_root, ignore_patterns)
        print(f"Formatting {len(files)} changed file(s)...")
    else:
        files = get_all_source_files(project_root, ignore_patterns)
        print(f"Formatting {len(files)} file(s)...")

    if not files:
        print("No files to format.")
        return 0

    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    failed = []
    for path in files:
        rel = path.relative_to(project_root)
        try:
            subprocess.run(
                [str(clang_format), "-i", str(path)],
                cwd=project_root,
                capture_output=True,
                text=True,
                timeout=10,
                creationflags=creationflags,
            )
            print(str(rel))
        except (subprocess.TimeoutExpired, Exception) as e:
            failed.append((str(rel), str(e)))
            print(f"Error: {rel} - {e}", file=sys.stderr)

    if failed:
        print(f"Failed: {len(failed)} file(s).", file=sys.stderr)
        return 1
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
