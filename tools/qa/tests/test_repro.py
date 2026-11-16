import json
from pathlib import Path

import pytest

import bagi_play
import bagi_qa
import bagi_qa_repro as repro
import scenario
from bagi_qa_checks import Finding
from scenario import Header, Step

HEADER = Header(1, "Fake", ["--headless", "--manual-clock"])
STEP = Step("step", {"frames": 1})
INC = Step("perform_action", {"type": "inc"})
NOOP = Step("perform_action", {"type": "noop"})
STATE = Step("get_state")
UI = Step("ui_tree")
TWELVE = [STEP, INC, STEP, NOOP, STEP, STATE, INC, STEP, NOOP, UI, STATE, STEP]
RECORDED_EXPECT = {"versionStep": 7}  # what a recording would say, wrong after a cut: the written file must not carry it


def write_scenario(folder, steps=TWELVE, header=HEADER, name="session.jsonl"):
    path = folder / name
    scenario.dump(path, header, steps)
    return path


def cli(*arguments):
    return bagi_qa.main(list(arguments))


def played(exe, path):
    header, steps = scenario.load(path)
    return bagi_play.play(Path(exe), header, steps)


def test_a_clean_scenario_finds_nothing(qa_game_exe, tmp_path):
    assert repro.replay(qa_game_exe, write_scenario(tmp_path)) == []


def test_replay_gives_the_finding_with_the_call_it_showed_after(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    findings = repro.replay(qa_game_exe, write_scenario(tmp_path))
    (finding,) = findings
    assert finding.name == "generic.log_warning" and TWELVE[finding.call_index - 1] is NOOP
    assert finding.call_index == 4


def test_a_late_warning_is_at_the_last_call_played(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_LATE_WARNING", "1")
    (finding,) = repro.replay(qa_game_exe, write_scenario(tmp_path))
    assert finding.call_index == len(TWELVE) and "after the game quit" in finding.message


def test_a_game_lost_on_the_way_keeps_what_was_found(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "1")
    monkeypatch.setenv("QA_FAKE_QUIT_AT", "2")
    header, steps = repro.load_scenario(write_scenario(tmp_path))
    run = repro.replay_steps(qa_game_exe, header, steps, None)
    assert [finding.name for finding in run.findings] == ["generic.log_warning"]
    assert run.played < len(steps) and run.session_errors


def test_twelve_calls_shrink_to_the_two_the_finding_needs(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    path = write_scenario(tmp_path)
    result = repro.shrink_finding(qa_game_exe, path, "generic.log_warning")
    assert [call.method for call in result.calls] == ["perform_action"] * 2
    assert result.original_calls == 12 and not result.limited and result.finding.call_index == 2
    out = tmp_path / "repro.jsonl"
    repro.write_repro(out, result.header, repro.settle(qa_game_exe, result.header, result.calls), result.finding)
    header, steps = scenario.load(out)
    assert len(steps) == 2 and steps[-1].expect == {"logClean": True} and "generic.log_warning" in steps[-1].note
    assert header.title and "generic.log_warning" in header.title
    outcome = played(qa_game_exe, out)
    assert outcome.failed_step == 1 and outcome.failure.expectation == "logClean"


def test_the_written_file_does_not_carry_the_expectations_of_the_recording(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    recorded = [Step(step.method, step.params, dict(RECORDED_EXPECT)) for step in TWELVE]
    path = write_scenario(tmp_path, recorded)
    out = tmp_path / "repro.jsonl"
    assert cli("shrink", "--exe", str(qa_game_exe), "--out", str(out), str(path)) == repro.EXIT_FOUND
    steps = scenario.load(out)[1]
    assert all("versionStep" not in step.expect or step.expect["versionStep"] != 7 for step in steps)
    assert played(qa_game_exe, out).failed_step == len(steps) - 1


def test_max_probes_stops_the_cut_early(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    path = write_scenario(tmp_path)
    result = repro.shrink_finding(qa_game_exe, path, "generic.log_warning", max_probes=2)
    assert result.limited and result.probes <= 2 and 2 < len(result.calls) <= 4
    assert all(step in TWELVE for step in result.calls)


def test_a_finding_that_does_not_recur_is_an_error(qa_game_exe, tmp_path, capsys):
    path = write_scenario(tmp_path)
    with pytest.raises(repro.ReproError, match="does not come back"):
        repro.shrink_finding(qa_game_exe, path, "generic.log_warning")
    out = tmp_path / "x.jsonl"
    assert cli("shrink", "--exe", str(qa_game_exe), "--finding", "generic.log_warning", "--out", str(out), str(path)) == repro.EXIT_USAGE
    assert not out.exists() and "does not come back" in capsys.readouterr().out


def test_a_refused_listed_action_is_written_with_no_expectation_so_the_line_fails(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_REFUSE_LISTED", "1")
    recorded = [Step(step.method, step.params, {"error": "illegal_action"} if step is NOOP else {}) for step in TWELVE]
    path = write_scenario(tmp_path, recorded)
    out = tmp_path / "repro.jsonl"
    assert cli("shrink", "--exe", str(qa_game_exe), "--out", str(out), str(path)) == repro.EXIT_FOUND
    (only,) = scenario.load(out)[1]
    assert only.method == "perform_action" and only.expect == {}
    outcome = played(qa_game_exe, out)
    assert outcome.failed_step == 0 and outcome.failure.expectation == "error"


def test_a_ui_finding_ends_on_the_ui_tree_with_the_canvas_pointer(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_UI_ISSUE", "1")
    path = write_scenario(tmp_path, [INC, STEP, NOOP, STEP])
    out = tmp_path / "repro.jsonl"
    assert cli("shrink", "--exe", str(qa_game_exe), "--out", str(out), str(path)) == repro.EXIT_FOUND
    steps = scenario.load(out)[1]
    assert steps[-1].method == "ui_tree" and steps[-1].expect == {"pointers": {"/canvases/0/issues": []}}
    assert len(steps) <= 2 and played(qa_game_exe, out).failure.expectation == "pointers"


def test_a_finding_without_an_expectation_is_reproduced_by_replay(qa_game_exe, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_VERSION_BACK_AT", "2")
    path = write_scenario(tmp_path)
    out = tmp_path / "repro.jsonl"
    assert cli("shrink", "--exe", str(qa_game_exe), "--out", str(out), str(path)) == repro.EXIT_FOUND
    assert "reproduce with bagi_qa.py replay" in capsys.readouterr().out
    assert played(qa_game_exe, out).passed
    assert cli("replay", "--exe", str(qa_game_exe), str(out)) == repro.EXIT_FOUND


def test_a_known_deviation_is_not_a_failure_of_replay(qa_game_exe, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    module = tmp_path / "invariants.py"
    module.write_text("KNOWN_DEVIATIONS = {'generic.log_warning'}\n")
    path = write_scenario(tmp_path)
    assert cli("replay", "--exe", str(qa_game_exe), "--invariants", str(module), str(path)) == repro.EXIT_NOTHING
    assert "KNOWN generic.log_warning" in capsys.readouterr().out
    assert cli("shrink", "--exe", str(qa_game_exe), "--invariants", str(module), str(path)) == repro.EXIT_NOTHING


def test_the_invariants_of_a_game_are_checked_in_a_replay(qa_game_exe, tmp_path):
    module = tmp_path / "invariants.py"
    module.write_text("def check_state(answer, previous):\n    return [('count_two', 'two')] if answer['state']['count'] >= 2 else []\n")
    path = write_scenario(tmp_path)
    result = repro.shrink_finding(qa_game_exe, path, "count_two", module)
    assert [call.params["type"] for call in result.calls] == ["inc", "inc"]
    assert result.finding.expect is None


def test_replay_and_shrink_exit_codes(qa_game_exe, tmp_path, capsys):
    clean = write_scenario(tmp_path)
    assert cli("replay", "--exe", str(qa_game_exe), str(clean)) == repro.EXIT_NOTHING
    assert cli("shrink", "--exe", str(qa_game_exe), str(clean)) == repro.EXIT_NOTHING
    assert cli("replay", "--exe", str(tmp_path / "Missing"), str(clean)) == repro.EXIT_SKIPPED
    assert cli("replay", "--exe", str(qa_game_exe), str(tmp_path / "missing.jsonl")) == repro.EXIT_USAGE
    assert cli("replay", str(clean)) == repro.EXIT_USAGE
    assert "ERROR" in capsys.readouterr().out


def test_a_bad_scenario_file_is_a_usage_error(qa_game_exe, tmp_path, capsys):
    bad = tmp_path / "bad.jsonl"
    bad.write_text('{"scenario": 1, "game": "Fake", "flags": ["--headless", "--manual-clock"]}\n{"method": 5}\n')
    assert cli("replay", "--exe", str(qa_game_exe), str(bad)) == repro.EXIT_USAGE
    assert "bad.jsonl:2" in capsys.readouterr().out


def test_a_header_without_the_manual_clock_or_for_another_game_is_refused(qa_game_exe, tmp_path):
    real_clock = write_scenario(tmp_path, header=Header(1, "Fake", ["--headless"]), name="real.jsonl")
    with pytest.raises(repro.ReproError, match="manual clock"):
        repro.replay(qa_game_exe, real_clock)
    other = write_scenario(tmp_path, header=Header(1, "Other", ["--headless", "--manual-clock"]), name="other.jsonl")
    with pytest.raises(repro.ReproError, match="'Other'"):
        repro.replay(qa_game_exe, other)


def test_the_reproduction_is_not_written_inside_a_work_tree(qa_game_exe, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    path = write_scenario(tmp_path)
    inside = Path(__file__).resolve().parent / "repro_must_not_exist.jsonl"
    assert cli("shrink", "--exe", str(qa_game_exe), "--out", str(inside), str(path)) == repro.EXIT_USAGE
    assert not inside.exists() and "git work tree" in capsys.readouterr().out
    finding = Finding("log", "generic.log_warning", "w", expect={"logClean": True})
    with pytest.raises(repro.ReproError, match="git work tree"):
        repro.write_repro(inside, HEADER, [INC], finding)


def test_write_reproduction_writes_a_file_that_fails_on_its_last_line(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    path = write_scenario(tmp_path)
    out = tmp_path / "repro" / "generic.log_warning.jsonl"
    shrunk, calls = repro.write_reproduction(qa_game_exe, path, "generic.log_warning", None, out, 20, [])
    assert shrunk.finding.name == "generic.log_warning" and len(scenario.load(out)[1]) == len(calls) == 2
    assert played(qa_game_exe, out).failed_step == len(calls) - 1


def test_write_reproduction_leaves_no_file_when_the_written_one_does_not_fail(qa_game_exe, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    monkeypatch.setattr(repro, "check_written", lambda exe, path, finding: "it does not reproduce")
    out = tmp_path / "repro.jsonl"
    with pytest.raises(repro.ReproError, match="does not reproduce"):
        repro.write_reproduction(qa_game_exe, write_scenario(tmp_path), "generic.log_warning", None, out)
    assert not out.exists()


def test_write_repro_puts_the_expectation_on_the_last_line_only(tmp_path):
    finding = Finding("action_refused", "generic.action_refused", "refused", expect={})
    calls = [Step("perform_action", {"type": "noop"}, {"error": "illegal_action"}), Step("perform_action", {"type": "inc"}, {"versionStep": 1}, note="mine")]
    out = tmp_path / "r.jsonl"
    repro.write_repro(out, HEADER, calls, finding)
    steps = scenario.load(out)[1]
    assert steps[0].expect == {"error": "illegal_action"}
    assert steps[1].expect == {} and steps[1].note.startswith("QA finding generic.action_refused")
    assert json.loads(out.read_text().splitlines()[0])["title"]


def test_write_repro_keeps_the_lines_of_a_finding_with_no_expectation(tmp_path):
    finding = Finding("version_back", "generic.version_back", "went back")
    out = tmp_path / "r.jsonl"
    repro.write_repro(out, HEADER, [Step("perform_action", {"type": "inc"}, {"versionStep": 1})], finding)
    assert scenario.load(out)[1][0].expect == {"versionStep": 1}


def test_a_scenario_of_no_calls_is_not_written(tmp_path):
    with pytest.raises(repro.ReproError, match="at least one call"):
        repro.write_repro(tmp_path / "r.jsonl", HEADER, [], Finding("log", "x", "m"))
