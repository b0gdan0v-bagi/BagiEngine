import json
from pathlib import Path

import pytest

import bagi_monkey
import bagi_play
import bagi_qa_report as report_module
import scenario

STEPS = 40


def walk(exe, out, seed=1, steps=STEPS, **kwargs):
    return bagi_monkey.run_monkey(exe, seed, out, steps, **kwargs)


def cli(exe, out, *arguments, seed="1", steps="40"):
    return bagi_monkey.main(["--exe", str(exe), "--seed", seed, "--steps", steps, "--out", str(out), *arguments])


def methods_of(result):
    return [step.method for step in scenario.load(result.out / "session.jsonl")[1]]


def notes_of(result):
    return json.loads((result.out / "report.json").read_text(encoding="utf-8"))["notes"]


def test_a_clean_walk_covers_more_than_one_kind(qa_game_exe, out_dir):
    result = walk(qa_game_exe, out_dir)
    assert result.stopped == "steps" and result.moves == STEPS and result.bugs == [] and result.repros == [] and result.drafts == []
    assert sum(result.per_kind.values()) == STEPS and len(result.per_kind) > 1
    assert any(note.startswith("moves by kind: ") for note in notes_of(result))
    assert "ui_click" in methods_of(result)


def test_the_notes_say_the_seed_the_charter_and_the_start(qa_game_exe, out_dir):
    result = walk(qa_game_exe, out_dir, seed=7, steps=5, start={"count": 3}, charter="a short walk")
    notes = notes_of(result)
    assert "monkey seed 7, 5 moves" in notes and "charter: a short walk" in notes and 'start parameters: {"count": 3}' in notes
    assert methods_of(result).count("reset") >= 1


def test_the_same_seed_walks_the_same_way_and_another_seed_does_not(qa_game_exe, out_dir):
    first = walk(qa_game_exe, out_dir / "a")
    again = walk(qa_game_exe, out_dir / "b")
    other = walk(qa_game_exe, out_dir / "c", seed=2)
    recording = lambda result: (result.out / "session.jsonl").read_bytes()  # noqa: E731
    assert recording(first) == recording(again)
    assert recording(first) != recording(other)


def test_a_bug_stops_the_walk_and_leaves_a_reproduction_and_a_draft(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "5")
    assert cli(qa_game_exe, out_dir, steps="200") == 1
    printed = capsys.readouterr().out
    path = report_module.repro_scenario_path(out_dir, "generic.log_warning")
    assert "FOUND generic.log_warning" in printed and str(path) in printed and str(out_dir) in printed
    header, steps = scenario.load(path)
    played = bagi_play.play(qa_game_exe, header, steps)
    assert not played.passed and played.failed_step == len(steps) - 1
    (plan,) = (out_dir / "bugs").glob("bug-*-repro.md")
    assert report_module.read_repro_markdown(plan.read_text(encoding="utf-8"))[1] == steps


def test_the_walk_stops_at_the_move_that_showed_the_bug(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "5")
    result = walk(qa_game_exe, out_dir, steps=200)
    assert result.stopped == "bug" and result.moves < 200
    assert [found.name for found in result.bugs] == ["generic.log_warning"]
    assert list(result.out.joinpath("repro").glob("*.jsonl")) == result.repros and len(result.drafts) == 1


def test_no_shrink_leaves_the_drafts_with_the_recording(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "5")
    result = walk(qa_game_exe, out_dir, steps=200, shrink=False)
    assert result.stopped == "bug" and result.repros == [] and not (out_dir / "repro").exists()
    assert len(result.drafts) == 1 and "generic.log_warning" in result.unshrunk


def test_a_game_that_crashes_is_a_lost_game_that_is_not_shrunk(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_CRASH_AT", "5")
    assert cli(qa_game_exe, out_dir, steps="200") == 1
    printed = capsys.readouterr().out
    assert "monkey.game_lost" in printed
    report = json.loads((out_dir / "report.json").read_text(encoding="utf-8"))
    assert [found["name"] for found in report["bugs"]] == ["monkey.game_lost"]
    assert report["bugs"][0]["details"]["move"] and list((out_dir / "bugs").glob("bug-*.md")) and not (out_dir / "repro").exists()


def test_the_minutes_budget_stops_the_walk_on_the_clock_it_is_given(qa_game_exe, out_dir):
    ticks = iter([0.0, 0.0, 0.0] + [1000.0] * 1000)
    result = walk(qa_game_exe, out_dir, steps=100, minutes=1.0, clock=lambda: next(ticks))
    assert result.stopped == "minutes" and 0 < result.moves < 100 and result.bugs == []


def test_a_module_that_drops_the_step_moves_is_honoured(qa_game_exe, out_dir, tmp_path):
    module = tmp_path / "invariants.py"
    module.write_text("def monkey_moves(answer, moves):\n    return [move for move in moves if move.kind != 'step']\n", encoding="utf-8")
    result = walk(qa_game_exe, out_dir, invariants=module)
    steps = [step for step in scenario.load(result.out / "session.jsonl")[1] if step.method == "step"]
    clicks = methods_of(result).count("ui_click")
    assert result.per_kind.get("step", 0) == 0 and clicks > 0
    assert len(steps) == 1 + clicks and all(step.params == {"frames": bagi_monkey.FOLLOW_FRAMES} for step in steps[1:])


def test_a_module_that_raises_is_a_finding_and_the_generic_moves_stay(qa_game_exe, out_dir, tmp_path):
    module = tmp_path / "invariants.py"
    module.write_text("def monkey_moves(answer, moves):\n    raise RuntimeError('no moves today')\n", encoding="utf-8")
    result = walk(qa_game_exe, out_dir, invariants=module, shrink=False)
    assert [found.name for found in result.bugs] == ["qa.module_error"] and result.moves == 1


def test_kinds_limit_the_walk(qa_game_exe, out_dir):
    result = walk(qa_game_exe, out_dir, steps=15, kinds=["action", "step"])
    assert set(result.per_kind) <= {"action", "step"} and "ui_click" not in methods_of(result)


def test_a_missing_executable_is_77(tmp_path, out_dir, capsys):
    assert cli(tmp_path / "NoSuchGame", out_dir) == 77
    assert "SKIPPED" in capsys.readouterr().out


@pytest.mark.parametrize("option", [["--start", "{nope"], ["--start", "[1]"], ["--kinds", "action,fly"], ["--kinds", ""]])
def test_a_bad_option_is_2(qa_game_exe, out_dir, option):
    with pytest.raises(SystemExit) as stopped:
        cli(qa_game_exe, out_dir, *option)
    assert stopped.value.code == 2


def test_a_refused_start_is_2_and_the_game_is_stopped(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_REFUSE_RESET", "1")
    assert cli(qa_game_exe, out_dir, "--start", '{"count": 3}', steps="5") == 2
    assert "refused the start parameters" in capsys.readouterr().out
    assert (out_dir / "report.json").is_file()


def test_a_session_folder_inside_a_repository_is_2(qa_game_exe, capsys):
    inside = Path(__file__).resolve().parent / "monkey-out"
    assert cli(qa_game_exe, inside) == 2
    assert "git work tree" in capsys.readouterr().out and not inside.exists()
