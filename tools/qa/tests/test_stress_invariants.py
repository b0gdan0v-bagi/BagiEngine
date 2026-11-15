"""tools/stress/qa/invariants.py on state dicts, with no game."""

from pathlib import Path

import pytest

from bagi_qa_checks import load_invariants

INVARIANTS = Path(__file__).resolve().parents[2] / "stress" / "qa" / "invariants.py"
SCENES = ["sprites", "entities", "ui"]


@pytest.fixture(scope="module")
def module():
    return load_invariants(INVARIANTS)


def answer(version=1, scene="sprites", count=500, language="en", max_count=1_000_000):
    return {"game": "Stress", "version": version, "state": {"scene": scene, "count": count, "language": language, "maxCount": max_count, "scenes": SCENES}}


def names(violations):
    return [violation.name for violation in violations]


def test_the_module_loads_with_both_checks_and_no_deviation(module):
    assert module.check_state is not None and module.check_action is not None
    assert module.known_deviations == frozenset()


def test_a_good_state_has_no_violation(module):
    assert module.check_state(answer(), None) == []
    assert module.check_state(answer(count=0), None) == []
    assert module.check_state(answer(count=1_000_000, language="ru", scene="ui"), None) == []


@pytest.mark.parametrize(
    "bad, expected",
    [
        (answer(scene="three"), "scene_unknown"),
        (answer(count=-1), "count_out_of_range"),
        (answer(count=1_000_001), "count_out_of_range"),
        (answer(count=2.5), "count_out_of_range"),
        (answer(count=True), "count_out_of_range"),
        (answer(language="de"), "language_unknown"),
        ({"game": "Stress", "version": 1}, "state_shape"),
    ],
)
def test_a_state_out_of_range_is_named(module, bad, expected):
    assert expected in names(module.check_state(bad, None))


def test_the_version_follows_the_state(module):
    first = answer(version=3)
    assert module.check_state(answer(version=3), first) == []
    assert module.check_state(answer(version=4, count=250), first) == []
    assert names(module.check_state(answer(version=4), first)) == ["version_moved_without_change"]
    assert names(module.check_state(answer(version=3, count=250), first)) == ["version_still_on_change"]


def test_an_action_shows_what_was_asked(module):
    before, after = answer(version=1), answer(version=2, count=100)
    action = {"type": "set_count", "count": 100}
    shown = {"game": "Stress", "version": 2, "outcome": {"scene": "sprites", "count": 100, "language": "en"}}
    assert module.check_action(action, shown, before, after) == []
    assert names(module.check_action(action, shown, before, answer(version=2, count=99))) == ["set_count_not_shown", "action_answer_differs"]
    assert names(module.check_action(action, {**shown, "outcome": {**shown["outcome"], "count": 99}}, before, after)) == ["action_answer_differs"]


@pytest.mark.parametrize(
    "action, after",
    [
        ({"type": "set_scene", "scene": "ui"}, answer(version=2, scene="ui")),
        ({"type": "set_language", "language": "ru"}, answer(version=2, language="ru")),
    ],
)
def test_scene_and_language_actions_are_checked_the_same_way(module, action, after):
    before = answer(version=1)
    result = {"outcome": {name: after["state"][name] for name in ("scene", "count", "language")}}
    assert module.check_action(action, result, before, after) == []
    wrong = answer(version=2)
    assert names(module.check_action(action, result, before, wrong))[0] == f"{action['type']}_not_shown"


def test_an_action_that_changes_nothing_is_fine_and_a_refused_one_must_change_nothing(module):
    state = answer(version=5)
    same = {"outcome": {"scene": "sprites", "count": 500, "language": "en"}}
    assert module.check_action({"type": "set_count", "count": 500}, same, state, answer(version=5)) == []
    assert module.check_action({"type": "set_count", "count": 0}, None, state, answer(version=5)) == []
    assert names(module.check_action({"type": "set_count", "count": 0}, None, state, answer(version=6, count=0))) == ["refused_action_changed_state"]
