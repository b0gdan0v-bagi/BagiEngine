"""headless_frames.py: the summary of the `frame_stats` frames (the whole script needs a game)."""

import sys
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PERF_DIR))

import headless_frames  # noqa: E402


def make_frame(primitives: int, gpu_ms: float) -> dict:
    return {
        "cpuMs": 1.0,
        "drawCalls": 1,
        "primitives": primitives,
        "triangles": 2 * primitives,
        "textureSwitches": 0,
        "allocCount": 0,
        "allocBytes": 0,
        "gpuMs": gpu_ms,
        "stagesMs": {"Present": 0.5},
    }


def test_the_gpu_time_is_the_mean_of_the_frames_that_have_one() -> None:
    result = headless_frames.summarize_frames([make_frame(10, 0.0), make_frame(20, 4.0), make_frame(30, 8.0)])
    assert result["gpuMs"] == 6.0
    assert result["primitives"] == 20


def test_no_frame_with_a_gpu_time_reports_zero() -> None:
    assert headless_frames.summarize_frames([make_frame(1, 0.0)])["gpuMs"] == 0.0
