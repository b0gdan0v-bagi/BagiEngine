#!/usr/bin/env python3
"""Smoke test of the control API: starts a game executable with --control and plays a few moves over WebSocket.

    python tools/control/tests/smoke.py --exe build/ClangmacOS/BagiMatch3

The game is started twice, once on the real clock and once with --manual-clock (game time moves only on `step`). The
scenario is the one of Match3 (a swap on a seeded board); another game brings its own script that calls run_smoke() with its
scenarios (games/Chess/tests/control_smoke.py). The game runs --headless (no window, drawn in memory), which also
lets the test look at screenshots; --window runs it in a real window instead (it needs a display, and a screenshot is not
possible on the Vulkan backend, so those checks are left out).
Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist; ctest reads 77 as "skipped").
"""

import argparse
import base64
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import free_port, log_tail  # noqa: E402
from png_reader import PngError, PngImage, read_png  # noqa: E402

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

CONNECT_RETRY_SECONDS = 0.1
BOARD_SIZE = 8
EVENT_TIMEOUT_SECONDS = 5.0
IDLE_CHECK_SECONDS = 0.3  # how long the manual clock is watched not moving
DEFAULT_STEP_SECONDS = 1.0 / 60.0
DEFAULT_SCREENSHOT_SIZE = 1024
SMALL_SCREENSHOT_SIZE = 200
BOARD_COLORS = 5  # a Match3 board shows at least this many different colours (tiles, background, frame)
CLOCK_TOLERANCE = 1e-6


class SmokeFailure(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(message)


def approx(left: float, right: float) -> bool:
    return abs(left - right) <= CLOCK_TOLERANCE


def expect_error(code: str, action: Callable[[], object], description: str) -> None:
    try:
        action()
    except ControlError as error:
        check(error.code == code, f"{description}: expected the error '{code}', got '{error.code}'")
        return
    raise SmokeFailure(f"{description}: expected the error '{code}', got a success")


def connect(process: subprocess.Popen, port: int, timeout: float) -> BagiControlClient:
    """Retries until the game listens; fails at once when the game exits instead."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SmokeFailure(f"the game exited with code {process.returncode} before the control API was up")
        client = BagiControlClient(port=port, timeout=timeout)
        try:
            client.connect()
            return client
        except (ConnectionRefusedError, ConnectionResetError):
            time.sleep(CONNECT_RETRY_SECONDS)
    raise SmokeFailure(f"the control API did not come up in {timeout} s")


def wait_for_event(client: BagiControlClient, name: str, version: int) -> dict:
    """The pushed event `name` about state version `version`: one that arrived during an earlier call, or one still to come.

    Events about older versions (the pushes of earlier actions can still be on their way) are skipped.
    """
    deadline = time.monotonic() + EVENT_TIMEOUT_SECONDS
    while True:
        for event in client.events:
            if event["event"] == name and event["data"].get("version") == version:
                client.events.remove(event)
                return event
        remaining = deadline - time.monotonic()
        check(remaining > 0, f"no '{name}' event about version {version} in {EVENT_TIMEOUT_SECONDS} s")
        try:
            message = client.receive_message(remaining)
        except TimeoutError:
            raise SmokeFailure(f"no '{name}' event about version {version} in {EVENT_TIMEOUT_SECONDS} s") from None
        check("event" in message, f"expected an event, got {message}")
        client.events.append(message)


def receive_response(client: BagiControlClient) -> dict:
    """The next response (pushed events on the way are kept in client.events)."""
    while True:
        message = client.receive_message()
        if "event" in message:
            client.events.append(message)
            continue
        return message


def play_match3(client: BagiControlClient) -> None:
    """The part of the scenario that does not depend on the clock: the game methods and the garbage handling."""
    pong = client.call("ping")
    check(pong.get("pong") is True and isinstance(pong.get("protocol"), int), f"ping answered {pong}")

    games = client.call("list_games")
    check(games["active"] is not None, "no game is active")
    check(any(game["controllable"] for game in games["games"]), f"no controllable game in {games}")

    # The same seed has to give the same board
    client.call("reset", {"seed": 1})
    first_board = client.call("get_state")["state"]["board"]
    client.call("reset", {"seed": 1})
    snapshot = client.call("get_state")
    check(snapshot["state"]["board"] == first_board, "the same seed gave two different boards")
    check(len(first_board) == BOARD_SIZE and all(len(row) == BOARD_SIZE for row in first_board), f"unexpected board {first_board}")
    version = snapshot["version"]

    actions = client.call("list_actions")["actions"]
    check(len(actions) > 0, "a fresh board has no legal action")

    outcome = client.call("perform_action", actions[0])
    check(outcome["outcome"]["scoreGained"] > 0, f"a legal swap scored nothing: {outcome}")
    check(outcome["version"] > version, "the state version did not grow after an action")
    check(client.call("get_state")["version"] == outcome["version"], "get_state disagrees with perform_action about the version")

    # An action that is not legal changes nothing
    version = client.call("get_state")["version"]
    not_neighbours = {"type": "swap", "from": {"col": 0, "row": 0}, "to": {"col": 5, "row": 5}}
    expect_error("illegal_action", lambda: client.call("perform_action", not_neighbours), "a swap of cells that are not neighbours")
    check(client.call("get_state")["version"] == version, "a refused action changed the state")

    expect_error("unknown_method", lambda: client.call("no_such_method"), "an unknown method")

    # What is not a request is answered, and the connection stays usable
    client.send_text("this is not json")
    reply = receive_response(client)
    check(reply.get("ok") is False and reply["error"]["code"] == "parse_error", f"garbage was answered with {reply}")
    check(reply.get("id") is None, "a request that cannot be parsed has no id to answer with")
    check(client.call("ping").get("pong") is True, "the connection broke after garbage")


def check_state_push(client: BagiControlClient, action_outcome: dict) -> None:
    """The push that follows an action: the same version and the same state that get_state gives."""
    event = wait_for_event(client, "state_changed", action_outcome["version"])
    data = event["data"]
    check(data["game"] == action_outcome["game"], f"state_changed is about the game '{data['game']}', the action was in '{action_outcome['game']}'")
    check(data["state"] == client.call("get_state")["state"], "state_changed carries another state than get_state")


def take_screenshot(client: BagiControlClient, params: dict | None = None) -> PngImage:
    """A `screenshot` call, decoded; the size it reports has to be the size of the picture."""
    shot = client.call("screenshot", params)
    try:
        image = read_png(base64.b64decode(shot["png_base64"]))
    except (PngError, ValueError) as error:
        raise SmokeFailure(f"the screenshot is not a readable PNG: {error}") from error
    check((image.width, image.height) == (shot["width"], shot["height"]), f"the screenshot says {shot['width']}x{shot['height']}, the picture is {image.width}x{image.height}")
    return image


def check_screenshot_basics(client: BagiControlClient) -> None:
    """What a screenshot has to be on either clock: a picture of the board, not larger than asked."""
    client.call("reset", {"seed": 1})
    image = take_screenshot(client)
    check(max(image.width, image.height) == DEFAULT_SCREENSHOT_SIZE, f"the default screenshot is {image.width}x{image.height}, its longer side should be {DEFAULT_SCREENSHOT_SIZE}")
    check(len(image.colors()) >= BOARD_COLORS, f"the screenshot has {len(image.colors())} colours, it does not look like a board")

    small = take_screenshot(client, {"maxSize": SMALL_SCREENSHOT_SIZE})
    check(max(small.width, small.height) == SMALL_SCREENSHOT_SIZE, f"maxSize {SMALL_SCREENSHOT_SIZE} gave {small.width}x{small.height}")
    expect_error("invalid_params", lambda: client.call("screenshot", {"maxSize": 5}), "a screenshot of 5 pixels")


def check_screenshots_on_the_manual_clock(client: BagiControlClient) -> None:
    """On the manual clock a screenshot is one frame in which nothing moves, and it shows what the game state says."""
    before = client.call("status")

    client.call("reset", {"seed": 1})
    first = take_screenshot(client)
    after = client.call("status")
    check(after["frame"] == before["frame"] + 1, f"a screenshot took {after['frame'] - before['frame']} frames, not one")
    check(approx(after["totalTime"], before["totalTime"]), "a screenshot moved the game time")

    client.call("reset", {"seed": 1})
    again = take_screenshot(client)
    check(again.rows == first.rows, "the same seed looked different in two screenshots")

    client.call("reset", {"seed": 2})
    other = take_screenshot(client)
    check(other.rows != first.rows, "another seed looked the same: the screenshot does not follow the game")


def find_ui_element(tree: dict, name: str) -> dict | None:
    """The first element called `name` in the canvases of a `ui_tree` answer."""
    def search(element: dict) -> dict | None:
        if element["name"] == name:
            return element
        for child in element["children"]:
            found = search(child)
            if found is not None:
                return found
        return None

    for canvas in tree["canvases"]:
        for element in canvas["elements"]:
            found = search(element)
            if found is not None:
                return found
    return None


def check_ui_methods(client: BagiControlClient) -> None:
    """The in-game UI over the control API: the tree has rects, and a click acts like a user's (Match3: from the menu into the game and back)."""
    client.call("step", {"frames": 2})
    tree = client.call("ui_tree")
    if tree["scene"] != "MainMenu":  # the earlier checks of this scenario may have switched to the game; go back by its Menu button
        client.call("ui_click", {"name": "Menu"})
        client.call("step", {"frames": 2})
        tree = client.call("ui_tree")
    check(tree["scene"] == "MainMenu", f"the Menu button did not lead to the menu: {tree['scene']}")
    play = find_ui_element(tree, "Play")
    check(play is not None and play["action"] == "Match3Play" and play["visible"], f"the menu has no visible Play button: {play}")
    check(play["outputRect"]["w"] > 0 and play["outputRect"]["h"] > 0, f"the Play button has no size: {play['outputRect']}")
    check(all(canvas["laidOut"] and not canvas["issues"] for canvas in tree["canvases"]), f"the menu has layout issues: {tree}")

    expect_error("not_found", lambda: client.call("ui_click", {"name": "NoSuchElement"}), "a click on an element that does not exist")
    expect_error("not_clickable", lambda: client.call("ui_click", {"name": "Title"}), "a click on a label")
    expect_error("invalid_params", lambda: client.call("ui_click", {}), "a click without a name")

    clicked = client.call("ui_click", {"name": "Play"})
    check(clicked["consumed"] and clicked["action"] == "Match3Play", f"the click on Play was not taken: {clicked}")
    client.call("step", {"frames": 2})
    tree = client.call("ui_tree")
    check(tree["scene"] != "MainMenu", f"Play did not leave the menu: {tree['scene']}")
    check(find_ui_element(tree, "Reset") is not None and find_ui_element(tree, "Score") is not None, "the game scene has no Reset button or Score label")

    client.call("reset", {"seed": 2})
    client.call("perform_action", client.call("list_actions")["actions"][0])
    check(client.call("get_state")["state"]["score"] > 0, "the first move scored nothing")
    client.call("ui_click", {"name": "Reset"})
    client.call("step", {"frames": 2})
    check(client.call("get_state")["state"]["score"] == 0, "the click on Reset did not reset the game")

    client.call("ui_click", {"name": "Menu"})
    client.call("step", {"frames": 2})
    check(client.call("ui_tree")["scene"] == "MainMenu", "the click on Menu did not go back to the menu")


def run_real_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["frame"] >= 1, f"status reports frame {status['frame']}")
    check(status["manualClock"] is False, "the manual clock is on without --manual-clock")

    play_match3(client)

    # A state change is pushed by itself, at the end of the frame that saw it
    client.call("reset", {"seed": 2})
    outcome = client.call("perform_action", client.call("list_actions")["actions"][0])
    check_state_push(client, outcome)

    # On the real clock a step is a wait for frames that run anyway
    before = client.call("status")["frame"]
    after = client.call("step", {"frames": 3})["frame"]
    check(after >= before + 3, f"step 3 moved the frame from {before} to {after}")

    if headless:
        check_screenshot_basics(client)


def run_manual_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["manualClock"] is True, f"--manual-clock did not turn the manual clock on: {status}")
    check(status["frame"] == 0 and status["totalTime"] == 0, f"a frame ran before the first step: {status}")

    # Nothing moves on its own, and the requests are still answered
    time.sleep(IDLE_CHECK_SECONDS)
    check(client.call("status")["frame"] == 0, "the manual clock ticked without a step")

    play_match3(client)
    check(client.call("status")["frame"] == 0, "a frame ran without a step")

    stepped = client.call("step", {"frames": 3, "dt": 0.1})
    check(stepped["frame"] == 3, f"step 3 gave frame {stepped['frame']}")
    check(approx(stepped["totalTime"], 0.3), f"step 3 of 0.1 s gave the time {stepped['totalTime']}")
    check(approx(stepped["deltaTime"], 0.1), f"the last frame lasted {stepped['deltaTime']} s, not 0.1")

    stepped = client.call("step")
    check(stepped["frame"] == 4, f"a step without params gave frame {stepped['frame']}")
    check(approx(stepped["deltaTime"], DEFAULT_STEP_SECONDS), f"a step without dt lasted {stepped['deltaTime']} s")
    check(approx(stepped["totalTime"], 0.3 + DEFAULT_STEP_SECONDS), f"the time after the default step is {stepped['totalTime']}")

    # A step holds back what follows it, so a pipelined status sees the frames
    client.send_text(json.dumps({"id": 900, "method": "step", "params": {"frames": 5}}))
    client.send_text(json.dumps({"id": 901, "method": "status"}))
    step_reply = receive_response(client)
    status_reply = receive_response(client)
    check(step_reply["id"] == 900 and step_reply["result"]["frame"] == 9, f"the pipelined step answered {step_reply}")
    check(status_reply["id"] == 901 and status_reply["result"]["frame"] == 9, f"the status behind the step answered {status_reply}")

    expect_error("invalid_params", lambda: client.call("step", {"frames": 0}), "a step of no frames")
    expect_error("invalid_params", lambda: client.call("step", {"frames": 1.5}), "a step of half a frame")
    expect_error("invalid_params", lambda: client.call("step", {"dt": -1}), "a step of negative time")
    expect_error("invalid_params", lambda: client.call("step", {"dt": 10}), "a step of ten seconds")
    check(client.call("status")["frame"] == 9, "a refused step ran frames")

    # The push of a state change comes at the end of the next frame, before the answer of the step that ran it
    client.call("reset", {"seed": 2})
    outcome = client.call("perform_action", client.call("list_actions")["actions"][0])
    client.events.clear()
    client.call("step")
    check_state_push(client, outcome)
    check(not client.events or all(event["data"].get("version") != outcome["version"] for event in client.events), "the same change was pushed twice")

    if headless:
        check_screenshot_basics(client)
        check_screenshots_on_the_manual_clock(client)
    else:
        expect_error("capture_failed", lambda: client.call("screenshot"), "a screenshot on a backend that cannot capture")

    check_ui_methods(client)


SCENARIOS = [
    ("real clock", [], run_real_clock_scenario),
    ("manual clock", ["--manual-clock"], run_manual_clock_scenario),
]


def play(exe: Path, flags: list[str], scenario: Callable[[BagiControlClient, bool], None], headless: bool, startup_timeout: float, shutdown_timeout: float, port: int) -> None:
    """Runs the game with the control API and the scenario, then quits it. Raises when anything fails; the log goes with it."""
    with tempfile.TemporaryDirectory() as temp_dir:
        log_path = Path(temp_dir) / "game.log"
        with open(log_path, "wb") as log:
            process = subprocess.Popen(
                [str(exe), "--control", f"--control-port={port}", *(["--headless"] if headless else []), *flags],
                cwd=exe.parent,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            try:
                client = connect(process, port, startup_timeout)
                try:
                    scenario(client, headless)
                    client.call("quit")
                finally:
                    try:
                        client.close()
                    except (OSError, ConnectionClosed):
                        pass
                try:
                    code = process.wait(timeout=shutdown_timeout)
                except subprocess.TimeoutExpired:
                    raise SmokeFailure(f"the game did not exit in {shutdown_timeout} s after quit")
                check(code == 0, f"the game exited with code {code} after quit")
            except (SmokeFailure, ControlError, ConnectionClosed, OSError) as error:
                raise SmokeFailure(f"{error}\n--- game log (tail) ---\n{log_tail(log_path)}") from error
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


def run_smoke(description: str, scenarios: list[tuple[str, list[str], Callable[[BagiControlClient, bool], None]]]) -> int:
    """Parses the command line and plays every scenario (name, extra game flags, function(client, headless)) in a fresh game."""
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, type=Path, help="the game executable")
    parser.add_argument("--port", type=int, default=0, help="control port (default: a free one for every run)")
    parser.add_argument("--window", action="store_true", help="run the game in a real window instead of --headless (needs a display; no screenshot checks)")
    parser.add_argument("--startup-timeout", type=float, default=30.0, help="seconds to wait for the control API")
    parser.add_argument("--shutdown-timeout", type=float, default=15.0, help="seconds to wait for the game to exit after quit")
    args = parser.parse_args()

    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED

    exe = args.exe.resolve()  # the game runs with its own directory as the working directory
    for name, flags, scenario in scenarios:
        print(f"{name}: running", flush=True)
        try:
            play(exe, flags, scenario, not args.window, args.startup_timeout, args.shutdown_timeout, args.port or free_port())
        except SmokeFailure as error:
            print(f"FAILED ({name}): {error}")
            return EXIT_FAILED

    print("PASSED")
    return EXIT_PASSED


def main() -> int:
    return run_smoke(__doc__, SCENARIOS)


if __name__ == "__main__":
    sys.exit(main())
