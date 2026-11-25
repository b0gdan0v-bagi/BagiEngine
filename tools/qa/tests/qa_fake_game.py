#!/usr/bin/env python3
"""A stand-in for a game for the QA session tests: a counter with a version, a `ui_tree`, and knobs by environment variable.

    qa_fake_game.py --control --control-port=N [--headless] [--manual-clock]

State: `count` (action `inc` adds one and moves the version; `noop` changes nothing and leaves the version). `list_actions` offers noop and inc.
`list_methods` names what the game serves. `ui_tree` has the canvas `Hud` with the elements `Plus` (`ui_click` does an `inc`), `Noop` (a click changes nothing),
`Hidden` (not visible, so `ui_click` answers `not_found`), `Locked` (disabled: a click answers and changes nothing) and `Volume` (a slider, `UISliderComponent`;
`at` of a click is stored as `volume`); a name that is not there is `not_found`, as the real `ui_click`.
Knobs (the number is the 1-based count of `perform_action` calls):
  QA_FAKE_WARN_AT=N          the Nth action logs a Warning at once
  QA_FAKE_LATE_WARNING=1     the first action's Warning is logged only when the game quits (a block-buffered stdout)
  QA_FAKE_UI_ISSUE=1         `ui_tree` has a canvas with a layout issue
  QA_FAKE_UI_NOT_LAID_OUT=1  `ui_tree` has a canvas that is not laid out
  QA_FAKE_VERSION_BACK_AT=N  the Nth action answers a version lower than before
  QA_FAKE_REFUSE_LISTED=1    `noop`, which is listed, answers `illegal_action`
  QA_FAKE_QUIT_AT=N          the Nth action makes the game quit (answers, then drops the connection)
  QA_FAKE_REFUSE_RESET=1     `reset` answers `invalid_params`
  QA_FAKE_CRASH_AT=N         the Nth action makes the game exit at once without an answer
  QA_FAKE_NO_UI=1            `ui_tree` has a canvas with no elements
  QA_FAKE_WINDOW=1           the game serves `set_window {width, height}` (a size of 0 is `invalid_params`) and `status` shows `window`
  QA_FAKE_EDITOR=1           an editor build: `status.runMode` is `Editor` (else `Game`); `imgui_find` lists the windows `Scene Editor` and `Game Panel` and
                             the items `Save` (Scene Editor) and `Close` (in both windows, so a click on it is ambiguous); `imgui_input` answers like the real one,
                             and a click on `Save` clears the dirty flag and writes the text QA_FAKE_SAVE_TEXT: into `config/SceneConfig.xml` of the
                             `--content-overlay=<dir>` it was given, else (no overlay, or QA_FAKE_WRITE_CHECKOUT) into the file QA_FAKE_CONTENT_FILE;
                             `editor_state` serves one `scene` document whose `file` is that target
  QA_FAKE_DIRTY=1            the scene document starts dirty (an edit was made)
  QA_FAKE_WRITE_CHECKOUT=1   Save writes QA_FAKE_CONTENT_FILE even when an overlay was given (an engine that ignores the flag)
  QA_FAKE_ARGS_OUT=F         the game writes its command line arguments into the file F, one per line
  QA_FAKE_OVERLAY_SEEN_OUT=F the game writes `yes` or `no` into F: whether the overlay's `config/SceneConfig.xml` existed when it started
  QA_FAKE_EDITOR_DIR_OUT=F   the game writes the BAGI_EDITOR_DIR it was started with into the file F (an empty line when it was not set)
`bad` (not listed) is always refused with `illegal_action`. `reset` takes {"count"}.
"""

import base64
import os
import sys
import time
from pathlib import Path

CONTROL_DIR = Path(__file__).resolve().parents[2] / "control"
sys.path.insert(0, str(CONTROL_DIR))
sys.path.insert(0, str(CONTROL_DIR / "tests"))

from fake_control_server import FakeControlServer, FakeError, QuitServer  # noqa: E402
from fake_game import png_2x1  # noqa: E402

WATCHDOG_SECONDS = 60.0


OVERLAY_FLAG = "--content-overlay="
SCENE_PATH = "config/SceneConfig.xml"
SAVE_RECT = {"x": 100.0, "y": 50.0, "width": 40.0, "height": 20.0}
EDITOR_WINDOWS = ("Scene Editor", "Game Panel")
EDITOR_ITEMS = [
    {"kind": "item", "label": "Save", "window": "Scene Editor", **SAVE_RECT},
    {"kind": "item", "label": "Close", "window": "Scene Editor", "x": 200.0, "y": 50.0, "width": 40.0, "height": 20.0},
    {"kind": "item", "label": "Close", "window": "Game Panel", "x": 200.0, "y": 150.0, "width": 40.0, "height": 20.0},
]


def knob(name: str) -> int:
    return int(os.environ.get(name, "0") or 0)


def main() -> int:
    args = sys.argv[1:]
    port = next((int(arg.split("=", 1)[1]) for arg in args if arg.startswith("--control-port=")), 0)
    state = {"count": 0, "version": 1, "actions": 0, "frame": 0, "volume": 0.0, "window": [1920, 1080]}
    window_served = bool(os.environ.get("QA_FAKE_WINDOW"))
    late: list[str] = []
    editor = bool(os.environ.get("QA_FAKE_EDITOR"))
    editor_dir_out = os.environ.get("QA_FAKE_EDITOR_DIR_OUT")
    args_out = os.environ.get("QA_FAKE_ARGS_OUT")
    if args_out:
        Path(args_out).write_text("\n".join(args) + "\n", encoding="utf-8")
    overlay = next((Path(arg.split("=", 1)[1]) for arg in args if arg.startswith(OVERLAY_FLAG)), None)
    overlay_file = None if overlay is None else overlay / SCENE_PATH
    overlay_seen_out = os.environ.get("QA_FAKE_OVERLAY_SEEN_OUT")
    if overlay_seen_out:
        Path(overlay_seen_out).write_text(("yes" if overlay_file is not None and overlay_file.exists() else "no") + "\n", encoding="utf-8")
    dirty = [bool(os.environ.get("QA_FAKE_DIRTY"))]
    if editor_dir_out:
        Path(editor_dir_out).write_text(os.environ.get("BAGI_EDITOR_DIR", "") + "\n", encoding="utf-8")

    def full_state() -> dict:
        return {"game": "Fake", "version": state["version"], "state": {"count": state["count"]}}

    def inc() -> None:
        state["count"] += 1
        state["version"] += 1

    def hud_elements() -> list[dict]:
        def button(name: str, **fields) -> dict:
            return {"name": name, "type": "UIElement", "visible": True, "interactive": True, **fields}

        return [
            button("Plus"),
            button("Noop"),
            button("Hidden", visible=False),
            button("Locked", disabled=True),
            button("Volume", components=["UISliderComponent"], fraction=state["volume"]),
        ]

    def ui_click(params: dict):
        name = params.get("name")
        element = next((e for e in hud_elements() if e["name"] == name and e["visible"]), None)
        if element is None:
            raise FakeError("not_found", f"no element {name}")
        if element.get("disabled"):
            return {"action": "", "value": ""}
        if name == "Plus":
            inc()
        if name == "Volume":
            state["volume"] = float(params.get("at", 0.5))
        return {"action": name, "value": ""}

    def set_window(params: dict):
        width, height = params.get("width", 0), params.get("height", 0)
        if not isinstance(width, int) or not isinstance(height, int) or width <= 0 or height <= 0:
            raise FakeError("invalid_params", "width and height must be positive")
        state["window"] = [width, height]
        return {"width": width, "height": height}

    served = ["get_state", "list_actions", "list_games", "list_methods", "perform_action", "ping", "quit", "reset", "screenshot", "status", "step", "ui_click", "ui_tree"]
    if window_served:
        served.append("set_window")
    if editor:
        served += ["editor_state", "imgui_find", "imgui_input"]

    def save_target() -> Path | None:
        checkout = os.environ.get("QA_FAKE_CONTENT_FILE")
        if overlay_file is not None and not os.environ.get("QA_FAKE_WRITE_CHECKOUT"):
            return overlay_file
        return Path(checkout) if checkout else None

    def editor_state() -> dict:
        target = save_target()
        document = {"id": "scene:Main", "kind": "scene", "name": "Main", "dirty": dirty[0], "path": SCENE_PATH, "file": "" if target is None else str(target)}
        return {"anyDirty": dirty[0], "documents": [document]}

    def imgui_find(params: dict) -> dict:
        windows = [{"kind": "window", "label": label, "id": index, "x": 0.0, "y": 0.0, "width": 400.0, "height": 300.0, "visible": True, "hovered": False} for index, label in enumerate(EDITOR_WINDOWS)]
        items = [{**item, "id": 10 + index, "visible": True, "hovered": False} for index, item in enumerate(EDITOR_ITEMS)]
        wanted = [match for match in windows + items if (params.get("kind") is None or match["kind"] == params["kind"]) and (params.get("window") is None or match.get("window") == params["window"])]
        if params.get("label") is not None:
            wanted = [match for match in wanted if match["label"] == params["label"]]
        elif params.get("kind") is None and "label" not in params:
            wanted = windows
        return {"frame": state["frame"], "display": [1920, 1080], "wantCaptureMouse": False, "wantCaptureKeyboard": False, "matches": wanted}

    def imgui_input(params: dict) -> dict:
        for event in params.get("events", []):
            if event.get("type") == "click" and SAVE_RECT["x"] <= event.get("x", -1) <= SAVE_RECT["x"] + SAVE_RECT["width"] and SAVE_RECT["y"] <= event.get("y", -1) <= SAVE_RECT["y"] + SAVE_RECT["height"]:
                dirty[0] = False
                target = save_target()
                if target is not None:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text(os.environ.get("QA_FAKE_SAVE_TEXT", "saved"), encoding="utf-8")
        return {"events": len(params.get("events", [])), "frames": 1, "wantCaptureMouse": False, "wantCaptureKeyboard": False, "wantTextInput": False}

    def handle(method: str, params: dict | None):
        params = params or {}
        if method == "ping":
            return {"pong": True, "protocol": 1}
        if method == "status":
            answer = {"frame": state["frame"], "manualClock": "--manual-clock" in args, "runMode": "Editor" if editor else "Game"}
            if window_served:
                answer["window"] = state["window"]
            return answer
        if method == "list_methods":
            return {"methods": [{"name": name, "summary": "", "params": []} for name in sorted(served)]}
        if method == "list_games":
            return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
        if method == "get_state":
            return full_state()
        if method == "list_actions":
            return {"game": "Fake", "version": state["version"], "actions": [{"type": "noop"}, {"type": "inc"}]}
        if method == "perform_action":
            state["actions"] += 1
            number = state["actions"]
            kind = params.get("type")
            if kind == "bad" or (kind == "noop" and os.environ.get("QA_FAKE_REFUSE_LISTED")):
                raise FakeError("illegal_action", "not allowed")
            if number == knob("QA_FAKE_CRASH_AT"):
                os._exit(1)
            if kind == "inc":
                inc()
            if number == knob("QA_FAKE_VERSION_BACK_AT"):
                state["version"] -= 2
            if number == knob("QA_FAKE_WARN_AT"):
                print("[Warning] [12:00:00.000] something odd happened", flush=True)
            if number == 1 and os.environ.get("QA_FAKE_LATE_WARNING"):
                late.append("[Warning] [12:00:00.000] a late warning")
            if number == knob("QA_FAKE_QUIT_AT"):
                raise QuitServer({"game": "Fake", "version": state["version"]})
            return {"game": "Fake", "version": state["version"], "outcome": params}
        if method == "reset":
            if os.environ.get("QA_FAKE_REFUSE_RESET"):
                raise FakeError("invalid_params", "no such start")
            state["count"] = params.get("count", 0)
            state["version"] += 1
            return {"game": "Fake", "version": state["version"]}
        if method == "step":
            state["frame"] += params.get("frames", 1)
            return {"frame": state["frame"]}
        if method == "ui_tree":
            canvas = {"node": "Hud", "visible": True, "laidOut": True, "issues": [], "elements": [] if os.environ.get("QA_FAKE_NO_UI") else hud_elements()}
            if os.environ.get("QA_FAKE_UI_ISSUE"):
                canvas["issues"] = [{"kind": "Overlap", "element": "Play", "other": "Quit", "rect": {}}]
            if os.environ.get("QA_FAKE_UI_NOT_LAID_OUT"):
                canvas["laidOut"] = False
            return {"scene": "Main", "canvases": [canvas]}
        if method == "ui_click":
            return ui_click(params)
        if editor and method == "editor_state":
            return editor_state()
        if editor and method == "imgui_find":
            return imgui_find(params)
        if editor and method == "imgui_input":
            return imgui_input(params)
        if method == "set_window" and window_served:
            return set_window(params)
        if method == "screenshot":
            data = png_2x1()
            return {"width": 2, "height": 1, "png_base64": base64.b64encode(data).decode("ascii")}
        if method == "quit":
            for line in late:
                print(line, flush=True)
            raise QuitServer({"quitting": True})
        raise FakeError("unknown_method", method)

    server = FakeControlServer(handle)
    server.start(port)
    print(f"qa fake game: listening on {port}", flush=True)
    deadline = time.monotonic() + WATCHDOG_SECONDS
    while not server.quit_requested.is_set() and time.monotonic() < deadline:
        time.sleep(0.02)
    time.sleep(0.05)
    server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
