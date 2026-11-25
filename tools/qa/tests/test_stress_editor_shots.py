"""The engine's screenshot set (tools/stress/qa/editor_shots.py and the block of qa-project.md under "Screenshots"), with no editor: the set and the command stay
in step, the pure functions, the command line's exit codes. The real run is a person's or the loop's (`shots.py capture` on a build with the editor)."""

import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest

ENGINE = Path(__file__).resolve().parents[3]
COMMAND = ENGINE / "tools" / "stress" / "qa" / "editor_shots.py"
SHOTS_TOOL = ENGINE / "tools" / "shots"

sys.path.insert(0, str(SHOTS_TOOL))

import shots  # noqa: E402
from png_reader import PngImage, read_png  # noqa: E402


@pytest.fixture(scope="module")
def command():
    spec = importlib.util.spec_from_file_location("editor_shots_under_test", COMMAND)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def gradient(width: int, height: int) -> PngImage:
    return PngImage(width, height, 3, [bytes((x * 7 + y) % 256 for x in range(width) for _ in range(3)) for y in range(height)])


def test_the_engines_set_has_the_scenes_of_the_command(command):
    engine_set = shots.load_set(ENGINE)
    assert list(engine_set["scenes"]) == list(command.SCENES)
    assert engine_set["sizes"] == [command.LAYOUT_WINDOW]
    assert engine_set["defaultSize"] == command.LAYOUT_WINDOW
    assert engine_set["targets"] == [engine_set["exe"]]
    assert Path(engine_set["command"][1].replace("{root}", str(ENGINE))) == COMMAND


def test_the_scenes_command_lists_the_scenes_of_the_set(command):
    done = subprocess.run([sys.executable, str(SHOTS_TOOL / "shots.py"), "scenes", "--root", str(ENGINE)], capture_output=True, text=True)
    assert done.returncode == 0, done.stdout + done.stderr
    for name in command.SCENES:
        assert f"{name}:" in done.stdout


def test_parse_scenes_keeps_the_order_asked_and_drops_repeats(command):
    assert command.parse_scenes("editor-viewport, editor-frame,editor-viewport,") == ["editor-viewport", "editor-frame"]


@pytest.mark.parametrize("text", ["", " , ", "editor-frame,nothing"])
def test_parse_scenes_refuses_an_empty_or_unknown_list(command, text):
    with pytest.raises(ValueError):
        command.parse_scenes(text)


def test_an_unknown_scene_names_the_known_ones(command):
    with pytest.raises(ValueError, match="editor-hierarchy"):
        command.parse_scenes("nothing")


def test_order_scenes_follows_the_table_whatever_the_order_asked(command):
    asked = [command.SCENE_VIEWPORT, command.SCENE_INSPECTOR, command.SCENE_FRAME]
    assert command.order_scenes(asked) == [command.SCENE_FRAME, command.SCENE_INSPECTOR, command.SCENE_VIEWPORT]
    assert command.order_scenes(list(command.SCENES))[-1] == command.SCENE_VIEWPORT  # its reset changes what the others show, so it is last


def test_panel_box_is_the_inclusive_box_of_the_rounded_rect(command):
    assert command.panel_box({"x": 10.4, "y": 20.0, "width": 100.0, "height": 50.0}, 1920, 1080) == [10, 20, 109, 69]


def test_panel_box_is_clamped_to_the_picture(command):
    box = command.panel_box({"x": -5.0, "y": 900.0, "width": 3000.0, "height": 500.0}, 1920, 1080)
    assert box == [0, 900, 1919, 1079]


@pytest.mark.parametrize(
    "rect",
    [
        {"x": 2000.0, "y": 0.0, "width": 10.0, "height": 10.0},
        {"x": 0.0, "y": -50.0, "width": 10.0, "height": 50.0},
        {"x": 5.0, "y": 5.0, "width": 0.0, "height": 10.0},
    ],
)
def test_panel_box_refuses_a_rect_with_no_area_in_the_picture(command, rect):
    with pytest.raises(ValueError):
        command.panel_box(rect, 1920, 1080)


def test_save_picture_shrinks_to_the_long_side_and_writes_a_png(command, tmp_path):
    path = tmp_path / "label" / "scene.png"
    command.save_picture(gradient(400, 200), path, 100)
    written = read_png(path.read_bytes())
    assert (written.width, written.height) == (100, 50)


def test_save_picture_never_enlarges(command, tmp_path):
    image = gradient(30, 20)
    command.save_picture(image, tmp_path / "scene.png", 640)
    written = read_png((tmp_path / "scene.png").read_bytes())
    assert (written.width, written.height, written.rows) == (30, 20, image.rows)


def test_an_out_folder_inside_a_git_work_tree_is_refused_and_nothing_is_written(command, qa_game_exe):
    out = ENGINE / "editor-shots-out-never-written"
    assert command.main(["--exe", str(qa_game_exe), "--label", "a", "--out", str(out), "--scenes", "editor-frame"]) == command.EXIT_USAGE
    assert not out.exists()


def test_an_unknown_scene_or_another_window_is_a_usage_error(command, qa_game_exe, tmp_path):
    out = tmp_path / "out"
    base = ["--exe", str(qa_game_exe), "--label", "a", "--out", str(out)]
    assert command.main([*base, "--scenes", "nothing"]) == command.EXIT_USAGE
    assert command.main([*base, "--scenes", "editor-frame", "--window", "1280x720"]) == command.EXIT_USAGE
    assert not out.exists()


def test_a_missing_executable_is_skipped_and_nothing_is_written(command, tmp_path):
    out = tmp_path / "out"
    assert command.main(["--exe", str(tmp_path / "nothing"), "--label", "a", "--out", str(out), "--scenes", "editor-frame"]) == command.EXIT_SKIPPED
    assert not out.exists()
