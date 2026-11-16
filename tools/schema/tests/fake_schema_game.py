#!/usr/bin/env python3
"""A stand-in for a game that serves `schema` over the control API: `--control-port=N`, and the answer comes from the file FAKE_SCHEMA_FILE.

FAKE_SCHEMA_VERSION replaces the `version` of the answer; FAKE_SCHEMA_REFUSE makes `schema` an unknown method (a game of an older engine).
`quit` ends the process. Ends by itself after WATCHDOG_SECONDS when nobody quits it.
"""

import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "control"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "control" / "tests"))

from fake_control_server import FakeControlServer, FakeError, QuitServer  # noqa: E402

WATCHDOG_SECONDS = 30.0


def main() -> int:
    port = next((int(arg.split("=", 1)[1]) for arg in sys.argv[1:] if arg.startswith("--control-port=")), 0)
    schema = json.loads(Path(os.environ["FAKE_SCHEMA_FILE"]).read_text(encoding="utf-8"))
    if "FAKE_SCHEMA_VERSION" in os.environ:
        schema["version"] = int(os.environ["FAKE_SCHEMA_VERSION"])

    def handle(method: str, params: dict | None):
        if method == "schema" and not os.environ.get("FAKE_SCHEMA_REFUSE"):
            return schema
        if method == "quit":
            raise QuitServer({"quitting": True})
        raise FakeError("unknown_method", method)

    server = FakeControlServer(handle)
    server.start(port)
    deadline = time.monotonic() + WATCHDOG_SECONDS
    while not server.quit_requested.is_set() and time.monotonic() < deadline:
        time.sleep(0.02)
    time.sleep(0.05)
    server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
