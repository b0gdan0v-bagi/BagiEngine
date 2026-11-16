import pytest

from bagi_monkey_log import Log, LogError, Run, add_run, next_charter, next_seeds, parse, render

TEXT = """# QA loop log: BagiStress

Walked by /qa-loop.
Second line of the note.

| date | charter | seeds | moves | build | result |
| --- | --- | --- | --- | --- | --- |
| 2026-11-14 | Every scene | 1..5 | 812 | 1a2b3c4 | clean |
| 2026-11-15 | Reset \\| the clock | 1..3 | 40 | 1a2b3c4 | bugs: generic.log_warning |
| 2026-11-16 | Every scene | 6..6 | 7 | 9f8e7d6 | clean |
"""


def run(charter="Every scene", first=1, last=5, date="2026-11-16", moves=100, build="abc", result="clean"):
    return Run(date, charter, first, last, moves, build, result)


def test_render_of_parse_is_the_same_text():
    log = parse(TEXT)
    assert log.product == "BagiStress"
    assert log.preamble == "Walked by /qa-loop.\nSecond line of the note."
    assert [r.charter for r in log.runs] == ["Every scene", "Reset | the clock", "Every scene"]
    assert render(log) == TEXT


def test_an_added_run_parses_back_equal():
    log = add_run(Log("BagiStress", "Notes.", ()), run(charter="A | B\\C", result="bugs: x\ny"))
    assert log.runs[0].result == "bugs: x y"
    assert parse(render(log)) == log


def test_an_empty_text_or_one_without_a_table_is_an_empty_log():
    assert parse("") == Log("", "", ())
    assert parse("# QA loop log: X\n\njust words\n") == Log("X", "just words", ())
    assert parse(render(Log("", "", ()))) == Log("", "", ())


def test_next_seeds_of_an_empty_log_start_at_one():
    assert next_seeds(Log("P", "", ()), "Any", 5) == (1, 5)


def test_next_seeds_go_after_the_highest_seed_of_that_charter_only():
    log = Log("P", "", ())
    log = add_run(log, run(first=1, last=5))
    log = add_run(log, run(charter="Other", first=1, last=30))
    assert next_seeds(log, "every scene ", 5) == (6, 10)
    assert next_seeds(log, "Other", 1) == (31, 31)
    assert next_seeds(log, "Third", 2) == (1, 2)


def test_a_range_covered_out_of_order_still_goes_after_the_highest():
    log = add_run(add_run(Log("P", "", ()), run(first=10, last=14)), run(first=1, last=3))
    assert next_seeds(log, "Every scene", 5) == (15, 19)


def test_a_count_below_one_is_refused():
    with pytest.raises(ValueError):
        next_seeds(Log("P", "", ()), "A", 0)


@pytest.mark.parametrize(
    "row, number",
    [
        ("| 2026-11-16 | A | 5..1 | 3 | b | clean |", 11),
        ("| 2026-11-16 | A | five | 3 | b | clean |", 11),
        ("| 2026-11-16 | A | 1..5 | many | b | clean |", 11),
        ("| 2026-11-16 | A | 1..5 | 3 | clean |", 11),
        ("not a row", 11),
    ],
)
def test_a_bad_row_is_a_log_error_naming_its_line(row, number):
    with pytest.raises(LogError, match=f"line {number}"):
        parse(TEXT + row + "\n")


def test_a_header_without_its_separator_is_a_log_error():
    with pytest.raises(LogError, match="separator"):
        parse("# QA loop log: P\n\n| date | charter | seeds | moves | build | result |\n")


def test_next_charter_takes_an_unused_one_first():
    log = add_run(Log("P", "", ()), run(charter="B"))
    assert next_charter(log, ["B", "A", "C"]) == "A"


def test_next_charter_then_takes_the_one_walked_longest_ago():
    log = Log("P", "", ())
    for charter, date in (("A", "2026-11-10"), ("B", "2026-11-08"), ("C", "2026-11-12"), ("B", "2026-11-13")):
        log = add_run(log, run(charter=charter, date=date))
    assert next_charter(log, ["C", "B", "A"]) == "A"


def test_next_charter_decides_one_day_by_the_position_in_the_log_and_then_by_the_order_given():
    log = add_run(add_run(Log("P", "", ()), run(charter="B")), run(charter="A"))
    assert next_charter(log, ["A", "B"]) == "B"
    with pytest.raises(ValueError):
        next_charter(log, [])

