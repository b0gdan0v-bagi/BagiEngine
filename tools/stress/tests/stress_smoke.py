"""Control smoke test of BagiStress (the ctest entry BagiStressControlSmoke, `be_add_game_control_tests`).

Starts the app in a fresh process per scene (sprites 500, entities 500, ui 200), `--headless --control --manual-clock`, and drives
it over the control API: the state, `set_count` shown in the next frame (`frame_stats` draw calls and the `Stress objects` counter),
the bad values, screenshots that are not flat, `set_language` rewording the `ui` scene, `set_scene`, `ui_tree`, the scene methods on the `sprites` scene and the `set_window` step of smoke.py
on the `ui` scene (its Screen canvas must follow the simulated window), and a log with no Warning or Error. The `rmlui` scenario probes `get_state.scenes`
and prints a skip line when the build has no `rmlui` (only `ENGINE_UI_RMLUI` builds list it), so the same script passes in every preset; with it the scene's documents, textures,
`wheel_at` on both axes, `ReloadRmlUiDocuments` and the scene switch are checked. The `reflection` scene drives the by-name methods over `StressGame` (`list_types`, `describe_type`, `schema`,
`get_property`, `set_property`, `call_method`) and checks the engine's `config/*.xml` against the XSD made from the live schema (`tools/schema`; printed as skipped when `xmllint` is missing). The `editor` scenario runs `--headless --editor` (a temp `BAGI_EDITOR_DIR`): `status.runMode` is `Editor`, a screenshot is the whole window and differs from the Game mode's of the same game; the View menu opens the Game panel, a chord closes it and opens it again, its filter takes typed text, and an action button changes `get_state` and the picture, all through `imgui_find` / `imgui_input`; it also runs with `--content-overlay=<temp folder>`, and `editor_state` must say nothing is dirty, name a `scene:` document whose `file` is inside that folder, and the checkout's `SceneConfig.xml` is unchanged with nothing written into the folder (a build without the editor prints a skip line). The `editor cycle` step starts an editor of its own through `GameSession` (the moves of `tools/qa/bagi_qa_editor.py` need its `imgui_*` helpers): a rename makes the scene dirty in `dirty`, `any_dirty` and `editor_state`, the overlay holds no scene file yet, Save clears the flag and writes the overlay's file only, and the checkout's `SceneConfig.xml` keeps its hash; first the Config Editor must sit docked under the Scene Editor (a stale `imgui.ini` leaves it floating). The helpers are those of tools/control/tests/smoke.py (the default scenario of `smoke.py` is not touched).

Unlike smoke.py's `run_smoke` this script drives the process through `GameProcess`, because the check of the game's log needs the log's
path and `smoke.play` keeps it to itself. Exit codes as smoke.py: 0 passed, 1 failed, 77 skipped (the executable is not built).
"""

import argparse
import contextlib
import functools
import hashlib
import os
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path
from typing import Callable

ENGINE_DIR = Path(os.environ["BAGI_ENGINE_DIR"]) if "BAGI_ENGINE_DIR" in os.environ else Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "qa"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "schema"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
import bagi_qa_editor as editor  # noqa: E402
from bagi_session import GameSession, SessionError  # noqa: E402
from game_process import BACKEND_UNAVAILABLE_MARKER, PORT_TAKEN_MARKER, GameLaunchError, GameProcess, build_game_command, free_port, preset_executable  # noqa: E402
from schema_to_xsd import Binding, check_files, convert_config_dir, find_xmllint  # noqa: E402
from smoke import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, SmokeFailure, check, check_scene_methods, check_window_control, expect_error, take_screenshot  # noqa: E402

STATUS_STARTUP_TIMEOUT = 30.0
SHUTDOWN_TIMEOUT = 15.0
STEADY_FRAMES = 6  # a window of frames of an unchanged scene
SETTLE_FRAMES = 3  # a change made through the control API reaches the scene in the next frame; one more for the scene switch
MIN_SHOT_COLORS = 3  # a picture of a scene is not flat: a background and at least two more colours
OBJECTS_COUNTER = "Stress objects"
SPRITES_COUNT = 500
ENTITIES_COUNT = 500
UI_COUNT = 200
REFLECTION_COUNT = 50
RMLUI_COUNT = 20
GPU_SPRITES_COUNT = 500
RMLUI_COUNTER_PREFIX = "Stress rmlui "
RMLUI_STEPS = 30  # RmlUi scrolls a wheel step smoothly over the frames of the game clock that follow
# Inside the scroller of the panel (tools/stress/assets/ui/stress_rmlui.rcss: 360 x 240 dp, 40 dp from the right and bottom of the 1920 x 1080 reference, which is the headless output)
RMLUI_PANEL_X = 1700
RMLUI_PANEL_Y = 920
EDITOR_WINDOW = (1280, 720)  # window units: room for the docks of the editor
CYCLE_FLAGS = ["--stress-scene=entities", "--stress-count=1"]  # the scene of the edit charter (tools/stress/qa/editor_charter.py)
CYCLE_NODE = "Run"
CYCLE_RENAMED = "RunRenamed"
CONFIG_EDITOR_WINDOW = "ConfigEditor"  # the id part of `Config Editor###ConfigEditor`; an ini that knew the window as `Config Editor` leaves it floating
EDITOR_FRAMES = 5  # ImGui lays its docks out over more than one frame
STRESS_SCENE_FILE = ENGINE_DIR / "tools" / "stress" / "config" / "SceneConfig.xml"  # the tracked file the editor's scene document stands for
EDITOR_SCENE_PATH = "config/SceneConfig.xml"
CONTENT_OVERLAY_FLAG = "--content-overlay="
EDITOR_DIR_VARIABLE = "BAGI_EDITOR_DIR"  # where the editor keeps its per-user files (widgets-module.md): a temp folder, never the real one
NO_EDITOR_WARNING = "this build has no editor"  # the one Warning a build without the editor logs for --editor
GAME_PANEL_WINDOW = "Game"  # the window of "Game Panel" in the View menu: its title is `Game###GamePanel`
ACTION_FILTER = "##gameActionFilter"  # the InputText that narrows the action buttons of the Game panel
FILTER_TEXT = "count"  # keeps `set_count ...` and drops `set_scene ...`
CLICKED_ACTION_TYPE = "set_count"  # a listed action whose button is labelled `set_count count=N` (the Console's syntax); the listed counts depend on the current one
RUN_NUMBER_MAX = 1000  # the range of the field `runNumber` of StressGame
ENGINE_CONFIG_DIR = ENGINE_DIR / "config"
ENGINE_PANELS_DIR = ENGINE_CONFIG_DIR / "panels"  # panel assets: the root element names the type, so no binding is needed
# The engine's config files whose reflected type is not named like their stem (tools/schema/CLAUDE.md, "What the XSD says")
ENGINE_CONFIG_BINDINGS = {
    "LoggerConfig": Binding("LoggerManager"),
    "AssertHandlersConfig": Binding("AssertHandlerManager"),
    "TestsConfig": Binding("TestList"),
    "WidgetsConfig": Binding("WidgetList"),
    "GameWidgetsConfig": Binding("WidgetList"),
    "SDLWindowConfig": Binding("SDLWindowConfig", "window"),
    "AudioConfig": Binding("AudioSettings"),
}
ENGINE_CONFIG_BY_HAND = {"TextConfig", "UIConfig"}  # read by hand: no reflected type, so no XSD
EDITOR_WIDGETS_CONFIG = "WidgetsConfig"  # the widgets of the Editor run mode
EDITOR_WIDGET_TYPE = "EditorLayoutWidget"  # in the live schema only when the build has the editor


def last_frame(client: BagiControlClient) -> dict:
    return client.call("frame_stats", {"frames": 1})["frames"][-1]


def settle(client: BagiControlClient) -> dict:
    """Steps the frames a change needs and gives the stats of the last one."""
    client.call("step", {"frames": SETTLE_FRAMES})
    return last_frame(client)


def stress_state(client: BagiControlClient) -> dict:
    return client.call("get_state")["state"]


def check_state(client: BagiControlClient, scene: str, count: int) -> None:
    state = stress_state(client)
    check(state["scene"] == scene and state["count"] == count, f"get_state shows {state['scene']} {state['count']}, expected {scene} {count}")
    check(client.call("list_games")["active"] == "Stress", f"list_games does not say Stress: {client.call('list_games')}")
    frame = settle(client)
    check(frame["counters"].get(OBJECTS_COUNTER) == count, f"the counter '{OBJECTS_COUNTER}' is {frame['counters'].get(OBJECTS_COUNTER)}, the count is {count}")


def set_count(client: BagiControlClient, count: int) -> dict:
    client.call("perform_action", {"type": "set_count", "count": count})
    frame = settle(client)
    check(stress_state(client)["count"] == count, f"set_count {count} did not change the state")
    check(frame["counters"].get(OBJECTS_COUNTER) == count, f"the counter '{OBJECTS_COUNTER}' is {frame['counters'].get(OBJECTS_COUNTER)} after set_count {count}")
    return frame


def check_bad_counts(client: BagiControlClient) -> None:
    version = client.call("get_state")["version"]
    for bad in (0, 1_000_001, 1.5, "7", None):
        expect_error("invalid_params", lambda bad=bad: client.call("perform_action", {"type": "set_count", "count": bad}), f"set_count {bad!r}")
    expect_error("invalid_params", lambda: client.call("perform_action", {"type": "set_count"}), "set_count without a count")
    expect_error("illegal_action", lambda: client.call("perform_action", {"type": "no_such_action"}), "an unknown action")
    check(client.call("get_state")["version"] == version, "a refused action moved the state version")


def check_not_flat(client: BagiControlClient, what: str) -> object:
    image = take_screenshot(client)
    check(len(image.colors()) >= MIN_SHOT_COLORS, f"the screenshot of {what} has {len(image.colors())} colours: it is flat")
    return image


def check_ui_tree(client: BagiControlClient, scene: str) -> None:
    tree = client.call("ui_tree")
    check(tree["scene"] == scene, f"ui_tree is of the scene '{tree['scene']}', not '{scene}'")
    check(len(tree["canvases"]) >= 1, "ui_tree has no canvas")


def scenario_sprites(client: BagiControlClient) -> None:
    check_state(client, "sprites", SPRITES_COUNT)
    before = last_frame(client)["drawCalls"]
    check(before >= SPRITES_COUNT, f"{SPRITES_COUNT} sprites made {before} draw calls")
    after = set_count(client, SPRITES_COUNT * 2)["drawCalls"]
    check(after > before, f"the draw calls did not grow with the count: {before} then {after}")
    check(set_count(client, SPRITES_COUNT // 2)["drawCalls"] < after, "the draw calls did not fall with the count")
    check_bad_counts(client)
    check_not_flat(client, "sprites")
    check_scene_methods(client)
    expect_error("invalid_params", lambda: client.call("perform_action", {"type": "set_scene", "scene": "nowhere"}), "a scene that does not exist")


def scenario_gpu_batching(client: BagiControlClient, batching: bool) -> None:
    """A settled frame of the sprites scene on a GPU backend: batched, far fewer submissions than sprites; with `--stress-no-batching`, one each."""
    check_state(client, "sprites", GPU_SPRITES_COUNT)
    frame = settle(client)
    check(frame["primitives"] == GPU_SPRITES_COUNT, f"{GPU_SPRITES_COUNT} sprites made {frame['primitives']} primitives")
    if batching:
        check(frame["drawCalls"] < GPU_SPRITES_COUNT, f"batched, {GPU_SPRITES_COUNT} sprites still made {frame['drawCalls']} draw calls")
    else:
        check(frame["drawCalls"] == GPU_SPRITES_COUNT, f"unbatched, {GPU_SPRITES_COUNT} sprites made {frame['drawCalls']} draw calls")


def scenario_entities(client: BagiControlClient) -> None:
    check_state(client, "entities", ENTITIES_COUNT)
    check(last_frame(client)["drawCalls"] == 0, "the entities scene drew something")
    check(set_count(client, ENTITIES_COUNT * 2)["drawCalls"] == 0, "the entities scene drew something after set_count")
    check_bad_counts(client)


def check_frame_memory_is_given_back(client: BagiControlClient) -> None:
    """Steady frames report the same arenaUsedBytes: the arenas of the frame are reset, not summed. It guards the frame arena only once a scene draws a user of it (a box with a shadow or glow); until then the value is constant and the check cannot fail."""
    settle(client)
    client.call("step", {"frames": STEADY_FRAMES})
    used = [frame["arenaUsedBytes"] for frame in client.call("frame_stats", {"frames": STEADY_FRAMES})["frames"]]
    check(len(used) == STEADY_FRAMES and max(used) == min(used), f"arenaUsedBytes of {STEADY_FRAMES} steady frames differ: {used}")


def scenario_ui(client: BagiControlClient) -> None:
    check_state(client, "ui", UI_COUNT)
    check_frame_memory_is_given_back(client)
    check(last_frame(client)["drawCalls"] > 0, "the ui scene drew nothing")
    check_ui_tree(client, "StressUi")
    check_window_control(client, headless=True)  # the scene with a Screen canvas: the layout follows the simulated window
    english = check_not_flat(client, "ui (en)")
    client.call("perform_action", {"type": "set_language", "language": "ru"})
    client.call("step", {"frames": SETTLE_FRAMES})
    russian = check_not_flat(client, "ui (ru)")
    check(russian.rows != english.rows, "set_language did not change the picture of the ui scene")
    check(set_count(client, UI_COUNT * 2)["drawCalls"] > 0, "the ui scene drew nothing after set_count")
    check_bad_counts(client)


def scenario_scenes(client: BagiControlClient) -> None:
    """Starts as sprites; every set_scene switches what is drawn and the app stays 'Stress'."""
    check_state(client, "sprites", SPRITES_COUNT)
    check(last_frame(client)["drawCalls"] > 0, "sprites drew nothing")
    client.call("perform_action", {"type": "set_scene", "scene": "entities"})
    frame = settle(client)
    check(stress_state(client)["scene"] == "entities" and frame["drawCalls"] == 0, f"after set_scene entities the frame drew {frame['drawCalls']}")
    client.call("perform_action", {"type": "set_scene", "scene": "ui"})
    frame = settle(client)
    check(stress_state(client)["scene"] == "ui" and frame["drawCalls"] > 0, f"after set_scene ui the frame drew {frame['drawCalls']}")
    check_ui_tree(client, "StressUi")
    check(client.call("list_games")["active"] == "Stress", "list_games lost the game after set_scene")


def rmlui_counters(client: BagiControlClient) -> dict[str, float]:
    counters = last_frame(client)["counters"]
    return {name.removeprefix(RMLUI_COUNTER_PREFIX): value for name, value in counters.items() if name.startswith(RMLUI_COUNTER_PREFIX)}


def rmlui_counter(client: BagiControlClient, name: str) -> float:
    counters = rmlui_counters(client)
    check(name in counters, f"frame_stats has no counter '{RMLUI_COUNTER_PREFIX}{name}': {sorted(counters)}")
    return counters[name]


def rmlui_wheel(client: BagiControlClient, dx: int, dy: int, flipped: bool = False) -> None:
    answer = client.call("wheel_at", {"x": RMLUI_PANEL_X, "y": RMLUI_PANEL_Y, "dx": dx, "dy": dy, "flipped": flipped})
    check(answer["consumed"] is True, f"wheel_at over the panel was not consumed: {answer}")
    client.call("step", {"frames": RMLUI_STEPS})


def scenario_rmlui(client: BagiControlClient) -> None:
    """Starts as sprites; the scene exists only in a build with the RmlUi host, so the scenario probes for it and skips itself otherwise."""
    if "rmlui" not in client.call("get_state")["state"]["scenes"]:
        print("rmlui: SKIPPED, get_state.scenes has no rmlui (the build has no ENGINE_UI_RMLUI)", flush=True)
        return
    check_state(client, "sprites", RMLUI_COUNT)
    client.call("perform_action", {"type": "set_scene", "scene": "rmlui"})
    settle(client)
    check_state(client, "rmlui", RMLUI_COUNT)
    check(rmlui_counter(client, "documents") == RMLUI_COUNT, f"the documents counter is {rmlui_counter(client, 'documents')}, the count is {RMLUI_COUNT}")
    check(rmlui_counter(client, "textures") > 0, "the scene made no texture")
    check(rmlui_counter(client, "scroll x") == 0 and rmlui_counter(client, "scroll y") == 0, "the panel starts scrolled")
    check(last_frame(client)["drawCalls"] > 0, "the rmlui scene drew nothing")
    check_not_flat(client, "rmlui")

    for count in (RMLUI_COUNT * 2, RMLUI_COUNT // 2):
        set_count(client, count)
        check(rmlui_counter(client, "documents") == count, f"the documents counter is {rmlui_counter(client, 'documents')} after set_count {count}")
    check_bad_counts(client)
    client.call("perform_action", {"type": "set_language", "language": "ru"})
    client.call("step", {"frames": SETTLE_FRAMES})
    check(rmlui_counter(client, "documents") == RMLUI_COUNT // 2, "set_language changed the documents counter")
    client.call("perform_action", {"type": "set_language", "language": "en"})

    # SDL: y negative scrolls down, x positive scrolls right, `flipped` reverses both
    client.call("step", {"frames": SETTLE_FRAMES})
    rmlui_wheel(client, 0, -1)
    down = rmlui_counter(client, "scroll y")
    check(down > 0 and rmlui_counter(client, "scroll x") == 0, f"a wheel down scrolled y to {down} and x to {rmlui_counter(client, 'scroll x')}")
    rmlui_wheel(client, 0, 1)
    check(rmlui_counter(client, "scroll y") < down, "the opposite wheel did not scroll y back")
    rmlui_wheel(client, 1, 0)
    right = rmlui_counter(client, "scroll x")
    check(right > 0, "a wheel to the right did not scroll x")
    rmlui_wheel(client, 1, 0, flipped=True)
    check(rmlui_counter(client, "scroll x") < right, "a flipped wheel to the right did not scroll x back")

    reloads = rmlui_counter(client, "reloads")
    client.call("call_method", {"root": "game", "method": "ReloadRmlUiDocuments"})
    client.call("step", {"frames": SETTLE_FRAMES})
    check(rmlui_counter(client, "reloads") == reloads + 1, f"the reloads counter is {rmlui_counter(client, 'reloads')} after one request")
    check(rmlui_counter(client, "scroll x") == 0 and rmlui_counter(client, "scroll y") == 0, "the panel is still scrolled after a reload")
    check(rmlui_counter(client, "documents") == RMLUI_COUNT // 2, "the reload changed the documents")
    check_not_flat(client, "rmlui after the reload")

    client.call("perform_action", {"type": "set_scene", "scene": "sprites"})
    settle(client)
    check(stress_state(client)["scene"] == "sprites" and last_frame(client)["drawCalls"] > 0, "the switch from rmlui to sprites did not draw sprites")
    client.call("perform_action", {"type": "set_scene", "scene": "rmlui"})
    settle(client)
    check(rmlui_counter(client, "documents") == RMLUI_COUNT // 2, "the switch back to rmlui lost its documents")


def keys_named(value: object, name: str) -> list[str]:
    """The paths of every member called `name` in a JSON value (the schema carries no hash: a number above 2^53 is not safe in a client)."""
    found: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            if key == name:
                found.append(key)
            found.extend(keys_named(child, name))
    elif isinstance(value, list):
        for child in value:
            found.extend(keys_named(child, name))
    return found


def named(items: list[dict], name: str, what: str) -> dict:
    matches = [item for item in items if item.get("name") == name]
    check(len(matches) == 1, f"{what}: {len(matches)} entries named '{name}'")
    return matches[0]


def check_stress_game_description(client: BagiControlClient) -> None:
    listed = client.call("list_types", {"base": "IGame"})["types"]
    check([entry["name"] for entry in listed] == ["StressGame"], f"list_types IGame: {[entry['name'] for entry in listed]}")
    expect_error("unknown_type", lambda: client.call("list_types", {"base": "NoSuchType"}), "list_types of an unknown base")
    expect_error("unknown_type", lambda: client.call("describe_type", {"name": "NoSuchType"}), "describe_type of an unknown type")
    expect_error("invalid_params", lambda: client.call("describe_type", {"name": 7}), "describe_type with a number as the name")

    described = client.call("describe_type", {"name": "StressGame"})
    check(described["parent"] == "IGame" and described["bases"] == ["IGame"], f"describe_type StressGame: parent {described['parent']}, bases {described['bases']}")
    run_number = named(described["fields"], "runNumber", "the fields of StressGame")
    check(run_number["type"]["kind"] == "uint32", f"runNumber is {run_number['type']}")
    check(run_number["attributes"]["range"] == [0, RUN_NUMBER_MAX], f"runNumber range: {run_number['attributes']}")
    check("tooltip" in run_number["attributes"], "runNumber has no tooltip")
    state_version = named(described["fields"], "stateVersion", "the fields of StressGame")
    check(state_version["attributes"].get("readonly") is True, f"stateVersion attributes: {state_version['attributes']}")
    check([field["owner"] for field in described["allFields"]] == ["StressGame"] * 2, "allFields of StressGame name another owner")
    set_count = named(described["methods"], "SetObjectCount", "the methods of StressGame")
    check(set_count["callable"] is True and set_count["isConst"] is False, f"SetObjectCount: {set_count}")
    check([param["name"] for param in set_count["params"]] == ["count"], f"SetObjectCount params: {set_count['params']}")
    describe = named(described["methods"], "Describe", "the methods of StressGame")
    check(describe["callable"] is True and describe["isConst"] is True and describe["result"]["kind"] == "string", f"Describe: {describe}")


def check_properties(client: BagiControlClient) -> None:
    first = client.call("get_property", {"root": "game", "path": "runNumber"})
    check(first == {"root": "game", "path": "runNumber", "value": 0}, f"get_property runNumber: {first}")
    written = client.call("set_property", {"root": "game", "path": "runNumber", "value": 42})
    check(written["value"] == 42, f"set_property answered {written}, not the value read back")
    check(client.call("get_property", {"root": "game", "path": "runNumber"})["value"] == 42, "the written runNumber was not read back")

    expect_error("out_of_range", lambda: client.call("set_property", {"root": "game", "path": "runNumber", "value": RUN_NUMBER_MAX + 1}), "runNumber over its range")
    expect_error("type_mismatch", lambda: client.call("set_property", {"root": "game", "path": "runNumber", "value": "seven"}), "a text for a number")
    expect_error("invalid_params", lambda: client.call("set_property", {"root": "game", "path": "runNumber", "value": None}), "a null value")
    expect_error("read_only", lambda: client.call("set_property", {"root": "game", "path": "stateVersion", "value": 1}), "the read-only stateVersion")
    expect_error("not_found", lambda: client.call("get_property", {"root": "game", "path": "noSuchField"}), "a path that does not exist")
    expect_error("invalid_path", lambda: client.call("get_property", {"root": "game", "path": "[["}), "a bad path")
    expect_error("unknown_root", lambda: client.call("get_property", {"root": "nowhere", "path": "runNumber"}), "an unknown root")
    expect_error("invalid_params", lambda: client.call("get_property", {"path": "runNumber"}), "get_property without a root")
    check(client.call("get_property", {"root": "game", "path": "runNumber"})["value"] == 42, "a refused write changed runNumber")


def described_state(state: dict) -> str:
    """What `Describe()` of StressGame says for a `get_state` state."""
    return f"scene={state['scene']} count={state['count']} language={state['language']}"


def check_methods(client: BagiControlClient) -> None:
    described = client.call("call_method", {"root": "game", "method": "Describe"})
    check(described == {"root": "game", "method": "Describe", "result": described_state(stress_state(client))}, f"Describe answered {described}")

    version = client.call("get_state")["version"]
    outcome = client.call("call_method", {"root": "game", "method": "SetObjectCount", "arguments": {"count": REFLECTION_COUNT * 2}})
    check("result" not in outcome, f"SetObjectCount answered a result: {outcome}")
    frame = settle(client)
    state = stress_state(client)
    check(state["count"] == REFLECTION_COUNT * 2, f"SetObjectCount did not change the count: {state}")
    check(frame["counters"].get(OBJECTS_COUNTER) == REFLECTION_COUNT * 2, f"the counter '{OBJECTS_COUNTER}' is {frame['counters'].get(OBJECTS_COUNTER)} after SetObjectCount")
    check(client.call("get_state")["version"] > version, "SetObjectCount did not move the state version")
    check(client.call("call_method", {"root": "game", "method": "Describe"})["result"] == described_state(state), "Describe does not follow get_state after SetObjectCount")
    check(client.call("get_property", {"root": "game", "path": "stateVersion"})["value"] == client.call("get_state")["version"], "stateVersion is not the state version")

    version = client.call("get_state")["version"]
    for bad, description in (("many", "a text"), (-1, "a negative count"), (1.5, "a fraction")):
        expect_error("bad_arguments", lambda bad=bad: client.call("call_method", {"root": "game", "method": "SetObjectCount", "arguments": {"count": bad}}), f"SetObjectCount with {description}")
    expect_error("bad_arguments", lambda: client.call("call_method", {"root": "game", "method": "SetObjectCount"}), "SetObjectCount without its count")
    expect_error("bad_arguments", lambda: client.call("call_method", {"root": "game", "method": "SetObjectCount", "arguments": {"count": 5, "extra": 1}}), "SetObjectCount with an argument it has not")
    check(stress_state(client)["count"] == REFLECTION_COUNT * 2 and client.call("get_state")["version"] == version, "a refused call changed the state")
    expect_error("unknown_function", lambda: client.call("call_method", {"root": "game", "method": "NoSuchMethod"}), "an unknown method")
    expect_error("unknown_root", lambda: client.call("call_method", {"root": "nowhere", "method": "Describe"}), "a call on an unknown root")
    expect_error("invalid_params", lambda: client.call("call_method", {"root": "game", "method": "SetObjectCount", "arguments": [1]}), "arguments that are not an object")


def check_schema(client: BagiControlClient) -> dict:
    schema = client.call("schema")
    check(schema["version"] == 1, f"schema version {schema['version']}")
    stress_game = named(schema["types"], "StressGame", "the types of the schema")
    check(stress_game["parent"] == "IGame" and stress_game["creatable"] is True, f"StressGame in the schema: parent {stress_game['parent']}, creatable {stress_game['creatable']}")
    named(schema["types"], "IGame", "the types of the schema")
    games = [entry for entry in schema["factories"] if entry["base"] == "IGame"]
    check(len(games) == 1 and "StressGame" in games[0]["derived"], f"IGame as a factory base: {games}")
    game_root = named(schema["roots"], "game", "the roots of the schema")
    check(game_root.get("type") == "StressGame", f"the root game is of the type {game_root.get('type')}")
    names = [entry["name"] for entry in schema["types"]]
    check(names == sorted(names), "the types of the schema are not sorted by name")
    check(not keys_named(schema, "hash"), "the schema carries a hash")
    return schema


def check_config_files(schema: dict) -> None:
    """Every config file of the engine that has a reflected type is valid against the XSD made from the live schema (xmllint judges)."""
    if find_xmllint() is None:
        print("reflection: SKIPPED the check of config/*.xml against the schema: xmllint is not on PATH", flush=True)
        return
    with tempfile.TemporaryDirectory() as temp_dir:
        results = convert_config_dir(schema, ENGINE_CONFIG_DIR, Path(temp_dir), ENGINE_CONFIG_BINDINGS)
        uncovered = {result.stem for result in results if result.xsd is None}
        covered = len(results) - len(uncovered)
        print(f"reflection: config/*.xml against the schema: {covered} covered, {len(uncovered)} not covered ({', '.join(sorted(uncovered))})", flush=True)
        check(uncovered == ENGINE_CONFIG_BY_HAND, f"the config files without an XSD are {sorted(uncovered)}, expected {sorted(ENGINE_CONFIG_BY_HAND)}: " + "; ".join(f"{result.stem}: {result.reason}" for result in results if result.xsd is None))
        # The editor's widgets are not reflected in a build without the editor, and such a build only ever runs the Game mode, which never reads this file
        editor_built = any(entry["name"] == EDITOR_WIDGET_TYPE for entry in schema["types"])
        checked = [result for result in results if editor_built or result.stem != EDITOR_WIDGETS_CONFIG]
        findings = check_files(checked, ENGINE_CONFIG_DIR)
        for stem, lines in findings.items():
            raise SmokeFailure(f"config/{stem}.xml does not match its XSD: {lines[0]}")
        # A panel asset is a pure class, reflected in every build, so none of the files is left out here
        panels = convert_config_dir(schema, ENGINE_PANELS_DIR, Path(temp_dir) / "panels")
        uncovered_panels = [result for result in panels if result.xsd is None]
        check(panels and not uncovered_panels, "config/panels has no file or one without an XSD: " + "; ".join(f"{result.stem}: {result.reason}" for result in uncovered_panels))
        for stem, lines in check_files(panels, ENGINE_PANELS_DIR).items():
            raise SmokeFailure(f"config/panels/{stem}.xml does not match its XSD: {lines[0]}")


def scenario_reflection(client: BagiControlClient) -> None:
    check_state(client, "sprites", REFLECTION_COUNT)
    check_stress_game_description(client)
    check_properties(client)
    check_methods(client)
    check_config_files(check_schema(client))


def editor_flags(count: int, editor: bool) -> list[str]:
    width, height = EDITOR_WINDOW
    return [f"--window={width}x{height}", "--stress-scene=sprites", f"--stress-count={count}", *(["--editor"] if editor else [])]


def game_picture(exe: Path) -> object:
    """A screenshot of the same game and window in the Game run mode, from a fresh process."""
    pictures: list[object] = []

    def shoot(client: BagiControlClient) -> None:
        client.call("step", {"frames": SETTLE_FRAMES})
        pictures.append(take_screenshot(client, {"maxSize": max(EDITOR_WINDOW)}))

    play(exe, editor_flags(SPRITES_COUNT, editor=False), shoot, free_port())
    return pictures[0]


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_editor_state(client: BagiControlClient, overlay: Path) -> None:
    """`editor_state` of a fresh editor run into an overlay: nothing is dirty, the scene document points into the overlay (not yet written), and nothing is written.

    BagiStress has no TextureLibrary, so the Sprite Library row is clean with no path, and no config is selected in the Config Editor, so there is no `config` row.
    Making a document dirty and saving it needs `imgui_input`: `scenario_editor_cycle` does it in an editor of its own.
    """
    state = client.call("editor_state")
    check(state["anyDirty"] is False, f"editor_state: anyDirty is {state['anyDirty']} in a fresh editor")
    documents = {document["id"]: document for document in state["documents"]}
    check(list(documents) == sorted(documents), f"editor_state: the documents are not sorted by id: {list(documents)}")
    scenes = [document for document in documents.values() if document["kind"] == "scene"]
    check(len(scenes) == 1 and scenes[0]["id"].startswith("scene:"), f"editor_state: expected one scene document, got {list(documents)}")
    scene = scenes[0]
    check(scene["path"] == EDITOR_SCENE_PATH, f"editor_state: the scene path is '{scene['path']}'")
    expected_file = overlay.resolve() / EDITOR_SCENE_PATH
    check(Path(scene["file"]).resolve() == expected_file, f"editor_state: the scene file is '{scene['file']}', expected {expected_file} inside the overlay")
    check(scene["dirty"] is False, "editor_state: a fresh scene is dirty")
    library = documents.get("library:sprites")
    check(library is not None and library["kind"] == "library" and library["dirty"] is False, f"editor_state: the Sprite Library row is {library}")
    check(not any(document["kind"] == "config" for document in documents.values()), f"editor_state: a config row with no config selected: {list(documents)}")
    print(f"editor: editor_state ok, {len(documents)} documents, scene file {scene['file']}", flush=True)


def imgui_matches(client: BagiControlClient, **params: object) -> list[dict]:
    """The matches of one `imgui_find`; an item that is not drawn in the frame is not in the list."""
    return client.call("imgui_find", params)["matches"]


def imgui_click(client: BagiControlClient, label: str) -> dict:
    """Clicks the centre of the one visible item called `label` and answers what ImGui made of the click."""
    found = imgui_matches(client, label=label, kind="item", visibleOnly=True)
    check(len(found) == 1, f"{len(found)} visible ImGui items are called '{label}', expected one")
    item = found[0]
    return client.call("imgui_input", {"events": [{"type": "click", "x": item["x"] + item["width"] / 2, "y": item["y"] + item["height"] / 2}]})


def check_game_panel_window(client: BagiControlClient, shown: bool, when: str) -> None:
    windows = imgui_matches(client, label=GAME_PANEL_WINDOW, kind="window")
    check(bool(windows) == shown, f"the Game panel is {'missing' if shown else 'still there'} {when}")
    if shown:
        check(windows[0]["width"] > 0 and windows[0]["height"] > 0, f"the Game panel has an empty rect {windows[0]}")


def drive_editor_panels(client: BagiControlClient) -> None:
    """The editor as a person uses it, through imgui_find and imgui_input: the View menu opens the Game panel, a chord closes and opens it, its filter takes text,
    and a click on an action changes the game and the picture (.claude/rules/control-imgui-api.md)."""
    check(imgui_matches(client, label="No Such Item") == [], "a name that is not there is not an empty list")
    check_game_panel_window(client, False, "before anyone opened it")
    check(not imgui_matches(client, label="Game Panel"), "a menu item is listed while its menu is closed")

    menu = imgui_click(client, "View")
    check(menu["wantCaptureMouse"], "ImGui did not take the click on the View menu")
    imgui_click(client, "Game Panel")
    check_game_panel_window(client, True, "after View, Game Panel")
    check(not imgui_matches(client, label="Game Panel"), "the View menu is still open after its item was clicked")

    chord = [{"type": "key", "key": "G", "mods": ["ctrl", "shift"]}]
    client.call("imgui_input", {"events": chord})
    client.call("step", {"frames": editor.PANEL_CLOSE_FRAMES})
    check_game_panel_window(client, False, "after the chord Ctrl+Shift+G")
    client.call("imgui_input", {"events": chord})
    check_game_panel_window(client, True, "after the chord Ctrl+Shift+G again")
    expect_error("invalid_params", lambda: client.call("imgui_input", {"events": [{"type": "key", "key": "NoSuchKey"}]}), "an unknown key")

    check(imgui_click(client, ACTION_FILTER)["wantTextInput"], "the filter field of the Game panel did not take the focus")
    check(client.call("imgui_input", {"events": [{"type": "text", "text": FILTER_TEXT}]})["wantTextInput"], "ImGui lost the focus of the filter while it took the text")
    check(not imgui_matches(client, label="set_scene scene=ui"), "the filter left an action that has no '" + FILTER_TEXT + "' in it")

    version = client.call("get_state")["version"]
    action = next(entry for entry in client.call("list_actions")["actions"] if entry["type"] == CLICKED_ACTION_TYPE)
    label = f"{CLICKED_ACTION_TYPE} count={action['count']}"
    before = take_screenshot(client, {"maxSize": max(EDITOR_WINDOW)})
    imgui_click(client, label)
    state = client.call("get_state")
    check(state["state"]["count"] == action["count"], f"the count is {state['state']['count']} after the button '{label}'")
    check(state["version"] > version, "the state version did not move after the action button")
    client.call("step", {"frames": SETTLE_FRAMES})
    after = take_screenshot(client, {"maxSize": max(EDITOR_WINDOW)})
    check(after.rows != before.rows, f"the picture of the editor did not change after the button '{label}'")


def scenario_editor(client: BagiControlClient, exe: Path, overlay: Path) -> None:
    """The Editor run mode headless: the picture is the whole window, not the game alone, and `editor_state` names the overlay's file. A build without the editor runs Game and the scenario skips itself."""
    run_mode = client.call("status")["runMode"]
    if run_mode == "Game":
        print("editor: SKIPPED, status.runMode is Game (the build has no editor)", flush=True)
        return
    check(run_mode == "Editor", f"status.runMode is '{run_mode}' with --editor")
    client.call("step", {"frames": EDITOR_FRAMES})
    shot = take_screenshot(client, {"maxSize": max(EDITOR_WINDOW)})
    check((shot.width, shot.height) == EDITOR_WINDOW, f"the screenshot of the editor is {shot.width}x{shot.height}, the window is {EDITOR_WINDOW[0]}x{EDITOR_WINDOW[1]}")
    check(len(shot.colors()) >= MIN_SHOT_COLORS, f"the screenshot of the editor has {len(shot.colors())} colours: it is flat")
    check(shot.rows != game_picture(exe).rows, "the screenshot of the editor is the picture of the Game run mode")
    scene_hash = file_hash(STRESS_SCENE_FILE)
    check_editor_state(client, overlay)
    check(file_hash(STRESS_SCENE_FILE) == scene_hash, f"the run changed {STRESS_SCENE_FILE}")
    check(not (overlay / EDITOR_SCENE_PATH).exists(), "a run that saved nothing wrote the overlay's scene file")
    print("editor: the checkout's SceneConfig.xml is unchanged and the overlay holds no scene file", flush=True)
    drive_editor_panels(client)


def editor_session_error(session: GameSession, error: Exception) -> SmokeFailure:
    return SmokeFailure(f"{error!r}\n--- game log (tail) ---\n{session.game_log()}")


def launch_cycle_editor(session: GameSession, exe: Path, overlay: Path) -> dict:
    """The editor of the cycle step; once more when the free port was lost to another process."""
    for attempt in range(2):
        try:
            return session.launch(exe=str(exe), extra=CYCLE_FLAGS, editor=True, content_overlay=str(overlay))
        except SessionError as error:
            if attempt == 0 and PORT_TAKEN_MARKER in str(error):
                continue
            raise
    raise AssertionError("unreachable")


def check_config_editor_docked(session: GameSession) -> None:
    """The Config Editor stacks under the Scene Editor in the left column; a layout file that knows the window by an old name leaves it floating."""
    scene = session.imgui_find(kind="window", label=editor.SCENE_EDITOR)["matches"]
    config = [match for match in session.imgui_find(kind="window", label_contains=CONFIG_EDITOR_WINDOW)["matches"] if match["label"].endswith(CONFIG_EDITOR_WINDOW)]
    check(len(scene) == 1 and len(config) == 1, f"expected one Scene Editor and one Config Editor window, found {len(scene)} and {len(config)}")
    top, bottom = scene[0], config[0]
    found = f"Scene Editor at ({top['x']}, {top['y']}) {top['width']}x{top['height']}, Config Editor at ({bottom['x']}, {bottom['y']}) {bottom['width']}x{bottom['height']}"
    docked = bottom["x"] == top["x"] and bottom["width"] == top["width"] and bottom["y"] >= top["y"] + top["height"]
    check(docked, f"the Config Editor is not docked under the Scene Editor: {found}; a layout file that knows the window by an old name is the usual cause")
    print("editor cycle: the Config Editor is docked under the Scene Editor", flush=True)


def drive_editor_cycle(session: GameSession, overlay: Path, scene_hash: str) -> None:
    check(editor.dirty(session) is False and editor.any_dirty(session) is False, "a fresh editor reports unsaved changes")
    session.call("reset", {"scene": "entities", "count": 1})
    session.step(EDITOR_FRAMES)
    check_config_editor_docked(session)
    editor.widen_left_dock(session)
    editor.open_panel(session, editor.SCENE_EDITOR)
    editor.select_node(session, CYCLE_NODE)
    editor.rename_selected(session, CYCLE_RENAMED)
    scene = editor.scene_document(session)
    check(editor.dirty(session) and editor.any_dirty(session), "after a rename the scene is not dirty")
    check(scene["id"].startswith("scene:") and scene["dirty"] is True, f"editor_state after the rename: {scene}")
    scene_file = Path(scene["file"])
    check(scene_file.resolve().is_relative_to(overlay.resolve()), f"the scene file '{scene_file}' is not inside the overlay {overlay}")
    check(not scene_file.exists(), "the overlay holds the scene file before Save")
    check(file_hash(STRESS_SCENE_FILE) == scene_hash, "the checkout's SceneConfig.xml changed before Save")
    print(f"editor cycle: the rename made {scene['id']} dirty, nothing written yet", flush=True)
    editor.save(session)
    check(not editor.dirty(session) and not editor.any_dirty(session), "after Save the editor still reports unsaved changes")
    check(scene_file.is_file(), f"Save wrote no file at {scene_file}")
    check(f'name="{CYCLE_RENAMED}"' in scene_file.read_text(encoding="utf-8"), f"the saved file has no node '{CYCLE_RENAMED}'")
    check(file_hash(STRESS_SCENE_FILE) == scene_hash, f"Save changed {STRESS_SCENE_FILE}")
    print("editor cycle: Save cleared the flag and wrote the overlay's file only; the checkout's SceneConfig.xml is unchanged", flush=True)


def scenario_editor_cycle(exe: Path) -> None:
    """An edit makes the scene dirty, Save clears it and writes the overlay's copy, the checkout is never written. Its own editor, not `play`: the moves need the helpers of a `GameSession`. A build without the editor skips."""
    scene_hash = file_hash(STRESS_SCENE_FILE)
    session = GameSession()
    try:
        with temporary_editor_directory(), temporary_folder() as overlay:
            run_mode = (launch_cycle_editor(session, exe, overlay).get("status") or {}).get("runMode")
            if run_mode == "Game":
                print("editor cycle: SKIPPED, status.runMode is Game (the build has no editor)", flush=True)
                return
            check(run_mode == "Editor", f"status.runMode is '{run_mode}' with --editor")
            drive_editor_cycle(session, overlay, scene_hash)
            code = session.quit().get("exitCode")
            check(code == 0, f"the game exited with code {code} after quit")
            problems = session.log_problems()
            check(not problems, "the game's log has problems:\n" + "\n".join(problems))
    except (SessionError, ControlError, ConnectionClosed, OSError, KeyError) as error:
        raise editor_session_error(session, error) from error
    finally:
        session.close()


@contextlib.contextmanager
def temporary_folder() -> Iterator[Path]:
    with tempfile.TemporaryDirectory() as folder:
        yield Path(folder)


@contextlib.contextmanager
def temporary_editor_directory() -> Iterator[None]:
    """The editor writes preferences and layouts under $BAGI_EDITOR_DIR; the game started inside inherits the variable."""
    previous = os.environ.get(EDITOR_DIR_VARIABLE)
    with tempfile.TemporaryDirectory() as folder:
        os.environ[EDITOR_DIR_VARIABLE] = folder
        try:
            yield
        finally:
            if previous is None:
                del os.environ[EDITOR_DIR_VARIABLE]
            else:
                os.environ[EDITOR_DIR_VARIABLE] = previous


# (name, backend, batching): a fresh game on a hidden window of the GPU; a machine without the backend skips them (never a failure)
GPU_SCENARIOS: list[tuple[str, str, bool]] = [
    ("sdl-gpu batched", "sdl-gpu", True),
    ("sdl-gpu unbatched", "sdl-gpu", False),
    ("vulkan batched", "vulkan", True),
    ("vulkan unbatched", "vulkan", False),
]
# The Vulkan backend has no blend modes and says so once, the first time the sprites scene asks for one
GPU_TOLERATED = ("the blend mode Add is not implemented",)


class BackendUnavailable(Exception):
    """The game said it cannot start the backend asked for (no display, no loader, not in this build)."""


SCENARIOS: list[tuple[str, list[str], Callable[..., None]]] = [  # `editor` takes the executable and its overlay folder too: main binds them
    ("sprites", ["--stress-scene=sprites", f"--stress-count={SPRITES_COUNT}"], scenario_sprites),
    ("entities", ["--stress-scene=entities", f"--stress-count={ENTITIES_COUNT}"], scenario_entities),
    ("ui", ["--stress-scene=ui", f"--stress-count={UI_COUNT}"], scenario_ui),
    ("scenes", ["--stress-scene=sprites", f"--stress-count={SPRITES_COUNT}"], scenario_scenes),
    ("reflection", ["--stress-scene=sprites", f"--stress-count={REFLECTION_COUNT}"], scenario_reflection),
    ("rmlui", ["--stress-scene=sprites", f"--stress-count={RMLUI_COUNT}"], scenario_rmlui),
    ("editor", editor_flags(SPRITES_COUNT, editor=True), scenario_editor),
]


def play(exe: Path, flags: list[str], scenario: Callable[[BagiControlClient], None], port: int, tolerated: tuple[str, ...] = (), headless: bool = True) -> None:
    """One fresh game: the scenario, `quit`, a clean exit and a log with no problems (a line with one of the `tolerated` texts is not one). Raises SmokeFailure (PortTaken text in the message when the port was lost) and BackendUnavailable (the game logged that the backend of `flags` cannot start)."""
    process = GameProcess(build_game_command(exe, port, headless=headless, manual_clock=True, extra=flags), cwd=exe.parent)
    try:
        process.start()
        try:
            try:
                client = process.connect(port, STATUS_STARTUP_TIMEOUT)
            except GameLaunchError as error:
                if BACKEND_UNAVAILABLE_MARKER in str(error):
                    raise BackendUnavailable(next(line for line in str(error).splitlines() if BACKEND_UNAVAILABLE_MARKER in line)) from error
                raise
            try:
                scenario(client)
                client.call("quit")
            finally:
                try:
                    client.close()
                except (OSError, ConnectionClosed):
                    pass
            code = process.stop(SHUTDOWN_TIMEOUT)
            check(code == 0, f"the game exited with code {code} after quit")
            problems = [line for line in process.log_problems() if not any(text in line for text in tolerated)]
            check(not problems, "the game's log has problems:\n" + "\n".join(problems))
        except (SmokeFailure, ControlError, ConnectionClosed, GameLaunchError, OSError, KeyError) as error:
            raise SmokeFailure(f"{error!r}\n--- game log (tail) ---\n{process.log_tail()}") from error
    finally:
        process.stop(0)


def play_with_retry(exe: Path, flags: list[str], scenario: Callable[[BagiControlClient], None], port: int, name: str, tolerated: tuple[str, ...] = (), headless: bool = True) -> None:
    """play() on `port` (0: a free one); once more on another port when the game says that port was taken (free_port hands it back before the bind)."""
    try:
        play(exe, flags, scenario, port or free_port(), tolerated, headless)
    except SmokeFailure as error:
        if port or PORT_TAKEN_MARKER not in str(error):
            raise
        print(f"{name}: the port was taken by another process; retrying once on another port", flush=True)
        play(exe, flags, scenario, free_port(), tolerated, headless)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, help="the BagiStress executable (or --preset)")
    parser.add_argument("--preset", help="the CMake preset that built it: the executable is build/<preset>/bin/BagiStress")
    parser.add_argument("--port", type=int, default=0, help="control port (default: a free one for every run)")
    args = parser.parse_args()
    if (args.exe is None) == (args.preset is None):
        parser.error("give the executable either as --exe or as --preset")
    exe = args.exe or preset_executable(args.preset, "BagiStress", ENGINE_DIR)
    if not exe.is_file():
        print(f"SKIPPED: {exe} does not exist")
        return EXIT_SKIPPED
    exe = exe.resolve()
    for name, flags, scenario in SCENARIOS:
        print(f"{name}: running", flush=True)
        is_editor = scenario is scenario_editor
        try:
            with contextlib.ExitStack() as stack:
                if is_editor:
                    stack.enter_context(temporary_editor_directory())
                    overlay = stack.enter_context(temporary_folder())
                    flags = [*flags, f"{CONTENT_OVERLAY_FLAG}{overlay}"]
                    scenario = functools.partial(scenario, exe=exe, overlay=overlay)
                play_with_retry(exe, flags, scenario, args.port, name, (NO_EDITOR_WARNING,) if is_editor else ())
        except SmokeFailure as error:
            print(f"FAILED ({name}): {error}")
            return EXIT_FAILED
    print("editor cycle: running", flush=True)
    try:
        scenario_editor_cycle(exe)
    except SmokeFailure as error:
        print(f"FAILED (editor cycle): {error}")
        return EXIT_FAILED
    for name, backend, batching in GPU_SCENARIOS:
        print(f"{name}: running", flush=True)
        flags = ["--stress-scene=sprites", f"--stress-count={GPU_SPRITES_COUNT}", "--hidden-window", f"--backend={backend}"]
        if not batching:
            flags.append("--stress-no-batching")
        try:
            play_with_retry(exe, flags, functools.partial(scenario_gpu_batching, batching=batching), args.port, name, GPU_TOLERATED, headless=False)
        except BackendUnavailable as error:
            print(f"SKIPPED ({name}): {error}")
        except SmokeFailure as error:
            print(f"FAILED ({name}): {error}")
            return EXIT_FAILED
    print("PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
