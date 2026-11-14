#!/usr/bin/env python3
"""Check the size of the CLAUDE.md files and the always-loaded rules of a repository.

The root CLAUDE.md and every rules file without a `paths:` front matter are loaded into every session and every
agent; a nested CLAUDE.md is loaded whenever a file of its folder is read. Both cost context, so both have a budget.

    python3 engine/CI/scripts/check_doc_budget.py --root .        (a game)
    python3 CI/scripts/check_doc_budget.py --root .               (the engine)

Exit code 0 when every file is within its budget, 1 otherwise. Standard library only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT_BUDGET = 12_000
NESTED_BUDGET = 20_000
ALWAYS_LOADED_RULES_BUDGET = 8_000

SKIP_DIRS = {".git", ".venv", "build", "external", "node_modules", "__pycache__"}


def is_always_loaded_rule(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---"):
        return True
    end = text.find("\n---", 3)
    return end < 0 or "paths:" not in text[3:end]


def walk_claude_files(root: Path, skip: set[Path]) -> list[Path]:
    found: list[Path] = []
    stack = [root]
    while stack:
        folder = stack.pop()
        for child in sorted(folder.iterdir()):
            # First, and with an lstat: is_dir() is a stat, which raises on Windows for a file symlink to a directory
            # (what a checkout makes when the target is missing at that moment). The file a link points to is counted
            # at its own place.
            if child.is_symlink():
                continue
            if child.is_dir():
                if child.name in SKIP_DIRS or child.resolve() in skip:
                    continue
                if child != root and (child / ".git").exists():
                    continue  # a submodule checks itself
                stack.append(child)
            elif child.name == "CLAUDE.md" and child.parent != root:
                found.append(child)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", help="the repository root")
    parser.add_argument("--root-budget", type=int, default=ROOT_BUDGET)
    parser.add_argument("--nested-budget", type=int, default=NESTED_BUDGET)
    parser.add_argument("--rules-budget", type=int, default=ALWAYS_LOADED_RULES_BUDGET,
                        help="the sum of the rules files without paths: front matter")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    problems: list[str] = []
    rows: list[tuple[int, int, str]] = []

    root_doc = root / "CLAUDE.md"
    if root_doc.exists():
        size = len(root_doc.read_text(encoding="utf-8"))
        rows.append((size, args.root_budget, "CLAUDE.md (every session)"))
        if size > args.root_budget:
            problems.append(f"CLAUDE.md: {size} > {args.root_budget}")

    rules_dir = root / ".claude" / "rules"
    if rules_dir.is_dir():
        always = [p for p in sorted(rules_dir.glob("*.md")) if is_always_loaded_rule(p)]
        total = sum(len(p.read_text(encoding="utf-8")) for p in always)
        names = ", ".join(p.name for p in always) or "none"
        rows.append((total, args.rules_budget, f".claude/rules without paths: ({names})"))
        if total > args.rules_budget:
            problems.append(f".claude/rules loaded always: {total} > {args.rules_budget} ({names})")

    for path in walk_claude_files(root, set()):
        size = len(path.read_text(encoding="utf-8"))
        rel = path.relative_to(root).as_posix()
        rows.append((size, args.nested_budget, rel))
        if size > args.nested_budget:
            problems.append(f"{rel}: {size} > {args.nested_budget}")

    for size, budget, name in sorted(rows, reverse=True):
        mark = "OVER" if size > budget else "ok"
        print(f"{size:>7} / {budget:<7} {mark:<4} {name}")
    if problems:
        print("\nOver budget. Move detail into a nested CLAUDE.md, or cut what the code, a test or a constant already says:",
              file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
