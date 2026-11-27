#!/usr/bin/env python3
"""Lays the example screen out on four phone and desktop windows and prints what the engine's validator finds.

    python3 docs/guide/examples/ui-screen/sizes.py [--exe build/macos-debug/bin/BagiDemo]
"""
import argparse
import shutil
import sys
import tempfile
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools" / "control"))
from bagi_session import GameSession  # noqa: E402

# name, width, height (window units), density, safe area (top, right, bottom, left)
SIZES = [
    ("desktop", 1280, 720, 1, (0, 0, 0, 0)),
    ("phone, notch", 390, 844, 3, (47, 0, 34, 0)),
    ("phone, sideways", 844, 390, 3, (0, 47, 21, 47)),
    ("small phone", 320, 480, 2, (0, 0, 0, 0)),
]

parser = argparse.ArgumentParser()
parser.add_argument("--exe", default=str(REPOSITORY_ROOT / "build" / "macos-debug" / "bin" / "BagiDemo"))
exe = parser.parse_args().exe

with tempfile.TemporaryDirectory(prefix="bagi-ui-screen-") as scratch:  # the game writes into its overlay, so it gets a copy
    overlay = shutil.copytree(Path(__file__).parent, Path(scratch) / "content", ignore=shutil.ignore_patterns("sizes.py"))
    session = GameSession(client_name="ui-screen-sizes")
    session.launch(exe=exe, content_overlay=str(overlay))
    for name, width, height, density, safe_area in SIZES:
        session.set_window(width, height, density=density, safe_area=safe_area)
        session.step(2, 0.016)
        canvas = next(c for c in session.call("ui_tree")["canvases"] if c["node"] == "Settings")
        issues = [f"{issue['kind']} {issue['element']}" for issue in canvas["issues"]]
        print(f"{name:16} {width}x{height} @{density}  scale {canvas['scale']:.2f}  issues: {', '.join(issues) or 'none'}")
    session.close()
