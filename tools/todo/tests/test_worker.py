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


def session(cwd: Path, state: str, ident: str = "s1") -> dict:
    return {"id": ident, "cwd": str(cwd), "kind": "background", "name": "todo-w1", "status": "idle", "state": state, "startedAt": 0}


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
