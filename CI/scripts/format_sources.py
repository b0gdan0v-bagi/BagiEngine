#!/usr/bin/env python3
"""
Apply .clang-format to project sources.

Modes:
  --changed  Only files with uncommitted changes (git).
  --all      All C/C++ sources under src/ and config/.

Ignore: .clang-format-ignore in project root (required).
Run from project root. Uses root .clang-format.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Extensions that clang-format should process
SOURCE_EXTENSIONS = {".cpp", ".c", ".h", ".hpp", ".cxx", ".hxx", ".cc"}

# Homebrew prefixes on Apple Silicon and Intel
MACOS_HOMEBREW_PREFIXES = ("/opt/homebrew", "/usr/local")


def load_ignore_patterns(project_root: Path) -> list[str]:
    """Load ignore patterns from .clang-format-ignore (required). Exits with error if not found."""
    ignore_file = project_root / ".clang-format-ignore"
    if not ignore_file.exists():
        sys.exit(f"[error] .clang-format-ignore not found at {ignore_file}")
    patterns = []
    for line in ignore_file.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            patterns.append(line.replace("\\", "/").rstrip("/"))
    return patterns


def is_excluded(rel_path: str, ignore_patterns: list[str]) -> bool:
    """Return True if path should be excluded (any pattern matches prefix or equals path)."""
    norm = rel_path.replace("\\", "/")
    for p in ignore_patterns:
        p_norm = p.rstrip("/")
        if norm == p_norm or norm.startswith(p_norm + "/"):
            return True
    return False


def _libclang_candidates(exe_name: str) -> list[Path]:
    """Candidates derived from LIBCLANG_PATH. It may be the LLVM root, its bin or its lib directory."""
    libclang_path = os.environ.get("LIBCLANG_PATH")
    if not libclang_path:
        return []
    base = Path(libclang_path)
    return [base / exe_name, base / "bin" / exe_name, base.parent / "bin" / exe_name]


def _venv_candidates(project_root: Path, exe_name: str) -> list[Path]:
    """The clang-format pip package from requirements.txt installed into the project venv."""
    bin_dir = "Scripts" if sys.platform == "win32" else "bin"
    return [project_root / ".venv" / bin_dir / exe_name]


def _macos_candidates(exe_name: str) -> list[Path]:
    """Homebrew LLVM is keg-only (never linked into PATH); Xcode ships clang-format behind xcrun."""
    result = []
    for prefix in MACOS_HOMEBREW_PREFIXES:
        opt = Path(prefix) / "opt"
        result.append(opt / "llvm" / "bin" / exe_name)
        result.extend(sorted(opt.glob("llvm@*/bin/" + exe_name), reverse=True))

    try:
        out = subprocess.run(["xcrun", "--find", "clang-format"], capture_output=True, text=True, timeout=10)
        if out.returncode == 0 and out.stdout.strip():
            result.append(Path(out.stdout.strip()))
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return result


def find_clang_format(project_root: Path) -> Path | None:
    """Locate clang-format: PATH, LIBCLANG_PATH, project .venv, then (macOS) Homebrew LLVM and Xcode."""
    import shutil

    exe = shutil.which("clang-format")
    if exe:
        return Path(exe)

    exe_name = "clang-format.exe" if sys.platform == "win32" else "clang-format"
    candidates = _libclang_candidates(exe_name) + _venv_candidates(project_root, exe_name)
    if sys.platform == "darwin":
        candidates += _macos_candidates(exe_name)
    for p in candidates:
        if p.is_file():
            return p

    return None


def get_version(clang_format: Path) -> str:
    """First line of `clang-format --version`, or an empty string if it cannot be run."""
    try:
        out = subprocess.run([str(clang_format), "--version"], capture_output=True, text=True, timeout=10)
    except (subprocess.TimeoutExpired, OSError):
        return ""
    return out.stdout.strip().splitlines()[0] if out.stdout.strip() else ""


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
    """Return list of all source files under src/, config/ and games/, respecting ignore."""
    result = []
    for top in ("src", "config", "games"):
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
    group.add_argument("--all", action="store_true", help="Format all sources under src/, config/ and games/")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent.parent
    if not (project_root / ".clang-format").exists():
        print("Error: .clang-format not found in project root.", file=sys.stderr)
        return 1

    clang_format = find_clang_format(project_root)
    if not clang_format:
        print(
            "Error: clang-format not found. Searched PATH, LIBCLANG_PATH (and its bin/ and ../bin), "
            "the project .venv and, on macOS, Homebrew LLVM and Xcode.\n"
            "Install it with `brew install llvm` or `pip install -r requirements.txt`, "
            "or set LIBCLANG_PATH to the LLVM directory.",
            file=sys.stderr,
        )
        return 1
    print(f"Using clang-format: {clang_format} ({get_version(clang_format)})")

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
            result = subprocess.run(
                [str(clang_format), "-i", str(path)],
                cwd=project_root,
                capture_output=True,
                text=True,
                timeout=10,
                creationflags=creationflags,
            )
            if result.returncode != 0:
                failed.append((str(rel), result.stderr.strip()))
                print(f"Error: {rel} - {result.stderr.strip()}", file=sys.stderr)
                continue
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
