#!/usr/bin/env python3
"""A stand-in for a game executable: takes the start-up flags of the engine and serves a tiny control API.

    fake_game.py --control --control-port=N [--headless] [--manual-clock] [--exit-immediately]

The environment variable FAKE_GAME_EXIT makes it exit at once (code 3) like --exit-immediately.

Methods: ping, status, list_games, get_state (echoes the flags), list_actions, perform_action (a refusal for {"type": "bad"}),
step, screenshot (a 2x1 PNG), quit (exits the process). Runs until quit, or until WATCHDOG_SECONDS have passed: on Windows the
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


def png_2x1() -> bytes:
    """A 2x1 PNG, one red pixel and one blue pixel."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack("!I", len(data)) + body + struct.pack("!I", zlib.crc32(body))

    header = struct.pack("!IIBBBBB", 2, 1, 8, 2, 0, 0, 0)
    pixels = b"\x00" + bytes([255, 0, 0, 0, 0, 255])
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")


def main() -> int:
    args = sys.argv[1:]
    if "--exit-immediately" in args or os.environ.get("FAKE_GAME_EXIT"):
        print("fake game: exiting at once", flush=True)
        return 3
    port = next((int(arg.split("=", 1)[1]) for arg in args if arg.startswith("--control-port=")), 0)
    manual_clock = "--manual-clock" in args
    flags = [arg for arg in args if arg.startswith("--") and "=" not in arg]
    frame = {"count": 0}

    def handle(method: str, params: dict | None):
        if method == "ping":
            return {"pong": True, "protocol": 1}
        if method == "status":
            return {"frame": frame["count"], "manualClock": manual_clock}
        if method == "list_games":
            return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
        if method == "get_state":
            return {"game": "Fake", "version": 1, "state": {"flags": flags}}
        if method == "list_actions":
            return {"game": "Fake", "version": 1, "actions": [{"type": "noop"}]}
        if method == "perform_action":
            if (params or {}).get("type") == "bad":
                raise FakeError("illegal_action", "not allowed")
            return {"game": "Fake", "version": 2, "outcome": params}
        if method == "step":
            frame["count"] += (params or {}).get("frames", 1)
            return {"frame": frame["count"]}
        if method == "screenshot":
            return {"width": 2, "height": 1, "png_base64": base64.b64encode(png_2x1()).decode("ascii")}
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
