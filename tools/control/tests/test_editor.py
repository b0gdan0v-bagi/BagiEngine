"""launch(editor=True): the flag `--editor` reaches the game and the header of a recording, so a scenario replays in the editor."""

import scenario
from bagi_play import EXIT_PASSED, main
from bagi_session import GameSession


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
