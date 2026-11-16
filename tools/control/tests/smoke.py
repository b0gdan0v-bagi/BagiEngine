#!/usr/bin/env python3
"""Smoke test of the control API: starts a game executable with --control and checks what every game answers, over WebSocket.

    python tools/control/tests/smoke.py --exe <path of a built game, e.g. BagiStress>
    python tools/control/tests/smoke.py --preset macos-debug --game BagiStress     # build/macos-debug/bin/BagiStress

The game is started twice, once on the real clock and once with --manual-clock (game time moves only on `step`). The default
scenario is game-neutral: it uses no game rules (the methods `ping`, `list_games`, `status`, `step`, `frame_stats`, `zone_allocs`,
the answer to garbage and to an unknown method, a screenshot, the UI tree, the log marks, `batch`, `run_until` and `set_window`), so it fits any game that implements the control API.
A game with rules brings its own script that imports the helpers of this file and calls run_smoke() with its scenarios
(BagiEngine-Chess has one: tests/control_smoke.py; `be_add_game_control_tests(<exe> SCRIPT <file>)` runs it). The game runs
--headless (no window, drawn in memory), which also lets the test look at screenshots; --window runs it in a real window instead
(it needs a display, and a screenshot is not possible on the Vulkan backend, so those checks are left out; `set_window` is then only `not_headless`,
and the scenario that starts the game with `--window=` / `--density=` is skipped, as the game refuses them without `--headless`).
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
from game_process import PORT_TAKEN_MARKER, free_port, log_tail, preset_executable  # noqa: E402
from png_reader import PngError, PngImage, read_png  # noqa: E402

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

CONNECT_RETRY_SECONDS = 0.1
EVENT_TIMEOUT_SECONDS = 5.0
IDLE_CHECK_SECONDS = 0.3  # how long the manual clock is watched not moving
DEFAULT_STEP_SECONDS = 1.0 / 60.0
DEFAULT_SCREENSHOT_SIZE = 1024
SMALL_SCREENSHOT_SIZE = 200
MIN_SCREENSHOT_COLORS = 3  # a picture of a game is not flat: a background and at least two more colours
RUN_UNTIL_FRAMES = 3
BOARD_COLORS = 5  # for the script of a game that draws a board (tiles, background, frame): at least this many colours
CLOCK_TOLERANCE = 1e-6
PIXEL_TOLERANCE = 0.5  # of a length that went through a float scale and back
WINDOW_SIZE = 480  # window units of the size the window step sets, a width and a height
HIGH_DENSITY = 2.0
NOTCH_TOP = 40.0  # window units of the safe-area inset the window step sets at the top
SCREENSHOT_LIMIT = 4096  # the longest side `screenshot` can be asked for: no shrinking of a picture of the window step
BAD_USER_SCALE = 100.0
WINDOW_FLAGS_SCENARIO = ["--manual-clock", f"--window={WINDOW_SIZE}x{WINDOW_SIZE}", f"--density={HIGH_DENSITY:g}"]
HEADLESS_ONLY_FLAGS = ("--window=", "--density=")  # the game stops at start-up when it gets one without --headless


class SmokeFailure(Exception):
    pass


class PortTaken(SmokeFailure):
    """The game could not bind its control port: free_port() hands the number back before the game binds it, so another process may have taken it in between."""


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


def play_basics(client: BagiControlClient) -> None:
    """The part of the scenario that does not depend on the clock or on the game's rules: the common methods and the garbage handling."""
    pong = client.call("ping")
    check(pong.get("pong") is True and isinstance(pong.get("protocol"), int), f"ping answered {pong}")

    games = client.call("list_games")
    check(games["active"] is not None, "no game is active")
    check(any(game["name"] == games["active"] for game in games["games"]), f"the active game is not in the list: {games}")

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
    """What a screenshot has to be on either clock: a picture of the game that is not flat, and not larger than asked."""
    image = take_screenshot(client)
    check(max(image.width, image.height) == DEFAULT_SCREENSHOT_SIZE, f"the default screenshot is {image.width}x{image.height}, its longer side should be {DEFAULT_SCREENSHOT_SIZE}")
    check(len(image.colors()) >= MIN_SCREENSHOT_COLORS, f"the screenshot has {len(image.colors())} colours, it is flat")

    small = take_screenshot(client, {"maxSize": SMALL_SCREENSHOT_SIZE})
    check(max(small.width, small.height) == SMALL_SCREENSHOT_SIZE, f"maxSize {SMALL_SCREENSHOT_SIZE} gave {small.width}x{small.height}")
    expect_error("invalid_params", lambda: client.call("screenshot", {"maxSize": 5}), "a screenshot of 5 pixels")


def check_screenshot_takes_one_frame(client: BagiControlClient) -> None:
    """On the manual clock a screenshot is one frame in which game time does not move."""
    before = client.call("status")
    take_screenshot(client)
    after = client.call("status")
    check(after["frame"] == before["frame"] + 1, f"a screenshot took {after['frame'] - before['frame']} frames, not one")
    check(approx(after["totalTime"], before["totalTime"]), "a screenshot moved the game time")


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


def check_ui_tree(client: BagiControlClient) -> None:
    """The in-game UI over the control API: the canvases of the active scene (a game may have none) are laid out and the validator finds nothing."""
    client.call("step", {"frames": 2})
    tree = client.call("ui_tree")
    check(isinstance(tree.get("scene"), str) and isinstance(tree.get("canvases"), list), f"ui_tree has no scene and canvases: {tree}")
    check(all(canvas["laidOut"] and not canvas["issues"] for canvas in tree["canvases"]), f"a canvas has layout issues: {tree}")
    expect_error("not_found", lambda: client.call("ui_click", {"name": "NoSuchElement"}), "a click on an element that does not exist")
    expect_error("invalid_params", lambda: client.call("ui_click", {}), "a click without a name")


def safe_area_of(top: float) -> dict:
    return {"top": top, "right": 0.0, "bottom": 0.0, "left": 0.0}


def check_screen_canvases_follow(client: BagiControlClient, window: dict) -> None:
    """Every laid-out Screen canvas of `ui_tree` is the output of the window: bounds times scale are its pixels, the safe area keeps the top inset, an element's outputRect is its rect times the scale.

    The elements may overflow a small window (that is the layout of the game, `issues` says so), so the checks are of the canvas and of the units.
    """
    width, height, density = window["outputWidth"], window["outputHeight"], window["density"]
    for canvas in client.call("ui_tree")["canvases"]:
        if canvas["space"] != "Screen" or not canvas["laidOut"]:
            continue
        name, scale, bounds, safe = canvas["node"], canvas["scale"], canvas["bounds"], canvas["safeBounds"]
        check(abs(bounds["w"] * scale - width) <= PIXEL_TOLERANCE and abs(bounds["h"] * scale - height) <= PIXEL_TOLERANCE,
              f"the canvas '{name}' is {bounds['w'] * scale}x{bounds['h'] * scale} pixels in a window of {width}x{height}: it did not follow the window")
        check(abs(safe["y"] * scale - window["safeArea"]["top"] * density) <= PIXEL_TOLERANCE, f"the canvas '{name}' has its safe area at {safe['y'] * scale} pixels, the window's top inset is {window['safeArea']['top'] * density}")
        check(canvas["contentBounds"] in (bounds, safe), f"the canvas '{name}' lays out in {canvas['contentBounds']}, neither its bounds nor its safe area")
        stack = list(canvas["elements"])
        while stack:
            element = stack.pop()
            stack.extend(element["children"])
            rect, output = element.get("rect"), element.get("outputRect")
            if element["visible"] and rect is not None and output is not None:
                check(abs(output["w"] - rect["w"] * scale) <= PIXEL_TOLERANCE and abs(output["h"] - rect["h"] * scale) <= PIXEL_TOLERANCE,
                      f"the element '{element['name']}' of '{name}' is {output['w']}x{output['h']} pixels, its rect {rect['w']}x{rect['h']} at the scale {scale}")


def check_window_applied(client: BagiControlClient, request: dict, density: float, top: float) -> None:
    """`set_window` answers the window object that `status` shows, the picture and the canvases of the next calls are of its size."""
    answer = client.call("set_window", {**request, "density": density, "safeArea": safe_area_of(top)})
    width, height = round(request["width"] * density), round(request["height"] * density)
    check((answer["width"], answer["height"], answer["density"]) == (request["width"], request["height"], density), f"set_window answered {answer}")
    check((answer["outputWidth"], answer["outputHeight"]) == (width, height), f"set_window answered the output {answer['outputWidth']}x{answer['outputHeight']}, not {width}x{height}")
    check(answer["safeArea"] == safe_area_of(top) and answer["headless"] is True, f"set_window answered the safe area {answer['safeArea']}")
    check(client.call("status")["window"] == answer, "status.window is not what set_window answered")
    shot = take_screenshot(client, {"maxSize": SCREENSHOT_LIMIT})
    check((shot.width, shot.height) == (width, height), f"the screenshot after set_window is {shot.width}x{shot.height}, not {width}x{height}")
    check_screen_canvases_follow(client, answer)


def check_window_control(client: BagiControlClient, headless: bool) -> None:
    """`set_window` and `status.window`: a real window refuses; a headless one takes a size, a density and a notch, and goes back to what it was."""
    original = client.call("status")["window"]
    check(original["headless"] is headless, f"status.window.headless is {original['headless']} in a run with headless {headless}")
    if not headless:
        expect_error("not_headless", lambda: client.call("set_window", {"width": WINDOW_SIZE, "height": WINDOW_SIZE}), "set_window on a real window")
        check(client.call("status")["window"] == original, "a refused set_window changed the window")
        return

    square = {"width": WINDOW_SIZE, "height": WINDOW_SIZE}
    check_window_applied(client, square, 1.0, 0.0)
    check_window_applied(client, square, HIGH_DENSITY, NOTCH_TOP)

    changed = client.call("status")["window"]
    for params, description in (
        ({"width": 0, "height": WINDOW_SIZE}, "a window of no width"),
        ({**square, "safeArea": safe_area_of(-1.0)}, "a negative inset"),
        ({**square, "safeArea": {"top": 1.0}}, "a safe area of one side"),
        ({**square, "userScale": BAD_USER_SCALE}, "a user scale out of range"),
        ({**square, "density": 0.0}, "a density of 0"),
    ):
        expect_error("invalid_params", lambda params=params: client.call("set_window", params), description)
        check(client.call("status")["window"] == changed, f"{description} changed the window")

    restored = client.call(
        "set_window",
        {"width": original["width"], "height": original["height"], "density": original["density"], "userScale": original["userScale"], "safeArea": original["safeArea"]},
    )
    check(restored == original, f"the window was {original} and is {restored} after it was set back")
    check_screen_canvases_follow(client, restored)


def check_window_flags(client: BagiControlClient, headless: bool) -> None:
    """A game started with --window=WxH --density=D has that window at once."""
    window = client.call("status")["window"]
    check((window["width"], window["height"], window["density"]) == (WINDOW_SIZE, WINDOW_SIZE, HIGH_DENSITY), f"the start flags gave the window {window}")
    size = round(WINDOW_SIZE * HIGH_DENSITY)
    check((window["outputWidth"], window["outputHeight"]) == (size, size), f"the start flags gave the output {window['outputWidth']}x{window['outputHeight']}")
    shot = take_screenshot(client, {"maxSize": SCREENSHOT_LIMIT})
    check((shot.width, shot.height) == (size, size), f"the screenshot of the flagged window is {shot.width}x{shot.height}")
    client.call("step", {"frames": 2})
    check_screen_canvases_follow(client, window)


def check_log_and_batch(client: BagiControlClient) -> None:
    """The log counts and marks, and `batch` with its stop at the first error: nothing here depends on the game's rules."""
    counts = client.call("status")["log"]
    check(all(isinstance(counts.get(name), int) for name in ("warnings", "errors")), f"status.log has no two counts: {counts}")

    mark = client.call("log_mark")
    since = client.call("log_since", {"mark": mark["mark"]})
    check(all(line["sequence"] >= mark["mark"] for line in since["lines"]), f"log_since gave a line before the mark {mark['mark']}: {since['lines']}")
    check(since["next"] >= mark["mark"], f"log_since says next {since['next']} behind the mark {mark['mark']}")
    expect_error("invalid_params", lambda: client.call("log_since", {"mark": -1}), "log_since of a negative mark")
    expect_error("invalid_params", lambda: client.call("log_since", {"mark": 0, "level": "info"}), "log_since of the level info")

    calls = [{"method": "ping"}, {"method": "no_such_method"}, {"method": "status"}]
    stopped = client.call("batch", {"calls": calls})
    check(stopped.get("stopped") == 1 and len(stopped["results"]) == 2, f"a batch did not stop at the unknown method: {stopped}")
    check(stopped["results"][0]["ok"] is True and stopped["results"][1]["error"]["code"] == "unknown_method", f"a batch answered {stopped['results']}")
    carried = client.call("batch", {"calls": calls, "continueOnError": True})
    check("stopped" not in carried and [entry["ok"] for entry in carried["results"]] == [True, False, True], f"a batch with continueOnError answered {carried}")
    expect_error("invalid_params", lambda: client.call("batch", {"calls": []}), "a batch of no calls")


def check_run_until(client: BagiControlClient, manual_clock: bool) -> None:
    """`run_until` ends at its limit when nothing moves the state; a game that cannot be read answers what `get_state` does."""
    games = client.call("list_games")
    active = next(game for game in games["games"] if game["name"] == games["active"])
    condition = {"versionMoved": True}
    if not active["controllable"]:
        expect_error("not_controllable", lambda: client.call("run_until", {"condition": condition}), "run_until of a game that is not controllable")
        return
    expect_error("invalid_params", lambda: client.call("run_until", {"condition": {}}), "run_until without a condition")
    expect_error("invalid_params", lambda: client.call("run_until", {"condition": condition, "maxFrames": 0}), "run_until of no frames")

    before = client.call("status")["frame"]
    answer = client.call("run_until", {"condition": condition, "maxFrames": RUN_UNTIL_FRAMES})
    check(isinstance(answer["met"], bool) and answer["frames"] <= RUN_UNTIL_FRAMES, f"run_until answered {answer}")
    if manual_clock:
        check(answer["met"] is False and answer["frames"] == RUN_UNTIL_FRAMES, f"run_until with no action did not end at its limit: {answer}")
        check(client.call("status")["frame"] == before + RUN_UNTIL_FRAMES, "run_until did not run exactly its frames")
        batch = client.call("batch", {"calls": [{"method": "step", "params": {"frames": 2}}, {"method": "status"}]})
        check(batch["results"][1]["result"]["frame"] == before + RUN_UNTIL_FRAMES + 2, f"the call behind a step of a batch ran before its frames: {batch}")


def run_real_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["frame"] >= 1, f"status reports frame {status['frame']}")
    check(status["manualClock"] is False, "the manual clock is on without --manual-clock")

    play_basics(client)

    # On the real clock a step is a wait for frames that run anyway
    before = client.call("status")["frame"]
    after = client.call("step", {"frames": 3})["frame"]
    check(after >= before + 3, f"step 3 moved the frame from {before} to {after}")

    if headless:
        check_screenshot_basics(client)

    check_log_and_batch(client)
    check_run_until(client, manual_clock=False)
    check_window_control(client, headless)


def check_frame_stats(client: BagiControlClient) -> None:
    """The frame statistics of the stepped frames: the scene draws something every frame."""
    frames = client.call("frame_stats", {"frames": 3})["frames"]
    check(len(frames) == 3, f"frame_stats of 3 frames answered {len(frames)}")
    check(frames[-1]["frame"] == frames[0]["frame"] + 2, f"frame_stats is not the last frames in order: {[f['frame'] for f in frames]}")
    check(all(frame["drawCalls"] > 0 for frame in frames), f"a frame drew nothing: {[f['drawCalls'] for f in frames]}")
    check(all(frame["cpuMs"] >= 0 and "Update" in frame["stagesMs"] for frame in frames), f"frame_stats has no stage times: {frames[-1]}")
    expect_error("invalid_params", lambda: client.call("frame_stats", {"frames": 0}), "frame_stats of no frames")
    check("totals" in client.call("zone_allocs"), "zone_allocs has no totals")


def run_manual_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["manualClock"] is True, f"--manual-clock did not turn the manual clock on: {status}")
    check(status["frame"] == 0 and status["totalTime"] == 0, f"a frame ran before the first step: {status}")

    # Nothing moves on its own, and the requests are still answered
    time.sleep(IDLE_CHECK_SECONDS)
    check(client.call("status")["frame"] == 0, "the manual clock ticked without a step")

    play_basics(client)
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

    check_frame_stats(client)

    if headless:
        check_screenshot_basics(client)
        check_screenshot_takes_one_frame(client)
    else:
        expect_error("capture_failed", lambda: client.call("screenshot"), "a screenshot on a backend that cannot capture")

    check_ui_tree(client)
    check_log_and_batch(client)
    check_run_until(client, manual_clock=True)
    check_window_control(client, headless)


SCENARIOS = [
    ("real clock", [], run_real_clock_scenario),
    ("manual clock", ["--manual-clock"], run_manual_clock_scenario),
    ("window flags", WINDOW_FLAGS_SCENARIO, check_window_flags),
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
                tail = log_tail(log_path)
                failure = PortTaken if PORT_TAKEN_MARKER in tail else SmokeFailure  # the one failure that play_with_retry retries
                raise failure(f"{error}\n--- game log (tail) ---\n{tail}") from error
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


def play_with_retry(exe: Path, flags: list[str], scenario: Callable[[BagiControlClient, bool], None], headless: bool, startup_timeout: float, shutdown_timeout: float, port: int, name: str) -> None:
    """play() on `port` (0: a free one). When the game reports that it could not bind a port that free_port() chose, tries once more on another one.

    Any other failure is not retried, and neither is a port given on the command line.
    """
    try:
        play(exe, flags, scenario, headless, startup_timeout, shutdown_timeout, port or free_port())
    except PortTaken:
        if port:
            raise
        print(f"{name}: the port was taken by another process before the game could bind it; retrying once on another port", flush=True)
        play(exe, flags, scenario, headless, startup_timeout, shutdown_timeout, free_port())


def run_smoke(description: str, scenarios: list[tuple[str, list[str], Callable[[BagiControlClient, bool], None]]]) -> int:
    """Parses the command line and plays every scenario (name, extra game flags, function(client, headless)) in a fresh game."""
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, help="the game executable (or --preset and --game)")
    parser.add_argument("--preset", help="the CMake preset that built the game: the executable is build/<preset>/bin/<game>")
    parser.add_argument("--game", help="the executable name for --preset, e.g. BagiStress")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    parser.add_argument("--port", type=int, default=0, help="control port (default: a free one for every run)")
    parser.add_argument("--window", action="store_true", help="run the game in a real window instead of --headless (needs a display; no screenshot checks)")
    parser.add_argument("--startup-timeout", type=float, default=30.0, help="seconds to wait for the control API")
    parser.add_argument("--shutdown-timeout", type=float, default=15.0, help="seconds to wait for the game to exit after quit")
    args = parser.parse_args()

    by_preset = args.preset is not None or args.game is not None
    if (args.exe is not None) == by_preset or (by_preset and (args.preset is None or args.game is None)):
        parser.error("give the executable either as --exe, or as --preset together with --game")
    if args.exe is None:
        args.exe = preset_executable(args.preset, args.game, args.root)

    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED

    exe = args.exe.resolve()  # the game runs with its own directory as the working directory
    for name, flags, scenario in scenarios:
        if args.window and any(flag.startswith(HEADLESS_ONLY_FLAGS) for flag in flags):
            print(f"{name}: skipped, a real window takes no --window / --density", flush=True)
            continue
        print(f"{name}: running", flush=True)
        try:
            play_with_retry(exe, flags, scenario, not args.window, args.startup_timeout, args.shutdown_timeout, args.port, name)
        except SmokeFailure as error:
            print(f"FAILED ({name}): {error}")
            return EXIT_FAILED

    print("PASSED")
    return EXIT_PASSED


def main() -> int:
    return run_smoke(__doc__, SCENARIOS)


if __name__ == "__main__":
    sys.exit(main())
