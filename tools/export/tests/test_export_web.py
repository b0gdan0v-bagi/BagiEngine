"""The web target of export.py: its commands, the content a build with scenes left out preloads, and the package."""

import json
from pathlib import Path

import pytest

from export_support import TARGET, tree, write

import export

import build_and_deploy

SCENES = '<SceneManager><scenes><scene name="Menu"/><scene name="Level"/></scenes></SceneManager>'


class FakeWebGame:
    """A project with two content roots, an installed SDK and a runner that does what cmake and package.py leave behind."""

    def __init__(self, root: Path, preset: str = "web-debug"):
        self.project = root / "project"
        self.engine_root = self.project / "engine"
        self.game_root = self.project / "game"
        self.build = self.project.resolve() / "build" / "export" / preset
        self.emsdk = root / "emsdk"
        for name in build_and_deploy.SDK_FILES:
            write(self.emsdk / name)
        write(self.engine_root / "config" / "App.xml", "<engine/>")
        write(self.engine_root / "config" / "SceneConfig.xml", SCENES.replace("Menu", "EngineScene"))
        write(self.game_root / "config" / "SceneConfig.xml", SCENES)
        write(self.game_root / "assets" / "art.png")
        self.codes = {}  # the exit code of a command whose line holds the key
        self.calls = []
        self.content_at_configure = []

    def __call__(self, command, cwd, env, cancel) -> int:
        self.calls.append(command)
        line = " ".join(command)
        code = next((code for key, code in self.codes.items() if key in line), 0)
        if "--preset" in command:
            write(self.build / "export" / f"{TARGET}.json", json.dumps(
                {"target": TARGET, "engineRoot": str(self.engine_root), "gameRoot": str(self.game_root)}))
            content = command[-1].partition("-DENGINE_WEB_COOKED_DIR=")[2]
            if content:
                self.content_at_configure.append(tree(Path(content)))
        elif "package.py" in line:
            write(Path(command[3]) / "index.html", f"page {len(self.calls)}")
        return code

    def export(self, *more: str, config: str = "debug", out: Path | None = None) -> int:
        arguments = ["build", "--project", str(self.project), "--target", TARGET, "--config", config, "--platform", "web", *more]
        if out:
            arguments += ["--out", str(out)]
        return export.main(arguments, runner=self, system="Darwin", emsdk_dir=self.emsdk)


def lines_of(capsys) -> list[str]:
    return capsys.readouterr().out.splitlines()


@pytest.mark.parametrize("system", ["Darwin", "Windows", "Linux"])
def test_a_web_export_configures_builds_and_packages_on_every_host(tmp_path, capsys, system):
    game = FakeWebGame(tmp_path)

    code = export.main(["build", "--project", str(game.project), "--target", TARGET, "--config", "debug", "--platform", "web"],
                       runner=game, system=system, emsdk_dir=game.emsdk)

    out = game.project.resolve() / "dist" / f"{TARGET}-web-debug"
    output = lines_of(capsys)
    assert code == 0, output
    assert [line for line in output if line.startswith("== ")] == [
        "== step 1/3: configure", "== step 2/3: build", "== step 3/3: package", f"== export ok: {out}"]
    configure, build, package = game.calls
    assert configure == ["cmake", "--preset", "web-debug", "-B", str(game.build), "-DENGINE_WEB_COOKED_DIR="]
    assert build == ["cmake", "--build", str(game.build), "--target", TARGET]
    assert package[1].endswith("package.py")
    assert package[2:] == [str(game.build / "bin"), f"{out}.partial", "--game", TARGET]
    assert (out / "bagi-package.txt").read_text(encoding="utf-8").splitlines()[0] == "index.html"
    assert (out / "index.html").is_file()


@pytest.mark.parametrize("preset, single", [("web-release-single-thread", True), ("web-release-single-thread-stats", True),
                                            ("web-release-stats", False)])
def test_package_py_gets_the_kind_of_page_and_the_games_app_file(tmp_path, preset, single):
    game = FakeWebGame(tmp_path, preset=preset)
    app = write(game.project / "web" / "app.json", "{}")

    assert game.export("--preset", preset, config="release") == 0

    package = game.calls[-1]
    assert ("--single-thread" in package) == single
    assert package[package.index("--app") + 1] == str(app.resolve())


def test_scenes_left_out_reach_the_build_through_a_copy_of_the_config_folders(tmp_path, capsys):
    game = FakeWebGame(tmp_path)
    sources = [tree(root) for root in (game.engine_root, game.game_root)]
    write(game.build / "export-content" / "layer7" / "config" / "Old.xml")

    assert game.export("--exclude-scene", "Level") == 0

    content = game.build / "export-content"
    assert [command[-1] for command in game.calls[:2]] == [
        "-DENGINE_WEB_COOKED_DIR=", f"-DENGINE_WEB_COOKED_DIR={content.as_posix()}"]
    assert [command[1] for command in game.calls[:3]] == ["--preset", "--preset", "--build"]
    staged = game.content_at_configure[0]
    assert sorted(name for name, data in staged.items() if data is not None) == [
        "layer0/config/App.xml", "layer0/config/SceneConfig.xml", "layer1/config/SceneConfig.xml"]
    assert staged["layer0/config/SceneConfig.xml"] == tree(game.engine_root)["config/SceneConfig.xml"]
    assert b'name="Menu"' in staged["layer1/config/SceneConfig.xml"] and b"Level" not in staged["layer1/config/SceneConfig.xml"]
    unchanged = content / "layer0" / "config" / "App.xml"
    assert unchanged.stat().st_mtime_ns == (game.engine_root / "config" / "App.xml").stat().st_mtime_ns
    assert [tree(root) for root in (game.engine_root, game.game_root)] == sources


def test_a_root_without_config_takes_no_layer_number(tmp_path):
    game = FakeWebGame(tmp_path)
    (game.engine_root / "config" / "App.xml").unlink()
    (game.engine_root / "config" / "SceneConfig.xml").unlink()
    (game.engine_root / "config").rmdir()

    assert game.export("--exclude-scene", "Level") == 0

    assert sorted(game.content_at_configure[0]) == ["layer0", "layer0/config", "layer0/config/SceneConfig.xml"]


def test_one_folder_as_both_roots_gives_two_layers_and_the_upper_one_is_edited(tmp_path):
    game = FakeWebGame(tmp_path)
    game.engine_root = game.game_root

    assert game.export("--exclude-scene", "Level") == 0

    staged = game.content_at_configure[0]
    assert b"Level" in staged["layer0/config/SceneConfig.xml"]
    assert b"Level" not in staged["layer1/config/SceneConfig.xml"]


def test_a_missing_sdk_ends_the_export_before_anything_runs(tmp_path, capsys):
    game = FakeWebGame(tmp_path)
    (game.emsdk / ".emscripten").unlink()

    assert game.export() == 1

    assert lines_of(capsys)[-1] == f"== export failed: {build_and_deploy.SDK_MISSING_MESSAGE}"
    assert game.calls == []


def test_a_dry_run_needs_no_sdk_and_writes_nothing(tmp_path, capsys):
    game = FakeWebGame(tmp_path)
    game.emsdk = tmp_path / "nowhere"
    before = tree(tmp_path)

    assert game.export("--exclude-scene", "Level", "--dry-run") == 0

    commands = [line for line in lines_of(capsys) if line.startswith("$ ")]
    assert [command.split()[2] for command in commands] == ["--preset", "--preset", "--build"]
    assert game.calls == []
    assert tree(tmp_path) == before


@pytest.mark.parametrize("make_out, reason", [
    (lambda game: write(game.project.parent / "mine" / "photo.png").parent, "is not empty and is no package"),
    (lambda game: game.engine_root / "web" / "config" / "page", "inside the content folder"),
    (lambda game: game.build / "page", "inside the build folder"),
])
def test_a_refused_output_folder_stops_before_the_build(tmp_path, capsys, make_out, reason):
    game = FakeWebGame(tmp_path)

    assert game.export(out=make_out(game)) == 1

    assert reason in lines_of(capsys)[-1]
    assert [command[1] for command in game.calls] == ["--preset"]


def test_a_refused_scene_stops_before_the_build(tmp_path, capsys):
    game = FakeWebGame(tmp_path)

    assert game.export("--exclude-scene", "Menu") == 1

    assert lines_of(capsys)[-1].startswith("== export failed: the scene 'Menu' is the first of ")
    assert [command[1] for command in game.calls] == ["--preset"]


@pytest.mark.parametrize("failing, reason", [
    ("--build", "build failed with exit code 4"),
    ("package.py", "package.py failed with exit code 4"),
])
def test_a_failed_export_keeps_the_previous_package(tmp_path, capsys, failing, reason):
    game = FakeWebGame(tmp_path)
    assert game.export() == 0
    dist = game.project / "dist"
    before = tree(dist)
    game.codes = {failing: 4}
    capsys.readouterr()

    assert game.export() == 1

    assert lines_of(capsys)[-1].startswith(f"== export failed: {reason}")
    assert tree(dist) == before


def test_run_refuses_a_web_package_and_a_deploy_leaves_it_out(tmp_path, capsys):
    game = FakeWebGame(tmp_path)
    assert game.export() == 0
    package = game.project / "dist" / f"{TARGET}-web-debug"
    capsys.readouterr()

    assert export.main(["run", "--dir", str(package)]) == 1

    assert "is a web package: serve it with `export.py serve --dir`" in lines_of(capsys)[-1]
    build_and_deploy.stage(game.project / "dist", game.project / "build" / "web-deploy", game.project)
    assert list((game.project / "build" / "web-deploy" / "dist").iterdir()) == []
