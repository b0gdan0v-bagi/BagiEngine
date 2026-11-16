"""The monkey on BagiStress: the game's `monkey_moves` on state dicts (no game), then a short walk on the built game.

The walk is skipped when BagiStress is not built, as `test_stress_session.py` is.
"""

import pytest

from bagi_monkey import run_monkey
from bagi_monkey_moves import KIND_ACTION, KIND_RESET, KIND_STEP, PERFORM_ACTION_METHOD, STEP_METHOD
from bagi_qa_checks import Move, load_invariants
from test_stress_session import GAME, INVARIANTS, built_executable

SCENES = ["sprites", "entities", "ui"]
WALK_SEED = 2  # a reset is rare (weight 1): this seed draws one within the moves
WALK_MOVES = 40


def answer(scenes=SCENES):
    return {"game": "Stress", "version": 1, "state": {"scene": "sprites", "count": 500, "language": "en", "maxCount": 1_000_000, "scenes": scenes}}


def set_count(count):
    return Move(KIND_ACTION, PERFORM_ACTION_METHOD, {"type": "set_count", "count": count}, "set_count")


@pytest.fixture(scope="module")
def module():
    return load_invariants(INVARIANTS)


@pytest.fixture(scope="module")
def stress_exe():
    exe = built_executable()
    if exe is None:
        pytest.skip(f"{GAME} is not built")
    return exe


def test_set_count_above_the_cap_is_dropped_and_the_rest_stays(module):
    scene = Move(KIND_ACTION, PERFORM_ACTION_METHOD, {"type": "set_scene", "scene": "ui"}, "set_scene")
    step = Move(KIND_STEP, STEP_METHOD, {"frames": 1}, "step 1")
    kept = module.monkey_moves(answer(), [set_count(1), set_count(100), set_count(101), set_count(2000), scene, step])
    counts = sorted(move.params["count"] for move in kept if move.params.get("type") == "set_count")
    assert counts == [1, 100]
    assert scene in kept and step in kept


def test_a_small_reset_is_added_for_each_scene_and_nothing_else(module):
    kept = module.monkey_moves(answer(), [])
    assert [(move.kind, move.method, move.params) for move in kept] == [(KIND_RESET, "reset", {"scene": scene, "count": 20}) for scene in sorted(SCENES)]


def test_without_scenes_the_moves_are_only_filtered(module):
    moves = [set_count(7), set_count(5000), set_count(1)]
    assert [move.params["count"] for move in module.monkey_moves(answer(scenes=[]), moves)] == [1, 7]
    assert module.monkey_moves({"state": {}}, moves) == module.monkey_moves(answer(scenes=[]), moves)


def test_a_walk_on_the_real_game_finds_nothing_and_covers_action_step_and_reset(stress_exe, tmp_path):
    result = run_monkey(stress_exe, WALK_SEED, tmp_path / "out", steps=WALK_MOVES, invariants=INVARIANTS)
    assert (result.stopped, [bug.name for bug in result.bugs]) == ("steps", [])
    assert {KIND_ACTION, KIND_STEP, KIND_RESET} <= set(result.per_kind)
