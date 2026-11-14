"""The scenario player (bagi_play.py) through its command line, against the fake game executable (tests/fake_game.py)."""

import json
from pathlib import Path

import pytest

import scenario
from bagi_play import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, EXIT_USAGE, main

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
HEADER = {"scenario": 1, "game": "Fake", "flags": ["--headless", "--manual-clock"]}
ACTION = {"type": "noop"}


def write(tmp_path: Path, *lines: dict, header: dict | None = None, name: str = "play.jsonl") -> Path:
    path = tmp_path / name
    path.write_text("\n".join(json.dumps(line) for line in [HEADER if header is None else header, *lines]) + "\n", encoding="utf-8")
    return path


def play(fake_game_exe: Path, path: Path, *extra: str) -> int:
    return main([str(fake_game_exe), str(path), *extra])


def test_a_passing_scenario_exits_zero_and_counts_the_calls(fake_game_exe, tmp_path, capsys):
    path = write(
        tmp_path,
        {"method": "get_state", "expect": {"equal": {"game": "Fake"}, "pointers": {"/state/flags/0": "--control"}}},
        {"method": "perform_action", "params": ACTION, "expect": {"versionStep": 1, "pointers": {"/outcome/type": "noop"}}},
        {"method": "perform_action", "params": {"type": "bad"}, "expect": {"error": "illegal_action"}},
        {"method": "step", "params": {"frames": 3}, "expect": {"result": {"frame": 3}}},
        {"method": "screenshot", "expect": {"equal": {"width": 2, "height": 1}}},
    )
    assert play(fake_game_exe, path) == EXIT_PASSED
    assert f"PASSED {path}: 5 calls" in capsys.readouterr().out


def test_a_failing_version_step_names_the_line_the_call_the_expected_and_the_actual(fake_game_exe, tmp_path, capsys):
    path = write(
        tmp_path,
        {"method": "get_state"},
        {"method": "perform_action", "params": ACTION, "expect": {"versionStep": 1}},
        {"method": "perform_action", "params": ACTION, "expect": {"versionStep": 5}},
        {"method": "get_state", "expect": {"error": "never reached"}},
    )
    assert play(fake_game_exe, path) == EXIT_FAILED
    output = capsys.readouterr().out
    assert f"FAILED {path}:4  perform_action " in output and '"type": "noop"' in output
    assert "expected versionStep: 5" in output and "actual: 1" in output
    assert "never reached" not in output  # stopped at the first failure


def test_a_blank_line_does_not_shift_the_line_number_of_a_failure(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"})
    path.write_text(path.read_text() + "\n\n" + json.dumps({"method": "get_state", "expect": {"equal": {"game": "Other"}}}) + "\n")
    assert play(fake_game_exe, path) == EXIT_FAILED
    assert f"FAILED {path}:5  get_state" in capsys.readouterr().out


def test_a_wrong_error_code_fails(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "perform_action", "params": {"type": "bad"}, "expect": {"error": "game_over"}})
    assert play(fake_game_exe, path) == EXIT_FAILED
    output = capsys.readouterr().out
    assert "expected error: game_over" in output and "illegal_action: not allowed" in output


def test_an_error_nobody_expected_fails(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "perform_action", "params": {"type": "bad"}})
    assert play(fake_game_exe, path) == EXIT_FAILED
    assert "expected error: no error" in capsys.readouterr().out


def test_an_event_that_arrives_is_met(fake_game_exe, tmp_path):
    path = write(tmp_path, {"method": "perform_action", "params": ACTION, "expect": {"event": {"name": "state_changed", "equal": {"version": 2}, "timeout": 5}}})
    assert play(fake_game_exe, path) == EXIT_PASSED


def test_an_event_that_does_not_arrive_fails(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "perform_action", "params": ACTION, "expect": {"event": {"name": "state_changed", "equal": {"version": 99}, "timeout": 0.3}}})
    assert play(fake_game_exe, path) == EXIT_FAILED
    output = capsys.readouterr().out
    assert "expected event" in output and "no such push arrived" in output


def test_log_clean_fails_after_a_warning_and_names_the_line_that_asked(fake_game_exe, tmp_path, capsys):
    path = write(
        tmp_path,
        {"method": "get_state", "expect": {"logClean": True}},
        {"method": "warn", "params": {"text": "something odd"}},
        {"method": "get_state"},
    )
    assert play(fake_game_exe, path) == EXIT_FAILED
    output = capsys.readouterr().out
    assert f"FAILED {path}:2  get_state" in output
    assert "expected logClean" in output and "[Warning] [12:00:00.000] something odd" in output


def test_log_clean_passes_on_a_clean_log(fake_game_exe, tmp_path):
    path = write(tmp_path, {"method": "get_state"}, {"method": "get_state", "expect": {"logClean": True}})
    assert play(fake_game_exe, path) == EXIT_PASSED


def test_a_game_that_is_not_the_one_of_the_header_fails_line_one(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"}, header={**HEADER, "game": "Sample"})
    assert play(fake_game_exe, path) == EXIT_FAILED
    output = capsys.readouterr().out
    assert f"FAILED {path}:1" in output and "expected game: Sample" in output and "actual: Fake" in output


def test_the_flags_of_the_header_start_the_game(fake_game_exe, tmp_path):
    path = write(
        tmp_path,
        {"method": "get_state", "expect": {"pointers": {"/state/flags": ["--control", "--manual-clock", "--extra"]}}},
        header={**HEADER, "flags": ["--manual-clock", "--extra"]},
    )
    assert play(fake_game_exe, path) == EXIT_PASSED


def test_a_future_format_version_exits_two(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"}, header={**HEADER, "scenario": 99})
    assert play(fake_game_exe, path) == EXIT_USAGE
    assert "99" in capsys.readouterr().out


def test_a_broken_file_exits_two_with_its_line(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"})
    path.write_text(path.read_text() + "{not json\n")
    assert play(fake_game_exe, path) == EXIT_USAGE
    assert ":3" in capsys.readouterr().out


def test_a_missing_scenario_file_exits_two(fake_game_exe, tmp_path):
    assert play(fake_game_exe, tmp_path / "nothing.jsonl") == EXIT_USAGE


def test_a_missing_executable_is_skipped(tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"})
    assert main([str(tmp_path / "NoSuchGame"), str(path)]) == EXIT_SKIPPED
    assert "SKIPPED" in capsys.readouterr().out


def test_the_game_is_named_by_exe_or_by_preset_and_game_but_not_both(fake_game_exe, tmp_path):
    path = write(tmp_path, {"method": "get_state"})
    with pytest.raises(SystemExit) as refusal:
        main([str(fake_game_exe), str(path), "--preset", "nothing", "--game", "Fake"])
    assert refusal.value.code == EXIT_USAGE
    with pytest.raises(SystemExit):
        main([str(path)])
    assert main(["--preset", "no-such-preset", "--game", "Fake", "--root", str(tmp_path), str(path)]) == EXIT_SKIPPED


def test_a_game_that_goes_away_mid_run_fails_at_the_call(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"}, {"method": "quit"}, {"method": "get_state"})
    assert play(fake_game_exe, path) == EXIT_FAILED
    output = capsys.readouterr().out
    assert f"FAILED {path}:4  get_state" in output and "closed the connection" in output


def test_update_fixes_a_stale_value_and_keeps_event_and_log_clean(fake_game_exe, tmp_path, capsys):
    event = {"name": "state_changed", "equal": {"version": 2}}
    path = write(
        tmp_path,
        {"method": "get_state", "expect": {"versionStep": 7}},
        {"method": "perform_action", "params": ACTION, "expect": {"versionStep": 9, "event": event, "logClean": True, "pointers": {"/outcome/type": "old"}}},
        {"method": "perform_action", "params": {"type": "bad"}},
        {"method": "get_state", "note": "kept", "expect": {"error": "gone"}},
    )
    assert play(fake_game_exe, path, "--update") == EXIT_PASSED
    output = capsys.readouterr().out
    assert "UPDATED" in output and "4 of 4 lines changed" in output
    _header, steps = scenario.load(path)
    assert steps[0].expect == {}  # the first version seen has no step: nothing stale is left
    assert steps[1].expect == {"pointers": {"/outcome/type": "noop"}, "versionStep": 1, "event": event, "logClean": True}
    assert steps[2].expect == {"error": "illegal_action"}
    assert steps[3].expect == {} and steps[3].note == "kept"  # the call stopped failing, so `error` is dropped
    assert play(fake_game_exe, path) == EXIT_PASSED  # what was written passes


def test_update_writes_nothing_when_a_call_raised_a_session_error(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state", "expect": {"versionStep": 7}}, {"method": "quit"}, {"method": "get_state"})
    before = path.read_text()
    assert play(fake_game_exe, path, "--update") == EXIT_FAILED
    assert path.read_text() == before
    assert "closed the connection" in capsys.readouterr().out


def test_every_shipped_scenario_loads():
    shipped = sorted((REPOSITORY_ROOT / "games").glob("*/tests/scenarios/*.jsonl"))
    if not shipped:
        pytest.skip("no scenario file is shipped yet")
    for path in shipped:
        scenario.load(path)
