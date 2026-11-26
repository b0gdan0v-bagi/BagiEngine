"""The web side of an export: the content a web build preloads without the excluded scenes, and the package of the built page
(tools/web/package.py) in the export's output folder. Standard library only; the rules: tools/export/CLAUDE.md.
"""

from __future__ import annotations

import json
import shutil
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

import package_desktop
import scenes
from package_desktop import PackageError

ENGINE_DIR = Path(__file__).resolve().parents[2]
PACKAGE_SCRIPT = ENGINE_DIR / "tools" / "web" / "package.py"
# The name package.py gives the page, and so the first line of a web package's marker
INDEX_NAME = "index.html"
# The folder of the build folder that holds the staged content
CONTENT_FOLDER = "export-content"


@dataclass(frozen=True)
class Manifest:
    engine_root: Path
    game_root: Path


def read_manifest(path: Path) -> Manifest:
    """The manifest of a game's web target (cmake/BagiExport.cmake): there from the configure on."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return Manifest(engine_root=Path(data["engineRoot"]), game_root=Path(data["gameRoot"]))
    except FileNotFoundError as error:
        raise PackageError(f"no export manifest {path}: the target is not a game executable") from error
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise PackageError(f"cannot read the export manifest {path}: {error!r}") from error


def check_output(out: Path, manifest: Manifest, project_dir: Path, build_dir: Path) -> None:
    # web/ is the third content root of a page, over the engine's and the game's
    roots = (manifest.engine_root, manifest.game_root, manifest.engine_root / "web")
    package_desktop.check_output(out, roots, project_dir, build_dir)


def stage_content(manifest: Manifest, folder: Path, excluded_scenes: Sequence[str], checkpoint: Callable[[], None]) -> None:
    """Makes `folder` anew with a copy of the config/ of the engine and the game, laid out as a cook's output (so that
    ENGINE_WEB_COOKED_DIR can name it), and removes the scenes from it."""

    def copy_content(source: str, target: str) -> None:
        checkpoint()
        shutil.copy2(source, target)  # with its time: the build packs the content again when a file is newer than the package

    layers: list[Path] = []
    try:
        if folder.exists():
            shutil.rmtree(folder)
        for root in (manifest.engine_root, manifest.game_root):
            if (root / "config").is_dir():
                # be_web_preload_args numbers only the roots that have a config/, lowest first
                layers.append(folder / f"layer{len(layers)}" / "config")
                shutil.copytree(root / "config", layers[-1], copy_function=copy_content)
    except OSError as error:
        raise PackageError(f"cannot copy the content into {folder}: {error}") from error
    scenes.exclude_scenes(layers, excluded_scenes)


def package(manifest_file: Path, out_dir: Path, *, target: str, single_thread: bool, project_dir: Path, app_file: Path,
            runner: package_desktop.Runner, checkpoint: Callable[[], None] = lambda: None) -> Path:
    """Packages the built page of `target` into `out_dir` and returns that folder, resolved. `app_file` is passed on when it
    is there. package.py runs through `runner`, so that a cancel stops it. Whatever fails or raises, a package that was in
    `out_dir` stays as it was."""
    manifest = read_manifest(manifest_file)
    out = out_dir.resolve()
    # The manifest lies in <build folder>/export/
    build_dir = manifest_file.resolve().parent.parent
    check_output(out, manifest, project_dir, build_dir)
    with package_desktop.partial_package(out) as partial:
        partial.mkdir(parents=True)
        # First: it makes what a killed export leaves behind replaceable
        (partial / package_desktop.MARKER_NAME).write_text(INDEX_NAME + "\n", encoding="utf-8")
        command = [sys.executable, str(PACKAGE_SCRIPT), str(build_dir / "bin"), str(partial), "--game", target]
        if single_thread:
            command.append("--single-thread")
        if app_file.is_file():
            command += ["--app", str(app_file)]
        code = runner(command, project_dir)
        if code != 0:
            raise PackageError(f"package.py failed with exit code {code}: its report is above")
        checkpoint()
    return out
