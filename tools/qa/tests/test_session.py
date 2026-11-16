from pathlib import Path

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_qa import QaSession
from bagi_session import SessionError

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
NOOP = {"type": "noop"}
INC = {"type": "inc"}


def make(qa_game_exe, out_dir, **kwargs):
    session = QaSession(out_dir, "a charter", **kwargs)
    session.launch(exe=str(qa_game_exe))
    return session


def recorded(session):
    return scenario.load(session.scenario_path)[1]


def names(session):
    return [finding.name for finding in session.findings]


def test_clean_run_finds_nothing(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    session.step(5)
    assert session.act(INC).accepted
    assert session.act(NOOP).accepted
    session.reset(count=3)
    session.note("covered the counter")
    assert session.finish().scenario == session.scenario_path
    assert session.findings == []
    assert session.bugs == [] and session.known == []
    assert session.notes == ["covered the counter"]
    assert (session.actions, session.refused, session.steps) == (2, 0, 1)


def test_recording_loads_and_matches_the_call_count(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    session.step(2)
    session.act(INC)
    session.finish()
    header, steps = scenario.load(session.scenario_path)
    assert header.game == "Fake" and header.flags == ["--headless", "--manual-clock"] == session.flags
    assert len(steps) == session.calls
    assert [step.method for step in steps].count("perform_action") == 1
    assert steps[0].method == "step"


def test_warning_in_the_log_is_found(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    session = make(qa_game_exe, out_dir)
    session.act(INC)
    assert session.findings == []
    session.act(INC)
    session.finish()
    (finding,) = session.findings
    assert (finding.kind, finding.name) == ("log", "generic.log_warning")
    assert "something odd" in finding.message and "block-buffered" not in finding.message
    performs = [i for i, step in enumerate(recorded(session), start=1) if step.method == "perform_action"]
    assert performs[1] <= finding.call_index <= performs[1] + 2


def test_late_warning_after_quit_says_so(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_LATE_WARNING", "1")
    session = make(qa_game_exe, out_dir)
    session.act(INC)
    assert session.findings == []
    session.finish()
    (finding,) = session.findings
    assert finding.name == "generic.log_warning" and "after the game quit" in finding.message and finding.details["late"]


def test_ui_issue_is_found_at_the_ui_tree_call(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_UI_ISSUE", "1")
    session = make(qa_game_exe, out_dir)
    session.step(3)
    session.finish()
    (finding,) = session.findings
    assert finding.name == "generic.ui_issues" and "Overlap on 'Play'" in finding.message
    assert recorded(session)[finding.call_index - 1].method == "ui_tree"


def test_canvas_that_is_not_laid_out_is_found(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_UI_NOT_LAID_OUT", "1")
    session = make(qa_game_exe, out_dir)
    session.finish()
    assert names(session) == ["generic.ui_not_laid_out"]


def test_ui_tree_runs_every_ui_every_steps(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir, ui_every=10)
    for _ in range(25):
        session.step(1)
    session.finish()
    assert [step.method for step in recorded(session)].count("ui_tree") == 1 + 2


def test_same_ui_problem_is_one_finding_with_a_count(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_UI_ISSUE", "1")
    session = make(qa_game_exe, out_dir, ui_every=1)
    for _ in range(3):
        session.step(1)
    session.finish()
    (finding,) = session.findings
    assert finding.details["repeats"] == 3


def test_version_going_back_is_found_at_the_action(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_VERSION_BACK_AT", "2")
    session = make(qa_game_exe, out_dir)
    session.act(INC)
    session.act(INC)
    session.finish()
    (finding,) = session.findings
    assert (finding.name, finding.expect) == ("generic.version_back", None)
    assert recorded(session)[finding.call_index - 1].method == "perform_action"


def test_listed_action_that_is_refused_is_found(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_REFUSE_LISTED", "1")
    session = make(qa_game_exe, out_dir)
    outcome = session.act(NOOP)
    session.finish()
    assert not outcome.accepted and outcome.error_code == "illegal_action"
    (finding,) = session.findings
    assert finding.name == "generic.action_refused" and finding.expect == {}
    line = recorded(session)[finding.call_index - 1]
    assert line.method == "perform_action" and line.expect == {"error": "illegal_action"}


def test_refusal_of_an_invented_action_is_not_a_finding(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    assert not session.act({"type": "bad"}).accepted
    assert not session.act({"type": "bad"}, expect_refusal=True).accepted
    session.finish()
    assert session.findings == [] and session.refused == 2


def test_a_games_invariants_module_runs_after_every_step_and_action(qa_game_exe, out_dir, tmp_path):
    module = tmp_path / "invariants.py"
    module.write_text(
        "KNOWN_DEVIATIONS = {'known_one'}\n"
        "def check_state(answer, previous):\n"
        "    out = []\n"
        "    if answer['state']['count'] >= 2:\n"
        "        out.append(('count_reached_two', 'count is ' + str(answer['state']['count'])))\n"
        "    if answer['state']['count'] == 1:\n"
        "        out.append(('known_one', 'one'))\n"
        "    return out\n"
    )
    session = make(qa_game_exe, out_dir, invariants=module)
    session.act(INC)
    assert names(session) == ["known_one"]
    session.act(INC)
    session.finish()
    assert [finding.name for finding in session.bugs] == ["count_reached_two"]
    assert [finding.name for finding in session.known] == ["known_one"]
    assert recorded(session)[session.bugs[0].call_index - 1].method == "get_state"


def test_a_raising_invariants_module_does_not_crash_the_session(qa_game_exe, out_dir, tmp_path):
    module = tmp_path / "invariants.py"
    module.write_text("def check_state(answer, previous):\n    raise RuntimeError('boom')\n")
    session = make(qa_game_exe, out_dir, invariants=module)
    session.step(1)
    session.finish()
    assert names(session) == ["qa.module_error"]


def test_shot_writes_a_png_and_a_safe_name(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    first = session.shot("count 3 / big")
    second = session.shot("again")
    session.finish()
    assert first == out_dir.resolve() / "shots" / "01-count-3-big.png" and second.name == "02-again.png"
    assert first.read_bytes().startswith(PNG_SIGNATURE)
    assert [label for label, _ in session.shots] == ["count 3 / big", "again"]


def test_flag_adds_a_finding_at_the_last_call_and_counts_repeats(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    session.step(1)
    calls = session.calls
    session.flag("hud_mismatch", "HUD shows 1, state 2", {"hud": ["1"]})
    session.flag("hud_mismatch", "HUD shows 1, state 2")
    session.finish()
    [finding] = session.findings
    assert (finding.kind, finding.name, finding.call_index) == ("invariant", "hud_mismatch", calls)
    assert finding.details["hud"] == ["1"] and finding.details["repeats"] == 1
    assert finding in session.bugs


def test_an_out_folder_inside_a_git_work_tree_is_refused():
    inside = Path(__file__).resolve().parent / "qa_out_must_not_exist"
    with pytest.raises(SessionError, match="git work tree"):
        QaSession(inside, "a charter")
    assert not inside.exists()


def test_a_lost_game_is_reported_and_the_session_still_finishes(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_QUIT_AT", "1")
    session = make(qa_game_exe, out_dir)
    with pytest.raises(SessionError):
        session.act(INC)
        session.step(1)
    assert session.session_errors
    assert session.finish().scenario == session.scenario_path
    assert session.scenario_path.is_file()


def test_launch_of_a_missing_game_is_a_session_error(out_dir, tmp_path):
    session = QaSession(out_dir, "a charter")
    with pytest.raises(SessionError, match="no game executable"):
        session.launch(exe=str(tmp_path / "Missing"))
    assert session.session_errors
    with pytest.raises(SessionError):
        session.finish()


def test_a_call_the_game_refuses_is_a_recorded_line(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    before = session.calls
    with pytest.raises(ControlError):
        session._call("nonsense")
    assert session.calls == before + 1
    session.finish()
    assert recorded(session)[-1].method == "nonsense" and len(recorded(session)) == session.calls


def test_call_of_an_unknown_method_is_an_outcome_and_no_finding(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    before = session.calls
    outcome = session.call("nonsense", {"a": 1})
    session.finish()
    assert (outcome.accepted, outcome.error_code) == (False, "unknown_method")
    assert session.calls == before + 1 and session.findings == []


def test_call_of_step_runs_the_checks(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "1")
    session = make(qa_game_exe, out_dir)
    session._call("perform_action", INC)  # past the checks of `act`: only the call that follows may show the Warning
    assert session.findings == []
    assert session.call("step", {"frames": 1}).accepted
    session.finish()
    assert names(session) == ["generic.log_warning"]


def test_a_click_on_a_button_reaches_the_game_and_an_unknown_name_is_refused(qa_game_exe, out_dir):
    session = make(qa_game_exe, out_dir)
    before = session.state()["state"]["count"]
    assert session.call("ui_click", {"name": "Plus", "canvas": "Hud"}).accepted
    assert session.state()["state"]["count"] == before + 1
    assert session.call("ui_click", {"name": "Nowhere"}).error_code == "not_found"
    session.finish()
    assert session.findings == []


def test_the_window_method_is_served_only_with_its_knob(qa_game_exe, out_dir, monkeypatch):
    session = make(qa_game_exe, out_dir)
    assert session.call("set_window", {"width": 800, "height": 600}).error_code == "unknown_method"
    session.finish()
    monkeypatch.setenv("QA_FAKE_WINDOW", "1")
    session = make(qa_game_exe, out_dir.parent / "windowed")
    assert session.call("set_window", {"width": 800, "height": 600}).accepted
    assert session.call("set_window", {"width": 0, "height": 600}).error_code == "invalid_params"
    assert "set_window" in [method["name"] for method in session._call("list_methods")["methods"]]
    session.finish()
    assert session.findings == []


def test_a_crashing_game_is_a_session_error(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_CRASH_AT", "2")
    session = make(qa_game_exe, out_dir)
    session.act(INC)
    with pytest.raises(SessionError):
        session.act(INC)
