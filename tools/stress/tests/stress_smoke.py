"""Control smoke test of BagiStress (the ctest entry BagiStressControlSmoke, `be_add_game_control_tests`).

Starts the app in a fresh process per scene (sprites 500, entities 500, ui 200), `--headless --control --manual-clock`, and drives
it over the control API: the state, `set_count` shown in the next frame (`frame_stats` draw calls and the `Stress objects` counter),
the bad values, screenshots that are not flat, `set_language` rewording the `ui` scene, `set_scene`, `ui_tree`, and a log with no
Warning or Error. The helpers are those of tools/control/tests/smoke.py (the Match3 scenarios are not touched).

Unlike smoke.py's `run_smoke` this script drives the process through `GameProcess`, because the check of the game's log needs the log's
path and `smoke.play` keeps it to itself. Exit codes as smoke.py: 0 passed, 1 failed, 77 skipped (the executable is not built).
"""

import argparse
import os
import sys
from pathlib import Path
from typing import Callable

ENGINE_DIR = Path(os.environ["BAGI_ENGINE_DIR"]) if "BAGI_ENGINE_DIR" in os.environ else Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import PORT_TAKEN_MARKER, GameLaunchError, GameProcess, build_game_command, free_port, preset_executable  # noqa: E402
from smoke import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, SmokeFailure, check, expect_error, take_screenshot  # noqa: E402

STATUS_STARTUP_TIMEOUT = 30.0
SHUTDOWN_TIMEOUT = 15.0
SETTLE_FRAMES = 3  # a change made through the control API reaches the scene in the next frame; one more for the scene switch
MIN_SHOT_COLORS = 3  # a picture of a scene is not flat: a background and at least two more colours
OBJECTS_COUNTER = "Stress objects"
SPRITES_COUNT = 500
ENTITIES_COUNT = 500
UI_COUNT = 200


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
    expect_error("invalid_params", lambda: client.call("perform_action", {"type": "set_scene", "scene": "nowhere"}), "a scene that does not exist")


def scenario_entities(client: BagiControlClient) -> None:
    check_state(client, "entities", ENTITIES_COUNT)
    check(last_frame(client)["drawCalls"] == 0, "the entities scene drew something")
    check(set_count(client, ENTITIES_COUNT * 2)["drawCalls"] == 0, "the entities scene drew something after set_count")
    check_bad_counts(client)


def scenario_ui(client: BagiControlClient) -> None:
    check_state(client, "ui", UI_COUNT)
    check(last_frame(client)["drawCalls"] > 0, "the ui scene drew nothing")
    check_ui_tree(client, "StressUi")
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


SCENARIOS: list[tuple[str, list[str], Callable[[BagiControlClient], None]]] = [
    ("sprites", ["--stress-scene=sprites", f"--stress-count={SPRITES_COUNT}"], scenario_sprites),
    ("entities", ["--stress-scene=entities", f"--stress-count={ENTITIES_COUNT}"], scenario_entities),
    ("ui", ["--stress-scene=ui", f"--stress-count={UI_COUNT}"], scenario_ui),
    ("scenes", ["--stress-scene=sprites", f"--stress-count={SPRITES_COUNT}"], scenario_scenes),
]


def play(exe: Path, flags: list[str], scenario: Callable[[BagiControlClient], None], port: int) -> None:
    """One fresh game: the scenario, `quit`, a clean exit and a log with no problems. Raises SmokeFailure (PortTaken text in the message when the port was lost)."""
    process = GameProcess(build_game_command(exe, port, headless=True, manual_clock=True, extra=flags), cwd=exe.parent)
    try:
        process.start()
        try:
            client = process.connect(port, STATUS_STARTUP_TIMEOUT)
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
            problems = process.log_problems()
            check(not problems, "the game's log has problems:\n" + "\n".join(problems))
        except (SmokeFailure, ControlError, ConnectionClosed, GameLaunchError, OSError, KeyError) as error:
            raise SmokeFailure(f"{error!r}\n--- game log (tail) ---\n{process.log_tail()}") from error
    finally:
        process.stop(0)


def play_with_retry(exe: Path, flags: list[str], scenario: Callable[[BagiControlClient], None], port: int, name: str) -> None:
    """play() on `port` (0: a free one); once more on another port when the game says that port was taken (free_port hands it back before the bind)."""
    try:
        play(exe, flags, scenario, port or free_port())
    except SmokeFailure as error:
        if port or PORT_TAKEN_MARKER not in str(error):
            raise
        print(f"{name}: the port was taken by another process; retrying once on another port", flush=True)
        play(exe, flags, scenario, free_port())


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
        try:
            play_with_retry(exe, flags, scenario, args.port, name)
        except SmokeFailure as error:
            print(f"FAILED ({name}): {error}")
            return EXIT_FAILED
    print("PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
