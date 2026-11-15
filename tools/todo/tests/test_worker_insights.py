"""The panel's doctor (worker.py doctor, the problems of list) and the views of worker_insights.py: on the small
repositories of test_worker.py, with the store and `claude agents` stubbed."""

import argparse
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import worker  # noqa: E402
import worker_insights as wi  # noqa: E402
from test_worker import game, run  # noqa: E402,F401  (the fixture and the git helper)


def no_store(monkeypatch, entries):
    monkeypatch.setattr(worker, "store_entries", lambda _layout: (entries, None))
    monkeypatch.setattr(worker, "list_sessions", lambda _claude: [])


def commit(repo: Path, name: str) -> str:
    (repo / name).write_text(name)
    run("add", name, cwd=repo)
    run("commit", "-q", "-m", name, cwd=repo)
    return run("rev-parse", "HEAD", cwd=repo).strip()


def test_doctor_moves_the_entry_branch_to_commits_made_on_a_detached_head(game: Path, monkeypatch, capsys):
    worker.main(["create", "1", "--no-configure"])
    engine = game.parent / "game.workers" / "w1" / "engine"
    run("branch", "todo/x", cwd=engine)  # cut, then the developer committed detached: the branch stayed empty
    tip = commit(engine, "work.txt")
    no_store(monkeypatch, [{"id": "x", "status": "in progress", "branch": "todo/x"}])
    capsys.readouterr()  # what create printed
    assert worker.main(["doctor", "1", "--json"]) == 0
    problems = json.loads(capsys.readouterr().out)["w1"]
    assert [p["kind"] for p in problems] == ["detached-engine"]
    assert "continue todo/x" in problems[0]["text"]
    assert worker.main(["doctor", "1", "--fix", "detached-engine", "--json"]) == 0
    assert run("rev-parse", "todo/x", cwd=engine).strip() == tip
    assert run("branch", "--show-current", cwd=engine).strip() == "todo/x"
    capsys.readouterr()
    assert worker.main(["doctor", "1", "--json"]) == 0
    assert json.loads(capsys.readouterr().out)["w1"] == []


def test_doctor_keeps_commits_of_no_entry_on_a_rescue_branch(game: Path, monkeypatch, capsys):
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    tip = commit(w1, "lost.txt")
    no_store(monkeypatch, [])
    capsys.readouterr()
    assert worker.main(["doctor", "1", "--fix", "detached-game", "--json"]) == 0
    done = json.loads(capsys.readouterr().out)["done"]
    assert "rescue/w1-" in done
    assert tip in run("branch", "--list", "rescue/*", "--format=%(objectname)", cwd=game)


def test_two_running_loops_in_one_folder_are_a_problem(tmp_path: Path):
    folder = tmp_path / "w2"
    sessions = [
        {"id": "new", "kind": "background", "cwd": str(folder), "state": "running", "status": "busy", "startedAt": 2},
        {"id": "old", "kind": "background", "cwd": str(folder), "state": "running", "status": "busy", "startedAt": 1},
        {"id": "done", "kind": "background", "cwd": str(folder), "state": "done", "startedAt": 0},
    ]
    assert [s["id"] for s in worker.running_sessions_in(sessions, folder)] == ["old", "new"]
    layout = worker.Layout(main=tmp_path, root=tmp_path, shared=None)
    problems = worker.worker_problems(layout, folder, [], sessions, None, [], False)
    assert problems[0]["kind"] == "sessions" and "2 loops" in problems[0]["text"]


def test_queue_clear_without_a_worker_empties_every_queue(game: Path, capsys):
    worker.main(["create", "2", "--no-configure"])
    worker.main(["queue", "1", "--add", "a"])
    worker.main(["queue", "2", "--add", "b"])
    capsys.readouterr()
    assert worker.main(["queue", "--clear", "--json"]) == 0
    shown = json.loads(capsys.readouterr().out)
    assert shown["w1"]["queue"] == [] and shown["w2"]["queue"] == []


def test_area_paths_are_relative_to_the_area_repository():
    engine_area = {"area": "engine", "repo": "engine", "base": "games/x"}
    game_area = {"area": "x", "repo": ".", "base": "main"}
    named = ["engine/src/A.h", "src/B.cpp", "(stage", "CLAUDE.md", "engine/CLAUDE.md"]
    assert wi.area_paths(named, engine_area, True) == ["src/A.h", "src/B.cpp"]
    assert wi.area_paths(named, game_area, True) == ["src/B.cpp"]
    assert wi.area_paths(["engine/x.h"], engine_area, False) == ["engine/x.h"]


def test_a_session_span_names_its_entry_and_how_it_ended(tmp_path: Path):
    prompt = {"type": "user", "timestamp": "2026-11-15T06:00:00Z", "message": {"content": "<command-name>/todo-loop</command-name>\n<command-args>1 e1</command-args>"}}
    claim = {"type": "assistant", "timestamp": "2026-11-15T06:10:00Z", "message": {"content": [{"type": "tool_use", "name": "Bash", "input": {"command": "python3 todo.py --actor todo-loop claim e1 --branch todo/e1"}}]}}
    review = {"type": "assistant", "timestamp": "2026-11-15T07:00:00Z", "message": {"content": [{"type": "tool_use", "name": "Bash", "input": {"command": "todo.py review e1 --branch todo/e1 --note \"x\""}}]}}
    path = tmp_path / "s1.jsonl"
    path.write_text("\n".join(json.dumps(r) for r in (prompt, claim, review)) + "\n")
    span = wi.session_span(path, 0, set())
    assert (span["entry"], span["outcome"], span["loop"]) == ("e1", "review", True)
    assert span["end"] - span["start"] == 3600 * 1000
    assert wi.session_span(path, 0, {"s1"})["outcome"] == "running"
    limited = {"type": "assistant", "timestamp": "2026-11-15T07:05:00Z", "error": "rate_limit", "message": {"content": [{"type": "text", "text": "You've hit your limit"}]}}
    path.write_text("\n".join(json.dumps(r) for r in (prompt, claim, limited)) + "\n")
    assert wi.session_span(path, 0, set())["outcome"] == "limited"


def test_review_stats_count_rounds_from_the_plan_names(tmp_path: Path, monkeypatch):
    plans = [
        {"name": "todo-loop-a-review-final.md", "entry": "a", "kind": "review", "updatedAt": "2999-01-01T00:00:00+00:00"},
        {"name": "todo-loop-a-review-final-r2.md", "entry": "a", "kind": "review", "updatedAt": "2999-01-01T00:00:00+00:00"},
        {"name": "todo-loop-a-s2-review-final.md", "entry": "a", "kind": "review", "updatedAt": "2999-01-01T00:00:00+00:00"},
        {"name": "todo-loop-a-remarks-1.md", "entry": "a", "kind": "remarks", "updatedAt": "2999-01-01T00:00:00+00:00"},
        {"name": "todo-loop-b-review-final.md", "entry": "b", "kind": "review", "updatedAt": "2000-01-01T00:00:00+00:00"},
    ]
    monkeypatch.setattr(worker, "todo_cli", lambda _layout, *args: json.dumps(plans))
    stats = wi.review_stats(worker.Layout(main=tmp_path, root=tmp_path, shared=None), 7)
    assert stats["entries"]["a"] == {"reviews": 2, "changes": 1, "remarks": 1}
    assert stats["entries"]["b"] == {"reviews": 1, "changes": 0, "remarks": 0}
    assert stats["total"] == {"entries": 1, "reviews": 2, "changes": 1, "remarks": 1, "firstTime": 1}


def test_the_day_says_what_is_not_pushed(game: Path):
    first = run("rev-parse", "HEAD", cwd=game).strip()
    commit(game, "more.txt")
    run("update-ref", "refs/remotes/origin/main", first, cwd=game)
    rows = wi.push_state(game, ".")
    assert rows == [{"repo": ".", "branch": "main", "upstream": "origin/main", "ahead": 1, "behind": 0, "gone": False}]


def test_the_merge_train_merges_a_blocker_first_and_stops_at_a_conflict(tmp_path: Path, monkeypatch, capsys):
    layout = worker.Layout(main=tmp_path, root=tmp_path, shared=None)
    monkeypatch.setattr(wi, "train_order", lambda _layout: [{"id": "game"}, {"id": "eng"}, {"id": "bad"}, {"id": "late"}])
    plans = {
        "game": {"first": ["eng"], "conflicts": [], "problems": []},
        "eng": {"first": [], "conflicts": [], "problems": []},
        "bad": {"first": [], "conflicts": ["src/x.h"], "problems": [], "base": "main"},
        "late": {"first": [], "conflicts": [], "problems": []},
    }
    merged_ids = []

    def plan_of(_layout, entry_id):
        if entry_id == "game" and "eng" in merged_ids:
            return {**plans["game"], "first": [], "heldBy": None, "id": entry_id}
        return {**plans[entry_id], "heldBy": None, "id": entry_id}

    monkeypatch.setattr(worker, "merge_plan", plan_of)
    monkeypatch.setattr(worker, "do_merge", lambda _layout, plan: merged_ids.append(plan["id"]) or {"id": plan["id"]})
    monkeypatch.setattr(worker, "entry_holders", lambda *_args: set())
    monkeypatch.setattr(worker, "reset_streaks", lambda *_args: None)
    wi.cmd_merge_train(layout, argparse.Namespace(dry_run=False, no_kick=True, json=True, claude="claude", delay=0))
    out = json.loads(capsys.readouterr().out)
    assert merged_ids == ["eng", "game"]
    assert out["stopped"]["id"] == "bad" and "src/x.h" in out["stopped"]["why"]
    assert out["left"] == ["late"]


def test_the_timeline_cache_keeps_finished_sessions_and_reparses_running_ones(tmp_path: Path, monkeypatch):
    monkeypatch.setattr(worker, "claude_home", lambda: tmp_path)
    prompt = {"type": "user", "timestamp": "2026-11-15T06:00:00Z", "message": {"content": "<command-name>/todo-loop</command-name>\n<command-args>1 e1</command-args>"}}
    path = tmp_path / "s1.jsonl"
    path.write_text(json.dumps(prompt) + "\n")
    seen: dict = {}
    assert wi.cached_span({}, seen, path, 0, {"s1"})["outcome"] == "running"
    assert seen == {}  # a running session is never cached
    review = {"type": "assistant", "timestamp": "2026-11-15T07:00:00Z", "message": {"content": [{"type": "tool_use", "name": "Bash", "input": {"command": "todo.py review e1 --branch todo/e1"}}]}}
    path.write_text("\n".join(json.dumps(r) for r in (prompt, review)) + "\n")
    assert wi.cached_span({}, seen, path, 0, set())["outcome"] == "review"
    wi.write_span_cache(seen)
    monkeypatch.setattr(wi, "session_span", lambda *_args: pytest.fail("a finished transcript of the same size is not parsed again"))
    assert wi.cached_span(wi.read_span_cache(), {}, path, 0, set())["outcome"] == "review"
