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

`--backend sdl-gpu|vulkan` runs the game on that GPU backend in a hidden window (`--hidden-window --backend=<it>` instead of `--headless`;
`software`, the default, is the headless run). A machine without the backend cannot start the game, which logs `BACKEND_UNAVAILABLE_MARKER`
before it exits: that is not an error here but the line `skip: <the game's own words>` on stdout and exit code 77 (`EXIT_SKIPPED`), which
`perf.py` reports as a skipped benchmark. Any other early exit stays an error. `--no-batching` passes `--stress-no-batching`, `--budget-ms`
passes `--stress-budget-ms` (the frame time a `--ramp` holds; the game checks the value). `--sprite-size`, `--world` and `--churn`, the loads
of the `sprites` scene (`tools/stress/src/Stress/CLAUDE.md`), are passed only when given and checked by the game too.
"""

import argparse
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))

from bagi_control_client import ConnectionClosed, ControlError  # noqa: E402
from game_process import BACKEND_UNAVAILABLE_MARKER, GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402

SCENES = ("sprites", "entities", "ui")
BACKENDS = ("software", "sdl-gpu", "vulkan")
DEFAULT_BACKEND = "software"
LOG_PREFIX_RE = re.compile(r"^\[[^\]]*\]\s*\[[^\]]*\]\s*")  # "[Error] [12:00:00.000] "
EXIT_SKIPPED = 77  # the convention of the smokes (tools/control/CLAUDE.md); perf.py reads it
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


class BackendUnavailable(Exception):
    """The game could not start on the backend asked for; the message is the game's own line about it."""


def stress_flags(args: argparse.Namespace) -> list[str]:
    """The `--stress-*` flags of the game for these arguments. A ramp starts at its own count and ignores `--count`, `--frames` and `--warmup`."""
    flags = [f"--stress-scene={args.scene}", f"--stress-measure={args.measure}", f"--stress-atlases={args.atlases}"]
    if args.ramp:
        flags.append("--stress-ramp")
    else:
        flags += [f"--stress-count={args.count}", f"--stress-frames={args.frames}", f"--stress-warmup={args.warmup}"]
    if args.interleave:
        flags.append("--stress-interleave")
    if args.no_batching:
        flags.append("--stress-no-batching")
    if args.budget_ms is not None:
        flags.append(f"--stress-budget-ms={args.budget_ms}")
    if args.sprite_size is not None:
        flags.append(f"--stress-sprite-size={args.sprite_size}")
    if args.world is not None:
        flags.append(f"--stress-world={args.world}")
    if args.churn is not None:
        flags.append(f"--stress-churn={args.churn}")
    if args.trace:
        flags.append(f"--trace={Path(args.trace).resolve()}")
    return flags


def game_command(exe: Path, port: int, args: argparse.Namespace) -> list[str]:
    """The software renderer is the headless run; a GPU backend needs a window, hidden so that nothing shows on the screen."""
    if args.backend == DEFAULT_BACKEND:
        return build_game_command(exe, port, extra=stress_flags(args))
    return build_game_command(exe, port, headless=False, extra=["--hidden-window", f"--backend={args.backend}", *stress_flags(args)])


def unavailable_reason(log_path: Path) -> str | None:
    """The game's line that says the backend cannot start on this machine (without its time stamp), or None."""
    try:
        text = log_path.read_text(errors="replace")
    except OSError:
        return None
    for line in text.splitlines():
        if BACKEND_UNAVAILABLE_MARKER in line:
            return LOG_PREFIX_RE.sub("", line).strip()
    return None


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
    with GameProcess(game_command(exe, port, args), cwd=exe.parent) as game:
        try:
            client = game.connect(port, timeout=args.timeout)  # also the limit of one step: a slow frame is not a dead game
        except GameLaunchError:
            reason = unavailable_reason(game.log_path)
            if reason is None:
                raise
            raise BackendUnavailable(reason) from None
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
                except (ConnectionClosed, OSError):
                    ended = True  # the game quit during the step
                except ControlError as error:
                    ended = True  # a control error ends the run too; the exit code and the line decide below, the text is kept for the log
                    print(f"stress_frames: the step answered with a control error: {error}", file=sys.stderr)
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
    parser.add_argument("--ramp", action="store_true", help="find the largest count whose frames fit the budget instead of a fixed run")
    parser.add_argument("--measure", choices=MEASURES, default="work", help="the number the ramp budgets: workMs or the whole cpuMs (default work)")
    parser.add_argument("--budget-ms", help="the frame time in ms the ramp holds (the game's default: 16.6, a frame of 60 fps)")
    parser.add_argument("--interleave", action="store_true", help="change the atlas and the blend mode on every sprite (the worst case)")
    parser.add_argument("--atlases", type=int, default=DEFAULT_ATLASES, help=f"atlases of the sprites scene (default {DEFAULT_ATLASES})")
    parser.add_argument("--backend", choices=BACKENDS, default=DEFAULT_BACKEND, help="the renderer: software (headless, default) or a GPU backend in a hidden window; a machine without it skips")
    parser.add_argument("--no-batching", action="store_true", help="turn the 2D batcher off for the run (--stress-no-batching)")
    parser.add_argument("--sprite-size", help="the side of every sprite of the sprites scene (the game's default: 16..48 by sprite)")
    parser.add_argument("--world", help="views per side of the field of the sprites scene, with an emulated camera (the game's default: 1)")
    parser.add_argument("--churn", help="sprites of the sprites scene despawned and spawned every frame (the game's default: none)")
    parser.add_argument("--chunk", type=int, default=DEFAULT_CHUNK, help=f"frames per step (default {DEFAULT_CHUNK})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS, help=f"seconds the whole run may take (default {DEFAULT_TIMEOUT_SECONDS:g})")
    parser.add_argument("--trace", help="record a Chrome trace into this file (a profile build)")
    args = parser.parse_args()
    if args.chunk < 1 or args.frames < 1 or args.warmup < 0 or args.count < 1 or args.timeout <= 0:
        parser.error("--chunk, --frames and --count must be at least 1, --warmup at least 0 and --timeout positive")
    try:
        print(run(args))
    except BackendUnavailable as skipped:
        print(f"skip: {skipped}")
        return EXIT_SKIPPED
    except (StressRunError, GameLaunchError) as error:
        print(f"stress_frames: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
