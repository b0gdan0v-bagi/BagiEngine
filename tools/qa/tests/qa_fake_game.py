#!/usr/bin/env python3
"""A stand-in for a game for the QA session tests: a counter with a version, a `ui_tree`, and knobs by environment variable.

    qa_fake_game.py --control --control-port=N [--headless] [--manual-clock]

State: `count` (action `inc` adds one and moves the version; `noop` changes nothing and leaves the version). `list_actions` offers noop and inc.
Knobs (the number is the 1-based count of `perform_action` calls):
  QA_FAKE_WARN_AT=N          the Nth action logs a Warning at once
  QA_FAKE_LATE_WARNING=1     the first action's Warning is logged only when the game quits (a block-buffered stdout)
  QA_FAKE_UI_ISSUE=1         `ui_tree` has a canvas with a layout issue
  QA_FAKE_UI_NOT_LAID_OUT=1  `ui_tree` has a canvas that is not laid out
  QA_FAKE_VERSION_BACK_AT=N  the Nth action answers a version lower than before
  QA_FAKE_REFUSE_LISTED=1    `noop`, which is listed, answers `illegal_action`
  QA_FAKE_QUIT_AT=N          the Nth action makes the game quit
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


def knob(name: str) -> int:
    return int(os.environ.get(name, "0") or 0)


def main() -> int:
    args = sys.argv[1:]
    port = next((int(arg.split("=", 1)[1]) for arg in args if arg.startswith("--control-port=")), 0)
    state = {"count": 0, "version": 1, "actions": 0, "frame": 0}
    late: list[str] = []

    def full_state() -> dict:
        return {"game": "Fake", "version": state["version"], "state": {"count": state["count"]}}

    def handle(method: str, params: dict | None):
        params = params or {}
        if method == "ping":
            return {"pong": True, "protocol": 1}
        if method == "status":
            return {"frame": state["frame"], "manualClock": "--manual-clock" in args}
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
            if kind == "inc":
                state["count"] += 1
                state["version"] += 1
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
            state["count"] = params.get("count", 0)
            state["version"] += 1
            return {"game": "Fake", "version": state["version"]}
        if method == "step":
            state["frame"] += params.get("frames", 1)
            return {"frame": state["frame"]}
        if method == "ui_tree":
            canvas = {"node": "Hud", "laidOut": True, "issues": [], "elements": []}
            if os.environ.get("QA_FAKE_UI_ISSUE"):
                canvas["issues"] = [{"kind": "Overlap", "element": "Play", "other": "Quit", "rect": {}}]
            if os.environ.get("QA_FAKE_UI_NOT_LAID_OUT"):
                canvas["laidOut"] = False
            return {"scene": "Main", "canvases": [canvas]}
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
