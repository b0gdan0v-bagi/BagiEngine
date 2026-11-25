"""The frame cost of the editor, a benchmark command of the perf runner. Standard library only.

    python3 tools/perf/editor_frames.py --exe build/macos-release-editor/bin/BagiStress --frames 300 --warmup 120
    python3 tools/perf/editor_frames.py --exe build/macos-profile-editor/bin/BagiStress --trace trace.json

Starts `BagiStress` as a headless editor (`--headless --editor --manual-clock`, a 1920x1080 window: the size the editor's default layout and
the dock constants of `tools/qa/bagi_qa_editor.py` are made for) on the scene `sprites` at `--count` sprites, opens the panels a person
edits with (Viewport, the Scene Editor with the left dock widened, the node `Run` selected and `TransformComponent` open, the Game panel),
parks the pointer outside the window, steps `--warmup` frames and then `--frames` frames in chunks of at most `--chunk`, reading `frame_stats`
after each chunk, and prints **one JSON line**: the nearest-rank p50 and p99 of `workMs` and `cpuMs`, the means of the draw calls and the
allocations, `allocTracking`, and what was measured. `workMs` is `cpuMs` minus the `Present` and `Wait` stages, as `StressRecorder` computes it
(`tools/stress/CLAUDE.md`, "Fixed mode"); the present of the software renderer, ImGui's draw data included, is outside it.

It steps from outside, like `headless_frames.py`, instead of the game's own `--stress-frames`: that run measures from the first frame and quits
by itself, so the frames in which the panels are opened would be counted or the run would end first. The panels are only the ones that draw
nothing of their own: the Profiler and the Console draw counters, and Generate Art starts work, so they would measure themselves. Save is never
clicked. `BAGI_EDITOR_DIR` is a temporary folder for the start, so no `imgui.ini` of this machine changes the layout. `--trace FILE` records the
profiler's trace of the whole run (a profile build, `perf.py --trace`).

Exit code: 0 a line printed; 1 a move of the editor failed (a label it renamed) or the game did not answer, the text is on stderr: the benchmark
must not silently stop measuring; 77 with a `skip: <reason>` line when the executable is missing or the build has no editor (`status.runMode` is
not `Editor`), which `perf.py` reports as skipped, never failed.
"""

import argparse
import contextlib
import json
import math
import os
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterator, Mapping, Sequence

TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR / "control"))
sys.path.insert(0, str(TOOLS_DIR / "qa"))

import bagi_qa_editor as editor  # noqa: E402
from bagi_control_client import ControlError  # noqa: E402
from bagi_session import GameSession, SessionError  # noqa: E402

EXIT_FAILED = 1
EXIT_SKIPPED = 77  # the convention of the smokes (tools/control/CLAUDE.md); perf.py reads it
FRAME_STATS_MAX = 240  # ProfilerControl: frame_stats keeps at most this many frames
STEP_DT = 1.0 / 60.0
DEFAULT_FRAMES = 300
DEFAULT_WARMUP = 120
DEFAULT_COUNT = 500
SCENE = "sprites"
WINDOW = "1920x1080"  # the layout window of bagi_qa_editor
SELECTED_NODE = "Run"
OPENED_COMPONENT = "TransformComponent"
PANELS = (editor.SCENE_EDITOR, "Viewport", "Game Panel")
PARKED_POINTER = (-10.0, -10.0)  # outside the window: no hover colour, no tooltip
SETTLE_FRAMES = 3
UNMEASURED_STAGES = ("Present", "Wait")
RUN_MODE_EDITOR = "Editor"
EDITOR_DIR_VARIABLE = "BAGI_EDITOR_DIR"
QUIT_METHOD = "quit"


def nearest_rank(values: Sequence[float], fraction: float) -> float:
    """`sorted[ceil(p * n) - 1]`, the rule of `StressRecorder`: a reported percentile is a frame that happened; one sample is every percentile."""
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def mean(values: Sequence[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def work_ms(frame: Mapping[str, Any]) -> float:
    """The CPU time of a frame without the stages that wait or copy the picture; never below 0."""
    stages = frame.get("stagesMs", {})
    return max(0.0, frame["cpuMs"] - sum(stages.get(stage, 0.0) for stage in UNMEASURED_STAGES))


def summarize(frames: Sequence[Mapping[str, Any]]) -> dict[str, float]:
    """The numbers of the line for the `frame_stats` frames: percentiles of `workMs` and `cpuMs`, means of the counters."""
    work = [work_ms(frame) for frame in frames]
    cpu = [float(frame["cpuMs"]) for frame in frames]
    return {
        "workMsP50": nearest_rank(work, 0.50),
        "workMsP99": nearest_rank(work, 0.99),
        "cpuMsP50": nearest_rank(cpu, 0.50),
        "cpuMsP99": nearest_rank(cpu, 0.99),
        "drawCalls": mean([float(frame["drawCalls"]) for frame in frames]),
        "allocsPerFrame": mean([float(frame["allocCount"]) for frame in frames]),
        "allocBytesPerFrame": mean([float(frame["allocBytes"]) for frame in frames]),
    }


def open_editor_state(session: Any, count: int) -> None:
    """Puts the editor in the state that is measured; a move that fails is a SessionError."""
    session.call("reset", {"scene": SCENE, "count": count})
    session.step(SETTLE_FRAMES, STEP_DT)  # the scene's nodes exist before the hierarchy is read
    editor.widen_left_dock(session)
    # The node is selected before the other panels open: with the Viewport and the Game panel already docked the hierarchy's search field
    # does not take the focus, and the search is the way to a node under the collapsed root
    editor.open_panel(session, editor.SCENE_EDITOR)
    editor.select_node(session, SELECTED_NODE)
    editor.open_component(session, OPENED_COMPONENT)
    for title in PANELS[1:]:
        editor.open_panel(session, title)
    x, y = PARKED_POINTER
    session.imgui_input([{"type": "move", "x": x, "y": y}])
    session.step(SETTLE_FRAMES, STEP_DT)
    shut = [title for title in PANELS if not editor.panel_open(session, title)]
    if shut:  # a panel that the later ones pushed out of the frame would be measured as if it were drawn
        raise SessionError(f"the panels {shut} are not drawn after the editor was set up")


def measure(session: Any, args: argparse.Namespace) -> dict[str, Any]:
    """The whole sequence over a started editor session; the dict is the JSON line."""
    open_editor_state(session, args.count)
    if args.warmup:
        session.step(args.warmup, STEP_DT)
    frames: list[dict[str, Any]] = []
    tracking = False
    left = args.frames
    while left > 0:
        count = min(left, args.chunk)
        session.step(count, STEP_DT)
        stats = session.call("frame_stats", {"frames": count})
        frames.extend(stats["frames"])
        tracking = bool(stats.get("allocTracking", False))
        left -= count
    result: dict[str, Any] = dict(summarize(frames))
    result.update(allocTracking=tracking, frames=len(frames), warmup=args.warmup, scene=SCENE, count=args.count, panels=list(PANELS))
    return result


@contextlib.contextmanager
def editor_environment(folder: Path) -> Iterator[None]:
    """`BAGI_EDITOR_DIR` is `folder` while the game starts (it inherits the variable) and what it was before afterwards."""
    previous = os.environ.get(EDITOR_DIR_VARIABLE)
    os.environ[EDITOR_DIR_VARIABLE] = str(folder)
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop(EDITOR_DIR_VARIABLE, None)
        else:
            os.environ[EDITOR_DIR_VARIABLE] = previous


def game_flags(args: argparse.Namespace) -> list[str]:
    flags = [f"--stress-scene={SCENE}", f"--stress-count={args.count}"]
    if args.trace:
        flags.append(f"--trace={Path(args.trace).resolve()}")
    return flags


def run(args: argparse.Namespace) -> int:
    exe = Path(args.exe).expanduser().resolve()
    if not exe.is_file():
        print(f"skip: {exe} does not exist")
        return EXIT_SKIPPED
    scratch = Path(tempfile.mkdtemp(prefix="bagi-editor-frames-"))
    session = GameSession()
    try:
        with editor_environment(scratch):
            description = session.launch(exe=str(exe), window=WINDOW, editor=True, extra=game_flags(args))
        run_mode = (description.get("status") or {}).get("runMode")
        if run_mode != RUN_MODE_EDITOR:
            print(f"skip: {exe.name} has no editor: status.runMode is {run_mode}")
            return EXIT_SKIPPED
        print(json.dumps(measure(session, args), sort_keys=True))
        return 0
    except (SessionError, ControlError) as error:
        print(f"editor_frames: {error}", file=sys.stderr)
        return EXIT_FAILED
    finally:
        with contextlib.suppress(SessionError, ControlError, OSError):
            session.quit()
        session.close()
        shutil.rmtree(scratch, ignore_errors=True)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--exe", required=True, help="the BagiStress executable of a build with the editor (macos-release-editor)")
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES, help="measured frames (default %(default)s)")
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP, help="frames stepped before measuring (default %(default)s)")
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT, help="sprites of the scene (default %(default)s)")
    parser.add_argument("--chunk", type=int, default=FRAME_STATS_MAX, help=f"frames per step, at most {FRAME_STATS_MAX} (frame_stats keeps that many)")
    parser.add_argument("--trace", help="record a Chrome trace of the whole run into this file (a profile build)")
    args = parser.parse_args(argv)
    if args.frames < 1 or args.warmup < 0 or args.count < 1 or not 1 <= args.chunk <= FRAME_STATS_MAX:
        parser.error(f"--frames and --count must be at least 1, --warmup not negative, --chunk 1..{FRAME_STATS_MAX}")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
