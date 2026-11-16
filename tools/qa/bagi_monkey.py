"""The monkey: a seeded random walk over a game's control API, every move checked, stopped at the first bug and cut down to a reproduction.

    result = run_monkey(exe, seed=1, steps=200, out="~/.claude/plans/qa/2026-11-16-monkey-s1")
    # result.stopped is "steps", "minutes", "bug" or "lost"; result.repros and result.drafts are what a bug left in `result.out`

    python3 tools/qa/bagi_monkey.py (--exe E | --preset P --game G) --seed S [--steps N] [--minutes M] [--invariants MODULE]
        [--start JSON] [--kinds a,b] [--charter TEXT] [--out DIR] [--max-probes N] [--no-shrink] [--flag F]
Exit code: 0 the budget ended with no bug (known deviations do not count), 1 a bug (the output names the folder, the findings and the files), 2 a bad option
or file, 77 the executable is missing.

It is a `QaSession`, so the generic checks, the recording and the report are the session's (`tools/qa/CLAUDE.md`, "The monkey" says what a move is and what
makes the walk repeatable). Standard library only (through tools/control).
"""

import argparse
import datetime
import json
import random
import sys
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Collection, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR))

import bagi_monkey_moves as moves_module  # noqa: E402
import bagi_play  # noqa: E402
import bagi_qa_checks as checks  # noqa: E402
import bagi_qa_repro as repro  # noqa: E402
import bagi_qa_report as report_module  # noqa: E402
from bagi_qa import UI_TREE_METHOD, QaSession  # noqa: E402
from bagi_qa_checks import Finding, Move  # noqa: E402
from bagi_session import SessionError  # noqa: E402

EXIT_NOTHING = bagi_play.EXIT_PASSED
EXIT_BUG = bagi_play.EXIT_FAILED
EXIT_USAGE = bagi_play.EXIT_USAGE
EXIT_SKIPPED = bagi_play.EXIT_SKIPPED

STOP_STEPS = "steps"
STOP_MINUTES = "minutes"
STOP_BUG = "bug"
STOP_LOST = "lost"

NAME_GAME_LOST = "monkey.game_lost"
DEFAULT_STEPS = 200
DEFAULT_CHARTER = "a monkey walk"
UI_EVERY = 5  # `ui_tree` is checked every this many `step` moves
SHRUNK_NAMES = 3  # distinct bug names that get a reproduction in one run
FOLLOW_FRAMES = 1  # frames after a click or a window change: nothing shows until a frame (.claude/rules/ui-control-api.md)
SECONDS_PER_MINUTE = 60
OUT_ROOT = Path("~/.claude/plans/qa")  # outside every repository: QaSession refuses an out folder inside one
LIST_METHODS = "list_methods"
FOLLOWED_KINDS = (moves_module.KIND_CLICK, moves_module.KIND_WINDOW)
FALLBACK_MOVE = Move(moves_module.KIND_STEP, moves_module.STEP_METHOD, {"frames": 1}, "step 1")  # when the game's filter leaves nothing to pick


class MonkeyError(Exception):
    """A start the game refused or an option that cannot be used; the text says what."""


@dataclass
class MonkeyResult:
    """What a walk came to. `moves` are the moves made, `calls` the control calls of the recording, `per_kind` the moves by kind.

    `repros` are the reproductions written, `unshrunk` the bug names that got none (the text says why: it did not recur, or it is the runner's own)
    and keep the recording in their draft.
    """

    seed: int
    moves: int
    calls: int
    stopped: str
    bugs: list[Finding]
    known: list[Finding]
    out: Path
    per_kind: dict[str, int] = field(default_factory=dict)
    repros: list[Path] = field(default_factory=list)
    unshrunk: dict[str, str] = field(default_factory=dict)
    drafts: list[Path] = field(default_factory=list)


def run_monkey(
    exe: str | Path,
    seed: int,
    out: str | Path,
    steps: int = DEFAULT_STEPS,
    minutes: float | None = None,
    invariants: str | Path | None = None,
    start: dict[str, Any] | None = None,
    kinds: Collection[str] | None = None,
    charter: str | None = None,
    max_probes: int = repro.DEFAULT_MAX_PROBES,
    shrink: bool = True,
    flags: Sequence[str] = (),
    clock: Callable[[], float] = time.monotonic,
) -> MonkeyResult:
    """Walks `steps` moves (or `minutes`) from `seed`, stops at the first bug, then writes the reproductions and the drafts into `out`.

    SessionError when the game does not start, MonkeyError when it refuses the `start` parameters, ReproError never (a bug that does not come
    back is in `unshrunk`). A game lost on the way is the finding `monkey.game_lost`. `clock` is injected for the tests.
    """
    text = charter or DEFAULT_CHARTER
    session = QaSession(out, text, invariants, ui_every=UI_EVERY)
    session.launch(exe=str(exe), extra=list(flags))
    rng = random.Random(seed)
    done = Counter[str]()
    stopped = STOP_STEPS
    refused_start: str | None = None
    try:
        methods = _listed_methods(session)
        if start:
            outcome = session.call(moves_module.RESET_METHOD, start)
            if not outcome.accepted:
                refused_start = f"the game refused the start parameters {json.dumps(start, sort_keys=True)}: {outcome.error_code} {outcome.error_message}"
        if refused_start is None:
            stopped = _walk(session, rng, done, methods, start, kinds, steps, minutes, clock)
    finally:
        session.note(f"monkey seed {seed}, {sum(done.values())} moves")
        session.note(f"charter: {text}")
        session.note(f"start parameters: {json.dumps(start, sort_keys=True) if start else 'none'}")
        session.note("moves by kind: " + (", ".join(f"{kind} {count}" for kind, count in sorted(done.items())) or "none"))
        session.finish()
    if refused_start is not None:
        raise MonkeyError(refused_start)
    result = MonkeyResult(seed, sum(done.values()), session.calls, stopped, session.bugs, session.known, session.out_dir, dict(done))
    _reproduce(session, exe, result, max_probes, shrink)
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    repro.add_game_arguments(parser)
    parser.add_argument("--seed", type=int, required=True, help="the seed of the walk: the same seed on the same game walks the same way")
    parser.add_argument("--steps", type=int, default=DEFAULT_STEPS, help="moves to make (default %(default)s)")
    parser.add_argument("--minutes", type=float, help="stop after this many minutes, checked between moves")
    parser.add_argument("--start", type=_json_object, metavar="JSON", help="the parameters of `reset` the walk starts with (and its `reset` moves use)")
    parser.add_argument("--kinds", type=_kinds, metavar="a,b", help=f"only these kinds of moves ({', '.join(moves_module.KINDS)}; default all)")
    parser.add_argument("--charter", help=f"the text the report names as the charter (default: {DEFAULT_CHARTER})")
    parser.add_argument("--out", type=Path, help="the session folder, outside every repository (default: ~/.claude/plans/qa/<date>-monkey-s<seed>)")
    parser.add_argument("--max-probes", type=int, default=repro.DEFAULT_MAX_PROBES, help="game launches a reproduction may spend (default %(default)s)")
    parser.add_argument("--no-shrink", action="store_true", help="write the drafts with the recording, no reproduction")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        exe = repro.executable(args)
        if exe is None:
            return EXIT_SKIPPED
        if args.steps < 1 or (args.minutes is not None and args.minutes <= 0):
            raise MonkeyError("--steps and --minutes have to be positive")
        out = args.out or OUT_ROOT / f"{datetime.date.today().isoformat()}-monkey-s{args.seed}"
        result = run_monkey(
            exe, args.seed, out.expanduser(), args.steps, args.minutes, args.invariants, args.start, args.kinds, args.charter, args.max_probes, not args.no_shrink, args.flag
        )
    except (repro.ReproError, MonkeyError, SessionError, checks.InvariantsError) as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    _print(result)
    return EXIT_BUG if result.bugs else EXIT_NOTHING


def _walk(
    session: QaSession,
    rng: random.Random,
    done: Counter[str],
    methods: Collection[str],
    start: dict[str, Any] | None,
    kinds: Collection[str] | None,
    steps: int,
    minutes: float | None,
    clock: Callable[[], float],
) -> str:
    """The moves, one after another; why the walk stopped. A game that is gone is flagged as `monkey.game_lost`."""
    began = clock()
    tree: Any = None
    settled = True  # a frame has run since the last change, so the layout of `ui_tree` is current
    last = "(none)"
    while True:
        if sum(done.values()) >= steps:
            return STOP_STEPS
        if minutes is not None and clock() - began >= minutes * SECONDS_PER_MINUTE:
            return STOP_MINUTES
        bugs = len(session.bugs)
        try:
            if settled and _wants_tree(kinds, methods):
                tree = _tree(session)
            moves, found = checks.run_monkey_moves(
                session.invariants, session.current_state(), moves_module.candidate_moves(session.listed_actions(), tree, methods, start, kinds)
            )
            session.add_findings(found)
            move = moves_module.pick(rng, moves) if moves else FALLBACK_MOVE
            last = move.label or move.method
            done[move.kind] += 1
            settled = _execute(session, move)
        except SessionError as error:
            session.flag(NAME_GAME_LOST, f"the game is gone after the move '{last}': {error}", {"move": last, "error": str(error)})
            return STOP_LOST
        if len(session.bugs) > bugs:
            return STOP_BUG


def _execute(session: QaSession, move: Move) -> bool:
    """Makes the move; whether a frame has run after it (the layout is current)."""
    if move.kind == moves_module.KIND_ACTION:
        session.act(move.params)
        return False
    if move.kind == moves_module.KIND_STEP:
        session.step(move.params.get("frames", 1))
        return True
    session.call(move.method, move.params)
    if move.kind in FOLLOWED_KINDS:
        session.step(FOLLOW_FRAMES)
        return True
    return False


def _wants_tree(kinds: Collection[str] | None, methods: Collection[str]) -> bool:
    return (kinds is None or moves_module.KIND_CLICK in kinds) and moves_module.UI_CLICK_METHOD in methods


def _tree(session: QaSession) -> Any:
    outcome = session.call(UI_TREE_METHOD)
    return outcome.result if outcome.accepted else None


def _listed_methods(session: QaSession) -> frozenset[str]:
    outcome = session.call(LIST_METHODS)
    entries = outcome.result.get("methods") if outcome.accepted and isinstance(outcome.result, dict) else None
    return frozenset(entry["name"] for entry in entries if isinstance(entry, dict) and isinstance(entry.get("name"), str)) if isinstance(entries, list) else frozenset()


def _reproduce(session: QaSession, exe: str | Path, result: MonkeyResult, max_probes: int, shrink: bool) -> None:
    """A reproduction for each of the first distinct bug names (the flagged ones have no replay), then the drafts again so they name the files."""
    out = session.out_dir
    if shrink:
        for group in report_module.groups(result.bugs)[:SHRUNK_NAMES]:
            name = group[0].name
            if group[0].details.get(checks.FLAGGED_KEY):
                result.unshrunk[name] = "raised by the walk itself: no replay shows it, the draft keeps the recording"
                continue
            path = report_module.repro_scenario_path(out, name)
            try:
                repro.write_reproduction(exe, session.scenario_path, name, session.invariants, path, max_probes)
            except repro.ReproError as error:
                result.unshrunk[name] = f"did not recur: {error}"
                continue
            result.repros.append(path)
    elif result.bugs:
        for name in dict.fromkeys(found.name for found in result.bugs):
            result.unshrunk[name] = "--no-shrink: the draft keeps the recording"
    drafts = report_module.make_drafts(report_module.load_report(out), out)
    result.drafts = report_module.write_drafts(drafts, out)


def _print(result: MonkeyResult) -> None:
    print(f"MONKEY seed {result.seed}: {result.moves} moves, {result.calls} calls, stopped on {result.stopped}, {len(result.bugs)} bug(s), {len(result.known)} known")
    for finding in result.bugs:
        print(f"FOUND {finding.name}  after call {finding.call_index}\n  {finding.message}")
    for finding in result.known:
        print(f"KNOWN {finding.name}\n  {finding.message}")
    for path in result.repros:
        print(f"REPRO {path}")
    for name, why in result.unshrunk.items():
        print(f"NO REPRO {name}: {why}")
    for path in result.drafts:
        print(f"DRAFT {path}")
    print(f"OUT {result.out}")


def _json_object(text: str) -> dict[str, Any]:
    try:
        value = json.loads(text)
    except json.JSONDecodeError as error:
        raise argparse.ArgumentTypeError(f"not JSON: {error}") from error
    if not isinstance(value, dict):
        raise argparse.ArgumentTypeError("has to be a JSON object")
    return value


def _kinds(text: str) -> list[str]:
    names = [name for name in text.split(",") if name]
    unknown = [name for name in names if name not in moves_module.KINDS]
    if unknown or not names:
        raise argparse.ArgumentTypeError(f"unknown kind {', '.join(unknown) or text!r}; the kinds are {', '.join(moves_module.KINDS)}")
    return names


if __name__ == "__main__":
    sys.exit(main())
