#!/usr/bin/env python3
"""Smoke test of the control API of a game in a real browser, through tools/web/web_game.py and the bridge.

    BAGI_WEB_DIST=<dist of a web-debug build> build/web-venv/bin/python tools/web/tests/web_control_smoke.py \\
        --exe tools/web/web_game.py --window --startup-timeout 120
    # BAGI_WEB_BROWSER=webkit for the other browser

The command line and the exit codes are those of tools/control/tests/smoke.py, whose checks both scenarios are built from. A page
differs from a headless desktop game: `set_window` is `not_headless`, a screenshot is possible all the same, and the methods
that need a file are `unsupported`. Pass --window: the game's window is the browser's, with or without it. It needs a browser and a
packaged page, so it is not a ctest and not a unittest.
"""

import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "control" / "tests"))

import smoke  # noqa: E402
from bagi_control_client import BagiControlClient  # noqa: E402
from smoke import approx, check, expect_error  # noqa: E402


def check_page_only(client: BagiControlClient) -> None:
    """What the page's browser decides: the window is not the harness's to set, and no file of the desktop is in reach."""
    smoke.check_window_control(client, headless=False)
    for method in ("record_start", "replay", "profiler_dump"):
        expect_error("unsupported", lambda method=method: client.call(method, {"path": "page.jsonl"}), f"{method} on a page")


def run_real_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    smoke.run_real_clock_scenario(client, headless=False)
    smoke.check_screenshot_basics(client)
    smoke.check_ui_tree(client)
    check_page_only(client)


def run_manual_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["manualClock"] is True, f"controlClock=manual did not turn the manual clock on: {status}")
    check(status["frame"] == 0 and status["totalTime"] == 0, f"a frame ran before the first step: {status}")

    # The page's own frames go on, and none of them is a frame of the game
    time.sleep(smoke.IDLE_CHECK_SECONDS)
    check(client.call("status")["frame"] == 0, "the manual clock ticked without a step")
    smoke.play_basics(client)

    stepped = client.call("step", {"frames": 3, "dt": 0.1})
    check(stepped["frame"] == 3 and approx(stepped["totalTime"], 0.3), f"step 3 of 0.1 s answered {stepped}")

    # A step holds back what follows it, so a pipelined status sees the frames
    client.send_text(json.dumps({"id": 900, "method": "step", "params": {"frames": 5}}))
    client.send_text(json.dumps({"id": 901, "method": "status"}))
    step_reply = smoke.receive_response(client)
    status_reply = smoke.receive_response(client)
    check(step_reply["id"] == 900 and step_reply["result"]["frame"] == 8, f"the pipelined step answered {step_reply}")
    check(status_reply["id"] == 901 and status_reply["result"]["frame"] == 8, f"the status behind the step answered {status_reply}")

    smoke.check_frame_stats(client)
    smoke.check_screenshot_basics(client)
    smoke.check_screenshot_takes_one_frame(client)
    smoke.check_ui_tree(client)
    smoke.check_log_and_batch(client)
    smoke.check_run_until(client, manual_clock=True)
    check_page_only(client)


SCENARIOS = [
    ("real clock", [], run_real_clock_scenario),
    ("manual clock", ["--manual-clock"], run_manual_clock_scenario),
]


if __name__ == "__main__":
    sys.exit(smoke.run_smoke(__doc__, SCENARIOS))
