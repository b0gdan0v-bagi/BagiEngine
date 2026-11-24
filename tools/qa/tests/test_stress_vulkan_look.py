"""The pure parts of tools/stress/qa/vulkan_look.py: the environment and the command, the PPM header, the split of validation lines.

The run itself needs a window and a display and is not tested here.
"""

import importlib.util
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[2] / "stress" / "qa" / "vulkan_look.py"


@pytest.fixture(scope="module")
def look():
    spec = importlib.util.spec_from_file_location("vulkan_look", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


KNOWN = "Validation Error: [ VUID-vkQueueSubmit-pSignalSemaphores-00067 ] | MessageID = 0x539277af"
KNOWN_SPEC = "The Vulkan spec states: Each binary semaphore ... (https://vulkan.lunarg.com/doc/view/1.4.363.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkQueueSubmit-pSignalSemaphores-00067)"


def test_the_environment_names_both_layers_and_the_screenshot_settings(look, tmp_path):
    environment = look.build_environment("/sdk/layers", "30", tmp_path)
    assert environment == {
        "VK_LAYER_PATH": "/sdk/layers",
        "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation:VK_LAYER_LUNARG_screenshot",
        "VK_SCREENSHOT_FRAMES": "30",
        "VK_SCREENSHOT_DIR": str(tmp_path),
    }


def test_the_command_is_a_window_with_the_control_api_on_the_manual_clock(look):
    command = look.build_command(Path("/game/BagiStress"), 4242)
    assert command[0] == "/game/BagiStress"
    assert "--headless" not in command
    assert "--control" in command and "--control-port=4242" in command and "--manual-clock" in command


def write_ppm(path, width, height, header=None):
    path.write_bytes((header or f"P6\n{width}\n{height}\n255\n".encode()) + bytes(width * height * 3))
    return path


def test_the_ppm_header_gives_the_size(look, tmp_path):
    assert look.read_ppm_size(write_ppm(tmp_path / "30.ppm", 7, 3)) == (7, 3)


@pytest.mark.parametrize("header", [b"P5\n7\n3\n255\n", b"P6\n7\n255\n", b"P6\nseven\n3\n255\n", b"P6\n0\n3\n255\n", b"P6\n7\n3\n65535\n", b""])
def test_a_bad_ppm_header_is_refused(look, tmp_path, header):
    path = tmp_path / "bad.ppm"
    path.write_bytes(header)
    with pytest.raises(look.PpmError):
        look.read_ppm_size(path)


def test_a_missing_ppm_is_refused(look, tmp_path):
    with pytest.raises(look.PpmError):
        look.read_ppm_size(tmp_path / "none.ppm")


def test_the_known_finding_is_split_from_a_new_one(look):
    new = "Validation Error: [ VUID-vkCmdDraw-None-02699 ] | MessageID = 0x1"
    lines = ["[Info] [12:00:00.000] started", KNOWN, KNOWN_SPEC, new, "[Info] done"]
    known, found = look.split_validation_lines(lines)
    assert known == [KNOWN, KNOWN_SPEC]
    assert found == [new]


def test_a_validation_warning_without_an_id_is_new(look):
    known, found = look.split_validation_lines(["Validation Warning: [ UNASSIGNED-x ] something"])
    assert known == [] and len(found) == 1


def test_a_log_without_validation_lines_has_none(look):
    assert look.split_validation_lines(["[Info] a", "[Warning] b"]) == ([], [])


def test_the_pictures_are_listed_in_order_and_only_ppm(look, tmp_path):
    write_ppm(tmp_path / "30.ppm", 1, 1)
    write_ppm(tmp_path / "10.ppm", 1, 1)
    (tmp_path / "note.txt").write_text("x")
    assert [path.name for path in look.written_pictures(tmp_path)] == ["10.ppm", "30.ppm"]
    assert look.written_pictures(tmp_path / "none") == []


def test_a_missing_executable_or_layer_is_named(look, tmp_path):
    exe = tmp_path / "Game"
    assert "does not exist" in look.missing_requirement(exe, str(tmp_path))
    exe.write_text("")
    assert "layer folder" in look.missing_requirement(exe, str(tmp_path / "no_layers"))
    assert "screenshot layer" in look.missing_requirement(exe, str(tmp_path))
    (tmp_path / look.SCREENSHOT_MANIFEST).write_text("{}")
    assert look.missing_requirement(exe, str(tmp_path)) is None
