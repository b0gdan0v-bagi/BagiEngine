#!/usr/bin/env python3
"""The test baseline of a commit and a preset, shared by every worktree of a repository.

    python3 CI/scripts/baseline_cache.py get --root <R> --preset <p> --out <file>     exit 0: a hit, written to <file>; 1: a miss
    python3 CI/scripts/baseline_cache.py put --root <R> --preset <p> --file <names>   exit 0: stored; 1: refused, the tree is dirty
    python3 CI/scripts/baseline_cache.py path --root <R> --preset <p> [--commit <rev>]

The baseline is the list of the tests that fail on a commit (one name per line, empty when all pass). It lives in
`<git common dir>/bagi-baseline/<full commit>-<preset>.txt`: the worker folders are worktrees of one `.git`, so a baseline one loop
took serves every other loop on the same commit and preset, and nothing else. `get` and `path` take `--commit` (default `HEAD`); `put`
always stores for `HEAD` and refuses when a tracked file or a submodule pointer differs from it, since such a run is not the commit's.
`get` writes `--out` only on a hit, so a miss never leaves an empty file that reads as "all green". `put` writes through a temporary
file and a rename (two loops may store the same key at once) and removes the baselines older than `STALE_DAYS`.
Exit 2 on a bad argument or a git failure. Standard library only.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List, Optional

EXIT_OK, EXIT_MISS, EXIT_ERROR = 0, 1, 2
EXIT_REFUSED = EXIT_MISS

CACHE_FOLDER = "bagi-baseline"
STALE_DAYS = 14
SECONDS_PER_DAY = 24 * 60 * 60
PRESET_NAME = re.compile(r"[A-Za-z0-9._-]+")


class GitError(Exception):
    pass


def git(root: Path, args: List[str]) -> str:
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise GitError(result.stderr.strip() or f"git {' '.join(args)} failed")
    return result.stdout.strip()


def cache_folder(root: Path) -> Path:
    common = Path(git(root, ["rev-parse", "--git-common-dir"]))
    # A relative answer is relative to the directory git ran in, which is `root`.
    return (common if common.is_absolute() else root / common).resolve() / CACHE_FOLDER


def cache_file(root: Path, commit: str, preset: str) -> Path:
    full = git(root, ["rev-parse", "--verify", f"{commit}^{{commit}}"])
    return cache_folder(root) / f"{full}-{preset}.txt"


def tree_is_clean(root: Path) -> bool:
    result = subprocess.run(["git", "-C", str(root), "diff", "--quiet", "HEAD"], capture_output=True)
    return result.returncode == 0


def remove_stale(folder: Path, now: float) -> None:
    for entry in folder.glob("*.txt"):
        try:
            if now - entry.stat().st_mtime > STALE_DAYS * SECONDS_PER_DAY:
                entry.unlink()
        except OSError:
            pass  # another loop removed or replaced it first


def store(target: Path, text: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=target.parent, prefix=".", suffix=".tmp")
    with os.fdopen(handle, "w", encoding="utf-8") as out:
        out.write(text)
    os.replace(temporary, target)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("get", "put", "path"))
    parser.add_argument("--root", type=Path, default=Path("."), help="the repository (any of its worktrees)")
    parser.add_argument("--preset", required=True, help="the test preset the baseline was run with")
    parser.add_argument("--commit", default="HEAD", help="get and path only")
    parser.add_argument("--out", type=Path, help="get: where to write a hit")
    parser.add_argument("--file", type=Path, help="put: the failing names of the run")
    args = parser.parse_args(argv)

    if not PRESET_NAME.fullmatch(args.preset):
        print(f"baseline_cache: not a preset name: {args.preset!r}", file=sys.stderr)
        return EXIT_ERROR
    if args.command == "get" and args.out is None:
        print("baseline_cache: get needs --out", file=sys.stderr)
        return EXIT_ERROR
    if args.command == "put" and (args.file is None or args.commit != "HEAD"):
        print("baseline_cache: put needs --file and stores for HEAD only", file=sys.stderr)
        return EXIT_ERROR

    try:
        target = cache_file(args.root, args.commit, args.preset)
        if args.command == "path":
            print(target)
            return EXIT_OK
        if args.command == "get":
            if not target.is_file():
                print(f"baseline_cache: no baseline for {target.name}", file=sys.stderr)
                return EXIT_MISS
            args.out.write_text(target.read_text(encoding="utf-8"), encoding="utf-8")
            return EXIT_OK
        if not tree_is_clean(args.root):
            print("baseline_cache: the tree differs from HEAD, not stored", file=sys.stderr)
            return EXIT_REFUSED
        store(target, args.file.read_text(encoding="utf-8"))
        remove_stale(target.parent, time.time())
        return EXIT_OK
    except (GitError, OSError) as error:
        print(f"baseline_cache: {error}", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
