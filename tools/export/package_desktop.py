"""Assembles a built desktop game into a folder that runs by itself, from the manifest its build wrote. Standard library only.

The layout, the manifest and the safety rules: tools/export/CLAUDE.md. The command line is export.py.
"""

from __future__ import annotations

import json
import os
import shutil
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

# The names src/Modules/BECore/FileSystem/PackagedContent.h reads
MARKER_NAME = "bagi-package.txt"
CONTENT_FOLDER = "content"
ENGINE_FOLDER = "engine"
GAME_FOLDER = "game"

MOUNT_FOLDERS = ("config", "assets")
NOTICES_NAME = "THIRD_PARTY_NOTICES.txt"
# What the editor and the art pipeline keep beside the content and no game reads
SKIPPED_NAMES = ("imgui.ini",)
SKIPPED_SUFFIXES = (".artgen.json",)
# The folders beside the output: the package while it is made, and the old one while the new one takes its place
PARTIAL_SUFFIX = ".partial"
PREVIOUS_SUFFIX = ".previous"
COOK_TEMP_PREFIX = "bagi-export-cook-"

# Runs a command in a folder and returns its exit code
Runner = Callable[[list[str], Path], int]


class PackageError(Exception):
    """The package cannot be made; the message says why."""


@dataclass(frozen=True)
class Library:
    source: Path
    name: str  # the file name the executable loads it by


@dataclass(frozen=True)
class Manifest:
    executable: Path
    engine_root: Path
    game_root: Path
    notices: Path
    libraries: tuple[Library, ...]


def read_manifest(path: Path) -> Manifest:
    """The manifest of a built target (cmake/BagiExport.cmake), every file it names checked to be there."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        manifest = Manifest(
            executable=Path(data["executable"]),
            engine_root=Path(data["engineRoot"]),
            game_root=Path(data["gameRoot"]),
            notices=Path(data["notices"]),
            libraries=tuple(Library(Path(entry["source"]), entry["name"]) for entry in data["libraries"]),
        )
    except FileNotFoundError as error:
        raise PackageError(f"no export manifest {path}: the target is not a game executable of a Ninja build") from error
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise PackageError(f"cannot read the export manifest {path}: {error!r}") from error
    for file in (manifest.executable, manifest.notices, *(library.source for library in manifest.libraries)):
        if not file.is_file():
            raise PackageError(f"{file} is missing: build the target first")
    return manifest


def _same_file(first: Path, second: Path) -> bool:
    try:
        return os.path.samefile(first, second)
    except OSError:
        return False


def is_within(path: Path, folder: Path) -> bool:
    """Whether `path` is `folder` or lies below it, also under another spelling of it (a link, another letter case)."""
    folder = folder.resolve()
    return any(candidate == folder or _same_file(candidate, folder) for candidate in (path, *path.parents))


def _check_replaceable(folder: Path) -> None:
    """A folder the export may delete: missing, empty, or a package (it has the marker)."""
    if folder.is_symlink():
        raise PackageError(f"{folder} is a link: choose a folder")
    if not folder.exists():
        return
    if not folder.is_dir():
        raise PackageError(f"{folder} exists and is not a folder")
    if not (folder / MARKER_NAME).is_file() and any(folder.iterdir()):
        raise PackageError(f"{folder} is not empty and is no package (no {MARKER_NAME}): choose an empty or a new folder")


def _sibling(folder: Path, suffix: str) -> Path:
    return folder.with_name(folder.name + suffix)


def check_output(out: Path, manifest: Manifest, project_dir: Path, build_dir: Path) -> None:
    """Refuses an output folder whose replacement could delete something of the owner's, or that a build or the game would read.

    `out` is resolved. Nothing is written."""
    roots = (manifest.engine_root, manifest.game_root)
    for protected in (project_dir, *roots):
        if is_within(protected.resolve(), out):
            raise PackageError(f"{out} is or contains {protected}: choose a folder of its own")
    for content in (root / mount for root in roots for mount in MOUNT_FOLDERS):
        if is_within(out, content):
            raise PackageError(f"{out} is inside the content folder {content}")
    if is_within(out, build_dir):
        raise PackageError(f"{out} is inside the build folder {build_dir}")
    for folder in (out, _sibling(out, PARTIAL_SUFFIX), _sibling(out, PREVIOUS_SUFFIX)):
        _check_replaceable(folder)


def _skipped(_folder: str, names: list[str]) -> list[str]:
    return [name for name in names if name.startswith(".") or name in SKIPPED_NAMES or name.endswith(SKIPPED_SUFFIXES)]


def _assemble(manifest: Manifest, folder: Path, checkpoint: Callable[[], None]) -> None:
    folder.mkdir(parents=True)
    # First: it makes what a killed export leaves behind replaceable, and the cook reads the packaged content because of it
    (folder / MARKER_NAME).write_text(manifest.executable.name + "\n", encoding="utf-8")
    shutil.copy2(manifest.executable, folder / manifest.executable.name)
    for library in manifest.libraries:
        shutil.copy(library.source, folder / library.name)
    shutil.copyfile(manifest.notices, folder / NOTICES_NAME)

    def copy_content(source: str, target: str) -> None:
        checkpoint()
        shutil.copyfile(source, target)

    for name, root in ((ENGINE_FOLDER, manifest.engine_root), (GAME_FOLDER, manifest.game_root)):
        packaged = folder / CONTENT_FOLDER / name
        packaged.mkdir(parents=True)
        for mount in MOUNT_FOLDERS:
            if (root / mount).is_dir():
                shutil.copytree(root / mount, packaged / mount, ignore=_skipped, copy_function=copy_content)


def _cook(manifest: Manifest, folder: Path, runner: Runner) -> None:
    """Replaces each packaged config/ with what the packaged executable cooks from it."""
    # The cook numbers the layers it sees, lowest first: a root without config/ is none
    configs = [folder / CONTENT_FOLDER / name / "config" for name in (ENGINE_FOLDER, GAME_FOLDER)]
    configs = [config for config in configs if config.is_dir()]
    with tempfile.TemporaryDirectory(prefix=COOK_TEMP_PREFIX) as cooked_root:
        # In the temp folder: the cook of a game leaves a log file in its working directory, which is no part of a package
        code = runner([str(folder / manifest.executable.name), f"--cook={cooked_root}", "--cook-verify"], Path(cooked_root))
        if code != 0:
            raise PackageError(f"the cook failed with exit code {code}: its report is above")
        for layer, config in enumerate(configs):
            cooked = Path(cooked_root) / f"layer{layer}" / "config"
            if not cooked.is_dir():
                raise PackageError(f"the cook wrote no {cooked}")
            shutil.rmtree(config)
            shutil.move(cooked, config)


def _replace(partial: Path, out: Path) -> None:
    previous = _sibling(out, PREVIOUS_SUFFIX)
    if previous.exists():
        shutil.rmtree(previous)
    if out.exists():
        out.rename(previous)
    try:
        partial.rename(out)
    except OSError:
        if previous.exists():
            previous.rename(out)
        raise
    # The new package is in place: a file of the old one that cannot be removed does not fail the export
    shutil.rmtree(previous, ignore_errors=True)


def package(manifest_file: Path, out_dir: Path, *, release: bool, project_dir: Path, runner: Runner,
            checkpoint: Callable[[], None] = lambda: None) -> Path:
    """Makes the package of the manifest's target in `out_dir` and returns that folder, resolved.

    `release` cooks the config through `runner`. `checkpoint` is called between the copies and raises to give the export up.
    Whatever fails or raises, a package that was in `out_dir` stays as it was."""
    manifest = read_manifest(manifest_file)
    out = out_dir.resolve()
    # The manifest lies in <build folder>/export/
    check_output(out, manifest, project_dir, manifest_file.resolve().parent.parent)
    partial = _sibling(out, PARTIAL_SUFFIX)
    try:
        if partial.exists():
            shutil.rmtree(partial)
        _assemble(manifest, partial, checkpoint)
        if release:
            _cook(manifest, partial, runner)
        checkpoint()
        _replace(partial, out)
    except OSError as error:
        raise PackageError(f"cannot write the package: {error}") from error
    finally:
        shutil.rmtree(partial, ignore_errors=True)  # not there after a success
    return out
