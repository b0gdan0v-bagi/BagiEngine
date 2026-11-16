"""The QA conveniences of the control API in GameSession (log counts and marks, batch, run_until) against the fake game, which serves them like the engine."""

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_session import (
    CALL_TIMEOUT_SECONDS,
    DEFAULT_RUN_UNTIL_FRAMES,
    STEP_SECONDS_PER_FRAME,
    GameSession,
    event_occurs,
    pointer_equals,
    version_moved,
)

PUSH_WAIT_SECONDS = 5.0


@pytest.fixture
def session(fake_game_exe):
    current = GameSession()
    current.launch(str(fake_game_exe))
    yield current
    current.close()


def warn(session: GameSession, text: str, level: str = "Warning") -> None:
    session.call("warn", {"level": level, "text": text})


def is_log_push(text: str):
    return lambda event: event.get("event") == "log" and event["data"]["message"] == text


# --- the log ---


def test_the_log_counts_follow_a_warning_and_an_error(session):
    assert session.log_counts() == {"warnings": 0, "errors": 0}
    warn(session, "odd")
    warn(session, "broken", "Error")
    assert session.log_counts() == {"warnings": 1, "errors": 1}


def test_log_since_gives_what_came_after_the_mark(session):
    warn(session, "before")
    mark = session.log_mark()
    assert mark["mark"] == 2 and mark["warnings"] == 1 and mark["errors"] == 0
    warn(session, "after")
    warn(session, "worse", "Error")

    since = session.log_since(mark["mark"])
    assert [line["message"] for line in since["lines"]] == ["after", "worse"]
    assert [line["level"] for line in since["lines"]] == ["warning", "error"]
    assert (since["warnings"], since["errors"], since["dropped"]) == (1, 1, 0)
    assert since["next"] == 4 and session.log_since(since["next"])["lines"] == []


def test_log_since_error_level_keeps_the_counts_and_cuts_the_lines(session):
    warn(session, "odd")
    warn(session, "broken", "Error")
    since = session.log_since(0, "error")
    assert [line["message"] for line in since["lines"]] == ["broken"]
    assert (since["warnings"], since["errors"]) == (1, 1)


@pytest.mark.parametrize("mark, level", [(-1, "warning"), ("1", "warning"), (1.5, "warning"), (0, "info")])
def test_a_bad_mark_or_level_reaches_the_caller_as_a_control_error(session, mark, level):
    with pytest.raises(ControlError) as refusal:
        session.log_since(mark, level)
    assert refusal.value.code == "invalid_params"


def test_a_subscribed_session_sees_the_log_push(session):
    warn(session, "unheard")  # off by default
    assert session.log_subscribe(True) == {"enabled": True, "level": "warning"}
    warn(session, "heard")
    event = session.take_event(is_log_push("heard"), PUSH_WAIT_SECONDS)
    assert event is not None and event["data"]["level"] == "warning" and event["data"]["sequence"] == 2
    assert session.take_event(is_log_push("unheard")) is None

    assert session.log_subscribe(True, "error")["level"] == "error"
    warn(session, "quiet")
    warn(session, "loud", "Error")
    assert session.take_event(is_log_push("loud"), PUSH_WAIT_SECONDS) is not None
    assert session.take_event(is_log_push("quiet")) is None

    session.log_subscribe(False)
    warn(session, "after off", "Error")
    assert session.take_event(is_log_push("after off")) is None


# --- batch ---


def test_a_batch_answers_every_call_in_order(session):
    answer = session.batch([("ping", None), ("perform_action", {"type": "noop"}), {"method": "status"}])
    results = answer["results"]
    assert [entry["ok"] for entry in results] == [True, True, True]
    assert results[0]["result"]["pong"] is True
    assert results[1]["result"]["version"] == 2
    assert "stopped" not in answer
    assert session.call("get_state")["version"] == 2


def test_the_first_error_stops_a_batch_and_continue_on_error_runs_all(session):
    calls = [("perform_action", {"type": "noop"}), ("nothing", None), ("perform_action", {"type": "noop"})]
    stopped = session.batch(calls)
    assert stopped["stopped"] == 1 and len(stopped["results"]) == 2
    assert stopped["results"][1] == {"ok": False, "error": {"code": "unknown_method", "message": "nothing"}}
    assert session.call("get_state")["version"] == 2  # the third call did not run

    everything = session.batch(calls, continue_on_error=True)
    assert [entry["ok"] for entry in everything["results"]] == [True, False, True]
    assert "stopped" not in everything
    assert session.call("get_state")["version"] == 4


def test_a_batch_inside_a_batch_and_quit_are_refused_per_call(session):
    answer = session.batch([("batch", {"calls": [{"method": "ping"}]}), ("quit", None)], continue_on_error=True)
    assert [entry["error"]["code"] for entry in answer["results"]] == ["invalid_params", "invalid_params"]
    assert session.call("ping")["pong"] is True


@pytest.mark.parametrize("calls", [[], [{"params": {}}], ["ping"]])
def test_a_bad_batch_shape_is_a_control_error_and_nothing_runs(session, calls):
    with pytest.raises(ControlError) as refusal:
        session.call("batch", {"calls": calls})
    assert refusal.value.code == "invalid_params"


def test_a_step_in_a_batch_runs_before_the_calls_behind_it(session):
    answer = session.batch([("step", {"frames": 3}), ("status", None)])
    assert answer["results"][1]["result"]["frame"] == 3


def test_a_recording_holds_a_batch_as_one_line(fake_game_exe, tmp_path):
    path = tmp_path / "batch.jsonl"
    recorded = GameSession()
    try:
        recorded.launch(str(fake_game_exe), record=str(path))
        recorded.batch([("perform_action", {"type": "noop"}), ("step", {"frames": 2})])
        recorded.quit()
    finally:
        recorded.close()
    _header, steps = scenario.load(path)
    assert [step.method for step in steps] == ["batch"]
    assert steps[0].params == {"calls": [{"method": "perform_action", "params": {"type": "noop"}}, {"method": "step", "params": {"frames": 2}}]}


# --- run_until ---


def test_run_until_a_pointer_is_met_after_the_frames_it_took(session):
    session.call("perform_action", {"type": "later", "frames": 3})
    answer = session.run_until(pointer_equals("/version", 2), max_frames=10)
    assert answer["met"] is True and answer["frames"] == 3 and answer["reason"] == "pointer"
    assert answer["version"] == 2 and answer["frame"] == 3


def test_run_until_a_condition_that_holds_at_once_runs_no_frame(session):
    answer = session.run_until(pointer_equals("/state/flags/0", "--control"))
    assert answer["met"] is True and answer["frames"] == 0 and answer["frame"] == 0


def test_run_until_the_limit_is_not_met_and_not_an_error(session):
    answer = session.run_until(version_moved(), max_frames=5)
    assert answer["met"] is False and answer["frames"] == 5 and "reason" not in answer
    assert session.call("status")["frame"] == 5


def test_run_until_the_version_moved(session):
    session.call("perform_action", {"type": "later", "frames": 2})
    answer = session.run_until(version_moved(), max_frames=10)
    assert (answer["met"], answer["frames"], answer["reason"]) == (True, 2, "versionMoved")


def test_run_until_an_event_with_and_without_a_member(session):
    session.call("perform_action", {"type": "later", "frames": 4})
    assert session.run_until(event_occurs("state_changed", {"version": 3}), max_frames=6)["met"] is False
    session.call("perform_action", {"type": "later", "frames": 1})
    answer = session.run_until(event_occurs("state_changed"), max_frames=3)
    assert (answer["met"], answer["frames"], answer["reason"]) == (True, 1, "event")


def test_run_until_a_missing_path_is_not_met(session):
    assert session.run_until(pointer_equals("/state/nothing", 1), max_frames=2)["met"] is False


@pytest.mark.parametrize("condition, max_frames", [({}, 5), ({"pointer": "/a", "equals": 1, "versionMoved": True}, 5), ({"pointer": "a", "equals": 1}, 5), (version_moved(), 0)])
def test_run_until_refuses_a_bad_condition_or_limit(session, condition, max_frames):
    with pytest.raises(ControlError) as refusal:
        session.run_until(condition, max_frames=max_frames)
    assert refusal.value.code == "invalid_params"


def test_run_until_sends_dt_only_when_given(session, monkeypatch):
    sent = []
    real = session._call_raw
    monkeypatch.setattr(session, "_call_raw", lambda method, params=None, timeout=None: sent.append((method, params)) or real(method, params, timeout))
    session.run_until(version_moved(), max_frames=1)
    session.run_until(version_moved(), max_frames=1, dt=0.25)
    assert [params for _method, params in sent] == [
        {"condition": {"versionMoved": True}, "maxFrames": 1},
        {"condition": {"versionMoved": True}, "maxFrames": 1, "dt": 0.25},
    ]


# --- the timeout of a call that waits ---


def seconds(frames: int) -> float:
    return CALL_TIMEOUT_SECONDS + frames * STEP_SECONDS_PER_FRAME


def test_the_timeout_grows_with_the_frames_a_call_may_wait():
    wait = GameSession._wait_timeout
    assert wait("step", {"frames": 1000}) == seconds(1000)
    assert wait("step", None) == seconds(1)
    assert wait("run_until", {"maxFrames": 2000}) == seconds(2000)
    assert wait("run_until", {}) == seconds(DEFAULT_RUN_UNTIL_FRAMES)
    steps = [{"method": "step", "params": {"frames": 100}}, {"method": "ping"}, {"method": "step", "params": {"frames": 50}}]
    assert wait("batch", {"calls": steps}) == seconds(150)
    assert wait("batch", {"calls": steps + [{"method": "run_until", "params": {"maxFrames": 10}}]}) == seconds(160)


def test_a_call_that_never_waits_has_no_timeout_of_its_own():
    wait = GameSession._wait_timeout
    assert wait("get_state", None) is None
    assert wait("batch", {"calls": [{"method": "ping"}]}) is None
    assert wait("batch", {"calls": "garbage"}) is None
    assert wait("batch", None) is None


def test_the_timeout_reaches_the_connection_for_a_run_until_and_a_batch(session, monkeypatch):
    seen = []
    client = session._client
    real = client.call

    def spy(method, params=None):
        seen.append((method, client.timeout))
        return real(method, params)

    monkeypatch.setattr(client, "call", spy)
    session.run_until(version_moved(), max_frames=1)
    session.batch([("step", {"frames": 2}), ("step", {"frames": 3})])
    session.batch([("ping", None)])
    assert seen == [("run_until", seconds(1)), ("batch", seconds(5)), ("batch", CALL_TIMEOUT_SECONDS)]
