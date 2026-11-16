"""What the monkey may do next: pure functions over control answers, with no process and no I/O.

    moves = candidate_moves(session.listed_actions(), tree, methods, start, kinds)
    moves, findings = run_monkey_moves(invariants, state, moves)    # the game's own say (bagi_qa_checks)
    move = pick(random.Random(seed), moves)

The walk has to depend only on the seed and the game's answers (`tools/qa/CLAUDE.md`, "The monkey"): the candidates are sorted before a pick and
`pick` uses `rng.random()` and integer arithmetic, because `choice` and `choices` change between Python releases. Standard library only.
"""

import json
import random
import sys
from pathlib import Path
from typing import Any, Collection, Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bagi_qa_checks import Move  # noqa: E402

KIND_ACTION = "action"
KIND_CLICK = "click"
KIND_STEP = "step"
KIND_RESET = "reset"
KIND_WINDOW = "window"
KINDS = (KIND_ACTION, KIND_CLICK, KIND_STEP, KIND_RESET, KIND_WINDOW)
WEIGHTS = {KIND_ACTION: 50, KIND_CLICK: 30, KIND_STEP: 15, KIND_RESET: 1, KIND_WINDOW: 1}  # a kind's share of the picks, however many moves it has

PERFORM_ACTION_METHOD = "perform_action"
UI_CLICK_METHOD = "ui_click"
STEP_METHOD = "step"
RESET_METHOD = "reset"
SET_WINDOW_METHOD = "set_window"

STEP_FRAMES = (1, 2, 5, 30)
SLIDER_POINTS = (0.0, 0.25, 0.5, 0.75, 1.0)  # `at` of a click on a slider
WINDOW_MATRIX = (  # (width, height)
    (1080, 2400),  # phone portrait
    (2400, 1080),  # phone landscape
    (2560, 1080),  # 21:9
    (1080, 1080),  # a square
    (1920, 1080),  # the default window
)
SLIDER_COMPONENT = "UISliderComponent"
_RANDOM_BITS = 53  # `random()` is a multiple of 2**-53, so scaling it by 2**53 is exact


def candidate_moves(
    listed_actions: Iterable[dict[str, Any]],
    ui_tree: Any,
    methods: Collection[str],
    start_params: dict[str, Any] | None,
    kinds: Collection[str] | None = None,
) -> list[Move]:
    """Every move the generic walk may make now, sorted by `(kind, method, canonical JSON of params)` and without duplicates.

    `listed_actions` are the answer of `list_actions`, `ui_tree` the answer of `ui_tree` (or None), `methods` the names `list_methods` gives;
    `click` needs `ui_click` in it, `window` needs `set_window`, `reset` needs `reset`. `kinds` limits the kinds (None: all).
    """
    allowed = frozenset(KINDS if kinds is None else kinds)
    found: list[Move] = []
    if KIND_ACTION in allowed:
        found += [Move(KIND_ACTION, PERFORM_ACTION_METHOD, dict(action), str(action.get("type", ""))) for action in listed_actions if isinstance(action, dict)]
    if KIND_CLICK in allowed and UI_CLICK_METHOD in methods:
        found += _click_moves(ui_tree)
    if KIND_STEP in allowed:
        found += [Move(KIND_STEP, STEP_METHOD, {"frames": frames}, f"step {frames}") for frames in STEP_FRAMES]
    if KIND_RESET in allowed and RESET_METHOD in methods:
        found.append(Move(KIND_RESET, RESET_METHOD, dict(start_params or {}), "reset"))
    if KIND_WINDOW in allowed and SET_WINDOW_METHOD in methods:
        found += [Move(KIND_WINDOW, SET_WINDOW_METHOD, {"width": width, "height": height}, f"window {width}x{height}") for width, height in WINDOW_MATRIX]
    return _sorted_unique(found)


def pick(rng: random.Random, moves: Sequence[Move]) -> Move:
    """One of `moves`: a kind is drawn by `WEIGHTS` (only the kinds present), then a move of it evenly; both from one `rng.random()` and integers.

    A kind that `WEIGHTS` does not name, or names with 0, is never picked. ValueError when nothing can be picked. The list is taken in the
    order given, so the caller sorts it (`candidate_moves` does).
    """
    weighted = [(kind, WEIGHTS[kind]) for kind in KINDS if WEIGHTS[kind] > 0 and any(move.kind == kind for move in moves)]
    total = sum(weight for _, weight in weighted)
    if total == 0:
        raise ValueError("no move to pick from")
    scaled = int(rng.random() * (1 << _RANDOM_BITS)) * total  # below total << _RANDOM_BITS
    ticket = scaled >> _RANDOM_BITS
    fraction = scaled & ((1 << _RANDOM_BITS) - 1)  # what is left of the draw, below 1 << _RANDOM_BITS
    for kind, weight in weighted:
        if ticket < weight:
            break
        ticket -= weight
    of_kind = [move for move in moves if move.kind == kind]
    return of_kind[(fraction * len(of_kind)) >> _RANDOM_BITS]


def _click_moves(ui_tree: Any) -> list[Move]:
    canvases = ui_tree.get("canvases") if isinstance(ui_tree, dict) else None
    moves: list[Move] = []
    for canvas in canvases if isinstance(canvases, list) else []:
        if not isinstance(canvas, dict) or not canvas.get("visible", True) or not canvas.get("node"):
            continue
        for element in canvas.get("elements", []):
            if not _clickable(element):
                continue
            base = {"name": element["name"], "canvas": canvas["node"]}
            if _is_slider(element):
                moves += [Move(KIND_CLICK, UI_CLICK_METHOD, {**base, "at": point}, f"click {element['name']} at {point}") for point in SLIDER_POINTS]
            else:
                moves.append(Move(KIND_CLICK, UI_CLICK_METHOD, base, f"click {element['name']}"))
    return moves


def _clickable(element: Any) -> bool:
    return isinstance(element, dict) and bool(element.get("name")) and bool(element.get("visible")) and bool(element.get("interactive")) and not element.get("disabled")


def _is_slider(element: dict[str, Any]) -> bool:
    return SLIDER_COMPONENT in element.get("components", [])


def _sorted_unique(moves: Iterable[Move]) -> list[Move]:
    keyed = {(move.kind, move.method, _canonical(move.params)): move for move in moves}
    return [keyed[key] for key in sorted(keyed)]


def _canonical(params: dict[str, Any]) -> str:
    return json.dumps(params, sort_keys=True, separators=(",", ":"))
