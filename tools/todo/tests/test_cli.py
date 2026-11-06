"""todo.py against the service running in a thread (uvicorn, a real port), and its guards."""

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


def test_edit_clears_and_sets_fields(cli):
    cli("add", "sample", "--area", "engine", "--title", "A sample", "--stage", "2")
    cli("edit", "sample", "--stage", "none", "--title", "Renamed")
    out, _ = cli("show", "sample", "--json")
    assert '"stage": null' in out and '"title": "Renamed"' in out
    _, err = cli("edit", "sample", "--title", "X", "--expected-version", "1", code=2)
    assert "HTTP 409" in err


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


def test_export_writes_entries_and_plans_outside_git(cli, plans_dir, tmp_path):
    cli("add", "sample", "--area", "engine", "--title", "A sample", "--stage", "1")
    (plans_dir / "p.md").write_text("- [ ] a\n")
    cli("plan", "put", "p.md", "--file", str(plans_dir / "p.md"), "--entry", "sample", "--kind", "slice")
    out, _ = cli("export", "--out", str(tmp_path / "backup"))
    assert "1 entries and 1 plans" in out
    assert (tmp_path / "backup" / "engine" / "sample.md").read_text().startswith("---\nstage: 1\n---\n# A sample")
    assert (tmp_path / "backup" / "plans" / "p.md").is_file()
