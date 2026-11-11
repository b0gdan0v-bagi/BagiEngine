"""Recording a session as a scenario file, the pushes the session keeps, and the problems of the game's log (bagi_session.py, game_process.py)."""

import base64

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_session import EVENT_BUFFER_SIZE, GameSession, SessionError
from fake_control_server import FakeControlServer, FakeError, QuitServer
from game_process import log_problems


class Game:
    """A small fake game: a state version that grows by `stride` with every action, and pushes when asked."""

    def __init__(self, version: int = 3, stride: int = 1) -> None:
        self.version = version
        self.stride = stride
        self.server = FakeControlServer(self.handle)
        self.port = self.server.start()

    def handle(self, method: str, params: dict | None):
        if method == "status":
            return {"frame": 7, "manualClock": True}
        if method == "list_games":
            return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
        if method == "get_state":
            return {"game": "Fake", "version": self.version, "state": {"board": "x"}}
        if method == "perform_action":
            if (params or {}).get("type") == "bad":
                raise FakeError("illegal_action", "not allowed")
            self.version += self.stride
            return {"game": "Fake", "version": self.version, "outcome": params}
        if method == "step":
            return {"frame": 8}
        if method == "screenshot":
            return {"width": 2, "height": 1, "png_base64": base64.b64encode(b"\x89PNG-fake").decode("ascii")}
        if method == "push_before":
            self.server.push("state_changed", {"version": 100})
            return {}
        if method == "push_after":
            self.server.push("state_changed", {"version": 101}, after_reply=True)
            return {}
        if method == "flood":
            for number in range(EVENT_BUFFER_SIZE + 10):
                self.server.push("state_changed", {"version": number})
            return {}
        if method == "quit":
            raise QuitServer({"quitting": True})
        raise FakeError("unknown_method", method)


@pytest.fixture
def game():
    fake = Game()
    yield fake
    fake.server.stop()


@pytest.fixture
def session():
    current = GameSession()
    yield current
    current.close()


def state_changed(version: int):
    return lambda event: event.get("event") == "state_changed" and event["data"]["version"] == version


def load(path):
    return scenario.load(path)


# --- recording ---


def test_a_recorded_session_has_a_header_and_a_line_per_call(session, game, tmp_path):
    path = tmp_path / "play.jsonl"
    session.connect(port=game.port)
    answer = session.start_recording(str(path))
    assert answer["game"] == "Fake" and answer["flags"] == ["--manual-clock"] and answer["path"] == str(path.resolve())
    assert "note" in answer  # a game that was only connected to may be past its start
    assert session.is_recording

    session.call("get_state")
    session.call("perform_action", {"type": "move", "uci": "e2e4"})
    with pytest.raises(ControlError):
        session.call("perform_action", {"type": "bad"})
    assert session.stop_recording() == {"path": str(path.resolve()), "calls": 3}
    assert not session.is_recording

    header, steps = load(path)
    assert header == scenario.Header(1, "Fake", ["--manual-clock"], None)
    assert [(step.method, step.params, step.expect) for step in steps] == [
        ("get_state", None, {}),  # the first version of the run: nothing to be relative to
        ("perform_action", {"type": "move", "uci": "e2e4"}, {"versionStep": 1}),
        ("perform_action", {"type": "bad"}, {"error": "illegal_action"}),
    ]


def test_the_internal_calls_of_the_session_are_not_recorded(session, game, tmp_path):
    path = tmp_path / "play.jsonl"
    session.connect(port=game.port, record=str(path))
    session.stop_recording()
    assert load(path)[1] == []
    assert [name for name, _ in game.server.requests] == ["list_games", "status", "list_games", "status"]  # asked, never written


def test_version_steps_are_relative_to_the_last_version_seen(session, tmp_path):
    fake = Game(version=1000, stride=5)
    try:
        path = tmp_path / "play.jsonl"
        session.connect(port=fake.port, record=str(path))
        session.call("get_state")
        session.call("perform_action", {"type": "a"})
        session.call("step", {"frames": 1})  # an answer without a version changes nothing
        session.call("perform_action", {"type": "b"})
        session.call("get_state")
        session.stop_recording()
        assert [step.expect for step in load(path)[1]] == [{}, {"versionStep": 5}, {}, {"versionStep": 5}, {"versionStep": 0}]
    finally:
        fake.server.stop()


def test_full_mode_records_the_answers_and_reduces_a_screenshot(session, game, tmp_path):
    path = tmp_path / "play.jsonl"
    session.connect(port=game.port, record=str(path), record_expect="full")
    session.call("get_state")
    session.call("perform_action", {"type": "move"})
    session.screenshot()
    session.stop_recording()
    text = path.read_text(encoding="utf-8")
    assert "PNG" not in text and "png_base64" not in text
    expects = [step.expect for step in load(path)[1]]
    assert expects[0] == {"result": {"game": "Fake", "version": 3, "state": {"board": "x"}}}
    assert expects[1]["versionStep"] == 1 and expects[1]["result"]["outcome"] == {"type": "move"}
    assert expects[2] == {"equal": {"width": 2, "height": 1}}


def test_a_trailing_quit_is_dropped_and_the_file_is_written(session, game, tmp_path):
    path = tmp_path / "play.jsonl"
    session.connect(port=game.port, record=str(path))
    session.call("get_state")
    result = session.quit()
    assert result["recording"] == {"path": str(path.resolve()), "calls": 1}
    assert [step.method for step in load(path)[1]] == ["get_state"]
    assert not session.is_recording


def test_close_writes_the_recording_too(session, game, tmp_path):
    path = tmp_path / "play.jsonl"
    session.connect(port=game.port, record=str(path))
    session.call("perform_action", {"type": "move"})
    session.close()
    assert [step.method for step in load(path)[1]] == ["perform_action"]


def test_a_caller_changing_its_params_afterwards_does_not_change_the_recording(session, game, tmp_path):
    path = tmp_path / "play.jsonl"
    session.connect(port=game.port, record=str(path))
    params = {"type": "move", "to": {"col": 1}}
    session.call("perform_action", params)
    params["to"]["col"] = 9
    session.stop_recording()
    assert load(path)[1][0].params == {"type": "move", "to": {"col": 1}}


def test_a_session_error_is_not_recorded(session, tmp_path):
    def handler(method: str, params: dict | None):
        if method == "list_games":
            return {"active": "Fake", "games": []}
        if method == "status":
            return {"manualClock": False}
        raise QuitServer({"bye": True})

    fake = FakeControlServer(handler)
    port = fake.start()
    try:
        path = tmp_path / "play.jsonl"
        session.connect(port=port, record=str(path))
        session.call("anything")  # answered, then the server drops the connection
        with pytest.raises(SessionError, match="closed the connection"):
            session.call("anything")
        assert session.stop_recording()["calls"] == 1
        assert load(path)[0].flags == []  # a game on the real clock
    finally:
        fake.stop()


def test_step_and_screenshot_are_recorded_and_move_the_manual_clock(session, fake_game_exe, tmp_path):
    path = tmp_path / "play.jsonl"
    description = session.launch(str(fake_game_exe), record=str(path))
    assert "note" not in description["recording"]  # nothing was called before
    session.step(2, 0.5)
    session.screenshot(64)
    assert session.call("status")["frame"] == 3  # the screenshot took a frame, as in the real game: a replay does the same
    session.stop_recording()
    steps = load(path)[1]
    assert [(step.method, step.params) for step in steps] == [("step", {"frames": 2, "dt": 0.5}), ("screenshot", {"maxSize": 64}), ("status", None)]


def test_a_launched_game_records_its_flags_and_the_extra_ones(session, fake_game_exe, tmp_path):
    path = tmp_path / "play.jsonl"
    session.launch(str(fake_game_exe), extra=["--seed=7"], record=str(path))
    assert session.call("get_state")["state"]["flags"] == ["--control", "--headless", "--manual-clock"]  # the fake echoes the flags without "="
    session.quit()
    header = load(path)[0]
    assert header.flags == ["--headless", "--manual-clock", "--seed=7"] and header.game == "Fake"

    session.launch(str(fake_game_exe), headless=False, manual_clock=False, record=str(path))
    session.close()
    assert load(path)[0].flags == []


def test_a_recording_that_starts_late_says_so(session, fake_game_exe, tmp_path):
    session.launch(str(fake_game_exe))
    session.call("get_state")
    session.call("get_state")
    answer = session.start_recording(str(tmp_path / "play.jsonl"))
    assert "2 call(s)" in answer["note"] and "fresh start" in answer["note"]


def test_recording_can_be_misused_only_with_a_clear_message(session, game, tmp_path):
    with pytest.raises(SessionError, match="launch_game or connect"):
        session.start_recording(str(tmp_path / "play.jsonl"))
    session.connect(port=game.port)
    with pytest.raises(SessionError, match="not recording"):
        session.stop_recording()
    with pytest.raises(SessionError, match="minimal, full"):
        session.start_recording(str(tmp_path / "play.jsonl"), expect="everything")
    with pytest.raises(SessionError, match="does not exist"):
        session.start_recording(str(tmp_path / "missing" / "play.jsonl"))
    session.start_recording(str(tmp_path / "play.jsonl"))
    with pytest.raises(SessionError, match="already recording"):
        session.start_recording(str(tmp_path / "other.jsonl"))
    assert session.is_recording  # the first recording goes on


def test_a_bad_record_target_stops_a_launch_before_the_game_starts(session, fake_game_exe, tmp_path):
    with pytest.raises(SessionError, match="does not exist"):
        session.launch(str(fake_game_exe), record=str(tmp_path / "missing" / "play.jsonl"))
    assert not session.is_connected


def test_nothing_is_recorded_when_not_asked(session, game, tmp_path):
    session.connect(port=game.port)
    session.call("get_state")
    assert not session.is_recording
    session.quit()
    assert list(tmp_path.iterdir()) == []


def test_connecting_again_writes_the_recording_of_the_old_game_and_starts_a_new_one(session, game, tmp_path):
    first, second = tmp_path / "first.jsonl", tmp_path / "second.jsonl"
    session.connect(port=game.port, record=str(first))
    session.call("get_state")
    session.connect(port=game.port, record=str(second))  # connecting again lets go of the old game and writes its file
    assert [step.method for step in load(first)[1]] == ["get_state"]
    assert session.is_recording
    session.call("get_state")
    session.call("get_state")
    session.stop_recording()
    assert len(load(second)[1]) == 2


# --- pushed events ---


def test_pushes_before_and_after_the_answer_are_kept(session, game):
    session.connect(port=game.port)
    session.call("push_before")
    event = session.take_event(state_changed(100))
    assert event == {"event": "state_changed", "data": {"version": 100}}
    assert session.take_event(state_changed(100)) is None  # taken once

    session.call("push_after")  # it arrives behind the answer, so the call has returned without it
    assert session.take_event(state_changed(101), timeout=3) is not None


def test_take_event_waits_and_then_gives_up(session, game):
    session.connect(port=game.port)
    assert session.take_event(state_changed(5), timeout=0.2) is None
    game.server.push("state_changed", {"version": 5})
    assert session.take_event(state_changed(5), timeout=3) is not None
    assert session.call("get_state")["version"] == 3  # the connection works after a wait that timed out


def test_events_that_do_not_match_are_kept_for_later(session, game):
    session.connect(port=game.port)
    game.server.push("state_changed", {"version": 1})
    game.server.push("state_changed", {"version": 2})
    assert session.take_event(state_changed(2), timeout=3) is not None
    assert session.take_event(state_changed(1)) is not None


def test_the_kept_events_are_bounded_and_the_oldest_go_first(session, game):
    session.connect(port=game.port)
    session.call("flood")
    assert session.take_event(state_changed(0)) is None
    assert session.take_event(state_changed(9)) is None  # 10 dropped
    assert session.take_event(state_changed(10)) is not None
    assert session.take_event(state_changed(EVENT_BUFFER_SIZE + 9)) is not None


def test_take_event_without_a_game_is_refused(session):
    with pytest.raises(SessionError, match="launch_game or connect"):
        session.take_event(lambda event: True)


def test_take_event_reports_a_game_that_is_gone(session):
    fake = FakeControlServer(lambda method, params: {"active": "Fake", "games": []} if method == "list_games" else {})
    port = fake.start()
    session.connect(port=port)
    fake.stop()
    with pytest.raises(SessionError, match="closed the connection"):
        session.take_event(lambda event: True, timeout=2)
    assert not session.is_connected


# --- the log of the game ---


def test_log_problems_of_a_launched_game(session, fake_game_exe):
    session.launch(str(fake_game_exe))
    assert session.log_problems() == []
    session.call("warn", {"text": "first"})
    session.call("warn", {"level": "Error", "text": "second"})
    session.call("warn", {"level": "Info", "text": "fine"})
    assert session.log_problems() == ["[Warning] [12:00:00.000] first", "[Error] [12:00:00.000] second"]


def test_log_problems_of_a_game_only_connected_to_is_refused(session, game):
    session.connect(port=game.port)
    with pytest.raises(SessionError, match="did not start the game"):
        session.log_problems()


def test_log_problems_reads_levels_and_ansi_colours_and_ignores_the_rest(tmp_path):
    log = tmp_path / "game.log"
    log.write_text(
        "[Info] [12:00:00.000] a Warning in the middle of a line\n"
        "\x1b[33m[Warning]\x1b[0m [12:00:00.001] coloured\n"
        "\x1b[35m[Fatal]\x1b[0m [12:00:00.002] fatal\n"
        "[Error] [12:00:00.003] plain\n"
        "[Debug] [12:00:00.004] [Error] inside\n"
        "  [Warning] indented\n",
        encoding="utf-8",
    )
    assert log_problems(log) == ["[Warning] [12:00:00.001] coloured", "[Fatal] [12:00:00.002] fatal", "[Error] [12:00:00.003] plain"]
    assert log_problems(tmp_path / "missing.log") == []
