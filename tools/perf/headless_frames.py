"""Frames of a game without a window: a benchmark command of the perf runner. Standard library only.

    python3 tools/perf/headless_frames.py --exe build/macos-release/bin/BagiMatch3 --frames 600
    python3 tools/perf/headless_frames.py --exe ... --call perform_action '{"action": "new_match"}' --frames 600 --trace trace.json

Starts the game `--headless --manual-clock` on a free port, runs the `--call`s (method and JSON params, in order; a `step` follows each so
the scene reacts), steps `--warmup` frames, then `--frames` frames in steps of at most `--chunk`, and prints **one JSON line**: the wall
time of the measured steps (`framesPerSecond`, `wallMsPerFrame`) and the means of `frame_stats` over the measured frames (`cpuMs`, the
stage times, `workMs` (the CPU time without the present, which headless is the software renderer's copy and most of the frame), draw calls, triangles, texture switches, allocations; the allocations are 0 without `ENGINE_PROFILER_ALLOC`). The frames
have `dt` 1/60 of game time, so animations run as in a real game; the wall time is the CPU of the frames, nothing waits for a display.
`--trace FILE` passes `--trace=FILE` to the game (a profile build), recorded over the whole run; `MarkTraceStart` is not reachable from
here, so the warm-up is in the trace too.
"""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))

from game_process import GameProcess, build_game_command, free_port  # noqa: E402

FRAME_STATS_MAX = 240  # ProfilerControl: frame_stats keeps at most this many frames
STEP_DT = 1.0 / 60.0
MS_PER_SECOND = 1000.0


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def summarize_frames(frames: list[dict]) -> dict:
    """The means of the `frame_stats` frames (and the 95th percentile of the CPU time)."""
    cpu = [frame["cpuMs"] for frame in frames]
    result = {
        "statFrames": len(frames),
        "cpuMs": mean(cpu),
        "cpuMsP95": percentile(cpu, 0.95),
        "drawCalls": mean([frame["drawCalls"] for frame in frames]),
        "triangles": mean([frame["triangles"] for frame in frames]),
        "textureSwitches": mean([frame["textureSwitches"] for frame in frames]),
        "allocsPerFrame": mean([frame["allocCount"] for frame in frames]),
        "allocBytesPerFrame": mean([frame["allocBytes"] for frame in frames]),
    }
    for stage in frames[0]["stagesMs"] if frames else {}:
        result[f"{stage}Ms"] = mean([frame["stagesMs"][stage] for frame in frames])
    # Headless, the present of the software renderer is most of a frame and does not depend on the scene: `workMs` is the rest
    result["workMs"] = result["cpuMs"] - result.get("PresentMs", 0.0)
    return result


def run(args: argparse.Namespace) -> dict:
    exe = Path(args.exe).resolve()
    port = free_port()
    extra = (f"--trace={Path(args.trace).resolve()}",) if args.trace else ()
    with GameProcess(build_game_command(exe, port, extra=extra), cwd=exe.parent) as game:
        client = game.connect(port)
        try:
            for method, params in args.call:
                client.call(method, json.loads(params) if params else None)
                client.call("step", {"frames": 1, "dt": STEP_DT})
            if args.warmup:
                client.call("step", {"frames": args.warmup, "dt": STEP_DT})
            frames: list[dict] = []
            left = args.frames
            wall = 0.0
            while left > 0:
                count = min(left, args.chunk)
                started = time.perf_counter()
                client.call("step", {"frames": count, "dt": STEP_DT})
                wall += time.perf_counter() - started
                stats = client.call("frame_stats", {"frames": min(count, FRAME_STATS_MAX)})
                frames.extend(stats["frames"])
                left -= count
            allocs = client.call("frame_stats", {"frames": 1}).get("allocTracking", False)
        finally:
            try:
                client.call("quit")
            except Exception:  # noqa: BLE001 - the game may close the connection as it quits
                pass
    result = {
        "frames": args.frames,
        "wallMs": wall * MS_PER_SECOND,
        "wallMsPerFrame": wall * MS_PER_SECOND / args.frames,
        "framesPerSecond": args.frames / wall if wall else 0.0,
        "allocTracking": allocs,
    }
    result.update(summarize_frames(frames))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--exe", required=True, help="the game executable")
    parser.add_argument("--frames", type=int, default=600, help="measured frames (default 600)")
    parser.add_argument("--warmup", type=int, default=120, help="frames stepped before measuring (default 120)")
    parser.add_argument("--chunk", type=int, default=FRAME_STATS_MAX, help=f"frames per step, at most {FRAME_STATS_MAX} (frame_stats keeps that many)")
    parser.add_argument("--call", nargs=2, action="append", default=[], metavar=("METHOD", "PARAMS"), help="a control call before the warm-up; PARAMS is JSON or ''")
    parser.add_argument("--trace", help="record a Chrome trace into this file (a profile build)")
    args = parser.parse_args()
    if args.frames < 1 or not 1 <= args.chunk <= FRAME_STATS_MAX:
        parser.error(f"--frames must be at least 1 and --chunk 1..{FRAME_STATS_MAX}")
    print(json.dumps(run(args), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
