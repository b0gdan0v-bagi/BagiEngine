import getpass
import json
import re
from pathlib import Path

import pytest

import bagi_qa
import bagi_qa_report as report_module
import bagi_qa_repro as repro
import scenario
from bagi_qa import QaSession
from bagi_qa_checks import Finding
from bagi_qa_report import Report

ID_RULE = re.compile(r"^[A-Za-z0-9_-]+$")
INC = {"type": "inc"}
NOOP = {"type": "noop"}


def run_session(qa_game_exe, out_dir, **kwargs):
    session = QaSession(out_dir, "count to two", **kwargs)
    session.launch(exe=str(qa_game_exe))
    session.act(INC)
    session.step(2)
    session.note("counted with inc")
    session.shot("after inc")
    session.act(NOOP)
    return session


def make_report(**changes):
    values = dict(charter="a charter", game="Fake", flags=["--headless"], calls=4, steps=1, actions=2, refused=0)
    values.update(changes)
    return Report(**values)


def test_a_clean_run_has_a_report_and_no_drafts(qa_game_exe, out_dir):
    session = run_session(qa_game_exe, out_dir)
    finished = session.finish()
    assert finished.drafts == [] and finished.scenario == session.scenario_path
    text = finished.report.read_text(encoding="utf-8")
    assert "# QA report: count to two" in text and "Result: no findings" in text
    assert "- counted with inc" in text and "`shots/01-after-inc.png`" in text
    data = json.loads(finished.report_json.read_text(encoding="utf-8"))
    assert (data["game"], data["bugs"], data["known"]) == ("Fake", [], [])
    assert data["calls"] == session.calls and data["scenario"] == "session.jsonl"
    assert data["shots"] == [{"label": "after inc", "path": "shots/01-after-inc.png"}]
    assert not (out_dir / "bugs").exists()


def test_finish_twice_gives_the_same_files(qa_game_exe, out_dir):
    session = run_session(qa_game_exe, out_dir)
    assert session.finish() is session.finish()


def test_a_run_with_two_findings_is_in_both_files_with_the_call(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    monkeypatch.setenv("QA_FAKE_UI_ISSUE", "1")
    session = run_session(qa_game_exe, out_dir)
    finished = session.finish()
    text = finished.report.read_text(encoding="utf-8")
    assert "Result: 2 bug(s), 0 known deviation(s)" in text
    assert "### generic.ui_issues (ui_issue), 1 time(s), first after call 3 (`ui_tree`)" in text
    assert "generic.log_warning (log)" in text and "something odd happened" in text
    loaded = report_module.load_report(out_dir)
    assert [finding.name for finding in loaded.bugs] == ["generic.ui_issues", "generic.log_warning"]
    assert loaded.bugs[0].details["call"] == "ui_tree" and loaded.bugs[0].expect == {"pointers": {"/canvases/0/issues": []}}
    assert [path.name for path in finished.drafts if path.suffix == ".md"] and len(finished.drafts) == 2


def test_a_known_deviation_is_under_known_and_has_no_draft(qa_game_exe, out_dir, tmp_path, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "1")
    module = tmp_path / "invariants.py"
    module.write_text("KNOWN_DEVIATIONS = {'generic.log_warning'}\n")
    session = run_session(qa_game_exe, out_dir, invariants=module)
    finished = session.finish()
    text = finished.report.read_text(encoding="utf-8")
    assert "Result: 0 bug(s), 1 known deviation(s)" in text and "## Known deviations" in text and "## Bugs" not in text
    assert "invariants.py" in text and finished.drafts == []
    assert [finding.name for finding in report_module.load_report(out_dir).known] == ["generic.log_warning"]


def test_session_errors_are_in_the_report(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_QUIT_AT", "1")
    session = QaSession(out_dir, "a charter")
    session.launch(exe=str(qa_game_exe))
    with pytest.raises(Exception):
        session.act(INC)
        session.step(1)
    finished = session.finish()
    assert "## Session errors" in finished.report.read_text(encoding="utf-8")


def test_the_draft_has_the_sections_of_the_entry_form_and_a_valid_id(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    session = run_session(qa_game_exe, out_dir)
    finished = session.finish()
    (path,) = finished.drafts
    body = path.read_text(encoding="utf-8")
    identifier = path.stem
    assert ID_RULE.match(identifier) and identifier.startswith("bug-") and len(identifier) <= 40
    assert [line.split(":**")[0] for line in body.splitlines() if line.startswith("**")] == ["**Where", "**What is wrong", "**Steps", "**Reproduction"]
    assert "the game `Fake`" in body and "count to two" in body and "`perform_action {\"type\": \"noop\"}`" in body
    assert f"`{identifier}-repro.md`" in body and "something odd happened" in body
    assert (out_dir / "bugs" / f"{identifier}-repro.md").is_file()


def test_the_repro_text_reads_back_as_a_scenario(qa_game_exe, out_dir, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    session = run_session(qa_game_exe, out_dir)
    finished = session.finish()
    (path,) = finished.drafts
    text = (path.parent / f"{path.stem}-repro.md").read_text(encoding="utf-8")
    header, steps = report_module.read_repro_markdown(text)
    recorded_header, recorded = scenario.load(session.scenario_path)
    finding = session.findings[0]
    assert header == recorded_header and steps == recorded[: finding.call_index]
    assert "not cut down" not in text and "cut it down with" in text


def test_drafts_pick_up_the_shrunk_scenario_that_shrink_wrote(qa_game_exe, out_dir, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    session = run_session(qa_game_exe, out_dir)
    session.finish()
    assert bagi_qa.main(["shrink", "--exe", str(qa_game_exe), str(session.scenario_path)]) == repro.EXIT_FOUND
    shrunk = report_module.repro_scenario_path(out_dir.resolve(), "generic.log_warning")
    assert shrunk.is_file() and shrunk.parent.name == "repro"
    assert bagi_qa.main(["drafts", str(out_dir)]) == 0
    (plan,) = (out_dir / "bugs").glob("*-repro.md")
    text = plan.read_text(encoding="utf-8")
    header, steps = report_module.read_repro_markdown(text)
    assert steps == scenario.load(shrunk)[1] and len(steps) == 2 and steps[-1].expect == {"logClean": True}
    assert "bagi_play.py <executable>" in text and "fails on its last line" in text
    assert "Steps:**\n1. " in (out_dir / "bugs" / plan.name.replace("-repro", "")).read_text(encoding="utf-8")
    assert "1 draft(s)" in capsys.readouterr().out


def test_two_findings_of_one_name_are_one_bug_with_a_count():
    first = Finding("ui_issue", "generic.ui_issues", "the canvas 'A' has layout issues", 7, {"repeats": 2}, {"pointers": {}})
    second = Finding("ui_issue", "generic.ui_issues", "the canvas 'B' has layout issues", 9)
    other = Finding("log", "generic.log_error", "[Error] [1] broke", 5)
    report = make_report(bugs=[first, other, second])
    drafts = report_module.make_drafts(report, Path("/nonexistent-out"))
    assert [draft.finding.name for draft in drafts] == ["generic.ui_issues", "generic.log_error"]
    assert "It showed 4 times in the session, the first after call 7" in drafts[0].body
    assert "It showed" not in drafts[1].body


def test_a_flagged_finding_says_replay_does_not_show_it_until_it_has_a_scenario_with_an_expect(tmp_path):
    pointers = {"pointers": {"/canvases/0/elements/2/text": "seed 1"}}
    flagged = Finding("invariant", "hud_seed", "HUD shows seed 1337, state seed 1", 3, {"flagged": True}, pointers)
    report = make_report(bugs=[flagged])
    uncut = report_module.draft_bug(flagged, report, None)
    assert "`bagi_qa.py replay` does not show it" in uncut.body and "bagi_qa.py replay --exe" not in uncut.repro_markdown
    assert "state seed 1. It showed" not in uncut.body and "state seed 1.\n" in uncut.body
    scenario_file = tmp_path / "hud_seed.jsonl"
    scenario_file.write_text('{"scenario": 1, "game": "Fake", "flags": []}\n{"method": "ui_tree", "expect": {"pointers": {}}}\n', encoding="utf-8")
    cut = report_module.draft_bug(flagged, report, scenario_file)
    assert "it fails on its last line" in cut.body and "bagi_play.py <executable>" in cut.repro_markdown
    assert "bagi_qa.py replay --exe" not in cut.repro_markdown


def test_ids_are_valid_short_and_unique():
    messages = ["score fell from 10 to 8", "!!! ??? ###", "x" * 200, "", "[Warning] [12:00:00.000] a very long message about what happened to the board"]
    for message in messages:
        identifier = report_module.draft_bug(Finding("invariant", "score_decreased", message, 1), make_report(), None).id
        assert ID_RULE.match(identifier) and identifier.startswith("bug-") and len(identifier) <= 40
    assert report_module.draft_bug(Finding("invariant", "score_decreased", "score fell from 10 to 8", 1), make_report(), None).id == "bug-score-decreased-fell-from"
    report = make_report(bugs=[Finding("invariant", "a_b", "m", 1), Finding("invariant", "a.b", "m", 2)])
    ids = [draft.id for draft in report_module.make_drafts(report, Path("/nonexistent-out"))]
    assert len(set(ids)) == 2 and all(ID_RULE.match(identifier) for identifier in ids)


def test_a_title_has_no_log_prefix_and_is_short():
    finding = Finding("log", "generic.log_warning", "[Warning] [12:00:00.000] " + "long " * 60, 1)
    title = report_module.draft_bug(finding, make_report(), None).title
    assert len(title) <= report_module.TITLE_LIMIT and title.startswith("long long") and title.endswith("(generic.log_warning)")


def test_nothing_printed_names_the_home_folder_the_user_or_the_out_folder(tmp_path):
    home, out = str(Path.home()), tmp_path / "out"
    user = getpass.getuser()
    message = f"cannot open {home}/Games/data.bin for {user} in {out}/shots/x.png"
    finding = Finding("log", "generic.log_error", f"[Error] [1] {message}", 3, {"line": message})
    report = make_report(bugs=[finding], notes=[f"looked at {home}"], charter=f"{user}'s charter", out_dir=out)
    out.mkdir()
    texts = [report_module.render_markdown(report), report_module.render_json(report)]
    texts += [part for draft in report_module.make_drafts(report, out) for part in (draft.title, draft.body, draft.repro_markdown)]
    for text in texts:
        assert home not in text and str(out) not in text
        if len(user) >= report_module.MIN_USER_LENGTH:
            assert user not in text
    assert "~/Games/data.bin" in texts[0] and "<out>/shots/x.png" in texts[0]


def test_report_and_drafts_commands(qa_game_exe, out_dir, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    session = run_session(qa_game_exe, out_dir)
    finished = session.finish()
    capsys.readouterr()
    assert bagi_qa.main(["report", str(out_dir)]) == 0
    assert capsys.readouterr().out == finished.report.read_text(encoding="utf-8")
    assert bagi_qa.main(["drafts", str(out_dir)]) == 0
    assert "bugs/bug-generic-log-warning" in capsys.readouterr().out
    assert bagi_qa.main(["report", str(tmp_path / "nothing")]) == 2
    assert "report.json" in capsys.readouterr().out


def test_a_repro_text_without_a_block_is_refused():
    with pytest.raises(ValueError, match="no ```jsonl block"):
        report_module.read_repro_markdown("# nothing here\n")
