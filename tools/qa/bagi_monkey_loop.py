"""The command line of the QA loop (`.claude/skills/qa-loop/SKILL.md`): the seed log, the duplicate check and a sweep of the monkey over a range of seeds.

    python3 tools/qa/bagi_monkey_loop.py next-seeds --log FILE --charter TEXT [--charter TEXT ...] --count N
    python3 tools/qa/bagi_monkey_loop.py log-add --log FILE --product P (--sweep FILE | --charter TEXT --seeds A..B --moves N) [--build TEXT] [--result TEXT] [--date D]
    python3 tools/qa/bagi_monkey_loop.py dupes --repro FILE [--known [FILE ...]]
    python3 tools/qa/bagi_monkey_loop.py sweep (--exe E | --preset P --game G) --charter TEXT --first S --count N [--steps N] [--minutes M]
        [--minutes-per-seed M] [--invariants MODULE] [--start JSON] [--kinds a,b] [--out-root DIR] [--stop-after-bugs K] [--max-probes N] [--no-shrink] [--flag F]

`next-seeds` prints `{"charter", "first", "last"}`; `log-add` appends a row to the log file; `dupes` prints `DUPLICATE <known>: <finding>` for each known file the
repro is similar to (`bagi_monkey_dupes`) and `SKIPPED <file>: <why>` for one it cannot read; `sweep` walks the seeds `S..S+N-1` with `run_monkey`, one folder
`s<seed>` each, and writes and prints `<out-root>/sweep.json`.

Exit codes: 0 nothing found, 1 a bug (`dupes`: a match), 2 a bad option or file or a start the game refused, 77 the executable is missing (`sweep`).
It reads and writes the files it is given and never the todo store: the skill moves the log and the plans to and from the store. Standard library only.
"""

import argparse
import datetime
import json
import os
import re
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR.parent / "shots"))
sys.path.insert(0, str(QA_DIR))

import bagi_monkey as monkey  # noqa: E402
import bagi_monkey_dupes as dupes_module  # noqa: E402
import bagi_monkey_log as log_module  # noqa: E402
import bagi_qa_checks as checks  # noqa: E402
import bagi_qa_repro as repro  # noqa: E402
import png_ops  # noqa: E402
from bagi_session import SessionError  # noqa: E402

EXIT_NOTHING = monkey.EXIT_NOTHING
EXIT_BUG = monkey.EXIT_BUG
EXIT_USAGE = monkey.EXIT_USAGE
EXIT_SKIPPED = monkey.EXIT_SKIPPED

STOP_COUNT = "count"
STOP_MINUTES = monkey.STOP_MINUTES
STOP_BUGS = "bugs"
STOP_ERROR = "error"

SWEEP_FILE = "sweep.json"
RESULT_CLEAN = "clean"
DEFAULT_STOP_AFTER_BUGS = 3
SLUG_LENGTH = 40
SEED_FOLDER = "s{}"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    seeds = commands.add_parser("next-seeds", help="the charter and the seeds to walk next")
    seeds.add_argument("--log", type=Path, required=True, help="the log file (a missing one is an empty log)")
    seeds.add_argument("--charter", action="append", required=True, metavar="TEXT", help="a charter to choose from; repeatable, the order is the rotation's tie-break")
    seeds.add_argument("--count", type=int, required=True, help="how many seeds")

    add = commands.add_parser("log-add", help="append a row to the log file")
    add.add_argument("--log", type=Path, required=True, help="the log file (a missing one is created)")
    add.add_argument("--product", required=True, help="the product named in the title of a new log")
    add.add_argument("--sweep", type=Path, help="the sweep.json that gives the charter, the seeds, the moves and the result")
    add.add_argument("--charter", help="the charter of the row (without --sweep)")
    add.add_argument("--seeds", help="the seeds of the row, A..B (without --sweep)")
    add.add_argument("--moves", type=int, help="the moves made (without --sweep)")
    add.add_argument("--build", default="", help="the commit the sweep ran on")
    add.add_argument("--result", help=f"the result cell (default: {RESULT_CLEAN}, or the names of the bugs a sweep found)")
    add.add_argument("--date", help="the date of the row (default: today)")

    dupes = commands.add_parser("dupes", help="is this reproduction one of the known ones")
    dupes.add_argument("--repro", type=Path, required=True, help="the reproduction: a .jsonl scenario or a bug-<short>-repro.md plan")
    dupes.add_argument("--known", type=Path, nargs="*", default=[], metavar="FILE", help="the reproductions already filed (none: no match)")

    sweep = commands.add_parser("sweep", help="walk the monkey over a range of seeds")
    repro.add_game_arguments(sweep)
    sweep.add_argument("--charter", required=True, help="the charter text of the walks")
    sweep.add_argument("--first", type=int, required=True, help="the first seed")
    sweep.add_argument("--count", type=int, required=True, help="how many seeds, in order from --first")
    sweep.add_argument("--steps", type=int, default=monkey.DEFAULT_STEPS, help="moves per seed (default %(default)s)")
    sweep.add_argument("--minutes", type=float, help="the total budget, checked between seeds (and the limit of the seed that runs)")
    sweep.add_argument("--minutes-per-seed", type=float, help="the budget of one seed")
    sweep.add_argument("--start", type=monkey.json_object, metavar="JSON", help="the parameters of `reset` every walk starts with")
    sweep.add_argument("--kinds", type=monkey.kinds_option, metavar="a,b", help="only these kinds of moves (default all)")
    sweep.add_argument("--out-root", type=Path, help="the folder of the sweep, outside every repository (default: ~/.claude/plans/qa/<date>-loop-<charter>)")
    sweep.add_argument("--stop-after-bugs", type=int, default=DEFAULT_STOP_AFTER_BUGS, metavar="K", help="stop after this many distinct finding names (default %(default)s)")
    sweep.add_argument("--max-probes", type=int, default=repro.DEFAULT_MAX_PROBES, help="game launches a reproduction may spend (default %(default)s)")
    sweep.add_argument("--no-shrink", action="store_true", help="write the drafts with the recording, no reproduction")
    return parser


def main(argv: Sequence[str] | None = None, clock: Callable[[], float] = time.monotonic) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "next-seeds":
            return _next_seeds(args)
        if args.command == "log-add":
            return _log_add(args)
        if args.command == "dupes":
            return _dupes(args)
        return _sweep(args, clock)
    except (log_module.LogError, repro.ReproError, checks.InvariantsError, ValueError, OSError) as error:
        print(f"ERROR {error}")
        return EXIT_USAGE


def run_sweep(
    exe: str | Path,
    charter: str,
    first: int,
    count: int,
    out_root: Path,
    steps: int = monkey.DEFAULT_STEPS,
    minutes: float | None = None,
    minutes_per_seed: float | None = None,
    stop_after_bugs: int = DEFAULT_STOP_AFTER_BUGS,
    clock: Callable[[], float] = time.monotonic,
    **walk: Any,
) -> dict[str, Any]:
    """Walks the seeds `first..first+count-1` in order and gives the summary that `sweep.json` holds; the budget `minutes` is checked before each seed.

    `walk` is what `run_monkey` takes besides the seed, the folder, the steps and the minutes. A seed whose game does not start ends the sweep with
    `stopped` "error" and the text in its record; the seeds before it stay.
    """
    began = clock()
    records: list[dict[str, Any]] = []
    names: dict[str, None] = {}
    stopped = STOP_COUNT
    for seed in range(first, first + count):
        left = None if minutes is None else minutes - (clock() - began) / monkey.SECONDS_PER_MINUTE
        if left is not None and left <= 0:
            stopped = STOP_MINUTES
            break
        limits = [limit for limit in (minutes_per_seed, left) if limit is not None]
        out = out_root / SEED_FOLDER.format(seed)
        try:
            result = monkey.run_monkey(exe, seed, out, steps, min(limits) if limits else None, charter=charter, **walk)
        except (SessionError, monkey.MonkeyError, repro.ReproError, checks.InvariantsError) as error:
            records.append({"seed": seed, "moves": 0, "stopped": STOP_ERROR, "error": str(error), "bugs": [], "known": [], "out": str(out), "repros": [], "drafts": []})
            stopped = STOP_ERROR
            break
        bugs = list(dict.fromkeys(found.name for found in result.bugs))
        names.update(dict.fromkeys(bugs))
        records.append(
            {
                "seed": seed,
                "moves": result.moves,
                "stopped": result.stopped,
                "bugs": bugs,
                "known": list(dict.fromkeys(found.name for found in result.known)),
                "out": str(result.out),
                "repros": [str(path) for path in result.repros],
                "drafts": [str(path) for path in result.drafts],
                "unshrunk": result.unshrunk,
            }
        )
        if len(names) >= stop_after_bugs:
            stopped = STOP_BUGS
            break
    walked = [record for record in records if record["stopped"] != STOP_ERROR]
    return {
        "charter": charter,
        "first": first,
        "last": first + len(walked) - 1,
        "moves": sum(record["moves"] for record in walked),
        "stopped": stopped,
        "seeds": records,
    }


def sweep_result(summary: dict[str, Any]) -> str:
    """The `result` cell a sweep gets in the log: `clean`, `bugs: <names>` or `error: <text>` (then ` (bugs: <names>)` when earlier seeds found some)."""
    errors = [record["error"] for record in summary["seeds"] if record["stopped"] == STOP_ERROR]
    names = list(dict.fromkeys(name for record in summary["seeds"] for name in record["bugs"]))
    if errors:
        return f"error: {errors[0]} (bugs: {', '.join(names)})" if names else f"error: {errors[0]}"
    return f"bugs: {', '.join(names)}" if names else RESULT_CLEAN


def _next_seeds(args: argparse.Namespace) -> int:
    log = _read_log(args.log)
    charter = log_module.next_charter(log, args.charter)
    first, last = log_module.next_seeds(log, charter, args.count)
    print(json.dumps({"charter": charter, "first": first, "last": last}))
    return EXIT_NOTHING


def _log_add(args: argparse.Namespace) -> int:
    if args.sweep is not None:
        if args.charter is not None or args.seeds is not None or args.moves is not None:
            raise ValueError("--sweep gives the charter, the seeds and the moves: do not name them as well")
        summary = json.loads(args.sweep.read_text(encoding="utf-8"))
        if summary["last"] < summary["first"]:
            raise ValueError(f"the sweep walked no seed ({summary['stopped']}): nothing to log")
        charter, first, last, moves = summary["charter"], summary["first"], summary["last"], summary["moves"]
        result = args.result if args.result is not None else sweep_result(summary)
    else:
        if args.charter is None or args.seeds is None or args.moves is None:
            raise ValueError("name a --sweep file, or --charter, --seeds and --moves")
        found = log_module.SEED_RANGE.match(args.seeds)
        if found is None or int(found.group(2)) < int(found.group(1)):
            raise ValueError(f"--seeds must look like 1..5, not {args.seeds!r}")
        charter, first, last, moves = args.charter, int(found.group(1)), int(found.group(2)), args.moves
        result = args.result if args.result is not None else RESULT_CLEAN
    log = _read_log(args.log)
    if not log.product:
        log = log._replace(product=args.product)
    date = args.date or datetime.date.today().isoformat()
    updated = log_module.add_run(log, log_module.Run(date, charter, first, last, moves, args.build, result))
    _write_whole(args.log, log_module.render(updated))
    print(f"LOGGED {args.log}: {date} {charter} {first}..{last} {moves} moves, {result}")
    return EXIT_NOTHING


def _dupes(args: argparse.Namespace) -> int:
    fresh = dupes_module.signature_of_file(args.repro)
    matched = False
    for path in args.known:
        try:
            known = dupes_module.signature_of_file(path)
        except ValueError as error:
            print(f"SKIPPED {path}: {error}")
            continue
        if dupes_module.similar(fresh, known):
            matched = True
            print(f"DUPLICATE {path}: {known.finding}")
    return EXIT_BUG if matched else EXIT_NOTHING


def _sweep(args: argparse.Namespace, clock: Callable[[], float]) -> int:
    exe = repro.executable(args)
    if exe is None:
        return EXIT_SKIPPED
    if args.count < 1 or args.steps < 1 or args.stop_after_bugs < 1:
        raise ValueError("--count, --steps and --stop-after-bugs have to be at least 1")
    for option, value in (("--minutes", args.minutes), ("--minutes-per-seed", args.minutes_per_seed)):
        if value is not None and value <= 0:
            raise ValueError(f"{option} has to be positive")
    out_root = (args.out_root or monkey.OUT_ROOT / f"{datetime.date.today().isoformat()}-loop-{_slug(args.charter)}").expanduser()
    if png_ops.inside_git_work_tree(out_root):
        raise ValueError(f"--out-root {out_root} is inside a git work tree: a sweep is written outside every repository")
    summary = run_sweep(
        exe,
        args.charter,
        args.first,
        args.count,
        out_root,
        args.steps,
        args.minutes,
        args.minutes_per_seed,
        args.stop_after_bugs,
        clock,
        invariants=args.invariants,
        start=args.start,
        kinds=args.kinds,
        max_probes=args.max_probes,
        shrink=not args.no_shrink,
        flags=args.flag,
    )
    text = json.dumps(summary, indent=2, ensure_ascii=False) + "\n"
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / SWEEP_FILE).write_text(text, encoding="utf-8")
    print(text, end="")
    if summary["stopped"] == STOP_ERROR:
        return EXIT_USAGE
    return EXIT_BUG if any(record["bugs"] for record in summary["seeds"]) else EXIT_NOTHING


def _read_log(path: Path) -> log_module.Log:
    return log_module.parse(path.read_text(encoding="utf-8")) if path.is_file() else log_module.Log("", "", ())


def _write_whole(path: Path, text: str) -> None:
    """Writes `text` through a temp file next to `path` and a rename, so a reader never sees half a log."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.", suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
        os.replace(temp, path)
    except BaseException:
        Path(temp).unlink(missing_ok=True)
        raise


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:SLUG_LENGTH].strip("-") or "charter"


if __name__ == "__main__":
    sys.exit(main())
