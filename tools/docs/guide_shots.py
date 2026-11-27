#!/usr/bin/env python3
"""Take the pictures of docs/guide/img/ from a built game, over the control API.

    python3 tools/docs/guide_shots.py [--only NAME[,NAME...]] [--preset P] [--bin DIR] [--out DIR]

One row of guide_shots.json is one picture: the game is started headless on the manual clock (`GameSession`; with a `contentOverlay`,
on a temporary copy of that repository folder as its content overlay), the steps of the row are called, the control API `screenshot` is
written as `<out>/<name>.png`. Exit 0 done, 1 an error, 77 an executable is missing (build the preset first). Standard library only,
Python 3.10+; the rules are in tools/docs/CLAUDE.md.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools" / "control"))

from bagi_control_client import ControlError  # noqa: E402
from bagi_session import GameSession, SessionError  # noqa: E402
from game_process import preset_executable  # noqa: E402

MANIFEST = REPOSITORY_ROOT / "tools" / "docs" / "guide_shots.json"
# The editor reads its layout and preferences from the per-user folder this variable names: a picture of it must not depend on, or write into, the
# folder of whoever takes it
EDITOR_DIRECTORY_VARIABLE = "BAGI_EDITOR_DIR"
DEFAULT_OUT = REPOSITORY_ROOT / "docs" / "guide" / "img"
DEFAULT_MAC_PRESET = "macos-debug"
DEFAULT_MAX_SIZE = 640
EXIT_SKIPPED = 77
NAME_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
EDITOR_RUN_MODE = "Editor"


class ShotError(Exception):
    """A row that cannot be taken: the message says which and why."""


def load_rows(manifest: Path) -> list[dict[str, Any]]:
    try:
        rows = json.loads(manifest.read_text(encoding="utf-8"))["shots"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ShotError(f'{manifest}: not a {{"shots": [rows]}} file ({error})') from error
    names: set[str] = set()
    for row in rows:
        name = row.get("name") if isinstance(row, dict) else None
        if not isinstance(name, str) or not NAME_PATTERN.fullmatch(name):
            raise ShotError(f"{manifest}: a row needs a `name` of lower case letters, digits and dashes: {row!r}")
        if name in names:
            raise ShotError(f"{manifest}: two rows are named {name}")
        names.add(name)
        if not isinstance(row.get("exe"), str):
            raise ShotError(f"{manifest}: row {name} needs `exe`, the target name of the game")
        if "contentOverlay" in row:
            overlay_folder(row)
        for step in row.get("steps", []):
            if not (isinstance(step, list) and len(step) == 2 and isinstance(step[0], str) and isinstance(step[1], dict)):
                raise ShotError(f"{manifest}: row {name}: a step is [method, params], not {step!r}")
    return rows


def overlay_folder(row: dict[str, Any]) -> Path:
    """The folder of the row's `contentOverlay` in the repository; a path that could leave it is refused."""
    value = row["contentOverlay"]
    if not isinstance(value, str) or not value:
        raise ShotError(f"row {row['name']}: `contentOverlay` is a folder path relative to the repository root")
    parts = PureWindowsPath(value).parts + PurePosixPath(value).parts
    if PurePosixPath(value).is_absolute() or PureWindowsPath(value).anchor or ".." in parts:
        raise ShotError(f"row {row['name']}: `contentOverlay` {value!r} has to stay inside the repository (relative, no `..`)")
    return REPOSITORY_ROOT / value


def copy_overlay(row: dict[str, Any], target: Path) -> Path:
    """Copies the row's folder to `target`: a game writes into its overlay, and the repository must never change."""
    source = overlay_folder(row)
    if not source.is_dir():
        raise ShotError(f"row {row['name']}: `contentOverlay` {row['contentOverlay']!r} is not a folder of the repository")
    shutil.copytree(source, target)
    return target


def select_rows(rows: list[dict[str, Any]], only: str | None) -> list[dict[str, Any]]:
    if not only:
        return rows
    wanted = [name for name in only.split(",") if name]
    unknown = [name for name in wanted if name not in {row["name"] for row in rows}]
    if unknown:
        raise ShotError(f"no row named {', '.join(unknown)} in the manifest")
    return [row for row in rows if row["name"] in wanted]


def preset_of(row: dict[str, Any], argument: str | None) -> str:
    preset = argument or row.get("preset") or (DEFAULT_MAC_PRESET if sys.platform == "darwin" else None)
    if not preset:
        raise ShotError(f"row {row['name']}: name the build with --preset (the default is only for macOS)")
    return preset


def executable_of(row: dict[str, Any], preset: str, bin_dir: Path | None) -> Path:
    if bin_dir is not None:
        return bin_dir / row["exe"]
    return preset_executable(preset, row["exe"], REPOSITORY_ROOT)


def take(row: dict[str, Any], exe: Path, out: Path) -> Path:
    """Starts the game of the row, runs its steps and writes the picture; the game is closed, and the copy of the overlay removed, whatever happens."""
    with tempfile.TemporaryDirectory(prefix="bagi-guide-shot-") as scratch:
        overlay = copy_overlay(row, Path(scratch) / "content") if "contentOverlay" in row else None
        session = GameSession(client_name="guide_shots")
        previous_editor_directory = os.environ.get(EDITOR_DIRECTORY_VARIABLE)
        os.environ[EDITOR_DIRECTORY_VARIABLE] = str(Path(scratch) / "editor")
        try:
            description = session.launch(
                exe=str(exe),
                window=row.get("window"),
                density=row.get("density"),
                editor=bool(row.get("editor")),
                content_overlay=None if overlay is None else str(overlay),
            )
            if row.get("editor") and description["status"].get("runMode") != EDITOR_RUN_MODE:
                raise ShotError(f"row {row['name']}: the build has no editor (status.runMode is {description['status'].get('runMode')!r})")
            for method, params in row.get("steps", []):
                session.call(method, params)
            png, _ = session.screenshot(row.get("maxSize", DEFAULT_MAX_SIZE))
        finally:
            session.close()
            if previous_editor_directory is None:
                os.environ.pop(EDITOR_DIRECTORY_VARIABLE, None)
            else:
                os.environ[EDITOR_DIRECTORY_VARIABLE] = previous_editor_directory
    target = out / f"{row['name']}.png"
    out.mkdir(parents=True, exist_ok=True)
    target.write_bytes(png)
    return target


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", help="the names of the rows to take, comma separated (default: all)")
    parser.add_argument("--preset", help="the build preset of the executables (default: the row's, else macos-debug on macOS)")
    parser.add_argument("--bin", type=Path, help="the folder of the executables instead of build/<preset>/bin")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="where the pictures go (default: docs/guide/img)")
    parser.add_argument("--manifest", type=Path, default=MANIFEST, help="the rows (default: tools/docs/guide_shots.json)")
    args = parser.parse_args(argv)

    try:
        rows = select_rows(load_rows(args.manifest), args.only)
        jobs = [(row, executable_of(row, preset_of(row, args.preset), args.bin)) for row in rows]
        missing = [exe for _, exe in jobs if not exe.is_file()]
        if missing:
            print("no game executable at " + ", ".join(str(exe) for exe in missing) + "; build the preset first", file=sys.stderr)
            return EXIT_SKIPPED
        for row, exe in jobs:
            print(f"{take(row, exe, args.out)}  ({row.get('about', '')})")
    except (ShotError, SessionError, ControlError) as error:
        print(f"guide_shots: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
