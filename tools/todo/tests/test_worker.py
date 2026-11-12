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
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))  # the transcripts and the stop marks
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
        assert f"args=--bg --name todo-w{k} --effort medium --model opus /todo-loop 3" in call  # --model after -- wins
        assert "jobs=" in call and "jobs= " not in call


def test_start_runs_the_loops_on_a_cheap_model_by_default(game: Path, fake_claude, monkeypatch):
    script, log, _sessions = fake_claude
    monkeypatch.setenv("CLAUDE_EFFORT", "high")
    worker.main(["create", "1", "--no-configure"])
    assert worker.main(["start", "--delay", "0", "--claude", str(script)]) == 0
    assert "args=--bg --name todo-w1 --model sonnet --effort medium /todo-loop 1" in log.read_text()
    assert worker.main(["start", "--delay", "0", "--claude", str(script), "--model", "", "--effort", "low", "--print"]) == 0


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


def test_list_says_stopped_for_a_session_that_stop_ended_and_done_for_one_that_ended_by_itself(game: Path, fake_claude, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    sessions.write_text(json.dumps([session(workers / "w1", "working", "abc", "busy"), session(workers / "w2", "done", "def")]))
    assert worker.main(["stop", "1", "--claude", str(script)]) == 0
    # claude agents lists a stopped session as finished, like one that ended by itself
    sessions.write_text(json.dumps([session(workers / "w1", "done", "abc"), session(workers / "w2", "done", "def")]))
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    rows = json.loads(capsys.readouterr().out)["workers"]
    assert isinstance(rows[0]["stopped"], int) and rows[1]["stopped"] is None
    assert worker.main(["list", "--claude", str(script)]) == 0
    lines = {line.split()[0]: line for line in capsys.readouterr().out.splitlines() if line.startswith("w")}
    assert "stopped by worker.py stop 0 min ago" in lines["w1"] and ": done" not in lines["w1"]
    assert ": done/" in lines["w2"]
    # a session started afterwards has another id: the mark does not follow it
    sessions.write_text(json.dumps([session(workers / "w1", "done", "new")]))
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    assert json.loads(capsys.readouterr().out)["workers"][0]["stopped"] is None


def test_the_stop_marks_keep_only_the_newest(fake_claude, monkeypatch):
    monkeypatch.setattr(worker, "STOPPED_KEEP", 3)
    for k in range(5):
        worker.mark_stopped({"id": f"s{k}"})
    assert sorted(worker.read_stopped()) == ["s2", "s3", "s4"]
    worker.stopped_file().write_text("not json")
    assert worker.read_stopped() == {} and worker.stopped_at({"id": "s4", "state": "done"}) is None


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
    assert "waits for you (input needed): claude attach a" in out
    assert out.count("waits for you") == 1
    sessions.write_text(json.dumps([session(workers / "w1", "blocked", "a"), session(workers / "w2", "done", "b")]))
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    first, second = json.loads(capsys.readouterr().out)["workers"]
    assert first["waiting"] and first["active"] and not second["waiting"]


def test_attach_and_logs_name_the_newest_session_of_the_worker_by_its_id(game: Path, fake_claude, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    older = session(workers / "w1", "done", "old")
    newer = dict(session(workers / "w1", "working", "new", status="waiting"), startedAt=5)
    sessions.write_text(json.dumps([older, newer, session(workers / "w2", "working", "other")]))
    capsys.readouterr()
    assert worker.main(["attach", "1", "--print", "--claude", str(script)]) == 0
    assert capsys.readouterr().out.strip() == f"{script} attach new"
    assert worker.main(["logs", "2", "--print", "--claude", str(script)]) == 0
    assert capsys.readouterr().out.strip() == f"{script} logs other"
    sessions.write_text("[]")
    assert worker.main(["attach", "1", "--print", "--claude", str(script)]) == 1
    assert "w1: no session" in capsys.readouterr().err


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
        json.dumps({"type": "permission-mode", "permissionMode": "auto"}),
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
    assert activity["updated"] == 1794132000000  # the last timestamped record, not the mtime the trailing record moved
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


def stored(entry: dict, base: str | None, origin: str | None = None) -> dict:
    """The entry as the store holds it after `claim --base` (`base` and `origin` of the todo store)."""
    return {**entry, "base": base, "origin": origin}


def test_branch_origin_trusts_the_stored_base_and_keeps_the_guess_for_old_entries(game: Path, store):
    eng = game / "engine"
    branch_with(eng, "games/other", "games/chess", "other.txt", "other game\n")
    branch_with(eng, "todo/e-f", "games/other", "f.txt", "f\n")
    branch_with(eng, "todo/e-n", "games/chess", "n.txt", "n\n")
    # (a) stored base is the area's: sharing commits with another base is no reason to call it foreign
    assert worker.branch_origin(eng, "todo/e-f", "games/chess", "games/chess") == {"local": True, "foreignBase": None}
    # (b) stored base the repository does not know (never fetched): foreign all the same, with no guess
    assert worker.branch_origin(eng, "todo/e-n", "games/chess", "games/never-fetched") == {"local": True, "foreignBase": "games/never-fetched"}
    # (c) no stored base: the commit guess, as before
    assert worker.branch_origin(eng, "todo/e-f", "games/chess") == {"local": True, "foreignBase": "games/other"}
    assert worker.branch_origin(eng, "todo/e-f", "games/chess", None) == {"local": True, "foreignBase": "games/other"}
    assert worker.branch_origin(eng, "todo/e-n", "games/chess") == {"local": True, "foreignBase": None}
    # (d) not local: whatever is stored
    assert worker.branch_origin(eng, "todo/e-gone", "games/chess", "games/other") == {"local": False, "foreignBase": None}
    assert worker.branch_origin(eng, "todo/e-gone", "games/chess", "games/chess") == {"local": False, "foreignBase": None}


def test_merge_plan_decides_from_the_stored_base(game: Path, store, capsys):
    entries, calls = store
    eng = game / "engine"
    branch_with(eng, "games/other", "games/chess", "other.txt", "other game\n")
    branch_with(eng, "todo/e-f", "games/other", "f.txt", "f\n")
    branch_with(eng, "todo/e-n", "games/chess", "n.txt", "n\n")
    # stored foreign base: refused with the base and the origin, though the repository knows nothing of it
    entries["e-n"] = stored(review("e-n", "engine", "todo/e-n"), "games/never-fetched", "Other@host")
    assert worker.main(["merge", "e-n", "--dry-run", "--json"]) == 1
    error = capsys.readouterr().err
    assert "todo/e-n was cut from games/never-fetched in Other@host, not from games/chess" in error
    # no origin stored: the message still names the base
    entries["e-n"] = stored(review("e-n", "engine", "todo/e-n"), "games/never-fetched")
    assert worker.main(["merge", "e-n", "--dry-run"]) == 1
    assert "cut from games/never-fetched in another checkout, not from games/chess" in capsys.readouterr().err
    # stored base equal to the area's: planned, though the branch holds commits of games/other too
    entries["e-f"] = stored(review("e-f", "engine", "todo/e-f"), "games/chess", "Game@host")
    assert worker.main(["merge", "e-f", "--dry-run", "--json"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["branch"] == "todo/e-f" and plan["base"] == "games/chess" and plan["problems"] == []
    # no stored base: the commit guess refuses it, as before
    entries["e-f"] = review("e-f", "engine", "todo/e-f")
    assert worker.main(["merge", "e-f", "--dry-run"]) == 1
    assert "games/other" in capsys.readouterr().err
    assert not [c for c in calls if c[0] == "close"]


def test_list_json_store_rows_carry_the_stored_base_and_origin(game: Path, store, fake_claude, monkeypatch, capsys):
    script, _log, _sessions = fake_claude
    eng = game / "engine"
    branch_with(eng, "games/other", "games/chess", "other.txt", "other game\n")
    branch_with(eng, "todo/e-f", "games/other", "f.txt", "f\n")
    branch_with(eng, "todo/e-n", "games/chess", "n.txt", "n\n")
    branch_with(eng, "todo/e-ok", "games/chess", "ok.txt", "ok\n")
    listed = [
        stored(review("e-n", "engine", "todo/e-n"), "games/never-fetched", "Other@host"),
        stored(review("e-ok", "engine", "todo/e-ok"), "games/chess", "Game@host"),
        review("e-f", "engine", "todo/e-f"),
        stored(review("e-gone", "engine", "todo/e-gone"), "games/other", "Other@host"),
    ]
    real_run = worker.subprocess.run

    def fake_run(command, *args, **kwargs):
        if any(str(part).endswith("todo.py") for part in command) and "list" in command:
            return subprocess.CompletedProcess(command, 0, json.dumps(listed), "")
        return real_run(command, *args, **kwargs)

    monkeypatch.setattr(worker.subprocess, "run", fake_run)
    capsys.readouterr()
    assert worker.main(["list", "--json", "--store", "--claude", str(script)]) == 0
    data = json.loads(capsys.readouterr().out)
    rows = {e["id"]: (e["base"], e["takenBase"], e["takenOrigin"], e["local"], e["foreignBase"]) for e in data["review"]}
    assert rows == {
        "e-n": ("games/chess", "games/never-fetched", "Other@host", True, "games/never-fetched"),
        "e-ok": ("games/chess", "games/chess", "Game@host", True, None),
        "e-f": ("games/chess", None, None, True, "games/other"),
        "e-gone": ("games/chess", "games/other", "Other@host", False, None),
    }
    assert data["elsewhere"] == ["e-n", "e-f", "e-gone"]


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


def test_start_moves_an_idle_worker_to_the_tips_of_the_bases_first(game: Path, store, fake_claude, capsys):
    script, log, _sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    workers = game.parent / "game.workers"
    run("commit", "-q", "--allow-empty", "-m", "newer game", cwd=game)
    (workers / "w2" / "engine" / "wip.txt").write_text("wip\n")
    w2_before = run("rev-parse", "HEAD", cwd=workers / "w2")
    capsys.readouterr()
    assert worker.main(["start", "--delay", "0", "--claude", str(script)]) == 0
    out = capsys.readouterr().out
    assert run("rev-parse", "HEAD", cwd=workers / "w1") == run("rev-parse", "main", cwd=game)
    assert "w1: moved to the base: game at main" in out
    assert run("rev-parse", "HEAD", cwd=workers / "w2") == w2_before
    assert "w2: not moved to the base" in out and "uncommitted" in out
    assert len(log.read_text().splitlines()) == 2


def test_start_print_moves_nothing(game: Path, store, fake_claude):
    script, log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    before = run("rev-parse", "HEAD", cwd=w1)
    run("commit", "-q", "--allow-empty", "-m", "newer game", cwd=game)
    assert worker.main(["start", "--delay", "0", "--claude", str(script), "--print"]) == 0
    assert run("rev-parse", "HEAD", cwd=w1) == before


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


# --- the usage limit and relaunch ---------------------------------------------------------------------------


def limited_session(w: Path, sessions: Path, state: str = "blocked", status: str = "idle", limited: bool = True) -> None:
    record = session(w, state, "e3cc", status)
    record["sessionId"] = "sess-limit"
    sessions.write_text(json.dumps([record]))
    path = worker.transcript_of(str(w), "sess-limit")
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps({"type": "assistant", "message": {"content": [{"type": "text", "text": "Starting the reviewer."}]}})]
    if limited:
        lines.append(json.dumps({"type": "assistant", "timestamp": "2026-11-09T19:40:00Z", "error": "rate_limit", "isApiErrorMessage": True, "apiErrorStatus": 429,
                                 "message": {"content": [{"type": "text", "text": "You've hit your 5-hour limit · resets 10:30pm (Europe/Belgrade)"}]}}))
    path.write_text("\n".join(lines) + "\n")


def test_list_says_when_the_usage_limit_stopped_a_session(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    limited_session(game.parent / "game.workers" / "w1", sessions)
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    row = json.loads(capsys.readouterr().out)["workers"][0]
    assert row["limited"]["resets"] == "10:30pm (Europe/Belgrade)" and "5-hour limit" in row["limited"]["text"]
    assert not row["waiting"]  # blocked by the limit is no question for the person
    assert worker.main(["list", "--claude", str(script)]) == 0
    assert "stopped by the usage limit, resets 10:30pm (Europe/Belgrade): worker.py relaunch" in capsys.readouterr().out
    limited_session(game.parent / "game.workers" / "w1", sessions, limited=False)
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    assert json.loads(capsys.readouterr().out)["workers"][0]["limited"] is None


def test_relaunch_resumes_the_limited_session_and_leaves_the_others(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    workers = game.parent / "game.workers"
    limited_session(workers / "w1", sessions)
    capsys.readouterr()
    assert worker.main(["relaunch", "--json", "--claude", str(script)]) == 0
    result = json.loads(capsys.readouterr().out)
    assert [r["name"] for r in result["relaunched"]] == ["w1"] and result["errors"] == []
    calls = log.read_text().splitlines()
    assert calls[0] == "stop e3cc"  # still listed as running (blocked): stopped before the resume
    assert f"/game.workers/w1 jobs=" in calls[1] and "args=--bg --resume sess-limit --name todo-w1 The usage limit has reset." in calls[1]
    assert len(calls) == 2  # w2 has no session and was not named


def test_relaunch_of_a_named_worker_refuses_a_busy_session_and_one_that_is_missing(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, log, sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    limited_session(game.parent / "game.workers" / "w1", sessions, state="working", status="busy", limited=False)
    capsys.readouterr()
    assert worker.main(["relaunch", "1", "2", "--json", "--claude", str(script)]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["relaunched"] == [] and any("busy" in e for e in result["errors"]) and any("w2: no session" in e for e in result["errors"])
    assert not log.exists()
    assert worker.main(["relaunch", "1", "--claude", str(script)]) == 1
    limited_session(game.parent / "game.workers" / "w1", sessions, state="done", limited=False)
    assert worker.main(["relaunch", "1", "--claude", str(script), "--prompt", "go on"]) == 0
    assert log.read_text().splitlines()[-1].endswith("args=--bg --resume sess-limit --name todo-w1 go on")  # done: no stop


# --- a run that ended its turn ------------------------------------------------------------------------------


def blocked_session(w: Path, sessions: Path, last: dict) -> None:
    record = session(w, "blocked", "ended1")
    record["sessionId"] = "sess-ended"
    sessions.write_text(json.dumps([record]))
    path = worker.transcript_of(str(w), "sess-ended")
    path.parent.mkdir(parents=True, exist_ok=True)
    first = {"type": "assistant", "message": {"content": [{"type": "text", "text": "Starting the reviewer."}], "stop_reason": "tool_use"}}
    path.write_text("\n".join(json.dumps(r) for r in (first, {"type": "user", "message": {"content": "ok"}}, last)) + "\n")


ENDED = {"type": "assistant", "message": {"content": [{"type": "text", "text": "Report. Run /todo-merge?"}], "stop_reason": "end_turn"}}
ASKING = {"type": "assistant", "message": {"content": [{"type": "tool_use", "name": "Bash", "input": {"command": "rm x"}}], "stop_reason": "tool_use"}}


def test_a_run_that_ended_its_turn_waits_for_nobody(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    w1 = game.parent / "game.workers" / "w1"
    blocked_session(w1, sessions, ENDED)
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    row = json.loads(capsys.readouterr().out)["workers"][0]
    assert row["endedTurn"] and not row["waiting"] and not row["active"]
    assert worker.main(["list", "--claude", str(script)]) == 0
    out = capsys.readouterr().out
    assert "its run ended" in out and "waits for you" not in out
    # start stops the ended session and starts the next loop
    assert worker.main(["start", "--delay", "0", "--claude", str(script)]) == 0
    calls = log.read_text().splitlines()
    assert "stop ended1" in calls and any("--bg --name todo-w1" in c for c in calls)


def test_a_run_with_a_pending_tool_call_still_waits(game: Path, fake_claude, tmp_path, monkeypatch, capsys):
    script, _log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    blocked_session(game.parent / "game.workers" / "w1", sessions, ASKING)
    capsys.readouterr()
    assert worker.main(["list", "--json", "--claude", str(script)]) == 0
    row = json.loads(capsys.readouterr().out)["workers"][0]
    assert row["waiting"] and row["active"] and not row["endedTurn"]


# --- the panel's checks ---------------------------------------------------------------------------------------


def commit_file(repo: Path, name: str, text: str, message: str) -> None:
    (repo / name).write_text(text)
    run("add", name, cwd=repo)
    run("commit", "-q", "-m", message, cwd=repo)


def test_review_checks_name_conflicts_moved_base_risky_files_and_each_other(game: Path):
    commit_file(game, "a.txt", "one\n", "a")
    commit_file(game, "b.txt", "one\n", "b")
    for branch, text in (("todo/x", "x\n"), ("todo/y", "y\n")):
        run("switch", "-q", "-c", branch, "main", cwd=game)
        commit_file(game, "a.txt", text, branch)
    run("switch", "-q", "-c", "todo/z", "main", cwd=game)
    commit_file(game, "b.txt", "z\n", "z")
    run("switch", "-q", "main", cwd=game)
    commit_file(game, "b.txt", "main\nmore\n", "main moves b")
    entries = [
        {"id": i, "status": "review", "local": True, "foreignBase": None, "repo": str(game), "base": "main", "branch": f"todo/{i}"}
        for i in ("x", "y", "z")
    ]
    worker.review_checks(entries)
    x, y, z = entries
    assert x["conflicts"] == [] and x["behind"]["count"] == 1 and x["riskFiles"] == []
    assert [c["id"] for c in x["conflictsWith"]] == ["y"] and x["conflictsWith"][0]["files"] == ["a.txt"]
    assert [c["id"] for c in y["conflictsWith"]] == ["x"]
    assert z["conflicts"] == ["b.txt"] and z["riskFiles"] == ["b.txt"] and z["conflictsWith"] == []


def test_pending_ask_names_the_tool_call_without_a_result(game: Path, tmp_path, monkeypatch):
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    record = {"cwd": str(game), "sessionId": "sess-ask"}
    path = worker.transcript_of(str(game), "sess-ask")
    path.parent.mkdir(parents=True, exist_ok=True)
    done = {"type": "assistant", "message": {"content": [{"type": "tool_use", "id": "t1", "name": "Bash", "input": {"command": "ls"}}]}}
    result = {"type": "user", "message": {"content": [{"type": "tool_result", "tool_use_id": "t1"}]}}
    ask = {"type": "assistant", "message": {"content": [{"type": "tool_use", "id": "t2", "name": "AskUserQuestion", "input": {"questions": [{"question": "Merge it?"}]}}]}}
    path.write_text("\n".join(json.dumps(r) for r in (done, result, ask)) + "\n")
    assert worker.pending_ask(record) == {"tool": "AskUserQuestion", "text": "Merge it?"}
    path.write_text("\n".join(json.dumps(r) for r in (done, result)) + "\n")
    assert worker.pending_ask(record) is None


def test_queue_keeps_an_entry_in_one_worker_and_pops_in_order(game: Path, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    worker.main(["create", "2", "--no-configure"])
    assert worker.main(["queue", "1", "--add", "a"]) == 0
    assert worker.main(["queue", "1", "--add", "b"]) == 0
    assert worker.main(["queue", "2", "--add", "a"]) == 0  # moves from w1 to w2
    assert worker.main(["queue", "1", "--add", "c", "--first"]) == 0
    capsys.readouterr()
    assert worker.main(["queue", "--json"]) == 0
    assert json.loads(capsys.readouterr().out) == {"w1": ["c", "b"], "w2": ["a"]}
    assert worker.main(["queue", "1", "--pop", "--json"]) == 0
    assert json.loads(capsys.readouterr().out) == {"popped": "c"}
    assert worker.main(["queue", "3", "--add", "x"]) == 1  # no such worker
    assert worker.main(["queue", "1", "--add", "no spaces"]) == 1


def test_paths_and_plan_files_meet_by_folder():
    body = "**Where:** `src/Mahjong/Table/TableLayout.{h,cpp}`, `config/nodes/*.xml`, the `--jobs` flag, `README.md`"
    assert worker.paths_in(body) == {"src/Mahjong/Table/TableLayout", "config/nodes/"}
    assert worker.overlapping(worker.paths_in(body), {"src/Mahjong/Table/TableLayout.cpp"}) == ["src/Mahjong/Table/TableLayout"]
    plan = "# x\n\n## Files\n- config/nodes/MahjongHud.xml\n- tests/X.cpp\n\n## Slice\n- not/a/file\n"
    assert worker.plan_files(plan) == {"config/nodes/MahjongHud.xml", "tests/X.cpp"}
    assert worker.overlapping({"config/nodes/"}, {"config/nodes/MahjongHud.xml", "tests/X.cpp"}) == ["config/nodes/"]
    assert worker.overlapping({"src/a.cpp"}, {"src/ab.cpp"}) == []


def test_build_info_reads_the_product_and_tests_of_the_rules(game: Path, capsys):
    rules = game / ".claude" / "rules"
    rules.mkdir(parents=True)
    (rules / "build-project.md").write_text("| Field | Meaning | Value here |\n|---|---|---|\n| Product | the exe | `BagiGame` |\n| Tests | gtests | `GameTests`, `MoreTests` |\n")
    capsys.readouterr()
    assert worker.main(["build-info", "--json", "--preset", "p1"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["product"] == "BagiGame" and info["tests"] == ["GameTests", "MoreTests"] and info["exe"] == "build/p1/bin/BagiGame" and info["shots"] is None


def test_pointer_state_says_when_the_engine_checkout_moved(game: Path):
    layout = worker.find_layout(game)
    before = worker.pointer_state(layout)
    assert before is not None and not before["moved"]
    commit_file(game / "engine", "new.txt", "n\n", "engine moves")
    after = worker.pointer_state(layout)
    assert after["moved"] and after["checkout"] != after["recorded"]


def test_terms_name_entries_by_area_id_and_word_prefix():
    entry = {"area": "engine", "id": "ui-themes", "title": "UI colours are literals"}
    assert worker.term_matches("engine", entry) and worker.term_matches("ui", entry) and worker.term_matches("colour", entry)
    assert not worker.term_matches("ui", {"area": "mahjong", "id": "mahjong-build-paths", "title": "Build folder"})
    assert worker.term_matches("ui", {"area": "mahjong", "id": "mahjong-ui-skin", "title": "x"})
    assert worker.preference_rank(["seat", "ui"], entry) == 1 and worker.preference_rank(["seat"], entry) is None


def test_prefs_are_saved_by_root_and_replaced_per_field(game: Path, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    worker.main(["create", "1", "--no-configure"])
    assert worker.main(["prefs", "--prefer", "ui, seat", "--avoid", "editor"]) == 0
    assert worker.main(["prefs", "--avoid", ""]) == 0
    capsys.readouterr()
    assert worker.main(["prefs", "--json"]) == 0
    shown = json.loads(capsys.readouterr().out)
    assert shown["prefer"] == ["ui", "seat"] and shown["avoid"] == []
    assert worker.main(["prefs", "--clear"]) == 0


def test_a_cached_verdict_lives_while_the_version_holds():
    now = 1_000_000.0
    eligible = {"verdict": "eligible", "reason": "x", "version": 3, "at": now - 60}
    assert worker.fresh_verdict(eligible, 3, now) is eligible
    assert worker.fresh_verdict(eligible, 4, now) is None  # the entry was edited
    assert worker.fresh_verdict({**eligible, "at": now - worker.VERDICT_TTL_S - 1}, 3, now) is None
    assert worker.fresh_verdict({"verdict": "skip", "reason": "blocked by x", "version": 3, "at": now}, 3, now) is None
    engine_wait = {"verdict": "skip", "reason": "needs engine change: y", "version": 3, "at": now - worker.VERDICT_SHORT_TTL_S - 1}
    assert worker.fresh_verdict(engine_wait, 3, now) is None


def test_dispatch_orders_by_preference_and_asks_only_for_the_missing_verdicts(game: Path, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude-config"))
    worker.main(["create", "1", "--no-configure"])
    entries = [
        {"id": "a-big", "area": "game", "title": "A", "status": "open", "version": 1, "blocks": [{"id": "x", "status": "open"}], "blockedBy": []},
        {"id": "ui-one", "area": "game", "title": "UI one", "status": "open", "version": 1, "blocks": [], "blockedBy": []},
        {"id": "ui-two", "area": "game", "title": "UI two", "status": "open", "version": 2, "blocks": [], "blockedBy": []},
        {"id": "held", "area": "game", "title": "H", "status": "open", "version": 1, "blocks": [], "blockedBy": [{"id": "z", "status": "open"}]},
        {"id": "editor-x", "area": "game", "title": "E", "status": "open", "version": 1, "blocks": [], "blockedBy": []},
    ]

    def fake_cli(_layout, *args):
        if args[0] == "list":
            return json.dumps(entries)
        raise worker.WorkerError("no plan")

    monkeypatch.setattr(worker, "todo_cli", fake_cli)
    monkeypatch.setattr(worker, "list_sessions", lambda _claude: [])
    answer = tmp_path / "answer.json"
    answer.write_text('text {"entries": [{"id": "ui-two", "verdict": "eligible", "size": "S", "touches": ["a.cpp"], "reason": "r"}]} text')
    assert worker.main(["dispatch", "--record", str(answer)]) == 0
    capsys.readouterr()
    assert worker.main(["dispatch", "--json", "--depth", "1", "--prefer", "ui", "--avoid", "editor"]) == 0
    state = json.loads(capsys.readouterr().out)
    assert [c["id"] for c in state["candidates"]] == ["ui-one", "ui-two", "a-big"]
    assert state["candidates"][1]["verdict"]["verdict"] == "eligible"
    assert state["skipped"]["avoided"] == ["editor-x"] and state["skipped"]["blocked"] == [{"id": "held", "by": ["z"]}]
    assert state["slots"] == 1 and state["classify"] == ["ui-one"]  # one cached eligible + one to classify fill 2 x 1 slot


def test_configure_shares_ccache_between_the_workers(game: Path, monkeypatch):
    layout = worker.find_layout(game)
    calls = []
    monkeypatch.setattr(worker.subprocess, "run", lambda command, **kw: calls.append(command) or subprocess.CompletedProcess(command, 0, "", ""))
    worker.configure(layout, layout.worker(1), "macos-debug")
    assert calls == [["cmake", "--preset", "macos-debug", f"-DENGINE_CCACHE_BASE_DIR={layout.root}"]]


def test_start_queued_only_takes_the_head_of_each_queue_and_skips_an_empty_one(game: Path, fake_claude, capsys):
    script, log, _sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    assert worker.main(["queue", "1", "--add", "big-entry"]) == 0
    assert worker.main(["queue", "1", "--add", "next-one"]) == 0
    assert worker.main(["start", "--delay", "0", "--queued-only", "--claude", str(script), "--report-to", "boss"]) == 0
    calls = log.read_text().splitlines()
    assert len(calls) == 1 and "--name todo-w1" in calls[0] and calls[0].endswith("/todo-loop 1 big-entry report-to=boss")
    assert "w2: its queue is empty, not started" in capsys.readouterr().out
    capsys.readouterr()
    worker.main(["queue", "--json"])
    assert json.loads(capsys.readouterr().out) == {"w1": ["next-one"], "w2": []}


def test_start_wait_free_starts_once_the_session_ends(game: Path, fake_claude, monkeypatch):
    script, log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    w1 = game.parent / "game.workers" / "w1"
    sessions.write_text(json.dumps([session(w1, "working", "a")]))
    waits = []

    def end_the_session(seconds):
        waits.append(seconds)
        sessions.write_text(json.dumps([session(w1, "done", "a")]))

    monkeypatch.setattr(worker.time, "sleep", end_the_session)
    assert worker.main(["start", "1", "--delay", "0", "--wait-free", "600", "--claude", str(script)]) == 0
    assert len(waits) == 1 and "--name todo-w1" in log.read_text()


def test_start_wait_free_gives_up_after_its_time(game: Path, fake_claude, monkeypatch):
    script, log, sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    sessions.write_text(json.dumps([session(game.parent / "game.workers" / "w1", "working", "a")]))
    clock = [1000.0]
    monkeypatch.setattr(worker.time, "time", lambda: clock[0])
    monkeypatch.setattr(worker.time, "sleep", lambda s: clock.__setitem__(0, clock[0] + s))
    assert worker.main(["start", "1", "--delay", "0", "--wait-free", "30", "--claude", str(script)]) == 1
    assert not log.exists() or "todo-w1" not in log.read_text()


def test_start_detach_runs_the_same_start_in_a_process_of_its_own(game: Path, fake_claude, monkeypatch, capsys):
    script, _log, _sessions = fake_claude
    worker.main(["create", "1", "--no-configure"])
    started = []
    real_popen = subprocess.Popen

    class Detached:
        pid = 4242

    def fake_popen(command, *args, **kwargs):
        if not kwargs.get("start_new_session"):
            return real_popen(command, *args, **kwargs)  # git and claude, through subprocess.run
        started.append((command, kwargs))
        return Detached()

    monkeypatch.setattr(worker.subprocess, "Popen", fake_popen)
    assert worker.main(["start", "1", "--detach", "--queued-only", "--wait-free", "900", "--claude", str(script)]) == 0
    command, kwargs = started[0]
    assert command[1].endswith("worker.py") and "--detach" not in command
    assert command[2:] == ["start", "1", "--queued-only", "--wait-free", "900", "--claude", str(script)]
    assert kwargs["start_new_session"] is True
    assert "detached: pid 4242" in capsys.readouterr().out


def test_dollars_take_the_first_price_key_in_the_model_id():
    models = {"claude-opus-5-5": {"input": 1_000_000, "output": 100_000}, "claude-haiku-5-5": {"cacheRead": 1_000_000}, "mystery": {"output": 5}}
    assert worker.dollars_of(models, worker.DEFAULT_PRICES) == 6.01  # 4 + 2 + 0.01


def test_remarks_store_a_plan_reopen_the_work_and_queue_it_on_the_worker_that_holds_the_branch(game: Path, store, fake_claude, tmp_path, capsys):
    entries, calls = store
    script, log, _sessions = fake_claude
    worker.main(["create", "2", "--no-configure"])
    w2 = game.parent / "game.workers" / "w2"
    run("switch", "-q", "-c", "todo/g-x", cwd=w2)
    entries["g-x"] = review("g-x", "mahjong", "todo/g-x")
    remarks = tmp_path / "remarks.md"
    remarks.write_text("The button is too small.\n")
    capsys.readouterr()
    assert worker.main(["remarks", "g-x", "--file", str(remarks), "--claude", str(script), "--json"]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result == {"entry": "g-x", "plan": "todo-loop-g-x-remarks-1.md", "round": 1, "worker": "w2", "started": False}
    put = next(call for call in calls if call[:2] == ["plan", "put"])
    assert put[2] == "todo-loop-g-x-remarks-1.md" and put[-2:] == ["--kind", "remarks"]
    assert "The button is too small." in Path(put[put.index("--file") + 1]).read_text()
    assert ["edit", "g-x", "--status", "in progress"] in calls
    assert any(call[:2] == ["note", "g-x"] and "round 1" in call[-1] for call in calls)
    capsys.readouterr()
    worker.main(["queue", "--json"])
    assert json.loads(capsys.readouterr().out) == {"w1": [], "w2": ["g-x"]}
    entries["g-y"] = {**review("g-y", "mahjong", "todo/g-y"), "status": "in progress"}
    assert worker.main(["remarks", "g-y", "--file", str(remarks), "--claude", str(script)]) == 1  # not in review
