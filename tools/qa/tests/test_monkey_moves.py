import json
import random

import pytest

import bagi_monkey_moves as moves_module
from bagi_monkey_moves import KINDS, SLIDER_POINTS, STEP_FRAMES, WINDOW_MATRIX, candidate_moves, pick, usable_moves
from bagi_qa_checks import Move

ALL_METHODS = {"perform_action", "ui_click", "set_window", "reset", "step"}
LISTED = [{"type": "inc"}, {"type": "noop"}]


def element(name, **fields):
    return {"name": name, "type": "UIElement", "visible": True, "interactive": True, **fields}


def tree(*canvases):
    return {"scene": "Main", "canvases": list(canvases)}


HUD = {
    "node": "Hud",
    "visible": True,
    "elements": [
        element("Plus"),
        element("Hidden", visible=False),
        element("Locked", disabled=True),
        element("", type="UIElement"),
        element("Label", interactive=False),
        element("Sound", components=["UIToggleComponent"], checked=True),
        element("Volume", components=["UISliderComponent"], fraction=0.5),
    ],
}
MENU = {"node": "Menu", "visible": True, "elements": [element("Plus"), element("Quit")]}
GONE = {"node": "Old", "visible": False, "elements": [element("Stale")]}


def clicks(found):
    return [move.params for move in found if move.kind == "click"]


def test_a_tree_gives_exactly_the_clickable_elements_with_their_canvas():
    found = candidate_moves(LISTED, tree(HUD, MENU, GONE), ALL_METHODS, None)
    sliders = [{"name": "Volume", "canvas": "Hud", "at": point} for point in SLIDER_POINTS]
    expected = [{"name": "Plus", "canvas": "Hud"}, {"name": "Plus", "canvas": "Menu"}, {"name": "Quit", "canvas": "Menu"}, {"name": "Sound", "canvas": "Hud"}, *sliders]
    assert sorted(clicks(found), key=json.dumps) == sorted(expected, key=json.dumps)
    assert all(move.method == "ui_click" for move in found if move.kind == "click")


def test_the_list_is_sorted_and_the_same_for_a_shuffled_input():
    found = candidate_moves(LISTED, tree(HUD, MENU), ALL_METHODS, {"count": 3})
    keys = [(move.kind, move.method, json.dumps(move.params, sort_keys=True, separators=(",", ":"))) for move in found]
    assert keys == sorted(keys) and len(set(keys)) == len(keys)
    shuffled_tree = tree(MENU, {**HUD, "elements": list(reversed(HUD["elements"]))})
    assert candidate_moves(list(reversed(LISTED)), shuffled_tree, ALL_METHODS, {"count": 3}) == found


def test_every_kind_is_offered_with_its_params():
    found = candidate_moves(LISTED, tree(MENU), ALL_METHODS, {"scene": "ui", "count": 5})
    assert {move.kind for move in found} == set(KINDS)
    assert [move.params for move in found if move.kind == "action"] == LISTED
    assert {move.params["frames"] for move in found if move.kind == "step"} == set(STEP_FRAMES)
    assert [move.params for move in found if move.kind == "reset"] == [{"scene": "ui", "count": 5}]
    assert {(move.params["width"], move.params["height"]) for move in found if move.kind == "window"} == set(WINDOW_MATRIX)


def test_the_window_matrix_holds_the_shapes_the_entry_names():
    assert (1920, 1080) in WINDOW_MATRIX and len(set(WINDOW_MATRIX)) == len(WINDOW_MATRIX) == 5
    assert any(height > width for width, height in WINDOW_MATRIX) and any(width * 9 >= height * 21 for width, height in WINDOW_MATRIX) and any(width == height for width, height in WINDOW_MATRIX)


def test_a_method_the_game_does_not_list_gives_no_move_of_its_kind():
    kinds = lambda methods: {move.kind for move in candidate_moves(LISTED, tree(MENU), methods, None)}  # noqa: E731
    assert kinds(ALL_METHODS - {"ui_click"}) == {"action", "step", "reset", "window"}
    assert kinds(ALL_METHODS - {"set_window"}) == {"action", "click", "step", "reset"}
    assert kinds(ALL_METHODS - {"reset"}) == {"action", "click", "step", "window"}
    assert kinds(set()) == {"action", "step"}


def test_kinds_limit_the_candidates():
    found = candidate_moves(LISTED, tree(MENU), ALL_METHODS, None, {"click", "step"})
    assert {move.kind for move in found} == {"click", "step"}


def test_no_tree_and_a_malformed_tree_give_no_click():
    for bad in (None, {}, {"canvases": 3}, {"canvases": [None, {"node": "A", "elements": [5]}]}):
        assert clicks(candidate_moves([], bad, ALL_METHODS, None)) == []


def draw(seed, moves, count=20):
    rng = random.Random(seed)
    return [pick(rng, moves) for _ in range(count)]


def test_pick_gives_the_same_sequence_for_a_seed_and_another_for_a_seed_more():
    found = candidate_moves(LISTED, tree(HUD, MENU), ALL_METHODS, None)
    assert draw(7, found) == draw(7, found)
    assert draw(7, found) != draw(8, found)


def test_pick_follows_the_weights():
    found = candidate_moves(LISTED, tree(MENU), ALL_METHODS, None)
    counts = {kind: 0 for kind in KINDS}
    for move in draw(3, found, 4000):
        counts[move.kind] += 1
    assert counts["action"] > counts["click"] > counts["step"] > counts["reset"] + counts["window"] > 0


def test_a_weight_of_zero_is_never_picked(monkeypatch):
    monkeypatch.setitem(moves_module.WEIGHTS, "step", 0)
    found = candidate_moves(LISTED, tree(MENU), ALL_METHODS, None)
    assert {move.kind for move in draw(1, found, 500)} == {"action", "click", "reset", "window"}


def test_usable_moves_drop_what_cannot_be_weighed_or_is_excluded_and_sort_the_rest():
    odd = Move("dance", "dance", {})
    nine = Move("step", "step", {"frames": 9})
    moves = [Move("step", "step", {"frames": 2}), odd, Move("action", "perform_action", {"type": "inc"}), Move("step", "step", {"frames": 2}), nine]
    assert usable_moves(moves) == [moves[2], moves[0], nine]
    assert usable_moves(moves, ["step"]) == [moves[0], nine]
    assert usable_moves([odd]) == [] and usable_moves(reversed(moves)) == usable_moves(moves)


def test_pick_covers_every_move_of_a_kind_and_fails_on_nothing():
    only_steps = candidate_moves([], None, set(), None)
    assert {move.params["frames"] for move in draw(2, only_steps, 200)} == set(STEP_FRAMES)
    with pytest.raises(ValueError):
        pick(random.Random(0), [])
    with pytest.raises(ValueError):
        pick(random.Random(0), [Move("custom", "x", {})])


def test_pick_does_not_depend_on_the_extremes_of_random():
    class Fixed:
        def __init__(self, value):
            self.value = value

        def random(self):
            return self.value

    found = candidate_moves(LISTED, tree(MENU), ALL_METHODS, None)
    assert pick(Fixed(0.0), found) == found[0]
    assert pick(Fixed(1 - 2**-53), found).kind == "window"
