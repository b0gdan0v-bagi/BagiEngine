"""The seed log of the QA loop: which seeds of which charter were walked, as a markdown text the todo store keeps (`qa-loop-log-<product>.md`).

    # QA loop log: BagiStress

    Free text, kept as it is.

    | date | charter | seeds | moves | build | result |
    | --- | --- | --- | --- | --- | --- |
    | 2026-11-16 | Every scene and every language switch | 1..5 | 812 | 1a2b3c4 | clean |

    log = parse(text)                                   # an empty text is an empty log
    first, last = next_seeds(log, charter, count=5)     # the seeds after the highest one this charter has covered
    text = render(add_run(log, Run(...)))

Pure functions: no file and no store is touched here (`bagi_monkey_loop.py` reads and writes the file). A cell escapes `\\` and `|` with a backslash and
a newline becomes a space, so what `render` writes `parse` reads back. Standard library only.
"""

import re
from typing import NamedTuple, Sequence

TITLE = "# QA loop log:"
COLUMNS = ("date", "charter", "seeds", "moves", "build", "result")
SEED_RANGE = re.compile(r"^(\d+)\.\.(\d+)$")
FIRST_SEED = 1
_TITLE_LINE = re.compile(r"^" + re.escape(TITLE) + r"\s*(.*?)\s*$")
_SEPARATOR_CELL = re.compile(r"^:?-+:?$")


class LogError(ValueError):
    """A log text that cannot be read: the message names the line, so the row is never dropped silently."""


class Run(NamedTuple):
    """One row: a sweep of the seeds `first..last` (both walked) of `charter`, `moves` made in all, on the commit `build`, ending in `result`."""

    date: str
    charter: str
    first: int
    last: int
    moves: int
    build: str
    result: str


class Log(NamedTuple):
    """`product` is the title's name, `preamble` the free text between the title and the table (without the blank lines around it)."""

    product: str
    preamble: str
    runs: tuple[Run, ...]


def parse(text: str) -> Log:
    """The log of `text`; an empty text, or one with no table, is an empty log (the text before a table is the preamble). LogError for a row it cannot read."""
    lines = text.splitlines()
    product = ""
    start = 0
    while start < len(lines) and not lines[start].strip():
        start += 1
    if start < len(lines):
        found = _TITLE_LINE.match(lines[start])
        if found:
            product = found.group(1)
            start += 1
    header = _find_header(lines, start)
    if header is None:
        return Log(product, "\n".join(lines[start:]).strip("\n"), ())
    preamble = "\n".join(lines[start:header]).strip("\n")
    if header + 1 >= len(lines) or not _is_separator(lines[header + 1]):
        raise LogError(f"line {header + 2}: the table header is not followed by its separator row")
    runs = []
    for index in range(header + 2, len(lines)):
        line = lines[index]
        if not line.strip():
            continue
        runs.append(_parse_row(line, index + 1))
    return Log(product, preamble, tuple(runs))


def render(log: Log) -> str:
    """The text of `log`: the title, the preamble, the table; `parse(render(log))` gives `log` back when its texts are trimmed and have no newline (`add_run` makes them so)."""
    parts = [f"{TITLE} {log.product}".rstrip()]
    if log.preamble:
        parts.append(log.preamble)
    table = [_row(COLUMNS), _row(["---"] * len(COLUMNS))]
    for run in log.runs:
        table.append(_row([run.date, run.charter, f"{run.first}..{run.last}", str(run.moves), run.build, run.result]))
    parts.append("\n".join(table))
    return "\n\n".join(parts) + "\n"


def add_run(log: Log, run: Run) -> Log:
    """`log` with `run` appended; the texts of `run` are trimmed and a newline in one becomes a space, as `render` writes them."""
    clean = run._replace(date=_one_line(run.date), charter=_one_line(run.charter), build=_one_line(run.build), result=_one_line(run.result))
    return log._replace(runs=(*log.runs, clean))


def next_seeds(log: Log, charter: str, count: int) -> tuple[int, int]:
    """`(first, last)` of the next `count` seeds of `charter`: after the highest seed any row of that charter covered, 1 when there is none.

    The highest one, not the newest row's: a range walked out of order must not be walked again. Other charters do not move it.
    """
    if count < 1:
        raise ValueError(f"count must be at least 1, not {count}")
    key = _charter_key(charter)
    covered = [run.last for run in log.runs if _charter_key(run.charter) == key]
    first = max(covered) + 1 if covered else FIRST_SEED
    return first, first + count - 1


def next_charter(log: Log, charters: Sequence[str]) -> str:
    """The charter to walk next: the first of `charters` (in the order given) with no row, else the one walked longest ago.

    Longest ago is the oldest date of a charter's newest row; the same date is decided by the position in the log, so two charters walked on one
    day go in the order they were walked, and a tie that remains by the order of `charters`.
    """
    if not charters:
        raise ValueError("no charter to choose from")
    newest: dict[str, tuple[str, int]] = {}
    for index, run in enumerate(log.runs):
        newest[_charter_key(run.charter)] = (run.date, index)
    for charter in charters:
        if _charter_key(charter) not in newest:
            return charter
    return min(charters, key=lambda charter: newest[_charter_key(charter)])


def _charter_key(charter: str) -> str:
    return charter.strip().lower()


def _one_line(text: str) -> str:
    return " ".join(text.split())


def _find_header(lines: Sequence[str], start: int) -> int | None:
    for index in range(start, len(lines)):
        cells = _split_row(lines[index])
        if cells is not None and [cell.lower() for cell in cells] == list(COLUMNS):
            return index
    return None


def _is_separator(line: str) -> bool:
    cells = _split_row(line)
    return cells is not None and len(cells) == len(COLUMNS) and all(_SEPARATOR_CELL.match(cell) for cell in cells)


def _parse_row(line: str, number: int) -> Run:
    cells = _split_row(line)
    if cells is None:
        raise LogError(f"line {number}: not a table row: {line!r}")
    if len(cells) != len(COLUMNS):
        raise LogError(f"line {number}: {len(cells)} cells, the table has {len(COLUMNS)}: {line!r}")
    date, charter, seeds, moves, build, result = cells
    found = SEED_RANGE.match(seeds)
    if found is None:
        raise LogError(f"line {number}: seeds must look like 1..5, not {seeds!r}")
    first, last = int(found.group(1)), int(found.group(2))
    if last < first:
        raise LogError(f"line {number}: the seeds {seeds!r} end before they start")
    if not moves.isdecimal():
        raise LogError(f"line {number}: moves must be a number, not {moves!r}")
    return Run(date, charter, first, last, int(moves), build, result)


def _split_row(line: str) -> list[str] | None:
    """The trimmed, unescaped cells of a `| a | b |` line; None when the line is not one."""
    text = line.strip()
    if not text.startswith("|"):
        return None
    cells: list[str] = []
    current: list[str] = []
    closed = False
    index = 1
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text) and text[index + 1] in "\\|":
            current.append(text[index + 1])
            index += 2
            continue
        if char == "|":
            cells.append("".join(current).strip())
            current = []
            closed = index == len(text) - 1
        else:
            current.append(char)
        index += 1
    if not closed:
        return None
    return cells


def _row(cells: Sequence[str]) -> str:
    return "| " + " | ".join(_escape(cell) for cell in cells) + " |"


def _escape(cell: str) -> str:
    return _one_line(cell).replace("\\", "\\\\").replace("|", "\\|")
