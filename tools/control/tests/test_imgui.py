"""The ImGui helpers of GameSession (imgui_find, imgui_click, imgui_type, imgui_key, imgui_wheel) against the fake editor of fake_game.py."""

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_session import GameSession, SessionError


@pytest.fixture
def editor(fake_game_exe):
    game = GameSession()
    game.launch(str(fake_game_exe), editor=True)
    yield game
    game.close()


def events_taken(game: GameSession) -> list[dict]:
    return game.call("get_state")["state"]["imguiEvents"]


def test_find_passes_its_filters_through(editor):
    found = editor.imgui_find("Close", window="Other", kind="item", visible_only=True, limit=5)
    assert [(match["window"], match["x"]) for match in found["matches"]] == [("Other", 800.0)]

    assert [match["label"] for match in editor.imgui_find(label_contains="lay")["matches"]] == ["Play##play"]
    assert editor.imgui_find("Play")["matches"][0]["label"] == "Play##play"  # the part before ##
    assert [match["label"] for match in editor.imgui_find("Hidden", visible_only=True)["matches"]] == []
    limited = editor.imgui_find(kind="item", limit=2)
    assert len(limited["matches"]) == 2 and limited["truncated"]


def test_a_name_that_is_not_there_is_an_empty_list(editor):
    assert editor.imgui_find("Nothing")["matches"] == []


def test_click_by_label_clicks_the_centre_of_the_one_match(editor):
    editor.imgui_click("Play")
    assert events_taken(editor) == [{"type": "click", "x": 150.0, "y": 90.0}]


def test_click_by_label_takes_the_window_to_tell_same_names_apart(editor):
    editor.imgui_click("Close", window="Other", button="right", count=2, mods=["ctrl"])
    assert events_taken(editor) == [{"type": "click", "x": 820.0, "y": 70.0, "button": "right", "count": 2, "mods": ["ctrl"]}]


def test_click_refuses_no_match_and_lists_several(editor):
    with pytest.raises(SessionError, match="no visible ImGui item 'Nothing'"):
        editor.imgui_click("Nothing")
    with pytest.raises(SessionError, match="no visible ImGui item 'Hidden'"):
        editor.imgui_click("Hidden")
    with pytest.raises(SessionError) as several:
        editor.imgui_click("Close")
    assert "2 visible" in str(several.value) and "'Game Panel'" in str(several.value) and "'Other'" in str(several.value)
    assert events_taken(editor) == []


def test_click_at_a_point_and_its_misuse(editor):
    editor.imgui_click(x=10, y=20)
    assert events_taken(editor) == [{"type": "click", "x": 10, "y": 20}]
    for bad in ({}, {"x": 1}, {"label": "Play", "x": 1, "y": 2}):
        with pytest.raises(SessionError):
            editor.imgui_click(**bad)


def test_type_key_and_wheel_events(editor):
    editor.imgui_type("héllo")
    editor.imgui_key("Enter")
    editor.imgui_key("P", ["ctrl", "shift"])
    editor.imgui_wheel(5, 6, -2)
    assert events_taken(editor) == [
        {"type": "text", "text": "héllo"},
        {"type": "key", "key": "Enter"},
        {"type": "key", "key": "P", "mods": ["ctrl", "shift"]},
        {"type": "move", "x": 5, "y": 6},
        {"type": "wheel", "dy": -2},
    ]


def test_the_answer_of_an_input_is_the_games(editor):
    answer = editor.imgui_input([{"type": "move", "x": 1, "y": 2}], space="imgui")
    assert answer["wantCaptureMouse"] is True and answer["events"] == 1


def test_without_the_editor_the_game_says_no_imgui(fake_game_exe):
    game = GameSession()
    try:
        game.launch(str(fake_game_exe))
        with pytest.raises(ControlError) as error:
            game.imgui_find()
        assert error.value.code == "no_imgui"
    finally:
        game.close()


def test_the_calls_wait_for_the_frames_the_game_may_run():
    assert GameSession._wait_timeout("imgui_input", {"events": []}) > GameSession._wait_timeout("step", {"frames": 1})
    assert GameSession._frames_waited("batch", {"calls": [{"method": "imgui_find"}, {"method": "step", "params": {"frames": 2}}]}) > 600


def test_a_recording_keeps_the_imgui_calls(fake_game_exe, tmp_path):
    path = tmp_path / "imgui.jsonl"
    game = GameSession()
    try:
        game.launch(str(fake_game_exe), editor=True, record=str(path))
        game.imgui_click("Play")
        game.imgui_type("a")
        game.quit()
    finally:
        game.close()
    header, steps = scenario.load(path)
    assert "--editor" in header.flags
    assert [step.method for step in steps] == ["imgui_find", "imgui_input", "imgui_input"]
    assert steps[1].params == {"events": [{"type": "click", "x": 150.0, "y": 90.0}]}
