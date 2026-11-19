"""tokstats on a fake set of transcripts and a fake store."""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tokstats  # noqa: E402

ROOT = Path("/work/Game")


def answer(mid, ts, *, branch=None, command=None, text=None, model="claude-sonnet-x", ctx=1000, out=10):
    content = []
    if command:
        content.append({"type": "tool_use", "id": f"t{mid}", "name": "Bash", "input": {"command": command}})
    if text:
        content.append({"type": "text", "text": text})
    return {"type": "assistant", "timestamp": ts, "gitBranch": branch,
            "message": {"id": f"m{mid}", "model": model, "content": content,
                        "usage": {"input_tokens": 0, "cache_creation_input_tokens": 0, "cache_read_input_tokens": ctx, "output_tokens": out}}}


def result(mid, size):
    return {"type": "user", "message": {"content": [{"type": "tool_result", "tool_use_id": f"t{mid}", "content": "x" * size}]}}


def write(path: Path, records):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(json.dumps(r) for r in records) + "\n")


@pytest.fixture
def home(tmp_path, monkeypatch):
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "claude"))
    monkeypatch.setenv("TOKSTATS_DIR", str(tmp_path / "stats"))
    projects = tmp_path / "claude" / "projects"
    worker = projects / tokstats.folder_name(Path(str(ROOT) + ".workers/w1"))
    # a worker's conductor on a detached head: the classifier turn holds nothing, the claim starts the entry,
    # the review ends it, and the next turn holds nothing again
    write(worker / "s1.jsonl", [
        answer(1, "2026-11-01T10:00:00Z", branch="HEAD"),
        answer(2, "2026-11-01T10:01:00Z", branch="HEAD", command="python3 engine/tools/todo/todo.py claim foo --branch todo/foo --base main"),
        result(2, 50),
        answer(3, "2026-11-01T10:05:00Z", branch="HEAD", ctx=3000),
        answer(3, "2026-11-01T10:05:00Z", branch="HEAD", ctx=3000),  # the same message on a second line
        answer(4, "2026-11-01T11:00:00Z", branch="HEAD", command="python3 engine/tools/todo/todo.py review foo --branch todo/foo --note x"),
        answer(5, "2026-11-01T11:01:00Z", branch="HEAD"),
    ])
    sub = worker / "s1" / "subagents"
    # an engine entry worked from a game: the developer's records name the game's head, so the session decides
    write(sub / "agent-a.jsonl", [
        answer(10, "2026-11-01T10:10:00Z", branch="HEAD", command="grep -rn Foo src", ctx=5000),
        result(10, 400),
        answer(11, "2026-11-01T10:20:00Z", branch="HEAD", text="WP incomplete: WP 2", ctx=9000),
    ])
    (sub / "agent-a.meta.json").write_text(json.dumps({"agentType": "todo-developer"}))
    # a reviewer whose records name the branch itself
    write(sub / "agent-b.jsonl", [answer(20, "2026-11-01T10:40:00Z", branch="todo/foo", model="claude-opus-x", ctx=200000)])
    (sub / "agent-b.meta.json").write_text(json.dumps({"agentType": "todo-reviewer"}))
    # another checkout's transcripts are not read
    write(projects / tokstats.folder_name(Path("/work/Other")) / "s9.jsonl", [answer(90, "2026-11-01T10:00:00Z", branch="todo/foo")])
    return projects


def store_of(plans=None, entries=None):
    plans = plans or {}
    entries = entries or {}

    def store(*args):
        if args[0] == "show":
            return json.dumps(entries[args[1]]) if args[1] in entries else None
        if args[:2] == ("plan", "list"):
            return json.dumps([{"name": n, "kind": k} for n, (k, _) in plans.items()])
        if args[:2] == ("plan", "get"):
            return plans[args[2]][1] if args[2] in plans else None
        return None
    return store


def test_folders_are_the_root_and_its_workers(home):
    names = [p.name for p in tokstats.transcript_folders(ROOT, home)]
    assert names == [tokstats.folder_name(Path(str(ROOT) + ".workers/w1"))]


def test_attribution_by_marker_branch_and_session(home):
    tokens = tokstats.tokens_by_branch(tokstats.scan_folders(tokstats.transcript_folders(ROOT, home)), prices=tokstats.DEFAULT_PRICES)
    roles = tokens["foo"]
    # turns 2, 3 and 4 of the conductor: before the claim and after the review it holds nothing
    assert roles["conductor"]["turns"] == 3
    assert roles["conductor"]["cacheRead"] == 1000 + 3000 + 1000
    assert roles["developer"]["turns"] == 2 and roles["developer"]["peak"] == 9000
    assert roles["developer"]["wpIncomplete"] == 1
    assert roles["developer"]["bashBytes"] == {"grep": 400}
    assert roles["reviewer"]["cacheRead"] == 200000 and roles["reviewer"]["dollars"] > 0
    assert roles["reviewer"]["weighted"] == round(200000 * 0.1 + 10 * 5)
    assert set(tokens) == {"foo"}


def test_blocking_kinds_verdicts_and_plan_accuracy(home, monkeypatch):
    plans = {
        "todo-loop-foo.md": ("slice", "# x\n\n## Files\n- `src/A.cpp`\n- docs/\n\n## WP 1\n"),
        "todo-loop-foo-review-final.md": ("review", "VERDICT: CHANGES\nFindings:\n- [blocking:docs] a.md:1 — x; verify: grep\n- [blocking] b.cpp:2 — y\n- [note] c:3 — z\n"),
        "todo-loop-foo-review-final-r2.md": ("review", "VERDICT: APPROVE\n"),
        "todo-loop-foobar-review-final.md": ("review", "VERDICT: CHANGES\n- [blocking:code] z:1 — w\n"),
    }
    entries = {"foo": {"id": "foo", "area": "engine", "base": "main", "closedAt": "2026-11-02T00:00:00Z", "reviewRounds": {"reviews": 2}}}
    monkeypatch.setattr(tokstats, "git_outcome", lambda repos, b, base: {"files": ["src/A.cpp", "docs/x.md", "src/B.cpp"], "lines": 42})
    tokens = tokstats.tokens_by_branch(tokstats.scan_folders(tokstats.transcript_folders(ROOT, home)))
    record = tokstats.build_record("foo", tokens, store_of(plans, entries), [ROOT])
    assert [r["verdict"] for r in record["reviews"]] == ["CHANGES", "APPROVE"]
    assert record["blockingByKind"] == {"docs": 1, "unknown": 1}
    assert record["plan"] == {"planned": 2, "changed": 3, "planHit": 2, "unplanned": 1}
    assert record["area"] == "engine" and record["lines"] == 42


def test_next_slice_branch_finds_its_entry():
    store = store_of(entries={"foo": {"id": "foo", "area": "chess"}})
    assert tokstats.entry_of("foo-2", store)["id"] == "foo"
    assert tokstats.entry_of("nothing", store) is None


def test_record_keeps_tokens_when_the_transcripts_are_gone(home, monkeypatch):
    monkeypatch.setattr(tokstats, "git_outcome", lambda *a: {})
    store = store_of(entries={"foo": {"id": "foo", "area": "engine", "closedAt": "2026-11-02T00:00:00Z"}})
    tokens = tokstats.tokens_by_branch(tokstats.scan_folders(tokstats.transcript_folders(ROOT, home)))
    first = tokstats.build_record("foo", tokens, store, [ROOT])
    tokstats.save_record(first)
    path = tokstats.save_record(tokstats.build_record("foo", {}, store, [ROOT]))
    assert json.loads(path.read_text())["total"]["weighted"] == first["total"]["weighted"] > 0


def test_report_groups_by_area_and_month():
    def rec(area, month, weighted, reviews, kinds=None):
        return {"area": area, "closedAt": f"{month}-05T00:00:00Z", "total": {"weighted": weighted, "dollars": 1.0},
                "roles": {"developer": {"weighted": weighted * 3 // 4}, "reviewer": {"weighted": weighted // 4}},
                "reviews": [{}] * reviews, "blockingByKind": kinds or {}, "wpIncomplete": 0, "plan": None}
    rows = tokstats.report([rec("engine", "2026-11", 100, 1), rec("engine", "2026-11", 300, 3, {"docs": 2}),
                            rec("chess", "2026-10", 50, 1), rec("chess", "2026-11", 0, 0)], since="2026-11")
    assert list(rows) == ["engine 2026-11"]
    row = rows["engine 2026-11"]
    assert row["entries"] == 2 and row["weightedMedian"] == 200 and row["twoPlusReviews"] == 1
    assert row["roleShare"] == {"developer": 75, "reviewer": 25} and row["blockingByKind"] == {"docs": 2}


def test_bash_kinds():
    assert tokstats.bash_kind("cmake --build --preset x > log") == "build"
    assert tokstats.bash_kind("git -C engine diff main...HEAD") == "git-diff"
    assert tokstats.bash_kind("grep -rn X src | head") == "grep"
    assert tokstats.bash_kind("ls") == "other"


def test_branch_names_are_words_not_paths():
    command = ("python3 tools/todo/todo.py claim foo-2 --branch todo/foo-2 --base main; cat ~/.claude/todo/agent.log; "
               "ls todo/README.md; git branch --list 'todo/*'; git switch todo/bar_x")
    assert tokstats.BRANCH_RE.findall(command) == ["foo-2", "bar_x"]
