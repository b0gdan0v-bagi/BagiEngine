"""The art generation flow of the skill /artgen in a headless editor (tools/artgen/headless_check.py), against the Debug BagiStress.

Skipped when that build is missing or has no editor. Accept is left to the command's own flags: it writes into the game's real folders.
"""

import sys
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))
sys.path.insert(0, str(ARTGEN_DIR.parent / "control"))

import headless_check  # noqa: E402
from game_process import preset_executable  # noqa: E402

DEBUG_PRESETS = ("macos-debug", "windows-msvc-debug", "windows-clang-debug", "debug")


def debug_stress_executable() -> Path | None:
    candidates = (preset_executable(preset, "BagiStress") for preset in DEBUG_PRESETS)
    return next((path for path in candidates if path.is_file()), None)


def test_generate_and_set_run_without_a_frame(capsys):
    exe = debug_stress_executable()
    if exe is None:
        pytest.skip("no Debug BagiStress build")
    code = headless_check.main([str(exe)])
    output = capsys.readouterr().out
    if code == headless_check.EXIT_SKIPPED:
        pytest.skip("this build has no editor")
    assert code == headless_check.EXIT_PASSED, output
