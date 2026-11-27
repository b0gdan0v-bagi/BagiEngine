#!/usr/bin/env python3
"""Drives BagiDemo from Python through the control API: what docs/guide/control-api.md shows, run by ctest as BagiDemoGuideScript.drive_the_demo.

    python3 docs/guide/scripts/drive_the_demo.py [path of BagiDemo]

Exit code: 0 every claim held, 1 one did not (an assert), 77 the game is not built (ctest reads it as skipped).
Standard library only; Python 3.10 or later.
"""

import struct
import sys
import tempfile
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_ROOT / "tools" / "control"))

from bagi_control_client import ControlError  # noqa: E402
from bagi_session import GameSession  # noqa: E402

DEFAULT_GAME = "build/macos-debug/bin/BagiDemo"
EXIT_SKIPPED = 77
FRAME_SECONDS = 1 / 60
TITLE_NODE = "Text/Title"
TITLE_COMPONENT = "TextComponent"
NEW_TITLE = "Hello from Python"
PICTURE_SIZE = 400
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def main() -> int:
    exe = Path(sys.argv[1] if len(sys.argv) > 1 else ENGINE_ROOT / DEFAULT_GAME)
    if not exe.is_file():
        print(f"skipped: no game at {exe.name}")
        return EXIT_SKIPPED

    session = GameSession()
    try:
        session.launch(exe=str(exe))
        print("frame", session.step(30, FRAME_SECONDS)["frame"])

        title = {"node": TITLE_NODE, "component": TITLE_COMPONENT, "path": "text"}
        print("title:", session.call("scene_get", title)["value"])
        session.call("scene_set", {**title, "value": NEW_TITLE})
        assert session.call("scene_get", title)["value"] == NEW_TITLE

        try:
            session.call("scene_set", {**title, "path": "nope", "value": 1})
            raise AssertionError("a write to a field that does not exist was accepted")
        except ControlError as error:
            print("refused:", error.code)
            assert error.code == "not_found"

        click = session.call("ui_click", {"name": "Burst"})
        print("click:", click["action"], click["takenBy"])
        assert (click["action"], click["takenBy"]) == ("DemoBurst", "element")
        session.step(1, FRAME_SECONDS)

        png, size = session.screenshot(max_size=PICTURE_SIZE)
        assert png.startswith(PNG_SIGNATURE)
        width, height = struct.unpack(">II", png[16:24])
        assert (width, height) == (size["width"], size["height"]) == (PICTURE_SIZE, PICTURE_SIZE * 9 // 16)
        with tempfile.TemporaryDirectory() as folder:
            picture = Path(folder) / "title.png"
            picture.write_bytes(png)
            print("picture:", picture.name, f"{width} x {height}")

        session.quit()  # the game's stdout is block-buffered: a Warning is in its log for sure only after it has exited
        problems = session.log_problems()
        print("log problems:", problems)
        assert problems == []
    finally:
        session.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
