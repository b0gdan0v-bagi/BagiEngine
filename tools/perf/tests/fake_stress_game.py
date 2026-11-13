#!/usr/bin/env python3
"""A stand-in for BagiStress for test_stress_frames.py: the control API of the control tests' fake server, and the end of a stress run.

    fake_stress_game.py --control --control-port=N [--stress-frames=F --stress-warmup=W | --stress-ramp] ...

It counts the stepped frames; when they reach warmup + frames (a ramp: RAMP_FRAMES) it prints a log line and its JSON line (the flags it
was given, so a test sees what the script passed) and exits in the middle of the step, as the real game does after its QuitEvent.
FAKE_STRESS_MODE changes the end: `noline` (exits 0 without a line), `crash` (a line, then exit code 4), `never` (never quits),
`torn` (a line that is not JSON). Runs until the watchdog at the latest.
"""

import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "control"))  # bagi_control_client
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "control" / "tests"))  # fake_control_server

from fake_control_server import FakeControlServer, FakeError  # noqa: E402

WATCHDOG_SECONDS = 60.0
RAMP_FRAMES = 300


def flag_value(args: list[str], name: str, default: str) -> str:
    return next((arg.split("=", 1)[1] for arg in args if arg.startswith(name + "=")), default)


def main() -> int:
    args = sys.argv[1:]
    mode = os.environ.get("FAKE_STRESS_MODE", "")
    port = int(flag_value(args, "--control-port", "0"))
    ramp = "--stress-ramp" in args
    run_frames = RAMP_FRAMES if ramp else int(flag_value(args, "--stress-warmup", "0")) + int(flag_value(args, "--stress-frames", "0"))
    stepped = {"frames": 0}

    def finish() -> None:
        print("[Info] [12:00:00.000] the run is over", flush=True)
        if mode == "noline":
            os._exit(0)
        if mode == "torn":
            print('{"scene":"sprites","frames":', flush=True)
            os._exit(0)
        print(json.dumps({"flags": [arg for arg in args if arg.startswith("--stress-") or arg.startswith("--trace=")], "mode": "ramp" if ramp else "fixed"}, sort_keys=True), flush=True)
        print("[Info] [12:00:00.001] after the line", flush=True)
        os._exit(4 if mode == "crash" else 0)

    def handle(method: str, params: dict | None):
        if method == "step":
            stepped["frames"] += (params or {}).get("frames", 1)
            if stepped["frames"] >= run_frames and mode != "never":
                finish()
            return {"frame": stepped["frames"]}
        raise FakeError("unknown_method", method)

    server = FakeControlServer(handle)
    server.start(port)
    print(f"fake stress game: listening on {port}", flush=True)
    time.sleep(WATCHDOG_SECONDS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
