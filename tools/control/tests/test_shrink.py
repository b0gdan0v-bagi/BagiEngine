"""The shrinker (scenario_shrink.py) as a pure function, and bagi_play.py --shrink against the fake game."""

import json
import random

import pytest

import scenario
from bagi_play import EXIT_FAILED, EXIT_PASSED, EXIT_USAGE, main
from scenario_shrink import shrink


def needing(*required):
    """A predicate that fails when all of `required` are present (in any order)."""
    return lambda candidate: all(item in candidate for item in required)


def test_a_failure_needing_two_calls_leaves_exactly_them_in_order():
    items = list(range(20))
    assert shrink(items, needing(4, 13)) == [4, 13]


def test_a_failure_needing_one_call_leaves_that_call():
    assert shrink(list(range(9)), needing(6)) == [6]


def test_a_failure_needing_every_call_leaves_all_of_them():
    items = list(range(7))
    assert shrink(items, lambda candidate: len(candidate) == len(items)) == items


def test_nothing_removable_in_a_single_item_list():
    assert shrink(["only"], needing("only")) == ["only"]


def test_an_empty_input_that_fails_is_returned_empty():
    assert shrink([], lambda candidate: True) == []


def test_a_failure_that_needs_no_call_leaves_the_empty_list():
    assert shrink([1, 2, 3], lambda candidate: True) == []


def test_a_passing_start_is_refused():
    with pytest.raises(ValueError, match="nothing to shrink"):
        shrink([1, 2, 3], lambda candidate: False)


def test_the_result_is_one_minimal_for_a_predicate_with_several_ways_to_fail():
    # Fails when it holds a 3 and (a 5 or a 7): either pair is a valid answer, and neither may be reducible further
    def fails(candidate):
        return 3 in candidate and (5 in candidate or 7 in candidate)

    result = shrink(list(range(12)), fails)
    assert fails(result)
    assert all(not fails(result[:index] + result[index + 1 :]) for index in range(len(result)))
    assert len(result) == 2


def test_the_probe_count_is_far_below_the_quadratic_bound_for_64_items():
    count = 0

    def fails(candidate):
        nonlocal count
        count += 1
        return 10 in candidate and 50 in candidate

    assert shrink(list(range(64)), fails) == [10, 50]
    assert count < 64 * 64 // 8


def test_max_probes_stops_early_and_returns_the_best_so_far():
    count = 0

    def fails(candidate):
        nonlocal count
        count += 1
        return 3 in candidate and 40 in candidate

    result = shrink(list(range(64)), fails, max_probes=6)
    assert count == 6
    assert 3 in result and 40 in result and len(result) < 64
    assert shrink(list(range(64)), needing(3, 40), max_probes=1) == list(range(64))  # the check of the start used the only probe


def test_the_input_is_not_changed_and_the_predicate_gets_fresh_lists():
    items = list(range(10))
    seen = []

    def fails(candidate):
        seen.append(candidate)
        candidate.append("scribble")  # a predicate that changes its argument must not corrupt the shrinker
        return 2 in candidate and 8 in candidate

    assert shrink(items, fails) == [2, 8]
    assert items == list(range(10))
    assert all(a is not b for index, a in enumerate(seen) for b in seen[index + 1 :])


def test_the_result_is_deterministic():
    items = list(range(30))
    predicate = needing(1, 17, 29)
    assert shrink(items, predicate) == shrink(items, predicate) == [1, 17, 29]


def test_random_failures_shrink_to_exactly_the_needed_items():
    generator = random.Random(7)
    for _ in range(40):
        items = list(range(generator.randint(1, 40)))
        required = generator.sample(items, generator.randint(0, min(4, len(items))))
        assert shrink(items, needing(*required)) == sorted(required)


# --- bagi_play.py --shrink ---

HEADER = {"scenario": 1, "game": "Fake", "flags": ["--headless", "--manual-clock"]}


def write(tmp_path, *lines, name="long.jsonl"):
    path = tmp_path / name
    path.write_text("\n".join(json.dumps(line) for line in [HEADER, *lines]) + "\n", encoding="utf-8")
    return path


def test_shrink_writes_the_shortest_scenario_that_still_fails(fake_game_exe, tmp_path, capsys):
    bad = {"method": "perform_action", "params": {"type": "bad"}, "expect": {"error": "game_over"}}
    path = write(
        tmp_path,
        {"method": "get_state", "expect": {"equal": {"game": "Fake"}}},
        {"method": "perform_action", "params": {"type": "noop"}, "expect": {"versionStep": 1}},
        {"method": "step", "params": {"frames": 2}},
        {"method": "perform_action", "params": {"type": "noop"}, "expect": {"versionStep": 1}},
        {"method": "list_actions"},
        bad,
        {"method": "get_state"},
        {"method": "screenshot"},
        {"method": "step"},
        {"method": "get_state", "expect": {"versionStep": 0}},
    )
    out = tmp_path / "short.jsonl"
    assert main([str(fake_game_exe), str(path), "--shrink", str(out)]) == EXIT_PASSED
    output = capsys.readouterr().out
    assert "10 calls" in output and "1 call" in output and "probes" in output
    header, steps = scenario.load(out)
    assert header.game == "Fake" and header.flags == HEADER["flags"]
    assert [(step.method, step.params, step.expect) for step in steps] == [("perform_action", {"type": "bad"}, {"error": "game_over"})]
    assert main([str(fake_game_exe), str(out)]) == EXIT_FAILED  # and the short file fails the same way


def test_shrink_of_a_passing_scenario_exits_two_and_writes_nothing(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "get_state"})
    out = tmp_path / "short.jsonl"
    assert main([str(fake_game_exe), str(path), "--shrink", str(out)]) == EXIT_USAGE
    assert "nothing to shrink" in capsys.readouterr().out
    assert not out.exists()


def test_shrink_keeps_the_calls_a_failure_depends_on(fake_game_exe, tmp_path):
    # The fake starts at version 1 and every noop raises it: the last line fails only after some noop ran, so one noop has to stay
    path = write(
        tmp_path,
        {"method": "get_state"},
        {"method": "perform_action", "params": {"type": "noop"}},
        {"method": "step"},
        {"method": "perform_action", "params": {"type": "noop"}},
        {"method": "get_state", "expect": {"equal": {"version": 1}}},
    )
    out = tmp_path / "short.jsonl"
    assert main([str(fake_game_exe), str(path), "--shrink", str(out), "--max-probes", "50"]) == EXIT_PASSED
    _header, steps = scenario.load(out)
    assert [step.method for step in steps] == ["perform_action", "get_state"]
    assert steps[1].expect == {"equal": {"version": 1}}


def test_shrink_and_update_exclude_each_other(fake_game_exe, tmp_path):
    path = write(tmp_path, {"method": "get_state"})
    with pytest.raises(SystemExit) as refusal:
        main([str(fake_game_exe), str(path), "--shrink", str(tmp_path / "o.jsonl"), "--update"])
    assert refusal.value.code == EXIT_USAGE
