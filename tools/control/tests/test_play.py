"""The scenario player (bagi_play.py) through its command line, against the fake game executable (tests/fake_game.py)."""

import json
import tempfile
from pathlib import Path

import pytest

import scenario
from bagi_play import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, EXIT_USAGE, main  # first: bagi_play puts tools/shots on sys.path for png_ops
from png_reader import PngImage, read_png

import png_ops  # noqa: E402

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


# --- reference pictures ---

SHOT = {"method": "screenshot", "params": {"maxSize": 400}}


def reference_line(png="references/x.png", **members):
    return {**SHOT, "expect": {"reference": {"png": png, **members}}}


def serve(monkeypatch, tmp_path, image):
    """Makes the fake game's screenshot `image` (a PngImage) for the games started from now on."""
    path = tmp_path / "served.png"
    path.write_bytes(png_ops.encode_png(image))
    monkeypatch.setenv("FAKE_GAME_PICTURE", str(path))


def two_by_one(left, right):
    return PngImage(2, 1, 3, [bytes(left + right)])


@pytest.fixture
def scenario_folder(tmp_path):
    folder = tmp_path / "scenarios"
    folder.mkdir()
    return folder


def test_a_reference_is_made_by_update_and_then_passes(fake_game_exe, scenario_folder, tmp_path, capsys):
    path = write(scenario_folder, reference_line())
    assert play(fake_game_exe, path, "--update") == EXIT_PASSED
    reference = scenario_folder / "references" / "x.png"
    assert f"created reference {reference}" in capsys.readouterr().out
    assert read_png(reference.read_bytes()).pixel(1, 0) == (0, 0, 255)  # the 2x1 picture of the fake game
    assert scenario.load(path)[1][0].expect == {"reference": {"png": "references/x.png"}}  # the line stays as written
    assert play(fake_game_exe, path, "--failures", str(tmp_path / "failures")) == EXIT_PASSED


def test_a_second_update_of_an_equal_picture_says_nothing_and_writes_nothing(fake_game_exe, scenario_folder, capsys):
    path = write(scenario_folder, reference_line())
    play(fake_game_exe, path, "--update")
    reference = scenario_folder / "references" / "x.png"
    stamp = reference.stat().st_mtime_ns
    capsys.readouterr()
    assert play(fake_game_exe, path, "--update") == EXIT_PASSED
    output = capsys.readouterr().out
    assert "created reference" not in output and "updated reference" not in output and reference.stat().st_mtime_ns == stamp


def test_a_changed_picture_fails_names_the_two_files_and_update_accepts_it(fake_game_exe, scenario_folder, tmp_path, monkeypatch, capsys):
    path = write(scenario_folder, reference_line(maxShare=0.25))
    play(fake_game_exe, path, "--update")
    serve(monkeypatch, tmp_path, two_by_one((255, 0, 0), (0, 255, 0)))
    failures = tmp_path / "failures"
    capsys.readouterr()
    assert play(fake_game_exe, path, "--failures", str(failures)) == EXIT_FAILED
    output = capsys.readouterr().out
    assert f"FAILED {path}:2  screenshot" in output and "expected reference:" in output and "50% changed" in output
    assert str(failures / "x.actual.png") in output and str(failures / "x.diff.png") in output
    assert (failures / "x.actual.png").is_file() and (failures / "x.diff.png").is_file()
    assert not list(scenario_folder.rglob("*.diff.png"))  # nothing is written into the scenario's folder

    assert play(fake_game_exe, path, "--update") == EXIT_PASSED
    assert "updated reference" in capsys.readouterr().out
    assert play(fake_game_exe, path, "--failures", str(failures)) == EXIT_PASSED


def test_the_threshold_and_the_region_of_a_line_decide(fake_game_exe, scenario_folder, tmp_path, monkeypatch):
    path = write(scenario_folder, reference_line(threshold=10, region=[0, 0, 0, 0]))
    play(fake_game_exe, path, "--update")
    serve(monkeypatch, tmp_path, two_by_one((255, 5, 0), (0, 0, 0)))  # the first pixel within the threshold, the second far off but outside the region
    assert play(fake_game_exe, path, "--failures", str(tmp_path / "failures")) == EXIT_PASSED


def test_a_missing_reference_fails_and_says_how_to_make_it(fake_game_exe, scenario_folder, tmp_path, capsys):
    path = write(scenario_folder, reference_line())
    assert play(fake_game_exe, path, "--failures", str(tmp_path / "failures")) == EXIT_FAILED
    output = capsys.readouterr().out
    assert "does not exist" in output and "--update" in output


def test_the_default_failure_pictures_go_to_the_temp_folder(fake_game_exe, scenario_folder, tmp_path, monkeypatch):
    monkeypatch.setattr(tempfile, "tempdir", str(tmp_path / "temp"))
    path = write(scenario_folder, reference_line(), name="shot_case.jsonl")
    (tmp_path / "temp").mkdir()
    play(fake_game_exe, path, "--update")
    serve(monkeypatch, tmp_path, two_by_one((1, 2, 3), (4, 5, 6)))
    assert play(fake_game_exe, path) == EXIT_FAILED
    assert (tmp_path / "temp" / "bagi-reference" / "shot_case" / "x.actual.png").is_file()


def test_a_reference_on_a_call_that_is_not_a_screenshot_exits_two(fake_game_exe, scenario_folder, capsys):
    path = write(scenario_folder, {"method": "get_state", "expect": {"reference": {"png": "x.png"}}})
    assert play(fake_game_exe, path) == EXIT_USAGE
    assert ":2" in capsys.readouterr().out


def test_a_failures_folder_inside_a_work_tree_exits_two(fake_game_exe, scenario_folder, capsys):
    path = write(scenario_folder, reference_line())
    assert play(fake_game_exe, path, "--failures", str(REPOSITORY_ROOT / "tools" / "shots")) == EXIT_USAGE
    assert "git work tree" in capsys.readouterr().out


def test_the_shrinker_keeps_a_failing_reference(fake_game_exe, scenario_folder, tmp_path, monkeypatch):
    path = write(scenario_folder, {"method": "get_state"}, {"method": "step", "params": {"frames": 1}}, reference_line())
    play(fake_game_exe, path, "--update")
    serve(monkeypatch, tmp_path, two_by_one((9, 9, 9), (9, 9, 9)))
    shrunk = tmp_path / "short.jsonl"
    assert play(fake_game_exe, path, "--shrink", str(shrunk), "--failures", str(tmp_path / "failures")) == EXIT_PASSED
    _header, steps = scenario.load(shrunk)
    assert [step.method for step in steps] == ["screenshot"] and "reference" in steps[0].expect
