#!/usr/bin/env python3
"""A stand-in for a game executable: takes the start-up flags of the engine and serves a tiny control API.

    fake_game.py --control --control-port=N [--headless] [--manual-clock] [--exit-immediately]

The environment variable FAKE_GAME_EXIT makes it exit at once (code 3) like --exit-immediately. FAKE_GAME_PICTURE names a PNG file that `screenshot` serves instead of
the 2x1 one (a test makes the picture of the game change this way); FAKE_GAME_WINDOW_PICTURE does the same for a start without `--headless` (a window), and
FAKE_GAME_NO_WINDOW makes such a start exit at once (no display). FAKE_GAME_NO_PICTURE makes `screenshot` answer without a picture.

Methods: ping, status (also `log`: the totals of warnings and errors, and `window`), set_window (the limits of WindowLimits; on a start without `--headless`
`not_headless`; `--window=WxH` and `--density=D` give the start size, the picture served by `screenshot` keeps its own size), list_games, get_state (echoes the flags), list_actions, perform_action (a refusal for {"type": "bad"}, and for
{"type": "needs_noop"} until some action has run; it raises the state version by one and pushes state_changed behind its answer; {"type": "later", "frames": N} raises
it by one after N frames instead), step, screenshot (a 2x1 PNG; one frame on the manual
clock, as the real game), warn (logs a line of the given level, "Warning" by default: {"level": "Error", "text": "..."}),
log_mark, log_since, log_subscribe (a `log` push behind each `warn` while on), batch (runs `handle` per call), run_until (the frames of a wait tick the frame
counter), imgui_find / imgui_input (only with `--editor`, else `no_imgui`: IMGUI_MATCHES filtered like the engine does; imgui_input keeps the
events it took, `get_state` shows them as `imguiEvents`), quit (exits the process). Runs until quit, or until WATCHDOG_SECONDS have passed: on Windows the
tests start it through a .bat, and killing the .bat (GameProcess.stop) would leave this process listening.
"""

import base64
import os
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # bagi_control_client
sys.path.insert(0, str(Path(__file__).resolve().parent))  # fake_control_server

from fake_control_server import FakeControlServer, FakeError, QuitServer  # noqa: E402

WATCHDOG_SECONDS = 60.0
MAX_FRAMES = 36000  # kMaxStepFrames of the game
DEFAULT_RUN_UNTIL_FRAMES = 600
LOG_LEVELS = {"warning": 1, "error": 2, "fatal": 2}  # the rank of a level name, as the game orders them (error and fatal are both errors)
MIN_WINDOW_SIZE, MAX_WINDOW_SIZE = 64, 4096  # WindowLimits::kMinSize / kMaxSize
MIN_DENSITY, MAX_DENSITY = 0.5, 4.0
MAX_OUTPUT_SIDE = 8192
MIN_USER_SCALE, MAX_USER_SCALE = 0.5, 3.0  # UISettings::kMinUserScale / kMaxUserScale
DEFAULT_WINDOW = (1280, 720)
SAFE_AREA_SIDES = ("top", "right", "bottom", "left")
# What imgui_find lists (kind, label, window, x, y, width, height, visible): "Close" is in two windows, "Hidden" is scrolled out
IMGUI_MATCHES = [
    ("window", "Game Panel", "Game Panel", 100.0, 50.0, 400.0, 300.0, True),
    ("window", "Other", "Other", 600.0, 50.0, 300.0, 300.0, True),
    ("item", "Play##play", "Game Panel", 120.0, 80.0, 60.0, 20.0, True),
    ("item", "Close", "Game Panel", 400.0, 60.0, 40.0, 20.0, True),
    ("item", "Close", "Other", 800.0, 60.0, 40.0, 20.0, True),
    ("item", "Hidden", "Game Panel", 120.0, 340.0, 60.0, 20.0, False),
]
DEFAULT_IMGUI_LIMIT = 50
PNG_SIZE_OFFSET = 16  # the signature (8 bytes), the IHDR length and name (8 bytes), then width and height


def png_2x1() -> bytes:
    """A 2x1 PNG, one red pixel and one blue pixel."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack("!I", len(data)) + body + struct.pack("!I", zlib.crc32(body))

    header = struct.pack("!IIBBBBB", 2, 1, 8, 2, 0, 0, 0)
    pixels = b"\x00" + bytes([255, 0, 0, 0, 0, 255])
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")


def served_picture(headless: bool = True) -> tuple[int, int, bytes]:
    """The width, height and bytes of the picture `screenshot` answers: the file FAKE_GAME_PICTURE names (a window: FAKE_GAME_WINDOW_PICTURE first), else the 2x1 one."""
    named = (None if headless else os.environ.get("FAKE_GAME_WINDOW_PICTURE")) or os.environ.get("FAKE_GAME_PICTURE")
    data = Path(named).read_bytes() if named else png_2x1()
    width, height = struct.unpack("!II", data[PNG_SIZE_OFFSET : PNG_SIZE_OFFSET + 8])  # the first fields of IHDR
    return width, height, data


class Pointer:
    """Why a JSON pointer is not usable, or its value: a small RFC 6901 reader."""

    @staticmethod
    def find(document, pointer: str):
        """(found, value); raises FakeError("invalid_params") for a pointer with bad syntax."""
        if pointer == "":
            return True, document
        if not pointer.startswith("/"):
            raise FakeError("invalid_params", "a pointer is empty or starts with '/'")
        value = document
        for raw in pointer[1:].split("/"):
            if "~" in raw.replace("~0", "").replace("~1", ""):
                raise FakeError("invalid_params", "'~' only before 0 or 1")
            key = raw.replace("~1", "/").replace("~0", "~")
            if isinstance(value, dict) and key in value:
                value = value[key]
            elif isinstance(value, list) and key.isdigit() and (key == "0" or not key.startswith("0")) and int(key) < len(value):
                value = value[int(key)]
            else:
                return False, None
        return True, value


def is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and value == value and abs(value) != float("inf")


def window_object(window: dict, headless: bool) -> dict:
    """The `window` object of `status` and of `set_window` for the state `window` ({width, height, density, userScale, safeArea})."""
    return {
        "width": window["width"],
        "height": window["height"],
        "density": window["density"],
        "displayScale": 1.0,
        "safeArea": dict(window["safeArea"]),
        "outputWidth": round(window["width"] * window["density"]),
        "outputHeight": round(window["height"] * window["density"]),
        "userScale": window["userScale"],
        "headless": headless,
    }


def merged_window(window: dict, params: dict) -> dict:
    """What set_window would make of `window`, or the invalid_params that the engine would answer; nothing is changed."""
    merged = {"width": window["width"], "height": window["height"], "density": window["density"], "userScale": window["userScale"], "safeArea": dict(window["safeArea"])}
    for key in ("width", "height"):
        value = params.get(key)
        if not isinstance(value, int) or isinstance(value, bool):
            raise FakeError("invalid_params", f"'{key}' is required and has to be an integer of window units")
        if not MIN_WINDOW_SIZE <= value <= MAX_WINDOW_SIZE:
            raise FakeError("invalid_params", f"'{key}' has to be {MIN_WINDOW_SIZE} to {MAX_WINDOW_SIZE} window units, not {value}")
        merged[key] = value
    if "density" in params:
        density = params["density"]
        if not is_number(density) or not MIN_DENSITY <= density <= MAX_DENSITY:
            raise FakeError("invalid_params", f"'density' has to be a number from {MIN_DENSITY} to {MAX_DENSITY}")
        merged["density"] = float(density)
    if "userScale" in params:
        scale = params["userScale"]
        if not is_number(scale) or not MIN_USER_SCALE <= scale <= MAX_USER_SCALE:
            raise FakeError("invalid_params", f"'userScale' has to be a number from {MIN_USER_SCALE} to {MAX_USER_SCALE}")
        merged["userScale"] = float(scale)
    if "safeArea" in params:
        area = params["safeArea"]
        if not isinstance(area, dict) or any(not is_number(area.get(side)) or area[side] < 0 for side in SAFE_AREA_SIDES):
            raise FakeError("invalid_params", "'safeArea' has to be an object with the not negative numbers top, right, bottom and left")
        if area["left"] + area["right"] >= merged["width"] or area["top"] + area["bottom"] >= merged["height"]:
            raise FakeError("invalid_params", "'safeArea' leaves no area in the window")
        merged["safeArea"] = {side: float(area[side]) for side in SAFE_AREA_SIDES}
    if max(merged["width"], merged["height"]) * merged["density"] > MAX_OUTPUT_SIDE:
        raise FakeError("invalid_params", f"the output is at most {MAX_OUTPUT_SIDE} pixels a side")
    return merged


def start_window(args: list[str]) -> dict:
    """The window of a start: the default size, or `--window=WxH`; `--density=D`."""
    width, height = DEFAULT_WINDOW
    density = 1.0
    for arg in args:
        if arg.startswith("--window="):
            width, height = (int(side) for side in arg.split("=", 1)[1].split("x"))
        elif arg.startswith("--density="):
            density = float(arg.split("=", 1)[1])
    return {"width": width, "height": height, "density": density, "userScale": 1.0, "safeArea": dict.fromkeys(SAFE_AREA_SIDES, 0.0)}


def imgui_find(params: dict) -> dict:
    """What `imgui_find` answers from IMGUI_MATCHES: the filters of the engine (label whole or before `##`, labelContains, window, kind, visibleOnly, limit)."""

    def visible_part(text: str) -> str:
        return text.split("##", 1)[0]

    matches = []
    for kind, label, window, x, y, width, height, visible in IMGUI_MATCHES:
        if "label" in params and params["label"] not in (label, visible_part(label)):
            continue
        if "labelContains" in params and params["labelContains"] not in label:
            continue
        if "window" in params and params["window"] != window:
            continue
        if "kind" in params and params["kind"] != kind:
            continue
        if params.get("visibleOnly") and not visible:
            continue
        matches.append({"kind": kind, "id": "0x00000001", "label": label, "window": window, "x": x, "y": y, "width": width, "height": height, "visible": visible, "hovered": False})
    limit = params.get("limit", DEFAULT_IMGUI_LIMIT)
    answer = {
        "frame": 1,
        "display": {"width": 1280.0, "height": 720.0, "scaleX": 1.0, "scaleY": 1.0},
        "wantCaptureMouse": False,
        "wantCaptureKeyboard": False,
        "wantTextInput": False,
        "matches": matches[:limit],
    }
    if len(matches) > limit:
        answer["truncated"] = True
    return answer


def read_level(params: dict) -> str:
    level = params.get("level", "warning")
    if level not in ("warning", "error"):
        raise FakeError("invalid_params", "'level' has to be \"warning\" or \"error\"")
    return level


def main() -> int:
    args = sys.argv[1:]
    if "--exit-immediately" in args or os.environ.get("FAKE_GAME_EXIT"):
        print("fake game: exiting at once", flush=True)
        return 3
    if "--headless" not in args and os.environ.get("FAKE_GAME_NO_WINDOW"):
        print("fake game: no display for a window", flush=True)
        return 3
    port = next((int(arg.split("=", 1)[1]) for arg in args if arg.startswith("--control-port=")), 0)
    manual_clock = "--manual-clock" in args
    headless = "--headless" in args
    window = start_window(args)
    flags = [arg for arg in args if arg.startswith("--") and "=" not in arg]
    frame = {"count": 0}
    state = {"version": 1}
    log = {"lines": [], "warnings": 0, "errors": 0, "subscribed": False, "level": "warning"}  # lines: {sequence, level, time, message}, sequence from 1
    published: list[tuple[str, dict]] = []  # every push, for run_until's `event` condition
    editor_scene = {"dirty": False}  # the one scene document of `editor_state`; the action `edit_scene` makes it dirty
    imgui_events: list[dict] = []  # what imgui_input took, in order
    later: list[int] = []  # the frame count at which each "later" action raises the version

    def publish(event: str, data: dict) -> None:
        published.append((event, data))
        server.push(event, data, after_reply=True)

    def raise_version() -> None:
        state["version"] += 1
        publish("state_changed", {"game": "Fake", "version": state["version"]})

    def tick() -> None:
        """One frame: the counter moves and the "later" actions that are due take effect."""
        frame["count"] += 1
        due = [at for at in later if at <= frame["count"]]
        for at in due:
            later.remove(at)
            raise_version()

    def clock() -> dict:
        return {"frame": frame["count"]}

    def counts() -> dict:
        return {"warnings": log["warnings"], "errors": log["errors"]}

    def editor_state() -> dict:
        if "--editor" not in args:
            raise FakeError("not_editor", "the game does not run in the editor run mode")
        overlay = next((arg.split("=", 1)[1] for arg in args if arg.startswith("--content-overlay=")), None)
        path = "config/SceneConfig.xml"
        file = path if overlay is None else f"{overlay}/{path}"
        scene = {"id": "scene:Fake", "kind": "scene", "name": "Fake", "dirty": editor_scene["dirty"], "path": path, "file": file}
        return {"anyDirty": scene["dirty"], "documents": [scene]}

    def log_mark() -> dict:
        return {"mark": len(log["lines"]) + 1, **counts()}

    def log_since(params: dict) -> dict:
        mark = params.get("mark")
        if not isinstance(mark, int) or isinstance(mark, bool) or mark < 0:
            raise FakeError("invalid_params", "'mark' has to be an integer, 0 or more")
        floor = LOG_LEVELS[read_level(params)]
        kept = [line for line in log["lines"] if line["sequence"] >= mark]
        return {
            "lines": [line for line in kept if LOG_LEVELS[line["level"]] >= floor],
            "warnings": sum(line["level"] == "warning" for line in kept),
            "errors": sum(line["level"] != "warning" for line in kept),
            "dropped": 0,
            "next": len(log["lines"]) + 1,
        }

    def log_subscribe(params: dict) -> dict:
        enabled = params.get("enabled")
        if not isinstance(enabled, bool):
            raise FakeError("invalid_params", "'enabled' has to be a boolean")
        log["level"] = read_level(params)
        log["subscribed"] = enabled
        return {"enabled": enabled, "level": log["level"]}

    def warn(params: dict) -> dict:
        level, text = params.get("level", "Warning"), params.get("text", "something odd")
        print(f"[{level}] [12:00:00.000] {text}", flush=True)
        name = str(level).lower()
        if name in LOG_LEVELS:
            line = {"sequence": len(log["lines"]) + 1, "level": name, "time": "12:00:00.000", "message": text}
            log["lines"].append(line)
            log["warnings" if name == "warning" else "errors"] += 1
            if log["subscribed"] and LOG_LEVELS[name] >= LOG_LEVELS[log["level"]]:
                publish("log", line)
        return {"logged": True}

    def batch(params: dict) -> dict:
        calls = params.get("calls")
        if not isinstance(calls, list) or not 1 <= len(calls) <= 1000:
            raise FakeError("invalid_params", "'calls' has to be an array of 1 to 1000 calls")
        continue_on_error = params.get("continueOnError", False)
        if not isinstance(continue_on_error, bool):
            raise FakeError("invalid_params", "'continueOnError' has to be a boolean")
        for call in calls:
            if not isinstance(call, dict) or not isinstance(call.get("method"), str) or not call["method"]:
                raise FakeError("invalid_params", "every call needs a non-empty string 'method'")
            if call.get("params") is not None and not isinstance(call["params"], dict):
                raise FakeError("invalid_params", "'params' of a call has to be an object")
        results, stopped = [], None
        for index, call in enumerate(calls):
            try:
                if call["method"] in ("batch", "quit"):
                    raise FakeError("invalid_params", f"'{call['method']}' cannot run inside a batch")
                results.append({"ok": True, "result": handle(call["method"], call.get("params") or {})})
            except FakeError as error:
                results.append({"ok": False, "error": {"code": error.code, "message": error.message}})
                if not continue_on_error:
                    stopped = index
                    break
        answer = {"results": results}
        if stopped is not None:
            answer["stopped"] = stopped
        return answer

    def run_until(params: dict) -> dict:
        condition = params.get("condition")
        keys = [key for key in ("pointer", "versionMoved", "event") if isinstance(condition, dict) and key in condition]
        if len(keys) != 1:
            raise FakeError("invalid_params", "'condition' has to have exactly one of 'pointer', 'versionMoved' and 'event'")
        max_frames = params.get("maxFrames", DEFAULT_RUN_UNTIL_FRAMES)
        if not isinstance(max_frames, int) or isinstance(max_frames, bool) or not 1 <= max_frames <= MAX_FRAMES:
            raise FakeError("invalid_params", f"'maxFrames' has to be an integer from 1 to {MAX_FRAMES}")
        kind, start_version, start_events = keys[0], state["version"], len(published)
        if kind == "pointer":
            if not isinstance(condition["pointer"], str) or "equals" not in condition:
                raise FakeError("invalid_params", "a 'pointer' condition needs a string pointer and 'equals'")
            Pointer.find({}, condition["pointer"])  # the syntax only
        elif kind == "event":
            event = condition["event"]
            if not isinstance(event, dict) or not isinstance(event.get("name"), str) or not event["name"]:
                raise FakeError("invalid_params", "'event' has to be an object with a non-empty string 'name'")

        def met() -> str | None:
            if kind == "versionMoved":
                return kind if state["version"] != start_version else None
            if kind == "event":
                wanted, equal = condition["event"]["name"], condition["event"].get("equal", {})
                seen = any(name == wanted and all(data.get(key) == value for key, value in equal.items()) for name, data in published[start_events:])
                return kind if seen else None
            found, value = Pointer.find(handle("get_state", {}), condition["pointer"])
            return kind if found and value == condition["equals"] and type(value) is type(condition["equals"]) else None

        frames = 0
        reason = met()
        while reason is None and frames < max_frames:
            tick()
            frames += 1
            reason = met()
        answer = {"met": reason is not None, "frames": frames, **clock(), "game": "Fake", "version": state["version"]}
        if reason is not None:
            answer["reason"] = reason
        return answer

    def handle(method: str, params: dict | None):
        if method == "ping":
            return {"pong": True, "protocol": 1}
        if method == "status":
            return {"frame": frame["count"], "manualClock": manual_clock, "log": counts(), "runMode": "Editor" if "--editor" in args else "Game", "window": window_object(window, headless)}
        if method == "set_window":
            if not headless:
                raise FakeError("not_headless", "the window of a real game cannot be simulated")
            window.update(merged_window(window, params or {}))
            tick()  # the answer waits one frame
            return window_object(window, headless)
        if method == "list_games":
            return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
        if method == "get_state":
            return {"game": "Fake", "version": state["version"], "state": {"flags": flags, "imguiEvents": list(imgui_events)}}
        if method == "list_actions":
            return {"game": "Fake", "version": state["version"], "actions": [{"type": "noop"}]}
        if method == "perform_action":
            if (params or {}).get("type") == "bad":
                raise FakeError("illegal_action", "not allowed")
            if (params or {}).get("type") == "needs_noop" and state["version"] == 1:
                raise FakeError("illegal_action", "a noop has to come first")  # a precondition: an action that is legal only after another
            if (params or {}).get("type") == "edit_scene":
                editor_scene["dirty"] = True
            if (params or {}).get("type") == "later":
                later.append(frame["count"] + (params or {}).get("frames", 1))
                return {"game": "Fake", "version": state["version"], "outcome": params}
            raise_version()
            return {"game": "Fake", "version": state["version"], "outcome": params}
        if method == "editor_state":
            return editor_state()
        if method == "warn":
            return warn(params or {})
        if method == "log_mark":
            return log_mark()
        if method == "log_since":
            return log_since(params or {})
        if method == "log_subscribe":
            return log_subscribe(params or {})
        if method == "batch":
            return batch(params or {})
        if method == "run_until":
            return run_until(params or {})
        if method == "step":
            for _ in range((params or {}).get("frames", 1)):
                tick()
            return clock()
        if method == "screenshot":
            if manual_clock:
                tick()
            width, height, data = served_picture("--headless" in args)
            if os.environ.get("FAKE_GAME_NO_PICTURE"):
                return {"width": width, "height": height}
            return {"width": width, "height": height, "png_base64": base64.b64encode(data).decode("ascii")}
        if method in ("imgui_find", "imgui_input"):
            if "--editor" not in args:
                raise FakeError("no_imgui", "there is no ImGui context")
            if method == "imgui_find":
                return imgui_find(params or {})
            imgui_events.extend((params or {})["events"])
            tick()
            return {"events": len((params or {})["events"]), "frames": 2, "wantCaptureKeyboard": False, "wantCaptureMouse": True, "wantTextInput": False}
        if method == "quit":
            raise QuitServer({"quitting": True})
        raise FakeError("unknown_method", method)

    server = FakeControlServer(handle)
    server.start(port)
    print(f"fake game: listening on {port}", flush=True)
    deadline = time.monotonic() + WATCHDOG_SECONDS
    while not server.quit_requested.is_set() and time.monotonic() < deadline:
        time.sleep(0.02)
    time.sleep(0.05)
    server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
