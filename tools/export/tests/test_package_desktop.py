"""The package of a desktop game: its layout, what stays out of it, and what the export never deletes."""

import os
import shutil
from pathlib import Path

import pytest

from export_support import TARGET, FakeGame, tree, write

import package_desktop
from package_desktop import PackageError


def no_runner(command, cwd):
    raise AssertionError(f"a debug export runs nothing: {command}")


class FakeCook:
    """Writes what a cook of `layers` layers writes, a file and one in a folder per layer, and remembers how it was started."""

    def __init__(self, layers: int, code: int = 0):
        self.layers = layers
        self.code = code
        self.calls = []

    def __call__(self, command, cwd):
        self.calls.append((command, cwd))
        output = Path(next(argument for argument in command if argument.startswith("--cook=")).removeprefix("--cook="))
        for layer in range(self.layers):
            write(output / f"layer{layer}" / "config" / "App.bin", f"cooked {layer}")
            write(output / f"layer{layer}" / "config" / "nodes" / "Node.bin", f"node {layer}")
        return self.code


def make(game: FakeGame, out: Path, *, release=False, runner=no_runner, **extra) -> Path:
    return package_desktop.package(game.manifest, out, release=release, project_dir=game.project, runner=runner, **extra)


def files_of(folder: Path) -> set[str]:
    return {name for name, data in tree(folder).items() if data is not None}


def test_the_package_holds_the_program_its_libraries_and_the_content(tmp_path):
    game = FakeGame(tmp_path)
    for skipped in ("config/imgui.ini", "config/.hidden", "assets/.cache/a.png", "assets/art/a.artgen.json"):
        write(game.game_root / skipped)
    write(game.game_root / "source.cpp")

    out = make(game, game.project / "dist" / "Game-debug")

    assert files_of(out) == {
        TARGET, "libfake.2.dylib", "THIRD_PARTY_NOTICES.txt", "bagi-package.txt",
        "content/engine/config/App.xml", "content/engine/assets/art/engine.png",
        "content/game/config/App.xml", "content/game/assets/art/game.png",
    }
    assert (out / "bagi-package.txt").read_text(encoding="utf-8").splitlines()[0] == TARGET
    if os.name != "nt":
        assert os.access(out / TARGET, os.X_OK)
    assert sorted(path.name for path in out.parent.iterdir()) == ["Game-debug"]


def test_one_folder_may_be_both_content_roots(tmp_path):
    game = FakeGame(tmp_path)
    game.manifest.write_text(game.manifest.read_text().replace(str(game.game_root), str(game.engine_root)))

    out = make(game, tmp_path / "out")

    assert tree(out / "content" / "game") == tree(out / "content" / "engine")


def test_a_release_package_ships_the_cooked_config_of_each_layer(tmp_path):
    game = FakeGame(tmp_path)
    cook = FakeCook(layers=2)

    out = make(game, tmp_path / "out", release=True, runner=cook)

    for layer, root in enumerate(("engine", "game")):
        assert tree(out / "content" / root / "config") == {
            "App.bin": f"cooked {layer}".encode(), "nodes": None, "nodes/Node.bin": f"node {layer}".encode()}
    (command, cwd), = cook.calls
    # The packaged executable cooks, so what it reads is what it will read as a package; what it leaves where it runs is no part of it
    assert Path(command[0]).parent.name == "out.partial"
    assert Path(command[0]).name == TARGET
    assert not cwd.is_relative_to(out.parent)
    assert "--cook-verify" in command


def test_a_root_without_config_is_no_layer_of_the_cook(tmp_path):
    game = FakeGame(tmp_path)
    shutil.rmtree(game.engine_root / "config")

    out = make(game, tmp_path / "out", release=True, runner=FakeCook(layers=1))

    assert (out / "content" / "game" / "config" / "App.bin").read_bytes() == b"cooked 0"
    assert not (out / "content" / "engine" / "config").exists()


def test_a_new_package_replaces_the_old_one_as_a_whole(tmp_path):
    game = FakeGame(tmp_path)
    out = tmp_path / "out"
    out.mkdir()  # an empty folder is taken
    make(game, out)
    (game.game_root / "assets" / "art" / "game.png").unlink()

    make(game, out)

    assert not (out / "content" / "game" / "assets" / "art" / "game.png").exists()
    assert sorted(path.name for path in tmp_path.iterdir()) == ["out", "project"]


@pytest.mark.parametrize("target", [
    "project",                      # the project
    ".",                            # contains the project
    "project/game",                 # a content root
    "project/game/config",          # a content folder
    "project/engine/assets/export",  # inside a content folder, not there yet
    "project/build/export/preset/package",
    "notes",                        # a folder of the owner's
    "notes/todo.txt",               # a file
    "taken",                        # its .partial sibling is a folder of the owner's
    "kept",                         # its .previous sibling is
])
def test_an_output_that_could_cost_the_owner_a_file_is_refused(tmp_path, target):
    game = FakeGame(tmp_path)
    write(tmp_path / "notes" / "todo.txt")
    write(tmp_path / "taken.partial" / "todo.txt")
    write(tmp_path / "kept.previous" / "todo.txt")
    before = tree(tmp_path)

    with pytest.raises(PackageError):
        make(game, tmp_path / target)

    assert tree(tmp_path) == before


@pytest.mark.skipif(os.name == "nt", reason="a symbolic link needs a privilege on Windows")
def test_a_link_to_a_protected_folder_is_refused(tmp_path):
    game = FakeGame(tmp_path)
    (tmp_path / "link").symlink_to(game.game_root / "assets", target_is_directory=True)
    before = tree(game.project)

    with pytest.raises(PackageError):
        make(game, tmp_path / "link" / "out")

    assert tree(game.project) == before


def test_a_marked_folder_that_holds_the_project_is_refused(tmp_path):
    game = FakeGame(tmp_path)
    write(tmp_path / "bagi-package.txt")
    before = tree(tmp_path)

    with pytest.raises(PackageError):
        make(game, tmp_path)

    assert tree(tmp_path) == before


def test_a_failed_cook_keeps_the_previous_package(tmp_path):
    game = FakeGame(tmp_path)
    out = make(game, tmp_path / "out")
    write(game.game_root / "config" / "New.xml")
    before = tree(tmp_path)

    with pytest.raises(PackageError, match="cook failed"):
        make(game, out, release=True, runner=FakeCook(layers=2, code=1))

    assert tree(tmp_path) == before


class GivenUp(Exception):
    pass


def test_an_export_given_up_during_the_copy_keeps_the_previous_package(tmp_path):
    game = FakeGame(tmp_path)
    out = make(game, tmp_path / "out")
    write(game.game_root / "config" / "New.xml")
    before = tree(tmp_path)

    def give_up():
        raise GivenUp

    with pytest.raises(GivenUp):
        make(game, out, checkpoint=give_up)

    assert tree(tmp_path) == before


def test_a_target_that_was_not_built_leaves_everything(tmp_path):
    game = FakeGame(tmp_path)
    out = make(game, tmp_path / "out")
    game.library.unlink()
    before = tree(tmp_path)

    with pytest.raises(PackageError, match="build the target"):
        make(game, out)

    assert tree(tmp_path) == before
