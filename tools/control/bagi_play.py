#!/usr/bin/env python3
"""Plays a scenario file (JSON lines of control calls, see tools/control/CLAUDE.md, "Scenario files") against a game and checks its expectations.

    python3 tools/control/bagi_play.py <exe> <scenario.jsonl>
    python3 tools/control/bagi_play.py --preset macos-debug --game BagiStress <scenario.jsonl>
    python3 tools/control/bagi_play.py <exe> <scenario.jsonl> --update        # rewrite the expectations from a run
    python3 tools/control/bagi_play.py <exe> <scenario.jsonl> --shrink short.jsonl   # the fewest calls that still fail the same way

The game is started with the flags of the header, the calls are sent in order, and the run stops at the first line whose expectation does not hold.
Exit code: 0 passed, 1 failed, 2 a bad file or arguments, 77 skipped (the executable does not exist; ctest reads 77 as "skipped").
Standard library only (through bagi_session.py).
"""

import argparse
import dataclasses
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import scenario
from bagi_control_client import ControlError
from bagi_session import GameSession, SessionError
from game_process import DEFAULT_STARTUP_TIMEOUT, PORT_TAKEN_MARKER, preset_executable
from scenario_shrink import shrink

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
EXIT_SKIPPED = 77

HEADLESS_FLAG = "--headless"
MANUAL_CLOCK_FLAG = "--manual-clock"
PARAMS_SHOWN = 200  # characters of a call's params in a failure message
DEFAULT_MAX_PROBES = 200  # game launches a --shrink may spend: each probe starts the game
HEADER_STEP = -1  # `failed_step` of a failure of the header (the game is not the one the file names)


@dataclass
class PlayResult:
    """What a run came to. `failed_step` is the index of the step whose expectation did not hold (HEADER_STEP: the header's game), with its `failure`.

    `error` is a problem that is not an expectation (the game did not start, the connection was lost, a bad exit), at `error_step` when a call met it.
    `steps` are the steps with the expectations rewritten and `changed` the indexes that differ, for an update run.
    """

    calls: int = 0
    failed_step: int | None = None
    failure: scenario.Failure | None = None
    error: str | None = None
    error_step: int | None = None
    steps: list[scenario.Step] = field(default_factory=list)
    changed: list[int] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        return self.failed_step is None and self.error is None


def launch_options(header: scenario.Header) -> tuple[bool, bool, list[str]]:
    """The header's flags as `launch` takes them: headless, manual clock, and the flags that go to the game as they are."""
    extra = [flag for flag in header.flags if flag not in (HEADLESS_FLAG, MANUAL_CLOCK_FLAG)]
    return HEADLESS_FLAG in header.flags, MANUAL_CLOCK_FLAG in header.flags, extra


def launch(session: GameSession, exe: Path, header: scenario.Header, startup_timeout: float) -> dict[str, Any]:
    """Starts the game; when it reports that a port free_port() chose was taken meanwhile, once more on another port."""
    headless, manual_clock, extra = launch_options(header)
    for attempt in range(2):
        try:
            return session.launch(str(exe), headless=headless, manual_clock=manual_clock, extra=extra, startup_timeout=startup_timeout)
        except SessionError as error:
            if attempt == 0 and PORT_TAKEN_MARKER in str(error):
                print("the port was taken by another process before the game could bind it; retrying once on another port", flush=True)
                continue
            raise
    raise AssertionError("unreachable")


def play(
    exe: Path,
    header: scenario.Header,
    steps: list[scenario.Step],
    update: bool = False,
    lenient: bool = False,
    strict_step: int | None = None,
    startup_timeout: float = DEFAULT_STARTUP_TIMEOUT,
) -> PlayResult:
    """Starts the game, plays `steps` and stops it. Stops at the first line that fails, unless `update`, which plays all and rewrites the expectations.

    `lenient` tolerates an error a line did not expect (the shrinker's probes), except at `strict_step`, the line the probe is about. `logClean` is judged after the game has quit: the game's stdout is
    block-buffered into its log, so a Warning may not be in the file while the game runs; the failure still names the line that asked for it.
    """
    result = PlayResult(steps=list(steps))
    session = GameSession()
    try:
        try:
            description = launch(session, exe, header, startup_timeout)
        except SessionError as error:
            result.error = f"the game did not start: {error}"
            return result
        if description.get("activeGame") != header.game:
            result.failed_step = HEADER_STEP
            result.failure = scenario.Failure("game", header.game, str(description.get("activeGame")))
            return result
        if not _play_steps(session, steps, result, update, lenient, strict_step):
            return result
        try:
            quit_answer = session.quit()
            problems = session.log_problems()
        except SessionError as error:
            result.error = str(error)
            return result
        if quit_answer.get("exitCode", 0) != 0:
            result.error = f"the game exited with code {quit_answer['exitCode']} after quit"
        elif not update:
            _check_log(steps, problems, result)
    finally:
        session.close()
    if update and result.passed:
        result.changed = [index for index, (old, new) in enumerate(zip(steps, result.steps)) if old.expect != new.expect]
    return result


def _play_steps(session: GameSession, steps: list[scenario.Step], result: PlayResult, update: bool, lenient: bool, strict_step: int | None) -> bool:
    """Sends the calls in order; False when the run has to stop (`result` says why)."""
    versions = scenario.VersionTracker()
    for index, step in enumerate(steps):
        outcome = scenario.Outcome()
        try:
            try:
                answer = session.call(step.method, step.params)
                outcome.result = answer
                outcome.version_step = versions.step_of(answer)
            except ControlError as error:
                outcome.error_code, outcome.error_message = error.code, error.message
            wanted = step.expect.get("event")
            if wanted is not None and not update:
                timeout = float(wanted.get("timeout", scenario.DEFAULT_EVENT_TIMEOUT_SECONDS))
                event = session.take_event(lambda pushed: scenario.match_event(wanted, pushed), timeout)
                outcome.events = [] if event is None else [event]
        except SessionError as error:
            result.error, result.error_step = str(error), index
            return False
        result.calls += 1
        if update:
            result.steps[index] = dataclasses.replace(step, expect=scenario.updated_expect(step.expect, outcome))
            continue
        failure = scenario.evaluate({name: value for name, value in step.expect.items() if name != "logClean"}, outcome, lenient and index != strict_step)
        if failure is not None:
            result.failed_step, result.failure = index, failure
            return False
    return True


def _check_log(steps: list[scenario.Step], problems: list[str], result: PlayResult) -> None:
    for index, step in enumerate(steps):
        if step.expect.get("logClean"):
            failure = scenario.evaluate({"logClean": True}, scenario.Outcome(log_problems=problems))
            if failure is not None:
                result.failed_step, result.failure = index, failure
                return


def step_lines(text: str) -> list[int]:
    """The line number in the file of every step: the non-blank lines after the header (a blank line is skipped by the loader)."""
    numbers = [number for number, line in enumerate(text.splitlines(), start=1) if line.strip()]
    return numbers[1:]


def describe_call(step: scenario.Step) -> str:
    params = "" if step.params is None else " " + json.dumps(step.params, ensure_ascii=False)
    text = step.method + params
    return text if len(text) <= PARAMS_SHOWN else text[:PARAMS_SHOWN] + "..."


def report(path: Path, steps: list[scenario.Step], lines: list[int], result: PlayResult) -> None:
    """Prints how a run that did not pass failed."""
    if result.failed_step == HEADER_STEP:
        print(f"FAILED {path}:1  the header: the game that started is not the one the file names")
    elif result.failed_step is not None:
        print(f"FAILED {path}:{lines[result.failed_step]}  {describe_call(steps[result.failed_step])}")
    elif result.error_step is not None:
        print(f"FAILED {path}:{lines[result.error_step]}  {describe_call(steps[result.error_step])}")
    else:
        print(f"FAILED {path}")
    if result.failure is not None:
        print(f"  {result.failure}")
    if result.error is not None:
        print(f"  {result.error}")


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", metavar="[exe] scenario", help="the game executable (unless --preset and --game name it) and the scenario file")
    parser.add_argument("--preset", help="the CMake preset that built the game: the executable is build/<preset>/bin/<game>")
    parser.add_argument("--game", help="the executable name for --preset, e.g. BagiStress")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    parser.add_argument("--update", action="store_true", help="play every call without checking and rewrite the expectations of the file from the run")
    parser.add_argument("--shrink", type=Path, metavar="OUT", help="write to OUT the fewest calls of the scenario that still fail the same way (the scenario has to fail)")
    parser.add_argument("--max-probes", type=int, default=DEFAULT_MAX_PROBES, help="game launches --shrink may spend (default %(default)s)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_STARTUP_TIMEOUT, help="seconds to wait for the control API after the start")
    args = parser.parse_args(argv)

    by_preset = args.preset is not None or args.game is not None
    if by_preset and (args.preset is None or args.game is None):
        parser.error("--preset and --game go together")
    if args.shrink is not None and args.update:
        parser.error("--shrink and --update do not go together")
    if args.max_probes < 1:
        parser.error("--max-probes has to be at least 1")
    if len(args.paths) != (1 if by_preset else 2):
        parser.error("give the scenario file after the executable, or after --preset and --game (the executable is then not given)")
    args.scenario = Path(args.paths[-1])
    args.exe = preset_executable(args.preset, args.game, args.root) if by_preset else Path(args.paths[0])
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    try:
        text = args.scenario.read_text(encoding="utf-8")
        header, steps = scenario.parse(text, str(args.scenario))
    except (OSError, UnicodeDecodeError) as error:
        print(f"ERROR cannot read {args.scenario}: {error}")
        return EXIT_USAGE
    except scenario.ScenarioError as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED

    exe = args.exe.resolve()  # the game runs in its own folder
    if args.shrink is not None:
        return _shrink(args, exe, header, steps, step_lines(text))
    result = play(exe, header, steps, update=args.update, startup_timeout=args.timeout)
    if not result.passed:
        report(args.scenario, steps, step_lines(text), result)
        return EXIT_FAILED
    if not args.update:
        print(f"PASSED {args.scenario}: {result.calls} calls")
        return EXIT_PASSED
    return _write_update(args.scenario, header, steps, step_lines(text), result)


def _shrink(args: argparse.Namespace, exe: Path, header: scenario.Header, steps: list[scenario.Step], lines: list[int]) -> int:
    """Plays the file, which has to fail, then looks for the fewest calls that fail the same way and writes them to --shrink."""
    full = play(exe, header, steps, startup_timeout=args.timeout)
    if full.passed:
        print(f"{args.scenario} passes, nothing to shrink")
        return EXIT_USAGE
    if full.failed_step is None or full.failed_step == HEADER_STEP or full.failure is None:
        report(args.scenario, steps, lines, full)
        print("  this is not a failed expectation of a call, so there is nothing to shrink")
        return EXIT_FAILED
    target, expectation = full.failed_step, full.failure.expectation
    print(f"shrinking: {full.failure.expectation} of line {lines[target]} ({describe_call(steps[target])}) fails; looking for the calls it needs", flush=True)

    # A dropped call changes the versions and the answers of the calls after it, so only the expectation that failed is kept, and lenient
    # mode lets the other calls answer errors: whatever fails then is the failure that was found, not noise from the rest of the file.
    # The target line itself is not lenient: when the dropped calls were what it needed, it answers an error, which is another failure.
    stripped = [dataclasses.replace(step, expect=step.expect if index == target else {}) for index, step in enumerate(steps)]
    launches = 0
    cache: dict[tuple[int, ...], bool] = {}

    def still_fails(candidate: list[int]) -> bool:
        nonlocal launches
        key = tuple(candidate)
        if target not in candidate:
            return False  # no other line has an expectation left, so nothing can fail
        if key not in cache:
            launches += 1
            position = candidate.index(target)
            probe = play(exe, header, [stripped[index] for index in candidate], lenient=True, strict_step=position, startup_timeout=args.timeout)
            cache[key] = probe.failure is not None and probe.failed_step == position and probe.failure.expectation == expectation
        return cache[key]

    try:
        kept = shrink(list(range(len(steps))), still_fails, args.max_probes)
    except ValueError:
        print(f"the failure did not come back with the other expectations stripped ({launches} launches); the scenario is left as it is")
        return EXIT_USAGE
    try:
        scenario.dump(args.shrink, header, [steps[index] for index in kept])
    except OSError as error:
        print(f"ERROR cannot write {args.shrink}: {error}")
        return EXIT_FAILED
    limit = f"; stopped at the limit of {args.max_probes} probes, so it may still shrink" if launches >= args.max_probes else ""
    print(f"SHRUNK {args.shrink}: {len(steps)} calls -> {len(kept)} calls (lines {', '.join(str(lines[index]) for index in kept)} of {args.scenario}), {launches} probes{limit}")
    return EXIT_PASSED


def _write_update(path: Path, header: scenario.Header, steps: list[scenario.Step], lines: list[int], result: PlayResult) -> int:
    for index in result.changed:
        old, new = steps[index].expect, result.steps[index].expect
        print(f"line {lines[index]}  {describe_call(steps[index])}: {json.dumps(old)} -> {json.dumps(new)}")
    try:
        scenario.dump(path, header, result.steps)
    except OSError as error:
        print(f"ERROR cannot write {path}: {error}")
        return EXIT_FAILED
    print(f"UPDATED {path}: {len(result.changed)} of {result.calls} lines changed")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
