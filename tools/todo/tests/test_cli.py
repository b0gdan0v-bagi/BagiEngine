"""todo.py against the service running in a thread (uvicorn, a real port), and its guards."""

import http.server
import json
import socket
import subprocess
import threading
import time

import pytest

import todo
from conftest import FIXTURES, TOKEN


@pytest.fixture
def server(clean_database):
    import uvicorn

    from service.app import Settings, create_app

    app = create_app(Settings(database_url=clean_database, api_token=TOKEN), start_mirror=False)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    server = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=port, log_level="warning"))
    thread = threading.Thread(target=server.run, daemon=True)
    thread.start()
    for _ in range(200):
        if server.started:
            break
        time.sleep(0.02)
    yield f"http://127.0.0.1:{port}"
    server.should_exit = True
    thread.join(timeout=10)


@pytest.fixture
def cli(server, monkeypatch, capsys, tmp_path):
    monkeypatch.setenv("TODO_API_URL", server)
    monkeypatch.setenv("TODO_API_TOKEN", TOKEN)
    monkeypatch.chdir(tmp_path)

    def run(*argv, code=0):
        result = todo.main(list(argv))
        captured = capsys.readouterr()
        assert result == code, captured.err
        return captured.out, captured.err

    run("area", "set", "engine", "--title", "Engine", "--roadmap", str(FIXTURES / "todo" / "roadmap.md"))
    return run


@pytest.fixture
def plans_dir(tmp_path):
    """A plan store outside any git work tree (tmp_path is not one)."""
    folder = tmp_path / "plans"
    folder.mkdir()
    return folder


def test_settings_come_from_the_nearest_env_file(tmp_path, monkeypatch):
    monkeypatch.delenv("TODO_API_URL", raising=False)
    monkeypatch.delenv("TODO_API_TOKEN", raising=False)
    (tmp_path / ".env").write_text("TODO_API_URL=https://todo.example/\nTODO_API_TOKEN='secret'\n")
    nested = tmp_path / "a" / "b"
    nested.mkdir(parents=True)
    monkeypatch.chdir(nested)
    assert todo.load_settings(None) == ("https://todo.example", "secret")
    (tmp_path / ".env").write_text("OTHER=1\n")
    with pytest.raises(todo.TodoError, match="TODO_API_URL, TODO_API_TOKEN not set"):
        todo.load_settings(None)


def test_a_path_inside_a_git_work_tree_is_refused(tmp_path):
    repo = tmp_path / "repo"
    repo.mkdir()
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    with pytest.raises(todo.TodoError, match="never go into a repository"):
        todo.outside_git(repo / "plans" / "deep" / "p.md", "the plan file")
    assert todo.outside_git(tmp_path / "plans" / "p.md", "the plan file") == (tmp_path / "plans" / "p.md").resolve()


def test_loop_plan_names_map_to_their_entry():
    ids = {"no-ci", "chess-vs-engine"}
    assert todo.loop_plan_entry("todo-loop-no-ci.md", ids) == ("no-ci", "slice")
    assert todo.loop_plan_entry("todo-loop-chess-vs-engine-5.md", ids) == ("chess-vs-engine", "slice")
    assert todo.loop_plan_entry("todo-loop-chess-vs-engine-5-review-wp1-r2.md", ids) == ("chess-vs-engine", "review")
    assert todo.loop_plan_entry("todo-loop-in-review-review-integration.md", {"in-review"}) == ("in-review", "review")
    assert todo.loop_plan_entry("todo-loop-in-review.md", {"in-review"}) == ("in-review", "slice")
    assert todo.loop_plan_entry("todo-loop-other.md", ids) is None
    assert todo.loop_plan_entry("magical-leaping-blossom.md", ids) is None


def test_ping_list_show_and_the_loop_commands(cli):
    out, _ = cli("ping")
    assert out.startswith("ok:") and "areas engine" in out and "mirror off" in out
    body = FIXTURES / "todo" / "open-with-stage.md"
    cli("add", "sample", "--area", "engine", "--title", "A sample", "--stage", "2", "--body-file", str(body))
    out, _ = cli("list")
    assert "sample" in out and "stage 2" in out and "A sample" in out
    out, _ = cli("show", "sample")
    assert out.startswith("# A sample\n\nid: sample · area: engine · status: open · stage: 2")

    out, _ = cli("claim", "sample", "--branch", "todo/sample")
    assert "in progress" in out and "branch todo/sample" in out
    _, err = cli("claim", "sample", "--branch", "todo/sample-2", code=2)
    assert "HTTP 409" in err
    cli("review", "sample", "--branch", "todo/sample", "--note", "done, slice 1 of 2")
    out, _ = cli("close", "sample", "--reopen")
    assert out.strip().endswith(": open") or "sample: open" in out
    out, _ = cli("list", "--json")
    assert '"status": "open"' in out
    out, _ = cli("history", "sample")
    assert out.splitlines()[0].split()[2] == "reopen"


def test_claim_stores_the_base_and_the_origin(cli, tmp_path, monkeypatch):
    cli("add", "sample", "--area", "engine", "--title", "A sample")
    cli("add", "second", "--area", "engine", "--title", "Second")
    cli("add", "third", "--area", "engine", "--title", "Third")
    out, _ = cli("claim", "sample", "--branch", "todo/sample", "--base", "games/chess", "--origin", "Game@host")
    assert out.startswith("sample: in progress") and "base" not in out
    entry = json.loads(cli("show", "sample", "--json")[0])
    assert entry["base"] == "games/chess" and entry["origin"] == "Game@host"
    out, _ = cli("show", "sample")
    assert "base: games/chess · origin: Game@host" in out
    assert json.loads(cli("list", "--json")[0])[0]["origin"] == "Game@host"

    repo = tmp_path / "Game"
    repo.mkdir()
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    monkeypatch.chdir(repo)
    cli("claim", "second", "--branch", "todo/second", "--base", "main")
    second = json.loads(cli("show", "second", "--json")[0])
    assert second["base"] == "main" and second["origin"] == "Game@" + socket.gethostname()

    cli("claim", "third", "--branch", "todo/third")
    third = json.loads(cli("show", "third", "--json")[0])
    assert third["base"] is None and third["origin"] is None
    assert "base:" not in cli("show", "third")[0]


def test_default_origin_names_the_game_repository_and_the_host(tmp_path, monkeypatch):
    host = socket.gethostname()
    worker = tmp_path / "Game.workers" / "w1"
    worker.mkdir(parents=True)
    plain = tmp_path / "Repo"
    plain.mkdir()
    for folder in (worker, plain):
        subprocess.run(["git", "init", "-q", str(folder)], check=True)
    monkeypatch.chdir(worker)
    assert todo.default_origin() == f"Game@{host}"
    monkeypatch.chdir(plain)
    assert todo.default_origin() == f"Repo@{host}"
    outside = tmp_path / "outside"
    outside.mkdir()
    monkeypatch.chdir(outside)
    assert todo.default_origin() == host


def test_edit_clears_and_sets_fields(cli):
    cli("add", "sample", "--area", "engine", "--title", "A sample", "--stage", "2")
    cli("edit", "sample", "--stage", "none", "--title", "Renamed")
    out, _ = cli("show", "sample", "--json")
    assert '"stage": null' in out and '"title": "Renamed"' in out
    _, err = cli("edit", "sample", "--title", "X", "--expected-version", "1", code=2)
    assert "HTTP 409" in err


def test_slicing_cost_cap_and_note(cli):
    cli("add", "sample", "--area", "engine", "--title", "A sample")
    cli("edit", "sample", "--slicing", "through", "--cost-cap", "20")
    out, _ = cli("show", "sample")
    assert "slicing: through, cap $20" in out
    cli("claim", "sample", "--branch", "todo/sample")
    out, _ = cli("note", "sample", "--note", "through: slice 1 of 3 approved at abc123")
    assert "in progress" in out and "slice 1 of 3 approved" in out
    cli("edit", "sample", "--slicing", "none", "--cost-cap", "none")
    out, _ = cli("show", "sample", "--json")
    assert '"slicing": null' in out and '"costCap": null' in out


def test_plans_go_through_a_folder_outside_git(cli, plans_dir, tmp_path):
    cli("add", "sample", "--area", "engine", "--title", "A sample")
    plan = plans_dir / "todo-loop-sample.md"
    plan.write_text("# Slice\n- [x] a\n- [ ] b\n")
    out, _ = cli("plan", "put", "todo-loop-sample.md", "--file", str(plan), "--entry", "sample", "--kind", "slice")
    assert "slice, version 1, 1/2" in out
    out, _ = cli("list")
    assert "[plan todo-loop-sample.md 1/2]" in out
    copy = tmp_path / "elsewhere" / "copy.md"
    out, _ = cli("plan", "get", "todo-loop-sample.md", "--out", str(copy))
    assert copy.read_text() == plan.read_text()
    repo = tmp_path / "repo"
    repo.mkdir()
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    _, err = cli("plan", "get", "todo-loop-sample.md", "--out", str(repo / "plan.md"), code=1)
    assert "never go into a repository" in err and not (repo / "plan.md").exists()
    inside = repo / "p.md"
    inside.write_text("- [ ] x")
    _, err = cli("plan", "put", "p.md", "--file", str(inside), "--kind", "owner", code=1)
    assert "never go into a repository" in err
    out, _ = cli("plan", "url", "todo-loop-sample.md")
    assert out.strip().endswith("/dashboard/plan/todo-loop-sample.md")


def test_import_takes_the_old_files_and_their_plans(cli, plans_dir):
    (plans_dir / "sample-plan.md").write_text((FIXTURES / "plans" / "sample-plan.md").read_text())
    (plans_dir / "todo-loop-in-review.md").write_text("# Slice\n- [x] done\n")
    (plans_dir / "todo-loop-in-review-review-wp1.md").write_text("APPROVE\n")
    out, _ = cli("import", "--area", "engine", "--todo", str(FIXTURES / "todo"), "--plans-dir", str(plans_dir), "--dry-run")
    assert "would add" in out
    assert cli("list", "--all")[0] == "no entries\n"
    out, _ = cli("import", "--area", "engine", "--todo", str(FIXTURES / "todo"), "--plans-dir", str(plans_dir))
    assert "put     plan todo-loop-in-review.md (slice, in-review)" in out
    assert "put     plan todo-loop-in-review-review-wp1.md (review, in-review)" in out
    out, _ = cli("show", "in-review", "--json")
    assert '"status": "review"' in out and '"branch": "todo/in-review"' in out
    out, _ = cli("import", "--area", "engine", "--todo", str(FIXTURES / "todo"), "--plans-dir", str(plans_dir))
    assert "skip    in-review (in the store)" in out
    out, _ = cli("show", "renamed-file", "--json")
    assert '"aliases": [\n    "the-old-id"\n  ]' in out  # the file name is the id, the old id: an alias


def test_export_writes_entries_and_plans_outside_git(cli, plans_dir, tmp_path):
    cli("add", "sample", "--area", "engine", "--title", "A sample", "--stage", "1")
    (plans_dir / "p.md").write_text("- [ ] a\n")
    cli("plan", "put", "p.md", "--file", str(plans_dir / "p.md"), "--entry", "sample", "--kind", "slice")
    out, _ = cli("export", "--out", str(tmp_path / "backup"))
    assert "1 entries and 1 plans" in out
    assert (tmp_path / "backup" / "engine" / "sample.md").read_text().startswith("---\nstage: 1\n---\n# A sample")
    assert (tmp_path / "backup" / "plans" / "p.md").is_file()


class _StubHandler(http.server.BaseHTTPRequestHandler):
    """Answers every request with `{}`, and 409 for a path that ends with /conflict."""

    def _answer(self):
        length = int(self.headers.get("Content-Length") or 0)
        self.rfile.read(length)
        code = 409 if self.path.endswith("/conflict") else 200
        body = b'{"detail": "taken"}' if code == 409 else b"{}"
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = _answer

    def log_message(self, *args):
        pass


@pytest.fixture
def stub_api():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _StubHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield todo.Api(f"http://127.0.0.1:{server.server_address[1]}", "token", "tester")
    server.shutdown()
    server.server_close()


def test_a_write_rewrites_the_signal_file_and_a_read_does_not(stub_api, changed_file):
    stub_api.get("/entries")
    assert not changed_file.exists()

    stub_api.call("POST", "/entries/x/claim", {"branch": "todo/x"})
    first = json.loads(changed_file.read_text())
    assert (first["actor"], first["method"], first["path"]) == ("tester", "POST", "/entries/x/claim")
    assert first["at"]

    stub_api.call("PUT", "/plans/p.md", {"content": "x"})
    assert json.loads(changed_file.read_text())["path"] == "/plans/p.md"
    assert [p.name for p in changed_file.parent.iterdir()] == ["changed.json"]  # no temporary file left


def test_a_refused_write_leaves_the_signal_alone(stub_api, changed_file):
    with pytest.raises(todo.TodoError, match="HTTP 409"):
        stub_api.call("POST", "/entries/x/conflict", {})
    assert not changed_file.exists()


def test_a_signal_that_cannot_be_written_does_not_fail_the_write(stub_api, changed_file, monkeypatch):
    blocker = changed_file.parent
    blocker.parent.mkdir(parents=True, exist_ok=True)
    blocker.write_text("a file where the folder should be")
    assert stub_api.call("PATCH", "/entries/x", {"title": "t"}) == {}


def test_priority_is_set_cleared_shown_and_sorted(cli):
    cli("add", "plain", "--area", "engine", "--title", "Unrated")
    cli("add", "slow", "--area", "engine", "--title", "Later", "--priority", "low")
    cli("add", "urgent", "--area", "engine", "--title", "Now", "--priority", "now", "--stage", "3")
    cli("add", "soon", "--area", "engine", "--title", "Soon")
    cli("edit", "soon", "--priority", "high")
    out, _ = cli("show", "soon")
    assert "priority: high" in out
    out, _ = cli("show", "plain")
    assert "priority:" not in out
    out, _ = cli("list")
    assert [line.split()[0] for line in out.splitlines()] == ["urgent", "soon", "plain", "slow"]
    assert out.splitlines()[1].split()[3] == "high" and out.splitlines()[2].split()[3] == "-"
    out, _ = cli("list", "--json")
    assert [entry["id"] for entry in json.loads(out)] == ["plain", "slow", "soon", "urgent"]
    cli("edit", "soon", "--priority", "none")
    out, _ = cli("show", "soon", "--json")
    assert json.loads(out)["priority"] is None


def test_list_filters_on_priority(cli):
    cli("add", "a", "--area", "engine", "--title", "A", "--priority", "now")
    cli("add", "b", "--area", "engine", "--title", "B", "--priority", "high")
    cli("add", "c", "--area", "engine", "--title", "C")
    cli("add", "d", "--area", "engine", "--title", "D", "--priority", "low")
    out, _ = cli("list", "--priority", "now,high")
    assert [line.split()[0] for line in out.splitlines()] == ["a", "b"]
    out, _ = cli("list", "--priority", "none,low")
    assert [line.split()[0] for line in out.splitlines()] == ["c", "d"]


def test_a_bad_priority_is_refused_before_the_request(cli, capsys):
    cli("add", "a", "--area", "engine", "--title", "A")
    for argv in (("add", "b", "--area", "engine", "--title", "B", "--priority", "none"),
                 ("edit", "a", "--priority", "urgent"), ("list", "--priority", "now,soon")):
        with pytest.raises(SystemExit) as stop:
            cli(*argv)
        assert stop.value.code == 2
        assert "priority" in capsys.readouterr().err
    out, _ = cli("list")
    assert [line.split()[0] for line in out.splitlines()] == ["a"]
