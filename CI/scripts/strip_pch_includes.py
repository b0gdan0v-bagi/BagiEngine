#!/usr/bin/env python3
"""
Remove #include lines from C/C++ sources that are already present in the PCH.

Parses src/Modules/BECore/pch.h (or given path), collects include paths, then
walks sources under src/ and config/ and removes matching includes. Supports
--dir to limit to a subfolder (e.g. src/Widgets) for incremental runs.

Modes:
  --changed  Only files with uncommitted changes (git).
  --all      All C/C++ sources under src/ and config/.

Options:
  --dir REL_PATH  Only process files under this path (relative to project root).
  --dry-run       Report what would be removed without writing files.

Run from project root. Reuses same ignore patterns as format_sources (vendored
paths excluded); BECore is not excluded so it can be processed by --dir.
"""

import argparse
import re
import sys
from pathlib import Path

# Ensure CI/scripts is on path when script is run from project root
_script_dir = Path(__file__).resolve().parent
if str(_script_dir) not in sys.path:
    sys.path.insert(0, str(_script_dir))

# Import file discovery and ignore logic from format_sources
from format_sources import (
    SOURCE_EXTENSIONS,
    get_all_source_files,
    get_changed_files,
    is_excluded,
    load_ignore_patterns,
)

# For strip_pch we do not exclude BECore so that --dir src/Modules/BECore works.
# Build ignore list from format_sources default but without src/Modules/BECore.
def get_strip_pch_ignore_patterns(project_root: Path) -> list[str]:
    base = load_ignore_patterns(project_root)
    return [p for p in base if p != "src/Modules/BECore" and not p.startswith("src/Modules/BECore/")]

INCLUDE_LINE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]\s*(?://.*)?$')
# Не трогать сам файл PCH
PCH_FILE_RE = re.compile(r"pch\.h$", re.IGNORECASE)


def normalize_include_path(path: str) -> str:
    return path.strip().replace("\\", "/")


def load_pch_includes(pch_path: Path) -> set[str]:
    """Parse PCH file and return set of normalized include paths."""
    includes = set()
    try:
        text = pch_path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"Error: cannot read PCH file {pch_path}: {e}", file=sys.stderr)
        return includes
    for line in text.splitlines():
        m = INCLUDE_LINE_RE.match(line)
        if m:
            includes.add(normalize_include_path(m.group(1)))
    return includes


def filter_by_dir(files: list[Path], project_root: Path, dir_rel: str) -> list[Path]:
    """Keep only files under dir_rel (relative path with forward slashes)."""
    prefix = dir_rel.rstrip("/")
    if not prefix:
        return files
    result = []
    for p in files:
        try:
            rel = p.relative_to(project_root)
        except ValueError:
            continue
        rel_str = str(rel).replace("\\", "/")
        if rel_str == prefix or rel_str.startswith(prefix + "/"):
            result.append(p)
    return result


def process_file(
    path: Path,
    pch_includes: set[str],
    dry_run: bool,
) -> tuple[list[str], bool]:
    """
    Remove include lines that are in pch_includes. Return (removed_lines, file_was_modified).
    If dry_run, removed_lines lists what would be removed; file is not written.
    """
    try:
        content = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return [], False
    lines = content.splitlines()
    new_lines = []
    removed = []
    for line in lines:
        m = INCLUDE_LINE_RE.match(line)
        if m and normalize_include_path(m.group(1)) in pch_includes:
            removed.append(line.strip())
            continue
        new_lines.append(line)
    if not removed:
        return [], False
    if not dry_run:
        new_content = "\n".join(new_lines)
        if not new_content.endswith("\n"):
            new_content += "\n"
        try:
            path.write_text(new_content, encoding="utf-8", newline="\n")
        except OSError as e:
            print(f"Error writing {path}: {e}", file=sys.stderr)
            return removed, False
    return removed, True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Remove #include lines already present in PCH from sources."
    )
    parser.add_argument(
        "pch",
        nargs="?",
        default="src/Modules/BECore/pch.h",
        help="Path to PCH header relative to project root (default: src/Modules/BECore/pch.h)",
    )
    scope = parser.add_mutually_exclusive_group(required=True)
    scope.add_argument("--changed", action="store_true", help="Only changed files (git)")
    scope.add_argument("--all", action="store_true", help="All sources under src/ and config/")
    parser.add_argument(
        "--dir",
        metavar="REL_PATH",
        default=None,
        help="Only process files under this path (e.g. src/Widgets, src/Modules/BECore)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only report what would be removed, do not write files",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent.parent
    pch_path = project_root / args.pch.replace("\\", "/")
    if not pch_path.is_file():
        print(f"Error: PCH file not found: {pch_path}", file=sys.stderr)
        return 1

    pch_includes = load_pch_includes(pch_path)
    if not pch_includes:
        print("Error: no #include lines found in PCH.", file=sys.stderr)
        return 1

    ignore_patterns = get_strip_pch_ignore_patterns(project_root)
    if args.changed:
        files = get_changed_files(project_root, ignore_patterns)
    else:
        files = get_all_source_files(project_root, ignore_patterns)

    if args.dir is not None:
        files = filter_by_dir(files, project_root, args.dir.strip().replace("\\", "/"))

    if not files:
        print("No files to process.")
        return 0

    scope_label = "changed" if args.changed else "all"
    dir_label = f" under {args.dir}" if args.dir else ""
    print(f"Strip PCH includes: {len(files)} file(s) ({scope_label}{dir_label}), PCH: {pch_path.name}")
    if args.dry_run:
        print("Dry run — no files will be modified.")

    modified = 0
    for path in files:
        if PCH_FILE_RE.search(str(path).replace("\\", "/")):
            continue
        removed, wrote = process_file(path, pch_includes, args.dry_run)
        if removed:
            rel = path.relative_to(project_root)
            print(f"  {rel}: removed {len(removed)} include(s)")
            for inc in removed:
                print(f"    - {inc}")
            if wrote:
                modified += 1

    if args.dry_run:
        print("Dry run complete.")
    else:
        print(f"Done. Modified {modified} file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
