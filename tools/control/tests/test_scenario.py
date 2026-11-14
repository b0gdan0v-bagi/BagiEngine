"""The scenario format (scenario.py): loading and writing, the expectations, what a recording writes, --update."""

import json

import pytest

import scenario
from scenario import ABSENT, Header, Outcome, ScenarioError, Step, VersionTracker, evaluate

HEADER_LINE = '{"scenario": 1, "game": "Sample", "flags": ["--headless", "--manual-clock"], "title": "A tour"}'


def parse_error(text: str) -> ScenarioError:
    with pytest.raises(ScenarioError) as raised:
        scenario.parse(text, "play.jsonl")
    return raised.value


def step_line(**members) -> str:
    return HEADER_LINE + "\n" + json.dumps({"method": "get_state", **members})


# --- loading and writing ---


def test_a_file_loads_into_a_header_and_steps(tmp_path):
    path = tmp_path / "play.jsonl"
    path.write_text(
        HEADER_LINE + "\n\n"
        '{"method": "reset", "params": {"seed": 1}, "expect": {"versionStep": 1}, "note": "start"}\n'
        '{"method": "get_state"}\n',
        encoding="utf-8",
    )
    header, steps = scenario.load(path)
    assert header == Header(1, "Sample", ["--headless", "--manual-clock"], "A tour")
    assert steps == [Step("reset", {"seed": 1}, {"versionStep": 1}, "start"), Step("get_state")]


def test_a_dumped_file_loads_and_dumps_to_the_same_text(tmp_path):
    header = Header(1, "Schach", ["--headless"], "Zuge: éè")
    steps = [
        Step("reset", {"seed": 1, "b": 2, "a": 3}, {"logClean": True, "versionStep": 0, "error": "x"}, "n"),
        Step("get_state"),
        Step("step", {"frames": 2}, {"event": {"name": "state_changed", "equal": {"version": 5}, "timeout": 2}}),
    ]
    path = tmp_path / "play.jsonl"
    scenario.dump(path, header, steps)
    text = path.read_text(encoding="utf-8")
    assert "é" in text  # not escaped
    assert text.endswith("\n") and all(line.startswith("{") for line in text.splitlines())
    assert text.splitlines()[1].index('"error"') < text.splitlines()[1].index('"versionStep"') < text.splitlines()[1].index('"logClean"')
    loaded = scenario.load(path)
    assert loaded[0] == header
    assert [step.method for step in loaded[1]] == ["reset", "get_state", "step"]
    scenario.dump(path, *loaded)
    assert path.read_text(encoding="utf-8") == text


def test_a_header_without_title_and_flags_is_valid():
    header, steps = scenario.parse('{"scenario": 1, "game": "G"}')
    assert header == Header(1, "G", [], None) and steps == []
    assert scenario.dumps(header, steps) == '{"scenario": 1, "game": "G", "flags": []}\n'


@pytest.mark.parametrize(
    "text, fragment",
    [
        ("", "empty"),
        ('{"scenario": 1, "game": "G"', "not JSON"),
        ("[1]", "JSON object"),
        ('{"game": "G"}', "header"),
        ('{"scenario": "1", "game": "G"}', "header"),
        ('{"scenario": true, "game": "G"}', "header"),
        ('{"scenario": 2, "game": "G"}', "version 2 is not known"),
        ('{"scenario": 1}', "\"game\""),
        ('{"scenario": 1, "game": "G", "flags": "--headless"}', "list of strings"),
        ('{"scenario": 1, "game": "G", "flags": [1]}', "list of strings"),
        ('{"scenario": 1, "game": "G", "title": 3}', "\"title\""),
        ('{"scenario": 1, "game": "G", "extra": 3}', "unknown member \"extra\""),
    ],
)
def test_a_bad_header_is_a_scenario_error_with_the_place(text, fragment):
    error = parse_error(text)
    assert fragment in str(error)
    if text:
        assert str(error).startswith("play.jsonl:1: ") and error.line == 1


@pytest.mark.parametrize(
    "line, fragment",
    [
        ('{"params": {}}', "\"method\""),
        ('{"method": ""}', "\"method\""),
        ('{"method": 3}', "\"method\""),
        ('{"method": "a", "params": [1]}', "\"params\""),
        ('{"method": "a", "params": null}', "\"params\""),
        ('{"method": "a", "expect": 1}', "\"expect\""),
        ('{"method": "a", "note": 1}', "\"note\""),
        ('{"method": "a", "extra": 1}', "unknown member \"extra\""),
        ('{"method": "a", "expect": {"nope": 1}}', "unknown member \"nope\""),
        ('{"method": "a", "expect": {"error": 3}}', "expect.error"),
        ('{"method": "a", "expect": {"equal": []}}', "expect.equal"),
        ('{"method": "a", "expect": {"pointers": []}}', "expect.pointers"),
        ('{"method": "a", "expect": {"pointers": {"state": 1}}}', "not a JSON pointer"),
        ('{"method": "a", "expect": {"pointers": {"/a~2": 1}}}', "not a JSON pointer"),
        ('{"method": "a", "expect": {"versionStep": "1"}}', "expect.versionStep"),
        ('{"method": "a", "expect": {"versionStep": 1.5}}', "expect.versionStep"),
        ('{"method": "a", "expect": {"logClean": 1}}', "expect.logClean"),
        ('{"method": "a", "expect": {"event": "x"}}', "expect.event"),
        ('{"method": "a", "expect": {"event": {}}}', "\"name\""),
        ('{"method": "a", "expect": {"event": {"name": "e", "equal": 1}}}', "expect.event.equal"),
        ('{"method": "a", "expect": {"event": {"name": "e", "timeout": -1}}}', "timeout"),
        ('{"method": "a", "expect": {"event": {"name": "e", "extra": 1}}}', "unknown member"),
        ("{oops", "not JSON"),
    ],
)
def test_a_bad_call_line_names_its_line_number(line, fragment):
    error = parse_error(HEADER_LINE + "\n\n" + '{"method": "ok"}\n' + line)
    assert fragment in str(error)
    assert error.line == 4 and str(error).startswith("play.jsonl:4: ")


def test_loading_a_missing_file_is_a_scenario_error(tmp_path):
    with pytest.raises(ScenarioError, match="cannot read"):
        scenario.load(tmp_path / "nothing.jsonl")


# --- pointers and equality ---


def test_json_pointer_follows_objects_and_lists():
    value = {"state": {"score": 7, "cells": [{"id": "a"}, {"id": "b"}], "none": None}}
    assert scenario.json_pointer(value, "") is value
    assert scenario.json_pointer(value, "/state/score") == 7
    assert scenario.json_pointer(value, "/state/cells/1/id") == "b"
    assert scenario.json_pointer(value, "/state/none") is None  # null is a value
    assert scenario.json_pointer(value, "/state/none") is not ABSENT


def test_json_pointer_escapes():
    value = {"a/b": 1, "m~n": 2, "~1": 3, "": 4}
    assert scenario.json_pointer(value, "/a~1b") == 1
    assert scenario.json_pointer(value, "/m~0n") == 2
    assert scenario.json_pointer(value, "/~01") == 3  # '~0' first: "~1", not "/"
    assert scenario.json_pointer(value, "/") == 4


@pytest.mark.parametrize("pointer", ["/missing", "/state/missing", "/state/score/deeper", "/state/cells/2", "/state/cells/-", "/state/cells/01", "/state/cells/x", "/state/cells/-1"])
def test_json_pointer_gives_absent_for_a_missing_path(pointer):
    assert scenario.json_pointer({"state": {"score": 7, "cells": [1, 2]}}, pointer) is ABSENT


def test_json_pointer_refuses_what_is_not_a_pointer():
    with pytest.raises(ValueError):
        scenario.json_pointer({}, "state")
    with pytest.raises(ValueError):
        scenario.json_pointer({}, "/a~2")


def test_same_does_not_take_true_for_one():
    assert scenario.same(1, 1.0) and scenario.same({"a": [1, "x", None]}, {"a": [1, "x", None]})
    assert not scenario.same(True, 1) and not scenario.same(0, False) and not scenario.same([True], [1])
    assert not scenario.same({"a": 1}, {"a": 1, "b": 2}) and not scenario.same("1", 1) and not scenario.same(None, 0)


def test_version_of_wants_an_integer_version_in_an_object():
    assert scenario.version_of({"version": 4}) == 4
    for result in ({"version": "4"}, {"version": True}, {"version": 1.5}, {}, [1], None, 4):
        assert scenario.version_of(result) is None


def test_the_version_tracker_gives_steps_relative_to_the_last_version_seen():
    tracker = VersionTracker()
    assert tracker.step_of({"version": 10}) is None  # nothing before
    assert tracker.step_of({"frame": 3}) is None  # not an answer with a version
    assert tracker.step_of({"version": 10}) == 0
    assert tracker.step_of({"version": 12}) == 2
    assert tracker.step_of({"version": 11}) == -1


# --- expectations ---


def ok(result=None, **members) -> Outcome:
    return Outcome(result=result, **members)


def refused(code="illegal_action", message="no") -> Outcome:
    return Outcome(error_code=code, error_message=message)


def test_an_empty_expect_holds_for_anything_but_an_unexpected_error():
    assert evaluate({}, ok({"a": 1})) is None
    failure = evaluate({}, refused())
    assert failure.expectation == "error" and failure.expected == "no error" and "illegal_action: no" in failure.actual
    assert evaluate({}, refused(), lenient=True) is None


def test_error_has_to_match_the_code_and_a_success_does_not_match_it():
    assert evaluate({"error": "illegal_action"}, refused()) is None
    wrong = evaluate({"error": "not_found"}, refused())
    assert (wrong.expectation, wrong.expected) == ("error", "not_found") and "illegal_action" in wrong.actual
    succeeded = evaluate({"error": "illegal_action"}, ok({}))
    assert succeeded.expectation == "error" and "succeeded" in succeeded.actual
    assert evaluate({"error": "illegal_action"}, ok({}), lenient=True) is not None  # lenient tolerates only an unexpected error


def test_result_is_the_whole_answer():
    assert evaluate({"result": {"a": 1}}, ok({"a": 1})) is None
    failure = evaluate({"result": {"a": 1}}, ok({"a": 1, "b": 2}))
    assert failure.expectation == "result" and failure.expected == '{"a": 1}' and failure.actual == '{"a": 1, "b": 2}'
    assert evaluate({"result": 1}, ok(True)) is not None
    assert evaluate({"result": {"a": 1}}, refused(), lenient=True).actual.startswith("the call failed")


def test_equal_checks_members_of_the_answer():
    assert evaluate({"equal": {"game": "G", "version": 3}}, ok({"game": "G", "version": 3, "more": 1})) is None
    failure = evaluate({"equal": {"game": "G", "version": 3}}, ok({"game": "G", "version": 4}))
    assert (failure.expectation, failure.expected, failure.actual) == ("equal", "version = 3", "version = 4")
    assert evaluate({"equal": {"x": 1}}, ok({})).actual == "x = <absent>"
    assert evaluate({"equal": {"x": 1}}, ok([1])).actual == "x = <absent>"
    assert evaluate({"equal": {"x": 1}}, ok({"x": True})) is not None


def test_pointers_check_paths_in_the_answer():
    answer = {"state": {"score": 7, "cells": [{"id": "a"}]}}
    assert evaluate({"pointers": {"/state/score": 7, "/state/cells/0/id": "a", "": answer}}, ok(answer)) is None
    failure = evaluate({"pointers": {"/state/score": 8}}, ok(answer))
    assert (failure.expectation, failure.expected, failure.actual) == ("pointers", "/state/score = 8", "/state/score = 7")
    assert evaluate({"pointers": {"/state/gone": 1}}, ok(answer)).actual == "/state/gone = <absent>"
    assert evaluate({"pointers": {"/state/gone": None}}, ok(answer)) is not None  # absent is not null


def test_version_step_is_relative_and_not_checked_without_a_version_before():
    assert evaluate({"versionStep": 1}, ok({"version": 5}, version_step=1)) is None
    failure = evaluate({"versionStep": 1}, ok({"version": 7}, version_step=2))
    assert (failure.expectation, failure.expected, failure.actual) == ("versionStep", "1", "2")
    assert evaluate({"versionStep": 1}, ok({"version": 7}, version_step=None)) is None  # the first version of the run
    no_version = evaluate({"versionStep": 0}, ok({"frame": 1}))
    assert no_version.expectation == "versionStep" and "no version" in no_version.actual


def test_event_needs_a_matching_push_among_those_taken():
    push = {"event": "state_changed", "data": {"version": 5, "game": "G"}}
    other = {"event": "log", "data": {}}
    expect = {"event": {"name": "state_changed", "equal": {"version": 5}}}
    assert evaluate(expect, ok(events=[other, push])) is None
    assert evaluate({"event": {"name": "state_changed"}}, ok(events=[push])) is None
    none = evaluate(expect, ok(events=[]))
    assert none.expectation == "event" and "none arrived" in none.actual
    wrong = evaluate(expect, ok(events=[{"event": "state_changed", "data": {"version": 6}}]))
    assert "version" in wrong.actual and "6" in wrong.actual
    assert scenario.match_event({"name": "state_changed", "equal": {"version": 5}}, {"event": "state_changed"}) is False
    assert scenario.match_event({"name": "x"}, {"event": "x", "data": None}) is True


def test_log_clean_fails_with_the_problem_lines():
    problems = ["[Warning] [12:00:00.000] a", "[Error] [12:00:01.000] b"]
    assert evaluate({"logClean": True}, ok(log_problems=[])) is None
    failure = evaluate({"logClean": True}, ok(log_problems=problems))
    assert failure.expectation == "logClean" and "2 line(s)" in failure.actual and "[Error] [12:00:01.000] b" in failure.actual
    assert evaluate({"logClean": False}, ok(log_problems=problems)) is None


def test_the_first_failing_expectation_is_reported_in_the_documented_order():
    expect = {"logClean": True, "versionStep": 9, "equal": {"a": 2}, "error": "x"}
    outcome = ok({"a": 1}, version_step=1, log_problems=["[Warning] w"])
    assert [evaluate({k: v}, outcome).expectation for k, v in expect.items()] == ["logClean", "versionStep", "equal", "error"]
    assert evaluate(expect, outcome).expectation == "error"
    del expect["error"]
    assert evaluate(expect, outcome).expectation == "equal"


def test_a_long_value_is_cut_in_the_message():
    failure = evaluate({"result": 1}, ok("x" * 1000))
    assert len(failure.actual) < 400 and failure.actual.endswith("...")
    assert str(failure).startswith("expected result: 1\n  actual: ")


# --- recording ---


def test_minimal_expect_is_the_error_code_or_the_version_step():
    assert scenario.minimal_expect(refused("not_found")) == {"error": "not_found"}
    assert scenario.minimal_expect(ok({"version": 4}, version_step=2)) == {"versionStep": 2}
    assert scenario.minimal_expect(ok({"version": 4}, version_step=0)) == {"versionStep": 0}
    assert scenario.minimal_expect(ok({"version": 4})) == {}
    assert scenario.minimal_expect(ok({"frame": 4})) == {}


def test_full_expect_adds_the_answer_and_reduces_a_screenshot():
    assert scenario.full_expect(ok({"version": 4, "x": 1}, version_step=1)) == {"result": {"version": 4, "x": 1}, "versionStep": 1}
    assert scenario.full_expect(refused("not_found")) == {"error": "not_found"}
    picture = ok({"width": 4, "height": 2, "png_base64": "AAAA"})
    recorded = scenario.full_expect(picture)
    assert recorded == {"equal": {"width": 4, "height": 2}}
    assert evaluate(recorded, picture) is None  # it replays against the real answer
    assert "AAAA" not in json.dumps(recorded)


def test_what_full_expect_writes_holds_for_the_same_outcome():
    for outcome in (ok({"version": 4, "a": [1, {"b": None}]}, version_step=1), ok([1, 2]), ok(None), refused()):
        assert evaluate(scenario.full_expect(outcome), outcome) is None
        assert evaluate(scenario.minimal_expect(outcome), outcome) is None


# --- --update ---


def test_updated_expect_rewrites_the_values_a_line_has():
    outcome = ok({"version": 9, "score": 3, "list": [10, 20]}, version_step=4)
    expect = {"result": {"old": 1}, "equal": {"score": 1, "gone": 2}, "pointers": {"/list/1": 0, "/nope": 1}, "versionStep": 1}
    assert scenario.updated_expect(expect, outcome) == {
        "result": {"version": 9, "score": 3, "list": [10, 20]},
        "equal": {"score": 3},
        "pointers": {"/list/1": 20},
        "versionStep": 4,
    }


def test_updated_expect_keeps_event_and_log_clean():
    event = {"name": "state_changed", "equal": {"version": 5}}
    updated = scenario.updated_expect({"versionStep": 1, "event": event, "logClean": True}, ok({"version": 6}, version_step=3))
    assert updated == {"versionStep": 3, "event": event, "logClean": True}
    assert list(updated) == ["versionStep", "event", "logClean"]


def test_updated_expect_adds_the_minimal_members_to_a_line_without_any():
    assert scenario.updated_expect({}, ok({"version": 6}, version_step=3)) == {"versionStep": 3}
    assert scenario.updated_expect({}, refused("not_found")) == {"error": "not_found"}
    assert scenario.updated_expect({}, ok({"frame": 1})) == {}
    kept = {"logClean": True}
    assert scenario.updated_expect(kept, ok({"version": 6}, version_step=3)) == {"versionStep": 3, "logClean": True}


def test_updated_expect_follows_a_call_that_changed_from_failing_to_working_and_back():
    assert scenario.updated_expect({"error": "illegal_action"}, ok({"version": 3}, version_step=1)) == {}
    assert scenario.updated_expect({"versionStep": 1}, refused("not_found")) == {"error": "not_found"}
    assert scenario.updated_expect({"error": "illegal_action"}, refused("not_found")) == {"error": "not_found"}
    assert scenario.updated_expect({"result": {"a": 1}, "error": "x"}, refused("y")) == {"error": "y"}


def test_updated_expect_keeps_a_screenshot_as_its_size():
    picture = ok({"width": 4, "height": 2, "png_base64": "AAAA"})
    assert scenario.updated_expect({"result": {"width": 1}}, picture) == {"equal": {"width": 4, "height": 2}}
    assert scenario.updated_expect({"equal": {"width": 1}}, picture) == {"equal": {"width": 4}}


def test_what_updated_expect_writes_holds_for_the_same_outcome():
    outcome = ok({"version": 9, "list": [10, 20]}, version_step=4)
    expect = {"result": {"x": 1}, "pointers": {"/list/1": 0}, "versionStep": 1, "logClean": True}
    assert evaluate(scenario.updated_expect(expect, outcome), outcome) is None


def test_what_updated_expect_writes_holds_for_a_refused_call():
    outcome = refused("not_found")
    for expect in ({"versionStep": 1}, {"equal": {"a": 1}}, {"result": {"x": 1}, "logClean": True}, {"error": "illegal_action"}, {"pointers": {"/a": 1}}):
        assert evaluate(scenario.updated_expect(expect, outcome), outcome) is None, expect
