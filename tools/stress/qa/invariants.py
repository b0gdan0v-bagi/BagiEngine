"""The invariants of BagiStress for tools/qa (the contract is in tools/qa/CLAUDE.md, "A game's invariants module").

The rules are those of tools/stress/CLAUDE.md, "Control API" and "The arguments and the state". The state is
{"scene", "count", "language", "maxCount", "scenes"}; an action changes one of the first three.
"""

import json

from bagi_qa_checks import Move, Violation

LANGUAGES = frozenset({"en", "ru"})
SET_SCENE = "set_scene"
SET_COUNT = "set_count"
SET_LANGUAGE = "set_language"
# The action type, the key that carries the value and the state member that shows it
ASKED = {SET_SCENE: ("scene", "scene"), SET_COUNT: ("count", "count"), SET_LANGUAGE: ("language", "language")}
SHOWN_KEYS = ("scene", "count", "language")
# A count above this takes tens of seconds a frame in a Debug build of the `ui` scene, which no walk can afford. The `rmlui` scene
# needs no lower cap: measured in `macos-rmlui-debug`, 100 documents take 68 ms for the frame that builds them and 18 ms steady
# (400: 172 ms and 35 ms), 109 MB resident, so this cap serves every scene.
MONKEY_MAX_COUNT = 100
MONKEY_RESET_COUNT = 20
RESET_KIND = "reset"
RESET_METHOD = "reset"
PERFORM_ACTION = "perform_action"
RMLUI_SCENE = "rmlui"
# Kind `click`: the runner makes a `click` move with `session.call` and follows it with a `step`, which the wheel and the reload need before they show
CLICK_KIND = "click"
WHEEL_METHOD = "wheel_at"
CALL_METHOD = "call_method"
# Inside the scroller of the panel (tools/stress/assets/ui/stress_rmlui.rcss: 360 x 240 dp, 40 dp from the right and bottom of the 1920 x 1080 output)
PANEL_X = 1700
PANEL_Y = 920
# (dx, dy, flipped): both axes, both signs and the flipped direction
WHEELS = ((0, -1, False), (0, 1, False), (1, 0, False), (-1, 0, False), (0, -1, True), (1, 0, True))

KNOWN_DEVIATIONS: set[str] = set()


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def check_state(answer, previous):
    """The state is inside its ranges, and the version moves exactly when the state changes."""
    state = answer.get("state")
    if not isinstance(state, dict):
        return [Violation("state_shape", f"get_state has no state object: {answer!r}")]
    found = []
    scenes = state.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        found.append(Violation("state_shape", f"'scenes' is not a list of names: {scenes!r}"))
        scenes = []
    if state.get("scene") not in scenes:
        found.append(Violation("scene_unknown", f"scene {state.get('scene')!r} is not one of {scenes}"))
    count, max_count = state.get("count"), state.get("maxCount")
    if not is_int(count) or not is_int(max_count) or not 0 <= count <= max_count:
        found.append(Violation("count_out_of_range", f"count {count!r} is not an integer in 0..{max_count!r}"))
    if state.get("language") not in LANGUAGES:
        found.append(Violation("language_unknown", f"language {state.get('language')!r} is not one of {sorted(LANGUAGES)}"))
    if previous is not None:
        found.extend(_version_follows_state(previous, answer))
    return found


def check_action(action, result, before, after):
    """A refused action changes nothing; an accepted one shows what was asked, in its outcome (`result` is the whole answer: game, outcome, version) and in the state."""
    found = []
    state_before, state_after = before.get("state", {}), after.get("state", {})
    if result is None:
        if state_before != state_after:
            found.append(Violation("refused_action_changed_state", f"{action!r} was refused, the state went from {state_before} to {state_after}"))
        return found
    asked = ASKED.get(action.get("type"))
    if asked is not None:
        key, member = asked
        if state_after.get(member) != action.get(key):
            found.append(Violation(f"{action['type']}_not_shown", f"{action!r} was accepted, the state shows {member} {state_after.get(member)!r}"))
    shown = {key: state_after.get(key) for key in SHOWN_KEYS}
    outcome = result.get("outcome") if isinstance(result, dict) else None
    if outcome != shown:
        found.append(Violation("action_answer_differs", f"the outcome of {action!r} is {outcome}, the state after it is {shown}"))
    return found


def monkey_moves(answer, moves):
    """The generic moves without a `set_count` above `MONKEY_MAX_COUNT`, plus a `reset` to each scene at `MONKEY_RESET_COUNT`; on the scene `rmlui` also a wheel over its panel and the reload of its documents."""
    state = answer.get("state") if isinstance(answer, dict) else None
    scenes = state.get("scenes") if isinstance(state, dict) else None
    kept = [move for move in moves if not _too_big(move)]
    added = [Move(RESET_KIND, RESET_METHOD, {"scene": scene, "count": MONKEY_RESET_COUNT}, f"reset {scene}") for scene in scenes if isinstance(scene, str)] if isinstance(scenes, list) else []
    if isinstance(state, dict) and state.get("scene") == RMLUI_SCENE:
        added += _rmlui_moves()
    unique = {(move.kind, move.method, json.dumps(move.params, sort_keys=True)): move for move in kept + added}
    return [unique[key] for key in sorted(unique)]


def _rmlui_moves():
    wheels = [
        Move(CLICK_KIND, WHEEL_METHOD, {"x": PANEL_X, "y": PANEL_Y, "dx": dx, "dy": dy, "flipped": flipped}, f"wheel {dx},{dy}{' flipped' if flipped else ''}")
        for dx, dy, flipped in WHEELS
    ]
    return wheels + [Move(CLICK_KIND, CALL_METHOD, {"root": "game", "method": "ReloadRmlUiDocuments"}, "reload documents")]


def _too_big(move):
    action = move.params if move.method == PERFORM_ACTION else {}
    return action.get("type") == SET_COUNT and is_int(action.get("count")) and action["count"] > MONKEY_MAX_COUNT


def _version_follows_state(previous, answer):
    changed = previous.get("state") != answer.get("state")
    moved = previous.get("version") != answer.get("version")
    if moved and not changed:
        return [Violation("version_moved_without_change", f"version {previous.get('version')} -> {answer.get('version')} with the state unchanged: {answer.get('state')}")]
    if changed and not moved:
        return [Violation("version_still_on_change", f"the state changed from {previous.get('state')} to {answer.get('state')} and the version stayed {answer.get('version')}")]
    return []
