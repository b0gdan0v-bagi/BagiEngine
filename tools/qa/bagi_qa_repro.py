"""Reproducing a finding: a recorded session played again with the checks, cut down to the calls the finding needs, written as a scenario.

    findings = replay(exe, "session.jsonl", "invariants.py")
    result = shrink_finding(exe, "session.jsonl", findings[0].name, "invariants.py")
    write_repro(Path("bug-x-repro.jsonl"), result.header, result.calls, result.finding)

The checks are the session's (`QaSession.replay_call`), judged after every call; a call the game refuses in a replay is tolerated, as in the
lenient mode of bagi_play, so a candidate that dropped the setup of a call still plays. A reproduction is matched by the finding's `name`.
The command line (`bagi_qa.py replay`, `bagi_qa.py shrink`) is `command_replay` and `command_shrink`. Standard library only (through tools/control).
"""

import argparse
import dataclasses
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "shots"))
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR))

import bagi_play  # noqa: E402
import bagi_qa_checks as checks  # noqa: E402
import bagi_qa_report as report  # noqa: E402
import png_ops  # noqa: E402
import scenario  # noqa: E402
from bagi_qa import HEADLESS_FLAG, MANUAL_CLOCK_FLAG, UI_TREE_METHOD, QaSession  # noqa: E402
from bagi_qa_checks import Finding, Invariants  # noqa: E402
from bagi_session import SessionError  # noqa: E402
from game_process import PORT_TAKEN_MARKER  # noqa: E402
from scenario_shrink import shrink  # noqa: E402

EXIT_NOTHING = bagi_play.EXIT_PASSED  # nothing found
EXIT_FOUND = bagi_play.EXIT_FAILED  # the finding recurs (shrink: and the reproduction is written)
EXIT_USAGE = bagi_play.EXIT_USAGE
EXIT_SKIPPED = bagi_play.EXIT_SKIPPED
DEFAULT_MAX_PROBES = bagi_play.DEFAULT_MAX_PROBES

QUIT_METHOD = "quit"
NOTE_LIMIT = 300  # characters of a finding's message in the note of the written line
TEMP_PREFIX = "bagi-qa-replay-"


class ReproError(Exception):
    """A scenario, a game or an option that cannot be used, or a finding that does not come back; the text says what."""


@dataclass
class ReplayRun:
    """What one play of a scenario came to.

    `findings` carry the number of the scenario call (1-based) after which they showed in `call_index`; one that showed only after the game
    quit has the number of the last call played. `appeared` is the same as the 0-based index of the first finding of each name.
    """

    findings: list[Finding] = field(default_factory=list)
    appeared: dict[str, int] = field(default_factory=dict)
    played: int = 0
    session_errors: list[str] = field(default_factory=list)


@dataclass
class Shrunk:
    """The result of `shrink_finding`: `calls` are what is left of the scenario, `finding` is the one the last play of them showed.

    `probes` is the number of game launches the cut made; `limited` says it stopped at `max_probes`, so it may still shrink.
    """

    header: scenario.Header
    calls: list[scenario.Step]
    finding: Finding
    original_calls: int
    probes: int
    limited: bool


def load_scenario(path: str | Path) -> tuple[scenario.Header, list[scenario.Step]]:
    try:
        return scenario.load(Path(path))
    except scenario.ScenarioError as error:
        raise ReproError(str(error)) from error


def load_module(invariants: Invariants | str | Path | None) -> Invariants | None:
    if isinstance(invariants, (str, Path)):
        try:
            return checks.load_invariants(invariants)
        except checks.InvariantsError as error:
            raise ReproError(str(error)) from error
    return invariants


def replay_steps(exe: str | Path, header: scenario.Header, steps: Sequence[scenario.Step], invariants: Invariants | None, extra: Sequence[str] = ()) -> ReplayRun:
    """Plays `steps` on a fresh game with the checks after every call. ReproError when the game does not start or is not the one the header names.

    A game that is lost on the way ends the play: what was found so far is kept and the cause is in `session_errors`.
    """
    for flag in (HEADLESS_FLAG, MANUAL_CLOCK_FLAG):
        if flag not in header.flags:
            raise ReproError(f"the scenario's flags {header.flags} lack {flag}: a QA replay runs headless on the manual clock")
    game_flags = [flag for flag in (*header.flags, *extra) if flag not in (HEADLESS_FLAG, MANUAL_CLOCK_FLAG)]
    with tempfile.TemporaryDirectory(prefix=TEMP_PREFIX) as folder:
        session = QaSession(folder, "replay", invariants, ui_every=1)
        _launch(session, exe, game_flags)
        run = ReplayRun()
        try:
            if session.game != header.game:
                raise ReproError(f"the scenario is for the game '{header.game}', but '{exe}' started '{session.game}'")
            for index, step in enumerate(steps):
                before = len(session.findings)
                try:
                    session.replay_call(step.method, step.params)
                except SessionError:
                    break
                run.played += 1
                _keep(run, session.findings[before:], index)
                if step.method == QUIT_METHOD:
                    break
        finally:
            before = len(session.findings)
            session.finish()
        _keep(run, session.findings[before:], max(run.played - 1, 0))
        run.session_errors = list(session.session_errors)
    return run


def replay(exe: str | Path, scenario_path: str | Path, invariants: Invariants | str | Path | None = None, extra: Sequence[str] = ()) -> list[Finding]:
    """Plays the calls of a scenario on a fresh game with the checks after every call and gives the findings (all of them, known deviations too)."""
    header, steps = load_scenario(scenario_path)
    return replay_steps(exe, header, steps, load_module(invariants), extra).findings


def shrink_finding(
    exe: str | Path,
    scenario_path: str | Path,
    finding_name: str,
    invariants: Invariants | str | Path | None = None,
    max_probes: int = DEFAULT_MAX_PROBES,
    extra: Sequence[str] = (),
) -> Shrunk:
    """Cuts the scenario down to the calls that make a finding named `finding_name` show (`scenario_shrink.shrink`).

    The calls after the one the finding showed at are dropped first. A probe is a fresh game, the candidate calls and the same checks; it fails
    when a finding of that name appears. ReproError when the finding does not come back on the whole scenario, or on the last play of the result.
    """
    header, steps = load_scenario(scenario_path)
    module = load_module(invariants)
    full = replay_steps(exe, header, steps, module, extra)
    if finding_name not in full.appeared:
        raise ReproError(f"the finding '{finding_name}' does not come back when the whole scenario is played again{_found_text(full)}")
    upto = full.appeared[finding_name] + 1
    launches = 0
    cache: dict[tuple[int, ...], bool] = {tuple(range(len(steps))): True} if upto == len(steps) else {}

    def fails(candidate: list[int]) -> bool:
        nonlocal launches
        key = tuple(candidate)
        if key not in cache:
            launches += 1
            cache[key] = finding_name in replay_steps(exe, header, [steps[index] for index in candidate], module, extra).appeared
        return cache[key]

    try:
        kept = shrink(list(range(upto)), fails, max_probes)
    except ValueError as error:
        raise ReproError(f"the finding '{finding_name}' showed once but not when its calls were played again; it is not deterministic") from error
    calls = [steps[index] for index in kept]
    last = replay_steps(exe, header, calls, module, extra)
    if finding_name not in last.appeared:
        raise ReproError(f"the finding '{finding_name}' did not come back on the last play of the {len(calls)} calls left; it is not deterministic")
    finding = next(found for found in last.findings if found.name == finding_name)
    return Shrunk(header, calls, finding, len(steps), launches, launches >= max_probes)


def with_check_call(calls: Sequence[scenario.Step], finding: Finding) -> list[scenario.Step]:
    """`calls`, with a `ui_tree` call added at the end for a `ui_issue` finding: its expectation holds for the answer of that call, which the cut may have dropped."""
    result = list(calls)
    if finding.kind == checks.KIND_UI_ISSUE and (not result or result[-1].method != UI_TREE_METHOD):
        result.append(scenario.Step(UI_TREE_METHOD))
    return result


def settle(exe: str | Path, header: scenario.Header, calls: Sequence[scenario.Step], extra: Sequence[str] = ()) -> list[scenario.Step]:
    """`calls` with the expectations of a run of exactly these calls (an error code for a refusal, the version step otherwise).

    A cut moves the versions and the refusals of the calls after a dropped one, so the expectations of the recording would fail on a line
    that has nothing to do with the finding. ReproError when the game cannot play them.
    """
    result = bagi_play.play(Path(exe).resolve(), merged_header(header, extra), [dataclasses.replace(step, expect={}) for step in calls], update=True)
    if not result.passed:
        raise ReproError(f"the game could not play the calls again to settle their expectations: {result.error or result.failure}")
    return result.steps


def merged_header(header: scenario.Header, extra: Sequence[str]) -> scenario.Header:
    return dataclasses.replace(header, flags=[*header.flags, *(flag for flag in extra if flag not in header.flags)])


def write_repro(path: str | Path, header: scenario.Header, calls: Sequence[scenario.Step], finding: Finding) -> None:
    """Writes the scenario that `bagi_play.py` plays: the header and the calls as they are, the finding's `expect` on the last line when it has one.

    A `{}` expect (a plain line that fails by itself) replaces what the line had, so the last call fails because it is refused. A finding with
    no expectation (`expect` is None) leaves the lines as they are: the file does not fail, and `bagi_qa.py replay` is the way to reproduce it.
    A `ui_issue` finding gets the `ui_tree` call at the end (`with_check_call`). The note of the last line names the finding.
    """
    target = Path(path).expanduser()
    if png_ops.inside_git_work_tree(target.parent):
        raise ReproError(f"{target.parent} is inside a git work tree: a reproduction is written outside every repository")
    steps = with_check_call(calls, finding)
    if not steps:
        raise ReproError("a reproduction needs at least one call")
    last = steps[-1]
    text = f"QA finding {finding.name}: {finding.message}"
    note = text if len(text) <= NOTE_LIMIT else text[:NOTE_LIMIT] + "..."
    steps[-1] = dataclasses.replace(last, expect=dict(finding.expect) if finding.expect is not None else last.expect, note=note)
    titled = header if header.title is not None else dataclasses.replace(header, title=f"QA reproduction of {finding.name}")
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        scenario.dump(target, titled, steps)
    except OSError as error:
        raise ReproError(f"cannot write {target}: {error}") from error


def check_written(exe: str | Path, path: str | Path, finding: Finding) -> str | None:
    """Plays the written file with `bagi_play`: None when it fails on its last line as the finding's `expect` says (or the finding has none), else why not."""
    if finding.expect is None:
        return None
    header, steps = load_scenario(path)
    result = bagi_play.play(Path(exe).resolve(), header, steps)
    if result.passed:
        return "the written file passes under bagi_play.py, so it does not reproduce the finding"
    if result.failed_step != len(steps) - 1:
        return f"the written file fails under bagi_play.py at call {_position(result)} and not at its last call: {result.failure or result.error}"
    return None


def write_reproduction(
    exe: str | Path,
    scenario_path: str | Path,
    finding_name: str,
    invariants: Invariants | None,
    out: str | Path,
    max_probes: int = DEFAULT_MAX_PROBES,
    extra: Sequence[str] = (),
) -> tuple[Shrunk, list[scenario.Step]]:
    """Cuts the scenario to what the finding needs and writes it to `out` as a scenario that `bagi_play.py` plays; the cut and the calls written.

    The file is played with `bagi_play` before this returns and is removed when it does not fail on its last line. ReproError for any problem
    (nothing is left at `out`).
    """
    shrunk = shrink_finding(exe, scenario_path, finding_name, invariants, max_probes, extra)
    calls = settle(exe, shrunk.header, with_check_call(shrunk.calls, shrunk.finding), extra)
    write_repro(out, merged_header(shrunk.header, extra), calls, shrunk.finding)
    problem = check_written(exe, out, shrunk.finding)
    if problem is not None:
        Path(out).unlink(missing_ok=True)
        raise ReproError(problem)
    return shrunk, calls


def command_replay(args: argparse.Namespace) -> int:
    """`bagi_qa.py replay`: plays the scenario with the checks and says what it found. Exit 0 nothing (known deviations apart), 1 findings, 2 a bad file, 77 no game."""
    try:
        exe = executable(args)
        if exe is None:
            return EXIT_SKIPPED
        module = load_module(args.invariants)
        header, steps = load_scenario(args.scenario)
        run = replay_steps(exe, header, steps, module, args.flag)
    except ReproError as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    bugs, known = checks.apply_known(run.findings, module.known_deviations if module is not None else ())
    lines = bagi_play.step_lines(Path(args.scenario).read_text(encoding="utf-8"))
    for finding in bugs:
        print(f"FOUND {finding.name}  {_where(args.scenario, steps, lines, finding)}\n  {finding.message}")
    for finding in known:
        print(f"KNOWN {finding.name}  {_where(args.scenario, steps, lines, finding)}\n  {finding.message}")
    for error in run.session_errors:
        print(f"SESSION {error}")
    print(f"REPLAYED {args.scenario}: {run.played} of {len(steps)} calls, {len(bugs)} finding(s), {len(known)} known")
    return EXIT_FOUND if bugs else EXIT_NOTHING


def command_shrink(args: argparse.Namespace) -> int:
    """`bagi_qa.py shrink`: cuts the scenario down to what a finding needs and writes it to `--out`.

    Exit 0 nothing found (nothing written), 1 the finding recurs and the reproduction is written, 2 a bad file or option, or the named finding
    does not recur on the whole scenario (nothing written), 77 no game.
    """
    try:
        exe = executable(args)
        if exe is None:
            return EXIT_SKIPPED
        module = load_module(args.invariants)
        header, steps = load_scenario(args.scenario)
        full = replay_steps(exe, header, steps, module, args.flag)
        bugs = checks.apply_known(full.findings, module.known_deviations if module is not None else ())[0]
        if args.finding is None and not bugs:
            print(f"nothing found on {args.scenario}: {full.played} of {len(steps)} calls played, nothing to shrink")
            return EXIT_NOTHING
        name = args.finding or bugs[0].name
        if args.finding is None and len({found.name for found in bugs}) > 1:
            print(f"several findings recur ({', '.join(sorted({found.name for found in bugs}))}); shrinking the first, '{name}' (--finding names another)")
        out = Path(args.out) if args.out else report.repro_scenario_path(Path(args.scenario).resolve().parent, name)
        if png_ops.inside_git_work_tree(out.parent.resolve()):
            raise ReproError(f"--out {out} is inside a git work tree: a reproduction is written outside every repository")
        print(f"shrinking '{name}': {len(steps)} calls, up to {args.max_probes} probes", flush=True)
        shrunk, calls = write_reproduction(exe, args.scenario, name, module, out, args.max_probes, args.flag)
    except ReproError as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    how = f"fails under bagi_play.py ({_expectation_text(shrunk.finding.expect)})" if shrunk.finding.expect is not None else "does not fail under bagi_play.py; reproduce with bagi_qa.py replay"
    limit = f"; stopped at the limit of {args.max_probes} probes, so it may still shrink" if shrunk.limited else ""
    print(f"SHRUNK {out}: {shrunk.original_calls} calls -> {len(calls)} calls, {shrunk.probes} probes{limit}\n  {shrunk.finding.name}: {shrunk.finding.message}\n  the file {how}")
    return EXIT_FOUND


def add_arguments(parser: argparse.ArgumentParser, shrinking: bool) -> None:
    """The arguments of `replay` and `shrink` (`bagi_qa.py` builds the parsers)."""
    parser.add_argument("scenario", help="the recorded scenario (the `session.jsonl` of a QA session)")
    add_game_arguments(parser)
    if shrinking:
        parser.add_argument("--finding", help="the name of the finding to shrink (default: the first one the whole scenario shows)")
        parser.add_argument("--out", metavar="FILE", help="where the reproduction goes (default: repro/<finding name>.jsonl next to the scenario, which `bagi_qa.py drafts` picks up)")
        parser.add_argument("--max-probes", type=int, default=DEFAULT_MAX_PROBES, help="game launches the cut may spend (default %(default)s)")


def add_game_arguments(parser: argparse.ArgumentParser) -> None:
    """The options that name the game and its checks, as `executable` reads them (also for the monkey, bagi_monkey)."""
    parser.add_argument("--exe", help="the built game executable (or --preset and --game)")
    parser.add_argument("--preset", help="the CMake preset that built the game: the executable is build/<preset>/bin/<game>")
    parser.add_argument("--game", help="the executable name for --preset, e.g. BagiMatch3")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    parser.add_argument("--invariants", help="the game's invariants module (.claude/rules/qa-project.md)")
    parser.add_argument("--flag", action="append", default=[], metavar="FLAG", help="a start flag for the game on top of the scenario's; repeatable")


def executable(args: argparse.Namespace) -> Path | None:
    """The game executable the arguments name, or None (and a line printed) when it does not exist. ReproError for a bad combination of options."""
    by_preset = args.preset is not None or args.game is not None
    if by_preset == (args.exe is not None) or (by_preset and (args.preset is None or args.game is None)):
        raise ReproError("name the game either by --exe, or by --preset and --game together")
    if getattr(args, "max_probes", 1) < 1:
        raise ReproError("--max-probes has to be at least 1")
    exe = bagi_play.preset_executable(args.preset, args.game, args.root) if by_preset else Path(args.exe)
    if not exe.is_file():
        print(f"SKIPPED: {exe} does not exist")
        return None
    return exe.resolve()


def _launch(session: QaSession, exe: str | Path, game_flags: Sequence[str]) -> None:
    """Starts the game; when it reports that a port `free_port()` chose was taken meanwhile, once more on another port."""
    for attempt in range(2):
        try:
            session.launch(exe=str(exe), extra=game_flags, replay=True)
            return
        except SessionError as error:
            if attempt == 0 and PORT_TAKEN_MARKER in str(error):
                continue
            raise ReproError(f"the game did not start: {error}") from error


def _keep(run: ReplayRun, found: Sequence[Finding], index: int) -> None:
    for finding in found:
        run.findings.append(finding.at(index + 1))
        run.appeared.setdefault(finding.name, index)


def _found_text(run: ReplayRun) -> str:
    return f" (it found: {', '.join(sorted(run.appeared))})" if run.appeared else " (it found nothing)"


def _where(path: Any, steps: Sequence[scenario.Step], lines: Sequence[int], finding: Finding) -> str:
    index = finding.call_index - 1
    if not 0 <= index < len(steps):
        return f"{path}"
    return f"{path}:{lines[index]}  after {bagi_play.describe_call(steps[index])}"


def _position(result: bagi_play.PlayResult) -> int | str:
    return "?" if result.failed_step is None else result.failed_step + 1


def _expectation_text(expect: dict[str, Any]) -> str:
    return ", ".join(expect) if expect else "the last call is refused"
