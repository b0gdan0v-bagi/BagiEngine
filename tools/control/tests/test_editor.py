"""launch(editor=True): the flag `--editor` reaches the game and the header of a recording, so a scenario replays in the editor.

editor_state and launch(content_overlay=...): the answer of the method, the refusal outside the editor and the flag that points the editor's writes into a copy.
"""

import json
from pathlib import Path

import pytest

import scenario
from bagi_play import EXIT_PASSED, main
from bagi_session import GameSession, SessionError

TESTS_DIR = Path(__file__).resolve().parent


def test_editor_reaches_the_game_and_its_run_mode(fake_game_exe):
    game = GameSession()
    try:
        game.launch(str(fake_game_exe), editor=True)
        assert "--editor" in game.call("get_state")["state"]["flags"]
        assert game.call("status")["runMode"] == "Editor"
    finally:
        game.close()


def test_a_launch_without_editor_does_not_ask_for_it(fake_game_exe):
    game = GameSession()
    try:
        game.launch(str(fake_game_exe))
        assert "--editor" not in game.call("get_state")["state"]["flags"]
    finally:
        game.close()


def test_a_recording_keeps_editor_in_its_flags_and_replays_in_the_editor(fake_game_exe, tmp_path, capsys):
    path = tmp_path / "editor.jsonl"
    recorder = GameSession()
    try:
        recorder.launch(str(fake_game_exe), editor=True, record=str(path))
        recorder.call("status")
        recorder.quit()
    finally:
        recorder.close()

    header, _ = scenario.load(path)
    assert "--editor" in header.flags

    assert main([str(fake_game_exe), str(path)]) == EXIT_PASSED
    assert "PASSED" in capsys.readouterr().out


def test_editor_state_answers_the_documents_and_turns_dirty_after_an_edit(fake_game_exe):
    game = GameSession()
    try:
        game.launch(str(fake_game_exe), editor=True)
        clean = game.editor_state()
        assert clean["anyDirty"] is False
        assert [(document["id"], document["kind"], document["dirty"]) for document in clean["documents"]] == [("scene:Fake", "scene", False)]
        game.call("perform_action", {"type": "edit_scene"})
        assert game.editor_state()["anyDirty"] is True
    finally:
        game.close()


def test_editor_state_outside_the_editor_says_to_launch_with_editor(fake_game_exe):
    game = GameSession()
    try:
        game.launch(str(fake_game_exe))
        with pytest.raises(SessionError, match="editor=True"):
            game.editor_state()
    finally:
        game.close()


def test_content_overlay_reaches_the_game_and_not_the_header_of_a_recording(fake_game_exe, tmp_path):
    overlay = tmp_path / "overlay"
    target = tmp_path / "editor.jsonl"
    game = GameSession()
    try:
        game.launch(str(fake_game_exe), editor=True, content_overlay=str(overlay), record=str(target))
        assert game.editor_state()["documents"][0]["file"] == f"{overlay}/config/SceneConfig.xml"
        game.quit()
    finally:
        game.close()
    assert json.loads(target.read_text().splitlines()[0])["flags"] == ["--headless", "--manual-clock", "--editor"]


def test_content_overlay_inside_a_git_work_tree_is_refused_before_the_game_starts(fake_game_exe):
    game = GameSession()
    with pytest.raises(SessionError, match="git work tree"):
        game.launch(str(fake_game_exe), editor=True, content_overlay=str(TESTS_DIR / "overlay-that-must-not-be-made"))
    assert not game.is_connected
    assert not (TESTS_DIR / "overlay-that-must-not-be-made").exists()


def test_launch_game_tool_takes_the_overlay_folder():
    pytest.importorskip("mcp")  # the server module needs the package
    import bagi_mcp

    launch = next(tool for tool in bagi_mcp.server._tool_manager.list_tools() if tool.name == "launch_game")  # no public list outside the async protocol
    assert "content_overlay" in launch.parameters["properties"]
