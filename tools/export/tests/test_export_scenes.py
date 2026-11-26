"""The scenes an export leaves out: which file is edited, what stays of it, and what is refused."""

import re
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

from export_support import TARGET, FakeGame, tree, write

import export
import package_desktop
import scenes
from scenes import SceneError

# What the engine's reader accepts and a strict parser would not (`--` in a comment), a `<scene` and a `>` where they are no markup
GAME_CONFIG = """<?xml version="1.0"?>
<SceneManager contentVersion="5">
  <!-- chosen by --start-scene; <scene name="Menu"> in a comment is none -->
  <scenes>
    <scene name="Menu" title="a &gt; b > c">
      <nodes><node name="scenes"><scene name="Level" /></node></nodes>
    </scene>
    <scene name="Level">
      <note><![CDATA[</scene>]]></note>
      <text>  two  spaces &amp; a &#10; break </text>
    </scene>
    <scene name='Credits &amp; thanks'/>
    <scene name="Last" viewWidth="10"></scene>
  </scenes>
</SceneManager>
"""
ENGINE_CONFIG = '<SceneManager><scenes><scene name="EngineScene"/><scene name="Level"/></scenes></SceneManager>'


def shape(element: ElementTree.Element) -> tuple:
    """Everything of an element that a reader sees."""
    return element.tag, element.attrib, (element.text or "").strip(" \n"), [shape(child) for child in element]


def scenes_of(path: Path) -> dict[str, tuple]:
    # The comments go first: expat refuses the `--` inside one
    data = re.sub(rb"<!--.*?-->", b"", path.read_bytes(), flags=re.DOTALL)
    return {scene.get("name"): shape(scene) for scene in ElementTree.fromstring(data).find("scenes")}


def layers(tmp_path: Path, engine: str | None = ENGINE_CONFIG, game: str | None = GAME_CONFIG) -> list[Path]:
    folders = [tmp_path / "engine", tmp_path / "game"]
    for folder, text in zip(folders, (engine, game)):
        if text is not None:
            write(folder / "SceneConfig.xml", text)
    return folders


def test_only_the_named_scenes_leave_the_game_file(tmp_path):
    folders = layers(tmp_path)
    before = scenes_of(folders[1] / "SceneConfig.xml")

    scenes.exclude_scenes(folders, ["Credits & thanks", "Level"])

    assert scenes_of(folders[1] / "SceneConfig.xml") == {name: before[name] for name in ("Menu", "Last")}
    assert (folders[0] / "SceneConfig.xml").read_text(encoding="utf-8") == ENGINE_CONFIG


@pytest.mark.parametrize("game_has_a_config_folder", [False, True])
def test_the_highest_layer_that_has_the_file_is_edited(tmp_path, game_has_a_config_folder):
    folders = layers(tmp_path, game=None)
    if game_has_a_config_folder:
        write(folders[1] / "App.xml")

    scenes.exclude_scenes(folders, ["Level"])

    assert list(scenes_of(folders[0] / "SceneConfig.xml")) == ["EngineScene"]


@pytest.mark.parametrize("engine, game, name, reason", [
    (ENGINE_CONFIG, GAME_CONFIG, "Nowhere", "no scene 'Nowhere' in .*its scenes are Menu, Level, Credits & thanks, Last$"),
    (ENGINE_CONFIG, GAME_CONFIG, "EngineScene", "no scene 'EngineScene'"),
    (ENGINE_CONFIG, GAME_CONFIG, "Menu", "'Menu' is the first of .* cannot be left out"),
    (None, None, "Level", "no content root has config/SceneConfig.xml"),
    (None, "<SceneManager><scenes/></SceneManager>", "Level", "its scenes are none"),
    (None, "<SceneManager/>", "Level", "has no <scenes>"),
    (None, GAME_CONFIG.replace("</scenes>", ""), "Level", "does not parse"),
    (None, GAME_CONFIG.replace("</SceneManager>", ""), "Level", "does not parse"),
    (None, GAME_CONFIG.replace('name="Last"', 'name="Last'), "Level", "does not parse"),
    (None, GAME_CONFIG.replace("<!-- chosen", "<! chosen"), "Level", r"does not parse \(line 3\)"),
    (None, "", "Level", "does not parse"),
])
def test_what_cannot_be_left_out_is_refused_and_nothing_is_written(tmp_path, engine, game, name, reason):
    folders = layers(tmp_path, engine, None)
    if game is not None:
        folders[1].mkdir(parents=True, exist_ok=True)
        (folders[1] / "SceneConfig.xml").write_text(game, encoding="utf-8")
    before = tree(tmp_path)

    with pytest.raises(SceneError, match=reason):
        scenes.exclude_scenes(folders, ["Last", name] if name != "Level" else [name])

    assert tree(tmp_path) == before


def test_a_cooked_file_on_top_is_refused(tmp_path):
    folders = layers(tmp_path, game=None)
    write(folders[1] / "SceneConfig.bin")

    with pytest.raises(SceneError, match="the cooked SceneConfig.bin"):
        scenes.exclude_scenes(folders, ["Level"])


# --- the package -------------------------------------------------------------------------------------------


def game_with_scenes(tmp_path: Path) -> FakeGame:
    game = FakeGame(tmp_path, preset="macos-debug")
    write(game.engine_root / "config" / "SceneConfig.xml", ENGINE_CONFIG)
    write(game.game_root / "config" / "SceneConfig.xml", GAME_CONFIG)
    return game


def export_without(game: FakeGame, *names: str, more: tuple[str, ...] = ()) -> int:
    arguments = ["build", "--project", str(game.project), "--target", TARGET, "--config", "debug", *more]
    for name in names:
        arguments += ["--exclude-scene", name]
    return export.main(arguments, runner=lambda *_: 0, system="Darwin")


def test_the_package_lacks_the_excluded_scenes_and_the_sources_keep_them(tmp_path):
    game = game_with_scenes(tmp_path)
    sources = [tree(root) for root in (game.engine_root, game.game_root)]

    assert export_without(game, "Level", "Last") == 0

    content = game.project / "dist" / f"{TARGET}-debug" / "content"
    assert list(scenes_of(content / "game" / "config" / "SceneConfig.xml")) == ["Menu", "Credits & thanks"]
    assert [tree(root) for root in (game.engine_root, game.game_root)] == sources


def test_the_cook_reads_content_without_the_excluded_scenes(tmp_path):
    game = game_with_scenes(tmp_path)
    cooked = []

    def cook(command, cwd):
        out = Path(command[0]).parent
        cooked.append(list(scenes_of(out / "content" / "game" / "config" / "SceneConfig.xml")))
        return 1

    with pytest.raises(package_desktop.PackageError):
        package_desktop.package(game.manifest, tmp_path / "out", release=True, project_dir=game.project, runner=cook,
                                excluded_scenes=["Level"])

    assert cooked == [["Menu", "Credits & thanks", "Last"]]


def test_a_refused_scene_keeps_the_previous_package(tmp_path, capsys):
    game = game_with_scenes(tmp_path)
    assert export_without(game) == 0
    dist = game.project / "dist"
    before = tree(dist)
    capsys.readouterr()

    assert export_without(game, "Menu") == 1

    captured = capsys.readouterr()
    assert captured.out.splitlines()[-1].startswith("== export failed: the scene 'Menu' is the first of ")
    assert "Traceback" not in captured.err
    assert tree(dist) == before


def test_a_dry_run_names_the_scenes_it_would_leave_out(tmp_path, capsys):
    game = game_with_scenes(tmp_path)

    assert export_without(game, "Level", "Last", more=("--dry-run",)) == 0

    assert any(line.startswith("would package ") and line.endswith(" without the scenes Level, Last")
               for line in capsys.readouterr().out.splitlines())
    assert not (game.project / "dist").exists()
