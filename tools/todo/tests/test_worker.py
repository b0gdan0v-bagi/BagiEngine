"""worker.py on small repositories made in a temp folder: a game with the submodule `engine`, which has a
nested submodule of its own, as the real game has `engine/external/*`."""

import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import worker  # noqa: E402

GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}


def run(*args: str, cwd: Path) -> str:
    env = {**os.environ, **GIT_ENV}
    return subprocess.run(["git", "-c", "protocol.file.allow=always", *args], cwd=cwd, env=env, check=True, capture_output=True, text=True).stdout


@pytest.fixture
def game(tmp_path: Path, monkeypatch) -> Path:
    for key, value in GIT_ENV.items():
        monkeypatch.setenv(key, value)
    ext = tmp_path / "ext"
    ext.mkdir()
    run("init", "-q", "-b", "main", cwd=ext)
    run("commit", "-q", "--allow-empty", "-m", "ext", cwd=ext)
    eng = tmp_path / "eng"
    eng.mkdir()
    run("init", "-q", "-b", "games/chess", cwd=eng)
    (eng / ".gitignore").write_text(".venv/\n")
    run("add", ".", cwd=eng)
    run("submodule", "add", "-q", "../ext", "external/ext", cwd=eng)
    run("commit", "-q", "-m", "eng", cwd=eng)
    main = tmp_path / "game"
    main.mkdir()
    run("init", "-q", "-b", "main", cwd=main)
    run("submodule", "add", "-q", "-b", "games/chess", "../eng", "engine", cwd=main)
    run("submodule", "update", "-q", "--init", "--recursive", cwd=main)
    (main / ".gitignore").write_text(".env\n.claude/settings.local.json\n")
    run("add", ".", cwd=main)
    run("commit", "-q", "-m", "game", cwd=main)
    (main / ".env").write_text("TODO_API_URL=x\n")
    (main / ".claude").mkdir()
    (main / ".claude" / "settings.local.json").write_text("{}\n")
    (main / "engine" / ".venv" / "bin").mkdir(parents=True)
    monkeypatch.chdir(main)
    return main


def engine_config(main: Path) -> str:
    return (main / ".git" / "modules" / "engine" / "config").read_text()


def test_create_makes_clean_workers_that_share_branches(game: Path):
    before = engine_config(game)
    assert worker.main(["create", "2", "--no-configure"]) == 0
    w1 = game.parent / "game.workers" / "w1"
    assert run("status", "--porcelain", cwd=w1) == ""
    assert run("submodule", "status", "engine", cwd=w1).startswith(" ")
    assert (w1 / "engine" / "external" / "ext" / ".git").exists()
    for rel in (".env", ".claude/settings.local.json", "engine/.venv"):
        assert (w1 / rel).is_symlink(), rel
    run("switch", "-q", "-c", "todo/probe", cwd=w1 / "engine")
    assert "todo/probe" in run("branch", "--list", "todo/*", cwd=game / "engine")
    run("switch", "-q", "-c", "todo/game-probe", cwd=w1)
    assert "todo/game-probe" in run("branch", "--list", "todo/*", cwd=game)
    assert engine_config(game) == before


def test_a_submodule_update_in_a_worker_leaves_the_main_engine_alone(game: Path):
    before = engine_config(game)
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    run("submodule", "update", "--init", "--recursive", cwd=w1)
    assert engine_config(game) == before
    assert run("status", "--porcelain", cwd=game / "engine") == ""
    assert worker.main(["check"]) == 0


def test_check_finds_and_fixes_a_broken_core_worktree(game: Path):
    worker.main(["create", "1", "--no-configure"])
    run("config", "-f", str(game / ".git" / "modules" / "engine" / "config"), "core.worktree", "../../../../elsewhere", cwd=game)
    with pytest.raises(SystemExit):
        worker.main(["check"])
    assert worker.main(["check", "--fix"]) == 0
    assert worker.main(["check"]) == 0
    assert run("status", "--porcelain", cwd=game / "engine") == ""


def test_remove_refuses_a_dirty_worker_and_keeps_its_branches(game: Path):
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    run("switch", "-q", "-c", "todo/kept", cwd=w1 / "engine")
    (w1 / "engine" / "junk").write_text("x")
    assert worker.main(["remove", "1"]) == 1
    (w1 / "engine" / "junk").unlink()
    assert worker.main(["remove", "1"]) == 0
    assert not w1.exists()
    assert "todo/kept" in run("branch", "--list", "todo/*", cwd=game / "engine")
    assert run("worktree", "list", cwd=game).count("\n") == 1


def test_run_print_only_prints(game: Path, capsys):
    worker.main(["create", "1", "--no-configure"])
    assert worker.main(["run", "1", "--print", "--", "--model", "sonnet"]) == 0
    assert "claude --model sonnet" in capsys.readouterr().out


# --- background sessions, against a fake `claude` that records its calls ---------------------------

FAKE_CLAUDE = """#!/bin/sh
log="$FAKE_CLAUDE_LOG"
if [ "$1" = agents ]; then cat "$FAKE_CLAUDE_SESSIONS" 2>/dev/null || echo '[]'; exit 0; fi
if [ "$1" = stop ]; then echo "stop $2" >> "$log"; echo "stopped $2"; exit 0; fi
echo "bg cwd=$(pwd) jobs=$CMAKE_BUILD_PARALLEL_LEVEL args=$*" >> "$log"
if [ -n "$FAKE_CLAUDE_UNTRUSTED" ]; then echo "Workspace not trusted. Run claude once and accept the trust prompt"; exit 1; fi
echo "backgrounded · abcd1234 · $3"
"""


@pytest.fixture
def fake_claude(tmp_path: Path, monkeypatch) -> tuple[Path, Path, Path]:
    script = tmp_path / "fake-claude"
    script.write_text(FAKE_CLAUDE)
    script.chmod(0o755)
    log = tmp_path / "claude.log"
    sessions = tmp_path / "sessions.json"
    monkeypatch.setenv("FAKE_CLAUDE_LOG", str(log))
    monkeypatch.setenv("FAKE_CLAUDE_SESSIONS", str(sessions))
    monkeypatch.delenv("CMAKE_BUILD_PARALLEL_LEVEL", raising=False)
    return script, log, sessions


def session(cwd: Path, state: str, ident: str = "s1", status: str = "idle", waiting_for: str | None = None) -> dict:
    found = {"id": ident, "cwd": str(cwd), "kind": "background", "name": "todo-w1", "status": status, "state": state, "startedAt": 0}
    if waiting_for is not None:
        found["waitingFor"] = waiting_for
    return found


def test_start_runs_the_loop_in_the_background_in_every_worker(game: Path, fake_claude):
    script, log, _sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    assert worker.main(["start", "--delay", "0", "--slices", "3", "--claude", str(script), "--", "--model", "opus"]) == 0
    calls = log.read_text().splitlines()
    assert len(calls) == 2
    for k, call in enumerate(calls, start=1):
        assert f"/game.workers/w{k} jobs=" in call
        assert f"args=--bg --name todo-w{k} --model opus /todo-loop 3" in call
        assert "jobs=" in call and "jobs= " not in call


def test_start_leaves_a_running_worker_alone_and_restarts_a_finished_one(game: Path, fake_claude):
    script, log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    sessions.write_text(json.dumps([session(workers / "w1", "working", "a"), session(workers / "w2", "done", "b")]))
    assert worker.main(["start", "--delay", "0", "--claude", str(script)]) == 0
    calls = log.read_text().splitlines()
    assert len(calls) == 1 and "--name todo-w2" in calls[0] and calls[0].endswith("/todo-loop 1")


def test_start_names_the_way_out_for_an_untrusted_folder(game: Path, fake_claude, monkeypatch, capsys):
    script, _log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    monkeypatch.setenv("FAKE_CLAUDE_UNTRUSTED", "1")
    assert worker.main(["start", "--delay", "0", "--claude", str(script)]) == 1
    assert "worker.py run 1` once" in capsys.readouterr().out


def test_start_refuses_a_broken_shared_config(game: Path, fake_claude):
    script, log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    run("config", "-f", str(game / ".git" / "modules" / "engine" / "config"), "core.worktree", "../../../../elsewhere", cwd=game)
    assert worker.main(["start", "--delay", "0", "--claude", str(script)]) == 1
    assert not log.exists()


def test_stop_and_list_find_the_session_of_a_worker(game: Path, fake_claude, capsys):
    script, log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    sessions.write_text(json.dumps([session(workers / "w1" / "engine", "working", "abc")]))
    assert worker.main(["list", "--claude", str(script)]) == 0
    out = capsys.readouterr().out
    assert "todo-w1: working/idle" in out and "no session" in out
    assert worker.main(["stop", "--claude", str(script)]) == 0
    assert log.read_text().splitlines() == ["stop abc"]


def test_start_passes_the_session_that_gets_the_reports(game: Path, fake_claude):
    script, log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    assert worker.main(["start", "--delay", "0", "--report-to", "bagiengine-chess-51", "--loop-args", "plan-only", "--claude", str(script)]) == 0
    assert log.read_text().strip().endswith("/todo-loop 1 plan-only report-to=bagiengine-chess-51")


def test_start_refuses_a_report_to_that_is_not_a_session_name(game: Path, fake_claude):
    script, log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    assert worker.main(["start", "--delay", "0", "--report-to", "two words", "--claude", str(script)]) != 0
    assert not log.exists()


def test_list_json_names_each_worker_its_heads_and_its_session(game: Path, fake_claude, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    sessions.write_text(json.dumps([session(workers / "w2", "working", "b")]))
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    state = json.loads(capsys.readouterr().out)
    assert state["store"] == "off" and state["sessionsKnown"] and state["review"] == []
    first, second = state["workers"]
    assert (first["name"], first["session"], first["active"], first["dirty"]) == ("w1", None, False, False)
    assert first["head"]["branch"] is None and first["head"]["commit"]
    assert second["session"]["id"] == "b" and second["active"] and second["entry"] is None
    assert not first["waiting"] and not second["waiting"]


def test_list_says_when_a_session_waits_for_the_person(game: Path, fake_claude, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    sessions.write_text(json.dumps([
        session(workers / "w1", "working", "a", status="waiting", waiting_for="input needed"),
        session(workers / "w2", "working", "b", status="idle"),
    ]))
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    first, second = json.loads(capsys.readouterr().out)["workers"]
    assert first["waiting"] and first["active"] and first["session"]["waitingFor"] == "input needed"
    assert not second["waiting"] and second["active"] and second["session"]["waitingFor"] is None
    assert worker.main(["list", "--claude", str(script)]) == 0
    out = capsys.readouterr().out
    assert "waits for you (input needed): claude attach todo-w1" in out
    assert out.count("waits for you") == 1


def test_list_json_activity_reads_the_session_and_its_agents_from_the_transcripts(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    record = session(w1, "working", "a")
    record["sessionId"] = "sess-1"
    sessions.write_text(json.dumps([record]))
    folder = worker.transcript_of(str(w1), "sess-1")
    folder.parent.mkdir(parents=True)

    def say(*parts, stop=None):
        return json.dumps({"type": "assistant", "timestamp": "2026-11-08T10:00:00Z", "message": {"stop_reason": stop, "content": list(parts)}})

    folder.write_text("\n".join([
        json.dumps({"type": "user", "message": {"content": "/todo-loop 1"}}),
        say({"type": "tool_use", "name": "Agent", "input": {"description": "Review WP 1/2", "subagent_type": "todo-reviewer"}}),
        say({"type": "text", "text": "The review   is\nrunning."}),
    ]) + "\n")
    agents = folder.with_suffix("") / "subagents"
    agents.mkdir(parents=True)
    (agents / "agent-x.meta.json").write_text(json.dumps({"agentType": "todo-reviewer", "description": "Review WP 1/2", "model": "opus"}))
    (agents / "agent-x.jsonl").write_text(say({"type": "tool_use", "name": "Bash", "input": {"command": "ctest", "description": "Run the tests"}}) + "\n")
    (agents / "agent-y.meta.json").write_text(json.dumps({"agentType": "todo-developer", "description": "WP 1/2"}))
    (agents / "agent-y.jsonl").write_text(say({"type": "tool_use", "name": "SubagentHandback", "input": {"message": "WP 1/2 done"}}) + "\n")
    capsys.readouterr()
    assert worker.main(["list", "--json", "--activity", "--claude", str(script)]) == 0
    activity = json.loads(capsys.readouterr().out)["workers"][0]["activity"]
    assert [(e["kind"], e.get("tool"), e["text"]) for e in activity["events"]] == [("tool", "Agent", "Review WP 1/2"), ("say", None, "The review is running.")]
    running, done = activity["agents"]
    assert (running["type"], running["done"], running["last"][-1]["text"]) == ("todo-reviewer", False, "Run the tests")
    assert (done["type"], done["done"], done["last"][-1]["text"]) == ("todo-developer", True, "WP 1/2 done")


def test_list_json_usage_counts_each_answer_once_over_the_session_and_its_agents(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    record = session(w1, "working", "a")
    record["sessionId"] = "sess-1"
    sessions.write_text(json.dumps([record]))
    main = worker.transcript_of(str(w1), "sess-1")
    main.parent.mkdir(parents=True)

    def answer(ident, model, inp, write, read, out, when="2020-01-01T00:00:00Z"):
        usage = {"input_tokens": inp, "cache_creation_input_tokens": write, "cache_read_input_tokens": read, "output_tokens": out}
        return json.dumps({"type": "assistant", "timestamp": when, "message": {"id": ident, "model": model, "usage": usage, "content": []}})

    # one answer written twice (two content blocks), the second with the final output
    main.write_text("\n".join([answer("m1", "claude-sonnet", 1, 100, 0, 5), answer("m1", "claude-sonnet", 1, 100, 0, 7), answer("m2", "claude-sonnet", 2, 10, 100, 3)]) + "\n")
    agents = main.with_suffix("") / "subagents"
    agents.mkdir(parents=True)
    (agents / "agent-x.jsonl").write_text(answer("a1", "claude-opus", 1, 50, 0, 20, when=datetime.now(timezone.utc).isoformat()) + "\n")
    capsys.readouterr()
    assert worker.main(["list", "--json", "--usage", "--claude", str(script)]) == 0
    state = json.loads(capsys.readouterr().out)
    usage = state["workers"][0]["usage"]
    assert usage["total"] == {"input": 4, "cacheWrite": 160, "cacheRead": 100, "output": 30, "total": 294}
    assert usage["session"]["total"] == 223 and usage["agents"]["total"] == 71
    assert {model: counts["total"] for model, counts in usage["models"].items()} == {"claude-sonnet": 223, "claude-opus": 71}
    assert usage["recent"]["total"] == 71  # only the agent's answer is within the last minutes
    assert usage["context"] == 112 and usage["contextModel"] == "claude-sonnet"  # the session's last answer: 2 + 10 + 100
    assert state["usage"]["total"]["total"] == 294


RULES = """# The todo areas

## Areas

| Area | `<area>` (todo store) | `<R>` (git repository) | `<base>` | Vikunja project (the service's) |
|------|-----------------------|------------------------|----------|---------------------------------|
| game | `mahjong` | `.` | `main` | `BagiEngine-Madjong` |
| engine | `engine` | `engine` | `games/mahjong` | `BagiEngine` |

`<engine>` is `engine`.

## Workers

| not | `areas` | here |
"""


def test_rule_areas_reads_the_areas_table(tmp_path: Path):
    assert worker.rule_areas(tmp_path) is None
    rules = tmp_path / worker.RULES_FILE
    rules.parent.mkdir(parents=True)
    rules.write_text(RULES)
    assert worker.rule_areas(tmp_path) == ["mahjong", "engine"]
    rules.write_text("# No table\n")
    assert worker.rule_areas(tmp_path) is None


def test_list_json_store_keeps_only_the_areas_of_the_rules_file(game: Path, fake_claude, monkeypatch, capsys):
    script, _log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    rules = game / worker.RULES_FILE
    rules.parent.mkdir(parents=True)
    rules.write_text(RULES)
    store = [
        {"id": "mahjong-x", "area": "mahjong", "status": "review", "branch": "todo/mahjong-x"},
        {"id": "eng-y", "area": "engine", "status": "review", "branch": "todo/eng-y"},
        {"id": "chess-z", "area": "chess", "status": "review", "branch": "todo/chess-z"},
        {"id": "chess-w", "area": "chess", "status": "in progress", "branch": "todo/chess-w"},
    ]
    real_run = subprocess.run

    def fake_run(command, *args, **kwargs):
        if any(str(part).endswith("todo.py") for part in command):
            return subprocess.CompletedProcess(command, 0, json.dumps(store), "")
        return real_run(command, *args, **kwargs)

    monkeypatch.setattr(worker.subprocess, "run", fake_run)
    capsys.readouterr()
    assert worker.main(["list", "--json", "--store", "--claude", str(script)]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["areas"] == ["mahjong", "engine"]
    assert [entry["id"] for entry in data["review"]] == ["mahjong-x", "eng-y"]


# --- merge: the quick merge, against a fake todo.py ------------------------------------------------

MERGE_RULES = """# The todo areas

## Areas

| Area | `<area>` (todo store) | `<R>` (git repository) | `<base>` | Vikunja project (the service's) |
|------|-----------------------|------------------------|----------|---------------------------------|
| game | `mahjong` | `.` | `main` | `G` |
| engine | `engine` | `engine` | `games/chess` (the `branch` of `.gitmodules`) | `E` |
"""


@pytest.fixture
def store(game: Path, monkeypatch):
    """The entries of a fake todo store; the calls of todo.py are recorded in `calls`."""
    rules = game / worker.RULES_FILE
    rules.parent.mkdir(parents=True, exist_ok=True)
    rules.write_text(MERGE_RULES)
    run("add", ".claude/rules", cwd=game)
    run("commit", "-q", "-m", "rules", cwd=game)
    run("switch", "-q", "games/chess", cwd=game / "engine")
    entries: dict[str, dict] = {}
    calls: list[list[str]] = []
    real_run = subprocess.run

    def fake_run(command, *args, **kwargs):
        if any(str(part).endswith("todo.py") for part in command):
            rest = [str(part) for part in command[command.index("--actor") + 2 :]]
            calls.append(rest)
            if rest[0] == "show":
                if rest[1] not in entries:
                    return subprocess.CompletedProcess(command, 2, "", "404 not found")
                return subprocess.CompletedProcess(command, 0, json.dumps(entries[rest[1]]), "")
            return subprocess.CompletedProcess(command, 0, "", "")
        return real_run(command, *args, **kwargs)

    monkeypatch.setattr(worker.subprocess, "run", fake_run)
    return entries, calls


def branch_with(repo: Path, branch: str, base: str, name: str, text: str) -> None:
    run("switch", "-q", "-c", branch, base, cwd=repo)
    (repo / name).write_text(text)
    run("add", name, cwd=repo)
    run("commit", "-q", "-m", f"{branch}: {name}", cwd=repo)
    run("switch", "-q", base, cwd=repo)


def review(entry_id: str, area: str, branch: str, note: str = "done") -> dict:
    return {"id": entry_id, "area": area, "title": f"Title of {entry_id}", "status": "review", "statusNote": note, "branch": branch}


def test_merge_dry_run_says_what_it_would_do(game: Path, store, capsys):
    entries, _calls = store
    branch_with(game, "todo/g-x", "main", "x.txt", "x\n")
    entries["g-x"] = review("g-x", "mahjong", "todo/g-x")
    capsys.readouterr()
    assert worker.main(["merge", "g-x", "--dry-run", "--json"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert (plan["base"], plan["conflicts"], plan["problems"], plan["moved"], plan["pointer"], plan["reopen"]) == ("main", [], [], False, False, False)
    assert plan["files"] == ["x.txt"] and len(plan["commits"]) == 1
    assert "todo/g-x" in run("branch", "--list", "todo/g-x", cwd=game)


def test_merge_of_a_game_branch_held_by_a_worker(game: Path, store, capsys):
    entries, calls = store
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    run("switch", "-q", "-c", "todo/g-x", cwd=w1)
    (w1 / "x.txt").write_text("x\n")
    run("add", "x.txt", cwd=w1)
    run("commit", "-q", "-m", "x", cwd=w1)
    entries["g-x"] = review("g-x", "mahjong", "todo/g-x")
    capsys.readouterr()
    assert worker.main(["merge", "g-x", "--json"]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["status"] == "closed" and result["deleted"] and result["detached"] == str(w1)
    assert ["close", "g-x"] in calls
    assert (game / "x.txt").read_text() == "x\n"
    assert run("log", "-1", "--format=%s", cwd=game).strip() == "Merge branch 'todo/g-x'"
    assert run("branch", "--list", "todo/g-x", cwd=game) == ""
    assert "todo/g-x" not in run("status", "--branch", "--porcelain", cwd=w1)


def test_merge_of_an_engine_slice_reopens_it_and_moves_the_pointer(game: Path, store, capsys):
    entries, calls = store
    branch_with(game / "engine", "todo/e-y", "games/chess", "y.txt", "y\n")
    entries["e-y"] = review("e-y", "engine", "todo/e-y", note=f"half of it, slice 1 of 2, {worker.SLICE_REOPEN}")
    capsys.readouterr()
    assert worker.main(["merge", "e-y", "--json"]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["status"] == "open" and result["pointer"]
    assert ["close", "e-y", "--reopen"] in calls
    assert run("log", "-1", "--format=%s", cwd=game).strip() == "Update engine: Title of e-y"
    assert run("status", "--porcelain", cwd=game) == ""
    assert (game / "engine" / "y.txt").exists()


def test_merge_refuses_conflicts_and_changes_nothing(game: Path, store, capsys):
    entries, calls = store
    branch_with(game, "todo/g-x", "main", "same.txt", "branch\n")
    (game / "same.txt").write_text("main\n")
    run("add", "same.txt", cwd=game)
    run("commit", "-q", "-m", "main", cwd=game)
    entries["g-x"] = review("g-x", "mahjong", "todo/g-x")
    head = run("rev-parse", "HEAD", cwd=game)
    capsys.readouterr()
    assert worker.main(["merge", "g-x", "--dry-run", "--json"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["conflicts"] == ["same.txt"] and plan["moved"]
    assert worker.main(["merge", "g-x"]) == 1
    assert "conflicts" in capsys.readouterr().err
    assert run("rev-parse", "HEAD", cwd=game) == head and not [c for c in calls if c[0] == "close"]


def test_merge_refuses_another_area_a_dirty_checkout_and_an_entry_not_in_review(game: Path, store, capsys):
    entries, _calls = store
    branch_with(game, "todo/g-x", "main", "x.txt", "x\n")
    entries["c-z"] = review("c-z", "chess", "todo/c-z")
    entries["g-o"] = {**review("g-o", "mahjong", "todo/g-x"), "status": "in progress"}
    entries["g-x"] = review("g-x", "mahjong", "todo/g-x")
    assert worker.main(["merge", "c-z"]) == 1
    assert "not of this checkout" in capsys.readouterr().err
    assert worker.main(["merge", "g-o"]) == 1
    assert "not in review" in capsys.readouterr().err
    (game / "README.md").write_text("changed\n")
    assert worker.main(["merge", "g-x"]) == 1
    assert "uncommitted" in capsys.readouterr().err
    assert "todo/g-x" in run("branch", "--list", "todo/g-x", cwd=game)


def test_merge_refuses_a_branch_of_another_checkout_and_one_cut_from_another_base(game: Path, store, capsys):
    entries, calls = store
    eng = game / "engine"
    branch_with(eng, "games/other", "games/chess", "other.txt", "other game\n")
    branch_with(eng, "todo/e-f", "games/other", "f.txt", "f\n")
    branch_with(eng, "todo/e-ok", "games/chess", "ok.txt", "ok\n")
    entries["e-gone"] = review("e-gone", "engine", "todo/e-gone")
    entries["e-f"] = review("e-f", "engine", "todo/e-f")
    assert worker.main(["merge", "e-gone", "--dry-run"]) == 1
    assert "another checkout or on another machine" in capsys.readouterr().err
    assert worker.main(["merge", "e-f", "--dry-run", "--json"]) == 1
    assert "games/other" in capsys.readouterr().err
    assert worker.main(["merge", "e-f"]) == 1
    assert not [c for c in calls if c[0] == "close"] and "todo/e-f" in run("branch", "--list", "todo/e-f", cwd=eng)
    assert worker.branch_origin(eng, "todo/e-gone", "games/chess") == {"local": False, "foreignBase": None}
    assert worker.branch_origin(eng, "todo/e-f", "games/chess") == {"local": True, "foreignBase": "games/other"}
    assert worker.branch_origin(eng, "todo/e-ok", "games/chess") == {"local": True, "foreignBase": None}
    # the other base merged into this one: its commits are no longer foreign
    run("merge", "-q", "--no-edit", "games/other", cwd=eng)
    assert worker.branch_origin(eng, "todo/e-f", "games/chess") == {"local": True, "foreignBase": None}


def test_list_json_store_marks_the_entries_whose_branch_is_elsewhere(game: Path, store, fake_claude, monkeypatch, capsys):
    script, _log, _sessions = fake_claude
    eng = game / "engine"
    branch_with(eng, "games/other", "games/chess", "other.txt", "other game\n")
    branch_with(eng, "todo/e-f", "games/other", "f.txt", "f\n")
    branch_with(game, "todo/g-x", "main", "x.txt", "x\n")
    listed = [review("g-x", "mahjong", "todo/g-x"), review("e-f", "engine", "todo/e-f"), review("e-gone", "engine", "todo/e-gone"),
              {**review("g-far", "mahjong", "todo/g-far"), "status": "in progress"}]
    real_run = worker.subprocess.run

    def fake_run(command, *args, **kwargs):
        if any(str(part).endswith("todo.py") for part in command) and "list" in command:
            return subprocess.CompletedProcess(command, 0, json.dumps(listed), "")
        return real_run(command, *args, **kwargs)

    monkeypatch.setattr(worker.subprocess, "run", fake_run)
    capsys.readouterr()
    assert worker.main(["list", "--json", "--store", "--claude", str(script)]) == 0
    data = json.loads(capsys.readouterr().out)
    origin = {e["id"]: (e["local"], e["foreignBase"]) for e in data["review"]}
    assert origin == {"g-x": (True, None), "e-f": (True, "games/other"), "e-gone": (False, None)}
    assert data["elsewhere"] == ["e-f", "e-gone", "g-far"]


def test_update_detaches_a_worker_at_the_tips_of_the_bases(game: Path, store, fake_claude, capsys):
    script, _log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    run("switch", "-q", "-c", "todo/e-y", cwd=w1 / "engine")
    branch_with(game / "engine", "todo/e-z", "games/chess", "z.txt", "z\n")
    run("merge", "-q", "--no-ff", "-m", "z", "todo/e-z", cwd=game / "engine")
    run("add", "engine", cwd=game)
    run("commit", "-q", "-m", "Update engine: z", cwd=game)
    capsys.readouterr()
    assert worker.main(["update", "1", "--json", "--claude", str(script)]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["errors"] == [] and result["updated"][0]["notes"] == []
    assert [r["base"] for r in result["updated"][0]["repos"]] == ["main", "games/chess"]
    assert run("rev-parse", "HEAD", cwd=w1) == run("rev-parse", "main", cwd=game)
    assert run("rev-parse", "HEAD", cwd=w1 / "engine") == run("rev-parse", "games/chess", cwd=game / "engine")
    assert (w1 / "engine" / "z.txt").exists()
    assert "todo/e-y" in run("branch", "--list", "todo/e-y", cwd=game / "engine")
    assert run("status", "--porcelain", cwd=w1) == ""


def test_update_refuses_a_running_session_and_uncommitted_changes(game: Path, store, fake_claude, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    sessions.write_text(json.dumps([session(workers / "w1", "working")]))
    (workers / "w2" / "engine" / "wip.txt").write_text("wip\n")
    before = [run("rev-parse", "HEAD", cwd=workers / f"w{k}") for k in (1, 2)]
    run("commit", "-q", "--allow-empty", "-m", "newer", cwd=game)
    capsys.readouterr()
    assert worker.main(["update", "--json", "--claude", str(script)]) == 1
    result = json.loads(capsys.readouterr().out)
    assert result["updated"] == []
    assert any("w1" in e and "runs" in e for e in result["errors"])
    assert any("w2" in e and "uncommitted" in e for e in result["errors"])
    assert [run("rev-parse", "HEAD", cwd=workers / f"w{k}") for k in (1, 2)] == before
