"""The moves that tools/stress/qa/invariants.py adds on the scene `rmlui`, on state dicts (no game)."""

from pathlib import Path

import pytest

from bagi_monkey_moves import KIND_ACTION, KIND_CLICK, KIND_RESET, KIND_STEP, PERFORM_ACTION_METHOD, STEP_METHOD
from bagi_qa_checks import Move, load_invariants

INVARIANTS = Path(__file__).resolve().parents[2] / "stress" / "qa" / "invariants.py"
SCENES = ["sprites", "entities", "ui", "rmlui"]
OUTPUT_WIDTH = 1920  # the reference size of the scene's documents, which is the output of a headless run
OUTPUT_HEIGHT = 1080


@pytest.fixture(scope="module")
def module():
    return load_invariants(INVARIANTS)


def answer(scene, scenes=SCENES):
    return {"game": "Stress", "version": 1, "state": {"scene": scene, "count": 20, "language": "en", "maxCount": 1_000_000, "scenes": scenes}}


def set_count(count):
    return Move(KIND_ACTION, PERFORM_ACTION_METHOD, {"type": "set_count", "count": count}, "set_count")


def rmlui_moves(moves):
    return [move for move in moves if move.method in ("wheel_at", "call_method")]


@pytest.mark.parametrize("scene", ["sprites", "entities", "ui"])
def test_no_move_of_the_scene_is_offered_elsewhere(module, scene):
    assert rmlui_moves(module.monkey_moves(answer(scene), [])) == []


def test_the_wheel_and_the_reload_are_offered_on_the_scene(module):
    moves = rmlui_moves(module.monkey_moves(answer("rmlui"), []))
    assert all(move.kind == KIND_CLICK for move in moves)
    wheels = [move for move in moves if move.method == "wheel_at"]
    reloads = [move for move in moves if move.method == "call_method"]
    assert [move.params for move in reloads] == [{"root": "game", "method": "ReloadRmlUiDocuments"}]
    # Both axes, both signs, and the flipped direction, each once
    assert len({(move.params["dx"], move.params["dy"], move.params["flipped"]) for move in wheels}) == len(wheels)
    assert {(move.params["dx"] != 0, move.params["dy"] != 0) for move in wheels} == {(True, False), (False, True)}
    assert {move.params["flipped"] for move in wheels} == {True, False}


def test_the_wheel_is_over_the_panel_and_inside_the_output(module):
    for move in rmlui_moves(module.monkey_moves(answer("rmlui"), [])):
        if move.method == "wheel_at":
            assert 0 <= move.params["x"] < OUTPUT_WIDTH and 0 <= move.params["y"] < OUTPUT_HEIGHT
            # the panel is in the lower right corner of the output
            assert move.params["x"] > OUTPUT_WIDTH / 2 and move.params["y"] > OUTPUT_HEIGHT / 2


def test_the_moves_are_unique_and_sorted_as_the_runner_needs(module):
    moves = module.monkey_moves(answer("rmlui"), [set_count(1), set_count(1)])
    keys = [(move.kind, move.method, sorted(move.params.items())) for move in moves]
    assert len(moves) == len(set(map(repr, keys)))
    assert [(move.kind, move.method) for move in moves] == sorted((move.kind, move.method) for move in moves)


def test_the_generic_moves_are_kept_and_the_cap_applies_on_the_scene(module):
    step = Move(KIND_STEP, STEP_METHOD, {"frames": 1}, "step 1")
    moves = module.monkey_moves(answer("rmlui"), [set_count(5), set_count(100), set_count(101), step])
    counts = sorted(move.params["count"] for move in moves if move.params.get("type") == "set_count")
    assert counts == [5, 100]
    assert step in moves


def test_a_reset_to_the_scene_is_still_added(module):
    moves = module.monkey_moves(answer("sprites"), [])
    resets = [move for move in moves if move.kind == KIND_RESET]
    assert sorted(move.params["scene"] for move in resets) == sorted(SCENES)
