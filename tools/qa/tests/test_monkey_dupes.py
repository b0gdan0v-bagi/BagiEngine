import pytest

import bagi_qa_report as report_module
import scenario
from bagi_monkey_dupes import SIMILAR_SHARE, Signature, signature, signature_of_file, similar
from bagi_qa import QaSession

HEADER = scenario.Header(1, "Fake", ["--headless", "--manual-clock"])
FINDING = "generic.log_warning"


def call(method, **params):
    return scenario.Step(method, params or None)


def jsonl(steps, finding=FINDING):
    """A scenario as `write_repro` writes it: the note of the last line names the finding."""
    if finding is not None:
        steps = [*steps[:-1], scenario.Step(steps[-1].method, steps[-1].params, steps[-1].expect, f"QA finding {finding}: something odd happened")]
    return scenario.dumps(HEADER, steps)


def plan(steps, finding=FINDING):
    return (
        f"# Reproduction of bug-x\n\nThe finding `{finding}` of the game `Fake` shows when the scenario below is played.\n\n"
        f"```{report_module.REPRO_FENCE}\n{scenario.dumps(HEADER, steps).rstrip()}\n```\n"
    )


COUNTS = [call("perform_action", type="set_scene", scene="ui"), call("perform_action", type="set_count", count=40), call("step", frames=2), call("get_state")]


def with_count(count):
    return [COUNTS[0], call("perform_action", type="set_count", count=count), *COUNTS[2:]]


def test_the_signature_names_the_finding_and_one_string_per_call():
    steps = [
        call("perform_action", type="inc"),
        call("ui_click", name="Plus", canvas="Hud"),
        call("set_window", width=1080, height=2400),
        call("step", frames=2),
    ]
    found = signature(jsonl(steps))
    assert found.finding == FINDING
    assert found.calls == ("perform_action:inc", "ui_click:Plus", "set_window:1080x2400", "step")
    assert found.last == '{"method":"step","params":{"frames":2}}'


def test_a_jsonl_and_a_plan_with_the_same_scenario_have_one_signature(tmp_path):
    steps = with_count(40)
    file = tmp_path / "repro.jsonl"
    file.write_text(jsonl(steps), encoding="utf-8")
    assert signature(plan(steps)) == signature(jsonl(steps)) == signature_of_file(file)


def test_a_plan_made_by_make_drafts_is_read(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    session = QaSession(out_dir, "dupes")
    session.launch(exe=str(qa_game_exe))
    session.act({"type": "inc"})
    session.act({"type": "noop"})
    (draft,) = session.finish().drafts
    found = signature((draft.parent / f"{draft.stem}-repro.md").read_text(encoding="utf-8"))
    assert found.finding == FINDING
    assert "perform_action:inc" in found.calls and "perform_action:noop" in found.calls and found.last


def test_the_same_finding_with_other_counts_is_similar():
    assert similar(signature(jsonl(with_count(40))), signature(jsonl(with_count(900))))


def test_the_same_finding_with_unrelated_calls_and_another_last_call_is_not():
    other = [call("perform_action", type="inc"), call("ui_click", name="Plus"), call("set_window", width=1, height=2), call("reset", count=1)]
    assert not similar(signature(jsonl(COUNTS)), signature(jsonl(other)))


def test_another_finding_name_with_the_same_calls_is_not_similar():
    assert not similar(signature(jsonl(COUNTS)), signature(jsonl(COUNTS, "generic.log_error")))


def test_the_same_last_call_alone_is_enough():
    other = [call("perform_action", type="inc"), call("perform_action", type="noop"), COUNTS[-1]]
    assert similar(signature(jsonl(COUNTS)), signature(jsonl(other)))


def test_the_share_is_of_the_longer_list_and_a_multiset():
    first = Signature(FINDING, ("a", "a", "b", "c", "d"), "x")
    assert similar(first, Signature(FINDING, ("a", "a", "b", "z", "y"), "y"))  # 3 of 5
    assert not similar(first, Signature(FINDING, ("a", "a", "b", "z", "y", "w"), "y"))  # 3 of 6
    assert not similar(first, Signature(FINDING, ("a", "z", "y", "w", "v"), "y"))  # 1 of 5
    assert SIMILAR_SHARE == pytest.approx(0.6)


def test_a_signature_without_a_finding_name_is_never_similar():
    nameless = signature(jsonl(COUNTS, None))
    assert nameless.finding is None and nameless.calls
    assert not similar(nameless, nameless)
    assert not similar(nameless, signature(jsonl(COUNTS)))
    assert not similar(signature(jsonl(COUNTS)), nameless)


def test_a_text_that_is_not_a_scenario_is_a_value_error(tmp_path):
    for text in ("", "# just a markdown note\n", "[1, 2]\n"):
        with pytest.raises(ValueError):
            signature(text)
    with pytest.raises(ValueError):
        signature("# plan\n\n```jsonl\nnot json\n```\n")
    with pytest.raises(ValueError, match="cannot read"):
        signature_of_file(tmp_path / "missing.jsonl")
