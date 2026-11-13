"""A run of BagiStress for the perf runner: the game measures itself, this script only steps it. Standard library only.

    python3 tools/perf/stress_frames.py --exe build/macos-release/bin/BagiStress --scene sprites --count 2000 --frames 3000
    python3 tools/perf/stress_frames.py --exe ... --scene ui --ramp
    python3 tools/perf/stress_frames.py --exe ... --scene sprites --count 2000 --frames 600 --trace trace.json

Starts the game `--headless --control --manual-clock` with the `--stress-*` flags (`tools/stress/CLAUDE.md`), steps it in chunks of
`--chunk` frames until it quits by itself (the fixed mode after its `--frames`, the ramp when the search is done), and prints **the
game's JSON line unchanged** and nothing else: the last line of its log that starts with `{`. The perf runner reads that line (a `json`
metric). The connection closing during a step is the normal end; every other end (no line, a non-zero exit code, the step bound, the
time limit) is an error with the end of the game's log on stderr and exit code 1. `--trace FILE` passes `--trace=FILE` to the game
(a profile build), recorded over the whole run: the warm-up is in the trace too.
"""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))

from bagi_control_client import ConnectionClosed, ControlError  # noqa: E402
from game_process import GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402

SCENES = ("sprites", "entities", "ui")
MEASURES = ("work", "cpu")
STEP_DT = 1.0 / 60.0
DEFAULT_CHUNK = 60  # frames per `step`: a second of game time
# The defaults of StressSettings (Stress/StressSettings.h): the bound of the steps needs warmup and frames even when the flags are not given
DEFAULT_COUNT = 2000
DEFAULT_FRAMES = 3000
DEFAULT_WARMUP = 120
DEFAULT_ATLASES = 4
DEFAULT_TIMEOUT_SECONDS = 600.0
# The game quits by itself after its last frame; the steps past the end of its run are slack for frames that are held or restart the warm-up
STEP_SLACK_FRAMES = 600
MAX_RAMP_FRAMES = 20000  # kMaxRampFrames of Stress/RampController.h: the game's own hard stop of a ramp
EXIT_WAIT_SECONDS = 10.0  # how long the game gets to exit after its connection closed


class StressRunError(Exception):
    """The run did not give a line; the message says why and carries the end of the game's log."""


def stress_flags(args: argparse.Namespace) -> list[str]:
    """The `--stress-*` flags of the game for these arguments. A ramp starts at its own count and ignores `--count`, `--frames` and `--warmup`."""
    flags = [f"--stress-scene={args.scene}", f"--stress-measure={args.measure}", f"--stress-atlases={args.atlases}"]
    if args.ramp:
        flags.append("--stress-ramp")
    else:
        flags += [f"--stress-count={args.count}", f"--stress-frames={args.frames}", f"--stress-warmup={args.warmup}"]
    if args.interleave:
        flags.append("--stress-interleave")
    if args.trace:
        flags.append(f"--trace={Path(args.trace).resolve()}")
    return flags


def step_bound(args: argparse.Namespace) -> int:
    """The most frames the script steps before it gives up: the game's own run and the slack."""
    return (MAX_RAMP_FRAMES if args.ramp else args.warmup + args.frames) + STEP_SLACK_FRAMES


def last_json_line(text: str) -> str | None:
    """The last line of `text` that starts with `{`, as it is (a log line starts with `[`), or None."""
    for line in reversed(text.splitlines()):
        if line.startswith("{"):
            return line
    return None


def read_line(log_path: Path) -> str | None:
    try:
        return last_json_line(log_path.read_text(errors="replace"))
    except OSError:
        return None


def fail(game: GameProcess, reason: str) -> StressRunError:
    return StressRunError(f"{reason}\n--- game log (tail) ---\n{game.log_tail()}")


def run(args: argparse.Namespace) -> str:
    exe = Path(args.exe).resolve()
    port = free_port()
    bound = step_bound(args)
    deadline = time.monotonic() + args.timeout
    with GameProcess(build_game_command(exe, port, extra=stress_flags(args)), cwd=exe.parent) as game:
        client = game.connect(port, timeout=args.timeout)  # also the limit of one step: a slow frame is not a dead game
        stepped = 0
        ended = False
        try:
            while not ended:
                if stepped >= bound:
                    raise fail(game, f"the game did not quit in {stepped} stepped frames")
                if time.monotonic() > deadline:
                    raise fail(game, f"the game did not quit in {args.timeout:g} s")
                try:
                    client.call("step", {"frames": args.chunk, "dt": STEP_DT})
                    stepped += args.chunk
                except TimeoutError as error:
                    raise fail(game, f"a step of {args.chunk} frames did not answer in {args.timeout:g} s") from error
                except (ConnectionClosed, ControlError, OSError):
                    ended = True  # the game quit during the step; a control error that is not a quit ends in the exit code below
        finally:
            client.close()
        exit_code = game.stop(EXIT_WAIT_SECONDS)
        if exit_code != 0:
            raise fail(game, f"the game ended with exit code {exit_code}")
        line = read_line(game.log_path)
        if line is None:
            raise fail(game, "the game quit without a JSON line")
        try:
            json.loads(line)
        except ValueError as error:
            raise fail(game, f"the game's last JSON line does not parse ({error}): {line[:200]}") from error
        return line


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--exe", required=True, help="the BagiStress executable")
    parser.add_argument("--scene", choices=SCENES, default="sprites", help="the scene (default sprites)")
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT, help=f"objects in a fixed run (default {DEFAULT_COUNT})")
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES, help=f"measured frames of a fixed run (default {DEFAULT_FRAMES})")
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP, help=f"frames before measuring (default {DEFAULT_WARMUP})")
    parser.add_argument("--ramp", action="store_true", help="find the objects at 60 fps instead of a fixed run")
    parser.add_argument("--measure", choices=MEASURES, default="work", help="the number the ramp budgets: workMs or the whole cpuMs (default work)")
    parser.add_argument("--interleave", action="store_true", help="change the atlas and the blend mode on every sprite (the worst case)")
    parser.add_argument("--atlases", type=int, default=DEFAULT_ATLASES, help=f"atlases of the sprites scene (default {DEFAULT_ATLASES})")
    parser.add_argument("--chunk", type=int, default=DEFAULT_CHUNK, help=f"frames per step (default {DEFAULT_CHUNK})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS, help=f"seconds the whole run may take (default {DEFAULT_TIMEOUT_SECONDS:g})")
    parser.add_argument("--trace", help="record a Chrome trace into this file (a profile build)")
    args = parser.parse_args()
    if args.chunk < 1 or args.frames < 1 or args.warmup < 0 or args.count < 1 or args.timeout <= 0:
        parser.error("--chunk, --frames and --count must be at least 1, --warmup at least 0 and --timeout positive")
    try:
        print(run(args))
    except (StressRunError, GameLaunchError) as error:
        print(f"stress_frames: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
