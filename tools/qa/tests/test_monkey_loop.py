import json
from pathlib import Path

import pytest

import bagi_monkey_log as log_module
import bagi_monkey_loop as loop
import bagi_qa_report as report_module
import scenario

CHARTER = "Every scene and every language switch"
OTHER = "Reset and the manual clock"
STEPS = "20"


def run(*arguments, clock=None):
    return loop.main(list(arguments)) if clock is None else loop.main(list(arguments), clock)


def next_seeds(log, capsys, *charters, count="5"):
    flags = [flag for charter in charters for flag in ("--charter", charter)]
    assert run("next-seeds", "--log", str(log), *flags, "--count", count) == 0
    return json.loads(capsys.readouterr().out)


def sweep(exe, out_root, *arguments, first="1", count="3", clock=None):
    return run("sweep", "--exe", str(exe), "--charter", CHARTER, "--first", first, "--count", count, "--steps", STEPS, "--out-root", str(out_root), *arguments, clock=clock)


def summary_of(out_root):
    return json.loads((out_root / "sweep.json").read_text(encoding="utf-8"))


def repro_plan(path, finding, calls):
    header = scenario.Header(1, "Fake", ["--headless", "--manual-clock"])
    text = scenario.dumps(header, [scenario.Step(method, params) for method, params in calls]).rstrip()
    path.write_text(f"# {path.stem}\n\nThe finding `{finding}` shows.\n\n```{report_module.REPRO_FENCE}\n{text}\n```\n", encoding="utf-8")
    return path


def test_next_seeds_of_a_missing_log_starts_at_one_and_goes_on_after_a_log_add(tmp_path, capsys):
    log = tmp_path / "log.md"
    assert next_seeds(log, capsys, CHARTER) == {"charter": CHARTER, "first": 1, "last": 5}
    assert run("log-add", "--log", str(log), "--product", "Fake", "--charter", CHARTER, "--seeds", "1..5", "--moves", "812", "--build", "1a2b3c4", "--date", "2026-11-16") == 0
    assert "LOGGED" in capsys.readouterr().out
    assert next_seeds(log, capsys, CHARTER, count="3") == {"charter": CHARTER, "first": 6, "last": 8}
    parsed = log_module.parse(log.read_text(encoding="utf-8"))
    assert parsed.product == "Fake" and parsed.runs == (log_module.Run("2026-11-16", CHARTER, 1, 5, 812, "1a2b3c4", "clean"),)


def test_with_two_charters_next_seeds_names_the_unused_one(tmp_path, capsys):
    log = tmp_path / "log.md"
    run("log-add", "--log", str(log), "--product", "Fake", "--charter", CHARTER, "--seeds", "1..5", "--moves", "10")
    capsys.readouterr()
    assert next_seeds(log, capsys, CHARTER, OTHER) == {"charter": OTHER, "first": 1, "last": 5}


def test_log_add_writes_a_row_from_a_sweep_file(tmp_path, capsys):
    summary = {"charter": CHARTER, "first": 6, "last": 8, "moves": 60, "stopped": "bugs", "seeds": [{"seed": 6, "stopped": "bug", "bugs": ["generic.log_warning", "x|y"]}]}
    sweep_file = tmp_path / "sweep.json"
    sweep_file.write_text(json.dumps(summary), encoding="utf-8")
    log = tmp_path / "deep" / "log.md"
    assert run("log-add", "--log", str(log), "--product", "Fake", "--sweep", str(sweep_file), "--date", "2026-11-17") == 0
    (row,) = log_module.parse(log.read_text(encoding="utf-8")).runs
    assert row == log_module.Run("2026-11-17", CHARTER, 6, 8, 60, "", "bugs: generic.log_warning, x|y")
    assert not list(log.parent.glob(".*.tmp"))


def test_log_add_keeps_the_text_and_the_title_of_an_existing_log(tmp_path):
    log = tmp_path / "log.md"
    log.write_text("# QA loop log: Old\n\nNotes.\n\n| date | charter | seeds | moves | build | result |\n| --- | --- | --- | --- | --- | --- |\n", encoding="utf-8")
    assert run("log-add", "--log", str(log), "--product", "New", "--charter", CHARTER, "--seeds", "3..3", "--moves", "4", "--result", "odd") == 0
    parsed = log_module.parse(log.read_text(encoding="utf-8"))
    assert parsed.product == "Old" and parsed.preamble == "Notes." and parsed.runs[0].result == "odd"


@pytest.mark.parametrize("seeds", ["5", "5..1", "a..b"])
def test_a_bad_seeds_option_is_2(tmp_path, capsys, seeds):
    log = tmp_path / "log.md"
    assert run("log-add", "--log", str(log), "--product", "Fake", "--charter", CHARTER, "--seeds", seeds, "--moves", "1") == 2
    assert "ERROR" in capsys.readouterr().out and not log.exists()


def test_log_add_needs_a_sweep_or_the_three_fields_and_never_both(tmp_path):
    base = ["log-add", "--log", str(tmp_path / "log.md"), "--product", "Fake"]
    assert run(*base) == 2
    assert run(*base, "--sweep", str(tmp_path / "sweep.json"), "--charter", CHARTER) == 2


def test_a_log_with_a_row_it_cannot_read_is_2(tmp_path, capsys):
    log = tmp_path / "log.md"
    log.write_text("| date | charter | seeds | moves | build | result |\n| --- | --- | --- | --- | --- | --- |\n| 2026 | c | oops | 1 | b | r |\n", encoding="utf-8")
    assert run("next-seeds", "--log", str(log), "--charter", CHARTER, "--count", "1") == 2
    assert "line 3" in capsys.readouterr().out


def test_dupes_names_the_known_file_of_the_same_finding_and_only_that(tmp_path, capsys):
    calls = [("step", {"frames": 1}), ("perform_action", {"type": "inc"}), ("perform_action", {"type": "inc"})]
    fresh = repro_plan(tmp_path / "fresh.md", "generic.log_warning", calls)
    same = repro_plan(tmp_path / "bug-same-repro.md", "generic.log_warning", calls + [("step", {"frames": 2})])
    other = repro_plan(tmp_path / "bug-other-repro.md", "generic.ui_issues", calls)
    broken = tmp_path / "broken.md"
    broken.write_text("nothing here\n", encoding="utf-8")
    assert run("dupes", "--repro", str(fresh), "--known", str(other), str(broken), str(same)) == 1
    printed = capsys.readouterr().out
    assert f"DUPLICATE {same}: generic.log_warning" in printed and f"SKIPPED {broken}" in printed and "bug-other" not in printed.replace("SKIPPED", "")
    assert run("dupes", "--repro", str(fresh), "--known", str(other)) == 0
    assert "DUPLICATE" not in capsys.readouterr().out


def test_dupes_with_no_known_file_matches_nothing(tmp_path, capsys):
    fresh = repro_plan(tmp_path / "fresh.md", "generic.log_warning", [("step", {"frames": 1}), ("perform_action", {"type": "inc"})])
    assert run("dupes", "--repro", str(fresh), "--known") == 0
    assert "DUPLICATE" not in capsys.readouterr().out


def test_dupes_with_a_bad_repro_is_2(tmp_path, capsys):
    assert run("dupes", "--repro", str(tmp_path / "missing.md"), "--known", str(tmp_path / "other.md")) == 2
    assert "ERROR" in capsys.readouterr().out


def test_a_clean_sweep_walks_every_seed_and_writes_sweep_json(qa_game_exe, out_dir, capsys):
    assert sweep(qa_game_exe, out_dir) == 0
    printed = json.loads(capsys.readouterr().out)
    summary = summary_of(out_dir)
    assert printed == summary
    assert (summary["charter"], summary["first"], summary["last"], summary["stopped"]) == (CHARTER, 1, 3, "count")
    assert [record["seed"] for record in summary["seeds"]] == [1, 2, 3] and summary["moves"] == 60
    assert all(record["bugs"] == [] and record["stopped"] == "steps" and (out_dir / f"s{record['seed']}" / "session.jsonl").is_file() for record in summary["seeds"])


def test_a_bug_stops_the_sweep_when_one_name_is_enough(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "5")
    assert sweep(qa_game_exe, out_dir, "--stop-after-bugs", "1") == 1
    capsys.readouterr()
    summary = summary_of(out_dir)
    (record,) = summary["seeds"]
    assert summary["stopped"] == "bugs" and summary["last"] == 1 and record["bugs"] == ["generic.log_warning"] and record["stopped"] == "bug"
    assert record["repros"] == [str(report_module.repro_scenario_path(out_dir / "s1", "generic.log_warning"))]
    assert len(record["drafts"]) == 1


def test_the_same_name_in_every_seed_counts_once_against_the_stop(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "5")
    assert sweep(qa_game_exe, out_dir, "--no-shrink", "--steps", "40") == 1
    capsys.readouterr()
    summary = summary_of(out_dir)
    assert summary["stopped"] == "count" and summary["last"] == 3 and len(summary["seeds"]) == 3
    assert loop.sweep_result(summary) == "bugs: generic.log_warning"


def test_the_total_minutes_end_the_sweep_between_seeds(qa_game_exe, out_dir, capsys):
    ticks = iter([0.0, 0.0] + [1000.0] * 100)
    assert sweep(qa_game_exe, out_dir, "--minutes", "1", clock=lambda: next(ticks)) == 0
    capsys.readouterr()
    summary = summary_of(out_dir)
    assert summary["stopped"] == "minutes" and summary["last"] == 1 and [record["seed"] for record in summary["seeds"]] == [1]


def test_a_game_that_refuses_the_start_ends_the_sweep_with_2(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_REFUSE_RESET", "1")
    assert sweep(qa_game_exe, out_dir, "--start", '{"count": 3}') == 2
    capsys.readouterr()
    summary = summary_of(out_dir)
    (record,) = summary["seeds"]
    assert summary["stopped"] == "error" and record["stopped"] == "error" and "refused the start parameters" in record["error"] and summary["last"] == 0
    assert loop.sweep_result(summary).startswith("error: ")


def test_a_missing_executable_is_77(tmp_path, out_dir, capsys):
    assert sweep(tmp_path / "NoSuchGame", out_dir) == 77
    assert "SKIPPED" in capsys.readouterr().out and not out_dir.exists()


def test_an_error_after_a_bug_keeps_the_names_in_the_result():
    summary = {"seeds": [{"stopped": "bug", "bugs": ["generic.log_warning"]}, {"stopped": "error", "error": "no start", "bugs": []}]}
    assert loop.sweep_result(summary) == "error: no start (bugs: generic.log_warning)"


def test_an_out_root_inside_a_git_work_tree_is_2_and_nothing_is_written(qa_game_exe, capsys):
    inside = Path(__file__).resolve().parent / "sweep-out"
    assert sweep(qa_game_exe, inside) == 2
    assert "git work tree" in capsys.readouterr().out and not inside.exists()


@pytest.mark.parametrize("option", [["--count", "0"], ["--stop-after-bugs", "0"], ["--minutes", "0"], ["--minutes-per-seed", "-1"]])
def test_a_bad_sweep_option_is_2(qa_game_exe, out_dir, capsys, option):
    arguments = ["sweep", "--exe", str(qa_game_exe), "--charter", CHARTER, "--first", "1", "--count", "1", "--out-root", str(out_dir)]
    assert run(*arguments, *option) == 2  # the last of a repeated option wins
    assert "ERROR" in capsys.readouterr().out


def test_the_default_out_root_is_named_by_the_date_and_the_charter():
    assert loop._slug("Every scene, and every language switch!") == "every-scene-and-every-language-switch"
    assert loop._slug("???") == "charter" and len(loop._slug("a" * 100)) == loop.SLUG_LENGTH
