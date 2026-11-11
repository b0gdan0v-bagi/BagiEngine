#!/usr/bin/env python3
"""Rewrites the content files of a game (the scenes and node files of a `config/` folder) to the current content version.

    python3 engine/tools/migrate/migrate.py [ROOT | --root DIR] [--steps DIR]... [--check] [--diff]

A content file carries `contentVersion="N"` on its root element (absent: 1). The engine has migration steps, one module each in
`steps/`, `@step(from_version=N, name=..., track="engine")` on a function `fn(doc, ctx)` over the lossless text model of
`xmltext.py`; the tool applies every step a file still needs, in order, and stamps the new version. A game's own steps (`--steps DIR`)
declare their own track, whose version is the attribute `contentVersion.<track>`. Standard library only. `CLAUDE.md` has the contract.

Exit code: 0 all current (or all written), 1 `--check` found a file that needs a step, 2 an error (a file that is not well-formed, a step
that failed, a bad registry). A file is written only when every step of it succeeded, through a temporary file renamed over it.
"""

from __future__ import annotations

import argparse
import difflib
import importlib.util
import os
import re
import shutil
import sys
import tempfile
import traceback
from pathlib import Path
from typing import Callable, Dict, Iterator, List, Optional, Sequence, Tuple

import xmltext

ENGINE_TRACK = "engine"
VERSION_ATTRIBUTE = "contentVersion"
CONTENT_ROOTS = ("SceneManager", "node")
CONFIG_FOLDER = "config"
FIRST_VERSION = 1
ENGINE_STEPS_DIR = Path(__file__).resolve().parent / "steps"
SKIPPED_FOLDERS = {".git", "node_modules", ".venv", "external", "__pycache__"}
SKIPPED_FOLDER_PREFIXES = ("build",)
ENGINE_SUBMODULE = "engine"
EXIT_OK, EXIT_PENDING, EXIT_ERROR = 0, 1, 2

_STEP_ATTRIBUTE = "_migration_step"
_TRACK_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_\-]*\Z")
_STEP_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_\-]*\Z")
_PROLOG_SKIP = re.compile(r"﻿?(?:\s+|<\?.*?\?>|<!--.*?-->|<!DOCTYPE(?:[^\[>]|\[.*?\])*>)*", re.DOTALL)
_ROOT_NAME = re.compile(r"<([^\s/>]+)")
_VERSION_TEXT = re.compile(r"[0-9]+\Z")


# ---- the step API -----------------------------------------------------------------------------------------------------------


def step(from_version: int, name: str, track: str = ENGINE_TRACK, to_version: Optional[int] = None) -> Callable:
    """Mark a function `fn(doc, ctx)` as the step that takes a file of `track` from `from_version` to the next version. The
    registry loads it from a step module; `to_version` is only for the check that a step does not skip a version."""

    def decorate(fn: Callable) -> Callable:
        setattr(fn, _STEP_ATTRIBUTE, (from_version, name, track, to_version))
        return fn

    return decorate


class Step:
    def __init__(self, from_version: int, name: str, track: str, fn: Callable, origin: str) -> None:
        self.from_version = from_version
        self.to_version = from_version + 1
        self.name = name
        self.track = track
        self.fn = fn
        self.origin = origin  # the module, for the messages

    def __repr__(self) -> str:
        return f"Step({self.track} {self.from_version} to {self.to_version} {self.name!r} in {self.origin})"


class RegistryError(Exception):
    pass


class Registry:
    """The steps of every track. A track runs `1 to 2`, `2 to 3`, ...: a gap, a duplicate or a step over two versions is an error
    that names the module."""

    def __init__(self, steps: Sequence[Step]) -> None:
        self.tracks: Dict[str, List[Step]] = {ENGINE_TRACK: []}
        for s in steps:
            self.tracks.setdefault(s.track, []).append(s)
        for track, items in self.tracks.items():
            items.sort(key=lambda s: s.from_version)
            self._validate(track, items)

    @staticmethod
    def _validate(track: str, items: List[Step]) -> None:
        names: Dict[str, Step] = {}
        for i, s in enumerate(items):
            expected = FIRST_VERSION + i
            if i > 0 and s.from_version == items[i - 1].from_version:
                other = items[i - 1]
                raise RegistryError(
                    f"track {track}: two steps from version {s.from_version}: {other.name} ({other.origin}) and {s.name} ({s.origin})"
                )
            if s.from_version != expected:
                raise RegistryError(f"track {track}: step {s.name} ({s.origin}) starts at version {s.from_version}, {expected} expected (a gap)")
            if s.name in names:
                raise RegistryError(f"track {track}: the step name {s.name} is used twice ({names[s.name].origin} and {s.origin})")
            names[s.name] = s

    def latest(self, track: str) -> int:
        items = self.tracks[track]
        return items[-1].to_version if items else FIRST_VERSION

    def steps_from(self, track: str, version: int) -> List[Step]:
        return [s for s in self.tracks[track] if s.from_version >= version]

    def track_names(self) -> List[str]:
        """The engine track first: a game's steps work on files that are already at the engine's version."""
        return [ENGINE_TRACK] + sorted(t for t in self.tracks if t != ENGINE_TRACK)


def attribute_of(track: str) -> str:
    return VERSION_ATTRIBUTE if track == ENGINE_TRACK else f"{VERSION_ATTRIBUTE}.{track}"


def load_steps(folder: Path, allow_engine_track: bool) -> List[Step]:
    """The steps of every `*.py` of `folder` (a module whose name starts with `_` is skipped), in the order of the file names."""
    steps: List[Step] = []
    if not folder.is_dir():
        raise RegistryError(f"the steps folder {folder} does not exist")
    for path in sorted(folder.glob("*.py")):
        if path.name.startswith("_"):
            continue
        module_name = f"migrate_steps_{abs(hash(str(path.resolve())))}_{path.stem}"
        spec = importlib.util.spec_from_file_location(module_name, path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        try:
            spec.loader.exec_module(module)
        except Exception as e:  # a broken step module is reported by name, not as a traceback of the tool
            raise RegistryError(f"{path}: cannot be loaded: {type(e).__name__}: {e}") from e
        for fn in vars(module).values():
            info = getattr(fn, _STEP_ATTRIBUTE, None) if callable(fn) else None
            if info is None or getattr(fn, "__module__", None) != module_name:
                continue
            from_version, name, track, to_version = info
            where = path.name
            if not isinstance(from_version, int) or isinstance(from_version, bool) or from_version < FIRST_VERSION:
                raise RegistryError(f"{where}: the step {name!r} has from_version {from_version!r}: a number from {FIRST_VERSION} up is expected")
            if to_version is not None and to_version != from_version + 1:
                raise RegistryError(f"{where}: the step {name!r} goes from {from_version} to {to_version}: a step moves one version")
            if not isinstance(name, str) or not _STEP_NAME.match(name):
                raise RegistryError(f"{where}: the step name {name!r} is not a name")
            if not isinstance(track, str) or not _TRACK_NAME.match(track):
                raise RegistryError(f"{where}: the step {name!r} has the track {track!r}: letters, digits, _ and - only")
            if track == ENGINE_TRACK and not allow_engine_track:
                raise RegistryError(f"{where}: the track {ENGINE_TRACK!r} is the engine's; a game's steps declare a track of their own")
            steps.append(Step(from_version, name, track, fn, where))
    return steps


def build_registry(engine_steps: Path = ENGINE_STEPS_DIR, game_steps: Sequence[Path] = ()) -> Registry:
    steps = load_steps(engine_steps, allow_engine_track=True)
    for folder in game_steps:
        steps.extend(load_steps(folder, allow_engine_track=False))
    return Registry(steps)


# ---- one file ---------------------------------------------------------------------------------------------------------------


class Note:
    def __init__(self, line: Optional[int], text: str) -> None:
        self.line = line
        self.text = text


class StepContext:
    """What a step reports with: `report` for an element it changed, `fail` for one it cannot change (the file is then left as it was)."""

    def __init__(self) -> None:
        self.notes: List[Note] = []
        self.failures: List[Note] = []

    def report(self, element: Optional[xmltext.Element], text: str) -> None:
        self.notes.append(Note(element.line if element is not None else None, text))

    def fail(self, element: Optional[xmltext.Element], reason: str) -> None:
        self.failures.append(Note(element.line if element is not None else None, reason))


class Applied:
    def __init__(self, step_: Step, notes: List[Note]) -> None:
        self.step = step_
        self.notes = notes


class FileResult:
    def __init__(self, rel: str) -> None:
        self.rel = rel
        self.applied: List[Applied] = []  # every step that ran, in order
        self.failures: List[Tuple[Step, Note]] = []
        self.error: Optional[str] = None  # the file could not be read, parsed or versioned
        self.pending = False  # some track is behind
        self.before = b""
        self.after = b""

    @property
    def ok(self) -> bool:
        return self.error is None and not self.failures

    @property
    def changed(self) -> bool:
        return self.ok and self.after != self.before


def sniff_root_name(text: str) -> Optional[str]:
    """The name of the root element read without parsing, so a file that is not a content file is not judged by its syntax."""
    match = _PROLOG_SKIP.match(text)
    root = _ROOT_NAME.match(text, match.end() if match else 0)
    return root.group(1) if root else None


def read_version(root: xmltext.Element, track: str, latest: int) -> int:
    """The version of the file for a track: absent is 1; malformed, 0 or newer than the tool knows is an error."""
    attribute = attribute_of(track)
    raw = root.get(attribute)
    if raw is None:
        return FIRST_VERSION
    if not _VERSION_TEXT.match(raw):
        raise ValueError(f'{attribute}="{raw}" is not a number')
    version = int(raw)
    if version < FIRST_VERSION:
        raise ValueError(f'{attribute}="{raw}" is older than the first version ({FIRST_VERSION}); no step moves it')
    if version > latest:
        raise ValueError(f'{attribute}="{raw}" is newer than this tool knows ({latest}): update the engine')
    return version


def process_file(path: Path, rel: str, registry: Registry) -> FileResult:
    """Run the pending steps of every track over one file, in memory. Nothing is written here."""
    result = FileResult(rel)
    try:
        data = path.read_bytes()
    except OSError as e:
        result.error = f"{rel}: cannot be read: {e.strerror or e}"
        return result
    result.before = result.after = data
    try:
        doc = xmltext.parse_bytes(data)
    except xmltext.XmlTextError as e:
        result.error = f"{rel}:{e.line}:{e.column}: {e.message}"
        return result
    for track in registry.track_names():
        latest = registry.latest(track)
        try:
            version = read_version(doc.root, track, latest)
        except ValueError as e:
            result.error = f"{rel}:{doc.root.line}: {e}"
            return result
        original = version
        for s in registry.steps_from(track, version):
            ctx = StepContext()
            try:
                s.fn(doc, ctx)
            except Exception as e:  # a bug in a step must not stop the run over the other files
                detail = traceback.extract_tb(e.__traceback__)[-1]
                result.error = f"{rel}: the step {s.name} ({s.origin}) raised {type(e).__name__}: {e} (at {Path(detail.filename).name}:{detail.lineno})"
                return result
            if ctx.failures:
                result.failures.extend((s, f) for f in ctx.failures)
                return result
            result.applied.append(Applied(s, ctx.notes))
            version = s.to_version
        if version != original:
            result.pending = True
            doc.root.set(attribute_of(track), str(version))
    result.after = doc.to_bytes()
    return result


def write_atomically(path: Path, data: bytes) -> None:
    descriptor, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name + ".", suffix=".tmp")
    try:
        with os.fdopen(descriptor, "wb") as f:
            f.write(data)
        shutil.copymode(path, temporary)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


# ---- finding the files ------------------------------------------------------------------------------------------------------


def _skip_folder(parent: Path, root: Path, name: str) -> bool:
    if name in SKIPPED_FOLDERS or name.startswith(SKIPPED_FOLDER_PREFIXES):
        return True
    if parent == root and name == ENGINE_SUBMODULE:
        folder = parent / name
        return (folder / ".git").exists() or (folder / "tools" / "migrate" / "migrate.py").exists()
    return False


def find_content_files(root: Path) -> Iterator[Tuple[Path, str]]:
    """(path, path relative to `root` with forward slashes) of every `*.xml` with a `config` folder in its path whose root element
    is one of `CONTENT_ROOTS`, in path order."""
    for folder, subfolders, files in os.walk(root):
        parent = Path(folder)
        subfolders[:] = sorted(n for n in subfolders if not _skip_folder(parent, root, n))
        relative = parent.relative_to(root).parts
        if CONFIG_FOLDER not in relative and root.name != CONFIG_FOLDER:
            continue
        for name in sorted(files):
            if not name.endswith(".xml"):
                continue
            path = parent / name
            try:
                text = path.read_bytes().decode("utf-8", errors="replace")
            except OSError:
                continue
            if sniff_root_name(text) in CONTENT_ROOTS:
                yield path, "/".join(relative + (name,))


# ---- the command ------------------------------------------------------------------------------------------------------------


def _plural(n: int, noun: str) -> str:
    return f"{n} {noun}" if n == 1 else f"{n} {noun}s"


def _report(result: FileResult, out) -> None:
    for applied in result.applied:
        s = applied.step
        count = len(applied.notes)
        what = "no element changed" if count == 0 else _plural(count, "element")
        print(f"{result.rel}: {s.from_version} to {s.to_version} ({s.name}): {what}", file=out)
        for note in applied.notes:
            where = f"{result.rel}:{note.line}" if note.line else result.rel
            print(f"  {where}: {note.text}", file=out)


def _report_problems(result: FileResult, err) -> None:
    if result.error:
        print(result.error, file=err)
    for s, failure in result.failures:
        where = f"{result.rel}:{failure.line}" if failure.line else result.rel
        print(f"{where}: {s.name}: {failure.text}", file=err)
    if result.failures:
        print(f"{result.rel}: not changed: a step failed", file=err)


def _diff(result: FileResult, out) -> None:
    before = result.before.decode("utf-8").splitlines(keepends=True)
    after = result.after.decode("utf-8").splitlines(keepends=True)
    for line in difflib.unified_diff(before, after, fromfile=f"a/{result.rel}", tofile=f"b/{result.rel}"):
        out.write(line if line.endswith("\n") else line + "\n\\ No newline at end of file\n")


def run(root: Path, registry: Registry, check: bool, show_diff: bool, out=None, err=None) -> int:
    out = out or sys.stdout
    err = err or sys.stderr
    results = [process_file(path, rel, registry) for path, rel in find_content_files(root)]
    changed = failed = current = 0
    for result in results:
        if not result.ok:
            failed += 1
            _report_problems(result, err)
            continue
        if not result.changed:
            current += 1
            continue
        changed += 1
        if check:
            print(f"{result.rel}: needs migration ({', '.join(f'{a.step.from_version} to {a.step.to_version}' for a in result.applied)})", file=out)
        else:
            _report(result, out)
        if show_diff:
            _diff(result, out)
        if not check and not show_diff:
            try:
                write_atomically(root / result.rel, result.after)
            except OSError as e:
                failed += 1
                changed -= 1
                print(f"{result.rel}: cannot be written: {e.strerror or e}", file=err)
    verb = "need migration" if check else ("would change" if show_diff else "changed")
    print(f"migrate: {_plural(len(results), 'content file')}: {changed} {verb}, {current} current, {failed} failed", file=out)
    if failed:
        return EXIT_ERROR
    if check and changed:
        return EXIT_PENDING
    return EXIT_OK


def main(argv: Optional[Sequence[str]] = None, engine_steps: Path = ENGINE_STEPS_DIR) -> int:
    parser = argparse.ArgumentParser(description="Rewrite the content files of a game to the current content version.")
    parser.add_argument("folder", nargs="?", help="the game folder (same as --root)")
    parser.add_argument("--root", help="the folder to search for content files (default: the current folder)")
    parser.add_argument("--steps", action="append", default=[], metavar="DIR", help="a folder of a game's own step modules (a track of its own); repeatable")
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 when a file needs a step")
    parser.add_argument("--diff", action="store_true", help="print a unified diff of what would change and write nothing")
    args = parser.parse_args(argv)
    if args.folder and args.root and Path(args.folder).resolve() != Path(args.root).resolve():
        parser.error("the folder and --root differ")
    root = Path(args.root or args.folder or ".").resolve()
    if not root.is_dir():
        print(f"migrate: {root} is not a folder", file=sys.stderr)
        return EXIT_ERROR
    try:
        registry = build_registry(engine_steps, [Path(p) for p in args.steps])
    except RegistryError as e:
        print(f"migrate: {e}", file=sys.stderr)
        return EXIT_ERROR
    return run(root, registry, args.check, args.diff)


if __name__ == "__main__":
    sys.modules.setdefault("migrate", sys.modules[__name__])  # a step module says `from migrate import step`
    sys.exit(main())
