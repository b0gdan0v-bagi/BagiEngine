import pytest

import bagi_qa_checks as checks
from bagi_qa_checks import Finding, InvariantsError


def laid_out_canvas(node="Screen", issues=()):
    return {"node": node, "laidOut": True, "issues": list(issues)}


def test_log_reports_only_the_new_lines():
    lines = ["[Warning] [12:00:00.000] odd", "[Error] [12:00:01.000] bad"]
    first = checks.check_log(lines[:1], 0)
    assert [(f.kind, f.name, f.message) for f in first] == [("log", "generic.log_warning", lines[0])]
    assert first[0].expect == {"logClean": True}
    second = checks.check_log(lines, 1)
    assert [f.name for f in second] == ["generic.log_error"]
    assert checks.check_log(lines, 2) == []


def test_log_clean_log_has_no_findings():
    assert checks.check_log([], 0) == []


def test_log_line_without_a_level_is_still_a_finding():
    assert [f.name for f in checks.check_log(["something"], 0)] == ["generic.log_problem"]


def test_ui_tree_good_answer_is_quiet():
    assert checks.check_ui_tree({"scene": "Main", "canvases": [laid_out_canvas(), laid_out_canvas("Hud")]}) == []


def test_ui_tree_without_canvases_is_quiet():
    assert checks.check_ui_tree({"scene": "Main", "canvases": []}) == []
    assert checks.check_ui_tree(None) == []
    assert checks.check_ui_tree({"scene": "Main"}) == []


def test_ui_tree_canvas_not_laid_out():
    tree = {"canvases": [laid_out_canvas(), {"node": "Hud", "laidOut": False, "issues": []}]}
    (finding,) = checks.check_ui_tree(tree)
    assert (finding.kind, finding.name) == ("ui_issue", "generic.ui_not_laid_out")
    assert "Hud" in finding.message
    assert finding.expect == {"pointers": {"/canvases/1/laidOut": True}}


def test_ui_tree_one_finding_per_canvas_with_the_issue_text():
    issue = {"kind": "Overlap", "element": "Play", "other": "Quit", "rect": {}}
    tree = {"canvases": [laid_out_canvas("A", [issue]), laid_out_canvas("B"), laid_out_canvas("C", [issue, {"kind": "TooSmall", "element": "Close"}])]}
    findings = checks.check_ui_tree(tree)
    assert [f.details["canvas"] for f in findings] == ["A", "C"]
    assert "Overlap on 'Play' (with 'Quit')" in findings[0].message
    assert "TooSmall on 'Close'" in findings[1].message
    assert findings[1].expect == {"pointers": {"/canvases/2/issues": []}}


def test_version_checks():
    assert checks.check_version(3, 4) == []
    assert checks.check_version(3, 3) == []
    assert checks.check_version(None, 1) == []
    assert checks.check_version(5, None) == []
    (finding,) = checks.check_version(5, 4)
    assert (finding.kind, finding.name, finding.expect) == ("version_back", "generic.version_back", None)
    assert "5" in finding.message and "4" in finding.message


def test_listed_action_that_is_refused_is_a_finding():
    listed = [{"type": "noop"}, {"type": "swap", "a": 1}]
    (finding,) = checks.check_action_accepted({"type": "swap", "a": 1}, listed, "illegal_action")
    assert (finding.kind, finding.name, finding.expect) == ("action_refused", "generic.action_refused", {})
    assert "illegal_action" in finding.message


def test_accepted_or_unlisted_action_is_fine():
    listed = [{"type": "noop"}]
    assert checks.check_action_accepted({"type": "noop"}, listed, None) == []
    assert checks.check_action_accepted({"type": "invented"}, listed, "illegal_action") == []


def test_apply_known_splits_by_name_and_keeps_the_order():
    findings = [Finding("invariant", "a", "1"), Finding("invariant", "k", "2"), Finding("invariant", "b", "3")]
    bugs, known = checks.apply_known(findings, {"k"})
    assert [f.name for f in bugs] == ["a", "b"]
    assert [f.name for f in known] == ["k"]
    assert checks.apply_known(findings, []) == (findings, [])


def test_finding_round_trips_through_a_dict():
    finding = Finding("invariant", "score_decreased", "7 -> 3", 4, {"a": 1}, {"equal": {"x": 1}}, ["shots/01-a.png"])
    assert Finding.from_dict(finding.to_dict()) == finding
    assert finding.at(9).call_index == 9 and finding.call_index == 4


def write_module(tmp_path, text, name="invariants.py"):
    path = tmp_path / name
    path.write_text(text)
    return path


def test_module_with_none_of_the_optional_members(tmp_path):
    module = checks.load_invariants(write_module(tmp_path, "# nothing here\n"))
    assert module.check_state is None and module.check_action is None and module.known_deviations == frozenset()
    assert checks.run_state_checks(module, {"version": 1}, None) == []
    assert checks.run_action_checks(module, {"type": "x"}, {}, None, None) == []
    assert checks.run_state_checks(None, {}, None) == []


def test_module_checks_become_invariant_findings(tmp_path):
    source = """
from bagi_qa_checks import Violation

KNOWN_DEVIATIONS = {"score_decreased"}

def check_state(answer, previous):
    out = []
    if previous is not None and answer["state"]["score"] < previous["state"]["score"]:
        out.append(Violation("score_decreased", "score fell"))
    if answer["state"]["score"] > 99:
        out.append(("too_big", "score over 99"))
    return out

def check_action(action, result, before, after):
    return [("noop_moved", "noop changed the version")] if action["type"] == "noop" and before["version"] != after["version"] else None
"""
    module = checks.load_invariants(write_module(tmp_path, source))
    assert module.known_deviations == frozenset({"score_decreased"})
    old, new = {"version": 1, "state": {"score": 5}}, {"version": 2, "state": {"score": 100}}
    assert checks.run_state_checks(module, old, None) == []
    findings = checks.run_state_checks(module, new, old)
    assert [(f.kind, f.name) for f in findings] == [("invariant", "too_big")]
    fell = checks.run_state_checks(module, {"version": 3, "state": {"score": 1}}, new)
    assert [f.name for f in fell] == ["score_decreased"]
    assert [f.name for f in checks.run_action_checks(module, {"type": "noop"}, {}, old, new)] == ["noop_moved"]
    assert checks.run_action_checks(module, {"type": "swap"}, {}, old, new) == []


def test_raising_module_is_a_finding_not_a_crash(tmp_path):
    source = "def check_state(answer, previous):\n    return answer['missing']\n\ndef check_action(*args):\n    return [1]\n"
    module = checks.load_invariants(write_module(tmp_path, source))
    (state,) = checks.run_state_checks(module, {}, None)
    assert (state.kind, state.name) == ("invariant", "qa.module_error")
    assert "KeyError" in state.message and "check_state" in state.message
    assert state.details["traceback"]
    (action,) = checks.run_action_checks(module, {"type": "x"}, {}, None, None)
    assert action.name == "qa.module_error" and "check_action" in action.message


def test_module_does_not_come_from_sys_path(tmp_path):
    write_module(tmp_path, "VALUE = 1\n", "qa_shadow_probe.py")
    checks.load_invariants(tmp_path / "qa_shadow_probe.py")
    with pytest.raises(ImportError):
        __import__("qa_shadow_probe")
    assert not list(tmp_path.glob("__pycache__"))


@pytest.mark.parametrize(
    "source",
    ["raise RuntimeError('boom')\n", "def broken(:\n", "check_state = 3\n", "KNOWN_DEVIATIONS = 'abc'\n", "KNOWN_DEVIATIONS = {1}\n"],
)
def test_unusable_module_is_an_invariants_error(tmp_path, source):
    with pytest.raises(InvariantsError):
        checks.load_invariants(write_module(tmp_path, source))


def test_missing_module_is_an_invariants_error(tmp_path):
    with pytest.raises(InvariantsError, match="cannot read"):
        checks.load_invariants(tmp_path / "nope.py")


MOVES = [checks.Move("step", "step", {"frames": 1}, "step 1"), checks.Move("action", "perform_action", {"type": "inc"}, "inc")]


def test_a_module_with_monkey_moves_is_loaded_and_its_list_is_used(tmp_path):
    source = """
from bagi_qa_checks import Move

def monkey_moves(answer, moves):
    return [move for move in moves if move.kind != "step"] + [Move("reset", "reset", {"count": answer["state"]["count"]}, "reset")]
"""
    module = checks.load_invariants(write_module(tmp_path, source))
    found, findings = checks.run_monkey_moves(module, {"state": {"count": 4}}, MOVES)
    assert findings == [] and [move.kind for move in found] == ["action", "reset"] and found[1].params == {"count": 4}


def test_a_module_without_monkey_moves_leaves_the_list(tmp_path):
    module = checks.load_invariants(write_module(tmp_path, "# nothing\n"))
    assert module.monkey_moves is None
    assert checks.run_monkey_moves(module, {}, MOVES) == (MOVES, [])
    assert checks.run_monkey_moves(None, {}, MOVES) == (MOVES, [])


def test_monkey_moves_is_given_a_copy_it_may_change(tmp_path):
    module = checks.load_invariants(write_module(tmp_path, "def monkey_moves(answer, moves):\n    moves.clear()\n    return moves\n"))
    found, _ = checks.run_monkey_moves(module, {}, MOVES)
    assert found == [] and len(MOVES) == 2


@pytest.mark.parametrize("returned", ["None", "[('step', 'step', {}, '')]", "(Move('a', 'b', {}),)", "[Move('a', 'b', {}), 3]"])
def test_a_raising_or_wrong_monkey_moves_is_a_module_error_and_the_list_survives(tmp_path, returned):
    source = f"from bagi_qa_checks import Move\ndef monkey_moves(answer, moves):\n    return {returned}\n"
    module = checks.load_invariants(write_module(tmp_path, source))
    found, (finding,) = checks.run_monkey_moves(module, {}, MOVES)
    assert found == MOVES and (finding.kind, finding.name) == ("invariant", "qa.module_error") and "monkey_moves" in finding.message


def test_a_raising_monkey_moves_has_its_traceback(tmp_path):
    module = checks.load_invariants(write_module(tmp_path, "def monkey_moves(answer, moves):\n    return answer['missing']\n"))
    found, (finding,) = checks.run_monkey_moves(module, {}, MOVES)
    assert found == MOVES and "KeyError" in finding.message and finding.details["traceback"]


def test_monkey_moves_that_is_not_a_function_is_an_invariants_error(tmp_path):
    with pytest.raises(InvariantsError, match="monkey_moves"):
        checks.load_invariants(write_module(tmp_path, "monkey_moves = 3\n"))
