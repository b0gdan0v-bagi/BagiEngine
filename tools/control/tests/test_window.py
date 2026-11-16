"""set_window and the start flags --window / --density through the session against the fake game (tests/fake_game.py), and in a scenario file."""

import json
from pathlib import Path

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_play import EXIT_FAILED, EXIT_PASSED, main
from bagi_session import GameSession

HEADER = {"scenario": 1, "game": "Fake", "flags": ["--headless", "--manual-clock"]}
NO_INSETS = {"top": 0.0, "right": 0.0, "bottom": 0.0, "left": 0.0}


@pytest.fixture
def session(fake_game_exe):
    game = GameSession()
    game.launch(str(fake_game_exe))
    yield game
    game.close()


def window_of(session: GameSession) -> dict:
    return session.call("status")["window"]


def test_set_window_answers_the_window_object_that_status_shows(session):
    answer = session.set_window(480, 480)
    assert answer["width"] == 480 and answer["height"] == 480 and answer["density"] == 1.0
    assert (answer["outputWidth"], answer["outputHeight"]) == (480, 480)
    assert answer["headless"] is True and answer["safeArea"] == NO_INSETS
    assert window_of(session) == answer


def test_a_value_that_is_left_out_keeps_what_it_was(session):
    session.set_window(300, 200, density=2, user_scale=1.5, safe_area=(40, 0, 0, 0))
    answer = session.set_window(320, 240)
    assert (answer["width"], answer["height"], answer["density"], answer["userScale"]) == (320, 240, 2.0, 1.5)
    assert answer["safeArea"]["top"] == 40.0
    assert (answer["outputWidth"], answer["outputHeight"]) == (640, 480)


def test_a_safe_area_of_zeros_clears_it(session):
    session.set_window(480, 480, safe_area={"top": 40, "right": 0, "bottom": 0, "left": 0})
    assert session.set_window(480, 480, safe_area=(0, 0, 0, 0))["safeArea"] == NO_INSETS


@pytest.mark.parametrize(
    "arguments",
    [
        {"width": 0, "height": 480},
        {"width": 480, "height": 4097},
        {"width": 480, "height": 480, "density": 0.4},
        {"width": 4096, "height": 4096, "density": 4},
        {"width": 480, "height": 480, "user_scale": 3.5},
        {"width": 480, "height": 480, "safe_area": (-1, 0, 0, 0)},
        {"width": 480, "height": 480, "safe_area": (0, 240, 0, 240)},
    ],
)
def test_a_value_outside_the_limits_is_invalid_params_and_changes_nothing(session, arguments):
    before = window_of(session)
    with pytest.raises(ControlError) as refused:
        session.set_window(**arguments)
    assert refused.value.code == "invalid_params"
    assert window_of(session) == before


def test_a_real_window_is_not_headless(fake_game_exe):
    game = GameSession()
    try:
        game.launch(str(fake_game_exe), headless=False)
        with pytest.raises(ControlError) as refused:
            game.set_window(480, 480)
        assert refused.value.code == "not_headless"
        assert game.call("status")["window"]["headless"] is False
    finally:
        game.close()


def test_a_recorded_set_window_replays_unchanged(fake_game_exe, tmp_path, capsys):
    path = tmp_path / "window.jsonl"
    recorder = GameSession()
    try:
        recorder.launch(str(fake_game_exe), window="480x480", record=str(path))
        recorder.set_window(300, 200, density=2, safe_area=(40, 0, 0, 0))
        with pytest.raises(ControlError):
            recorder.set_window(0, 200)
        recorder.quit()
    finally:
        recorder.close()

    header, steps = scenario.load(path)
    assert header.flags == ["--headless", "--manual-clock", "--window=480x480"]
    assert [(step.method, step.expect.get("error")) for step in steps] == [("set_window", None), ("set_window", "invalid_params")]
    assert steps[0].params == {"width": 300, "height": 200, "density": 2, "safeArea": {"top": 40, "right": 0, "bottom": 0, "left": 0}}

    assert main([str(fake_game_exe), str(path)]) == EXIT_PASSED
    assert "PASSED" in capsys.readouterr().out


def test_the_player_gives_the_window_flags_of_a_header_to_the_game(fake_game_exe, tmp_path):
    lines = [
        {**HEADER, "flags": ["--headless", "--manual-clock", "--window=480x480", "--density=2"]},
        {"method": "status", "expect": {"pointers": {"/window/outputWidth": 960, "/window/width": 480}}},
    ]
    path = Path(tmp_path) / "flags.jsonl"
    path.write_text("\n".join(json.dumps(line) for line in lines) + "\n", encoding="utf-8")
    assert main([str(fake_game_exe), str(path)]) == EXIT_PASSED
    lines[1]["expect"]["pointers"]["/window/width"] = 481
    path.write_text("\n".join(json.dumps(line) for line in lines) + "\n", encoding="utf-8")
    assert main([str(fake_game_exe), str(path)]) == EXIT_FAILED
