"""The API of the service over a real Postgres, no mirror."""

import threading
from datetime import date

import psycopg

from conftest import TOKEN

ENTRY = {"id": "no-ci", "area": "engine", "title": "No CI", "body": "**What is missing:** a build on every push.\n", "stage": 1}


def add(api, **fields):
    response = api.post("/api/v1/entries", json={**ENTRY, **fields})
    assert response.status_code == 201, response.text
    return response.json()


def test_a_request_without_the_token_is_refused(api):
    assert api.get("/api/v1/entries", headers={"Authorization": "Bearer wrong"}).status_code == 401
    assert api.get("/api/v1/entries", headers={"Authorization": ""}).status_code == 401
    assert api.get("/dashboard/", headers={"Authorization": ""}).status_code == 401


def test_the_pages_take_the_token_as_a_basic_auth_password(api):
    add(api)
    response = api.get("/dashboard/", headers={"Authorization": ""}, auth=("anyone", TOKEN))
    assert response.status_code == 200
    assert "No CI" in response.text and "Core" in response.text
    assert api.get("/dashboard/entry/no-ci", auth=("x", TOKEN), headers={"Authorization": ""}).status_code == 200


def test_an_entry_reads_back_with_its_summary_and_status_line(api):
    created = add(api)
    assert created["status"] == "open" and created["version"] == 1
    assert created["summary"] == "A build on every push."
    assert created["statusLine"] == "open"
    listed = api.get("/api/v1/entries", params={"area": "engine"}).json()
    assert [entry["id"] for entry in listed] == ["no-ci"]
    assert "body" not in listed[0]
    assert api.get("/api/v1/entries/no-ci").json()["body"] == ENTRY["body"]


def test_bad_entries_are_refused(api):
    assert api.post("/api/v1/entries", json={**ENTRY, "id": "a b"}).status_code == 422
    assert api.post("/api/v1/entries", json={**ENTRY, "area": "nowhere"}).status_code == 422
    assert api.post("/api/v1/entries", json={**ENTRY, "status": "done"}).status_code == 422
    assert api.post("/api/v1/entries", json={**ENTRY, "plan": "dir/plan.md"}).status_code == 422
    add(api)
    assert api.post("/api/v1/entries", json=ENTRY).status_code == 409
    assert api.get("/api/v1/entries/missing").status_code == 404


def test_the_loop_life_of_an_entry(api):
    add(api)
    claimed = api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci"}).json()
    assert claimed["status"] == "in progress" and claimed["branch"] == "todo/no-ci"
    assert claimed["statusLine"] == f"in progress {date.today().isoformat()} branch todo/no-ci"
    assert api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci-2"}).status_code == 409
    assert api.post("/api/v1/entries/no-ci/review", json={"branch": "todo/other", "note": "x"}).status_code == 409

    stopped = api.post("/api/v1/entries/no-ci/stop", json={"reason": "BLOCKED"}).json()
    assert stopped["statusLine"].endswith("branch todo/no-ci: stopped: BLOCKED")

    reviewed = api.post("/api/v1/entries/no-ci/review", json={"branch": "todo/no-ci", "note": "a CI job, slice 1 of 2"}).json()
    assert reviewed["status"] == "review" and reviewed["statusNote"] == "a CI job, slice 1 of 2"

    reopened = api.post("/api/v1/entries/no-ci/close", json={"reopen": True}).json()
    assert reopened["status"] == "open" and reopened["branch"] is None and reopened["statusNote"] == ""

    api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci-2"}).raise_for_status()
    closed = api.post("/api/v1/entries/no-ci/close", json={}).json()
    assert closed["status"] == "closed" and closed["closedAt"]
    assert api.post("/api/v1/entries/no-ci/close", json={}).status_code == 409

    kinds = [event["kind"] for event in api.get("/api/v1/entries/no-ci/events").json()]
    assert kinds == ["close", "claim", "reopen", "review", "stop", "claim", "create"]


def test_a_claim_records_the_base_and_the_origin(api):
    add(api)
    claim_path = "/api/v1/entries/no-ci/claim"
    claimed = api.post(claim_path, json={"branch": "todo/no-ci", "base": "games/chess", "origin": "Chess@host"}).json()
    assert claimed["base"] == "games/chess" and claimed["origin"] == "Chess@host" and claimed["version"] == 2
    assert api.get("/api/v1/entries/no-ci").json()["base"] == "games/chess"
    listed = api.get("/api/v1/entries").json()[0]
    assert listed["base"] == "games/chess" and listed["origin"] == "Chess@host"
    events = api.get("/api/v1/entries/no-ci/events").json()
    assert events[0]["kind"] == "claim" and events[0]["data"]["new"]["base"] == "games/chess" and events[0]["data"]["new"]["origin"] == "Chess@host"

    closed = api.post("/api/v1/entries/no-ci/close", json={"reopen": True}).json()
    assert closed["branch"] is None and closed["base"] is None and closed["origin"] is None
    again = api.post(claim_path, json={"branch": "todo/no-ci-2", "base": "main", "origin": "Engine@other"}).json()
    assert again["base"] == "main" and again["origin"] == "Engine@other"
    final = api.post("/api/v1/entries/no-ci/close", json={}).json()
    assert final["base"] is None and final["origin"] is None


def test_a_claim_without_a_base_stores_nothing_and_blanks_count_as_none(api):
    add(api)
    old = api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci"}).json()
    assert old["base"] is None and old["origin"] is None
    api.post("/api/v1/entries/no-ci/close", json={"reopen": True}).raise_for_status()
    blank = api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci", "base": "  ", "origin": ""}).json()
    assert blank["base"] is None and blank["origin"] is None
    api.post("/api/v1/entries/no-ci/close", json={"reopen": True}).raise_for_status()
    only_origin = api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci", "origin": "Chess@host"}).json()
    assert only_origin["base"] is None and only_origin["origin"] == "Chess@host"


def test_clearing_the_branch_clears_the_base_and_the_origin(api):
    add(api)
    api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci", "base": "main", "origin": "Engine@host"}).raise_for_status()
    cleared = api.patch("/api/v1/entries/no-ci", json={"clear": ["branch"]}).json()
    assert cleared["branch"] is None and cleared["base"] is None and cleared["origin"] is None


def test_an_entry_from_before_migration_003_survives_it_with_no_base(clean_database, monkeypatch, tmp_path):
    from service import db

    old_schema = tmp_path / "schema"
    old_schema.mkdir()
    for name in ("001_init.sql", "002_aliases.sql"):
        (old_schema / name).write_text((db.SCHEMA_DIR / name).read_text(encoding="utf-8"), encoding="utf-8")
    with psycopg.connect(clean_database) as conn:
        conn.row_factory = psycopg.rows.dict_row
        with monkeypatch.context() as patch:
            patch.setattr(db, "SCHEMA_DIR", old_schema)
            assert db.migrate(conn) == ["001_init.sql", "002_aliases.sql"]
        conn.execute("INSERT INTO areas (id, title) VALUES ('engine', 'Engine')")
        conn.execute("INSERT INTO entries (id, area, title, status, branch) VALUES ('old', 'engine', 'Old', 'in progress', 'todo/old')")
        conn.commit()
        assert db.migrate(conn) == ["003_branch_origin.sql", "004_dependencies.sql", "005_slicing.sql", "006_priority.sql"]
        row = conn.execute("SELECT branch, base, origin FROM entries WHERE id = 'old'").fetchone()
    assert row == {"branch": "todo/old", "base": None, "origin": None}


def test_two_claims_at_once_give_one_winner(api, clean_database):
    add(api)
    results = []
    barrier = threading.Barrier(2)

    def take(branch):
        barrier.wait()
        results.append(api.post("/api/v1/entries/no-ci/claim", json={"branch": branch}).status_code)

    threads = [threading.Thread(target=take, args=(f"todo/no-ci-{n}",)) for n in (1, 2)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert sorted(results) == [200, 409]


def test_a_patch_with_a_stale_version_is_refused(api):
    add(api)
    first = api.patch("/api/v1/entries/no-ci", json={"title": "No CI at all", "expectedVersion": 1}).json()
    assert first["version"] == 2
    assert api.patch("/api/v1/entries/no-ci", json={"title": "Again", "expectedVersion": 1}).status_code == 409
    same = api.patch("/api/v1/entries/no-ci", json={"title": "No CI at all"}).json()
    assert same["version"] == 2  # nothing changed, no new version
    cleared = api.patch("/api/v1/entries/no-ci", json={"clear": ["stage"]}).json()
    assert cleared["stage"] is None
    assert api.patch("/api/v1/entries/no-ci", json={"clear": ["title"]}).status_code == 422


def test_plans_belong_to_an_entry_and_give_its_progress(api):
    add(api)
    content = "# Slice\n\n- [x] one\n- [ ] two\n\n```\n- [ ] not a box\n```\n"
    plan = api.put("/api/v1/plans/todo-loop-no-ci.md", json={"content": content, "entry": "no-ci", "kind": "slice"}).json()
    assert plan["progress"] == {"done": 1, "total": 2}
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["planShown"] == "todo-loop-no-ci.md" and entry["planProgress"] == {"done": 1, "total": 2}

    api.put("/api/v1/plans/owner.md", json={"content": "# Owner\n- [ ] a\n", "kind": "owner"}).raise_for_status()
    api.patch("/api/v1/entries/no-ci", json={"plan": "owner.md"}).raise_for_status()
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["planShown"] == "owner.md" and entry["planProgress"] == {"done": 0, "total": 1}

    names = {plan["name"] for plan in api.get("/api/v1/plans", params={"entry": "no-ci"}).json()}
    assert names == {"todo-loop-no-ci.md", "owner.md"}
    assert api.get("/api/v1/plans/owner.md").json()["content"].startswith("# Owner")
    assert api.put("/api/v1/plans/bad name.md", json={"content": "x", "kind": "slice"}).status_code == 422
    assert api.put("/api/v1/plans/x.md", json={"content": "x", "kind": "draft"}).status_code == 422
    assert api.put("/api/v1/plans/x.md", json={"content": "x", "kind": "review", "entry": "missing"}).status_code == 404
    assert api.delete("/api/v1/plans/owner.md").status_code == 204
    assert api.get("/api/v1/plans/owner.md").status_code == 404
    assert api.get("/dashboard/plan/todo-loop-no-ci.md", auth=("x", TOKEN), headers={"Authorization": ""}).status_code == 200


def test_slicing_and_the_cost_cap_are_set_cleared_and_checked(api):
    entry = add(api)
    assert entry["slicing"] is None and entry["costCap"] is None
    through = api.patch("/api/v1/entries/no-ci", json={"slicing": "through", "costCap": 12.5}).json()
    assert through["slicing"] == "through" and through["costCap"] == 12.5
    same = api.patch("/api/v1/entries/no-ci", json={"costCap": 12.5}).json()
    assert same["version"] == through["version"]  # the same cap is no change
    assert api.patch("/api/v1/entries/no-ci", json={"slicing": "sometimes"}).status_code == 422
    assert api.patch("/api/v1/entries/no-ci", json={"costCap": -1}).status_code == 422
    cleared = api.patch("/api/v1/entries/no-ci", json={"clear": ["slicing", "costCap"]}).json()
    assert cleared["slicing"] is None and cleared["costCap"] is None
    created = add(api, id="through-from-start", slicing="through", costCap=3)
    assert created["slicing"] == "through" and created["costCap"] == 3


def test_the_priority_is_set_on_create_and_by_patch_cleared_and_checked(api):
    assert add(api)["priority"] is None
    high = api.patch("/api/v1/entries/no-ci", json={"priority": "high"}, headers={"X-Todo-Actor": "owner"}).json()
    assert high["priority"] == "high" and high["version"] == 2
    assert api.patch("/api/v1/entries/no-ci", json={"priority": "high"}).json()["version"] == 2  # the same level is no change
    for bad in ("urgent", "none", ""):
        assert api.patch("/api/v1/entries/no-ci", json={"priority": bad}).status_code == 422
    assert api.post("/api/v1/entries", json={**ENTRY, "id": "bad", "priority": "urgent"}).status_code == 422
    assert add(api, id="first", priority="now")["priority"] == "now"
    cleared = api.patch("/api/v1/entries/no-ci", json={"clear": ["priority"]}).json()
    assert cleared["priority"] is None and api.get("/api/v1/entries/no-ci").json()["priority"] is None

    events = api.get("/api/v1/entries/no-ci/events").json()
    by_change = [(event["actor"], event["data"]["old"], event["data"]["new"]) for event in events if event["kind"] == "update"]
    assert by_change == [("test", {"priority": "high"}, {"priority": None}), ("owner", {"priority": None}, {"priority": "high"})]


def test_the_priority_filter_takes_levels_and_none(api):
    for name, level in (("a-now", "now"), ("b-high", "high"), ("c-normal", "normal"), ("d-low", "low"), ("e-unset", None)):
        add(api, id=name, **({"priority": level} if level else {}))

    def ids(value):
        response = api.get("/api/v1/entries", params={"priority": value})
        assert response.status_code == 200, response.text
        return [entry["id"] for entry in response.json()]

    assert ids("now,high") == ["a-now", "b-high"]
    assert ids("low") == ["d-low"]
    assert ids("none") == ["e-unset"]
    assert ids("none, low") == ["d-low", "e-unset"]
    assert len(api.get("/api/v1/entries").json()) == 5
    assert api.get("/api/v1/entries", params={"priority": "now,urgent"}).status_code == 422
    assert api.get("/api/v1/entries", params={"priority": ""}).status_code == 422


def test_a_priority_change_bumps_the_version_and_queues_the_mirror(api, clean_database):
    add(api)
    with psycopg.connect(clean_database) as conn:
        conn.execute("DELETE FROM vikunja_outbox")
        conn.commit()
    api.patch("/api/v1/entries/no-ci", json={"priority": "low"}).raise_for_status()
    with psycopg.connect(clean_database) as conn:
        assert conn.execute("SELECT entry_id FROM vikunja_outbox").fetchall() == [("no-ci",)]
        assert conn.execute("SELECT version, priority FROM entries WHERE id = 'no-ci'").fetchone() == (2, "low")


def test_an_entry_sums_its_slice_plans_and_names_its_stage_plan(api):
    add(api)
    assert api.get("/api/v1/entries/no-ci").json()["slices"] is None
    api.put("/api/v1/plans/todo-loop-no-ci.md", json={"content": "- [x] a\n- [x] b\n", "entry": "no-ci", "kind": "slice"}).raise_for_status()
    api.put("/api/v1/plans/todo-loop-no-ci-s2.md", json={"content": "- [x] c\n- [ ] d\n- [ ] e\n", "entry": "no-ci", "kind": "slice"}).raise_for_status()
    api.put("/api/v1/plans/todo-loop-no-ci-stages.md", json={"content": "- [x] 1\n- [ ] 2\n", "entry": "no-ci", "kind": "stages"}).raise_for_status()
    api.put("/api/v1/plans/todo-loop-no-ci-remarks-1.md", json={"content": "fix the name", "entry": "no-ci", "kind": "remarks"}).raise_for_status()
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["planShown"] == "todo-loop-no-ci-s2.md" and entry["planProgress"] == {"done": 1, "total": 3}
    assert entry["slices"] == {"count": 2, "done": 3, "total": 5}
    assert entry["stagesPlan"] == "todo-loop-no-ci-stages.md"


def test_every_write_queues_the_entry_for_the_mirror_once(api, clean_database):
    add(api)
    api.patch("/api/v1/entries/no-ci", json={"title": "Changed"}).raise_for_status()
    api.put("/api/v1/plans/p.md", json={"content": "- [ ] a", "entry": "no-ci", "kind": "slice"}).raise_for_status()
    with psycopg.connect(clean_database) as conn:
        assert conn.execute("SELECT entry_id FROM vikunja_outbox").fetchall() == [("no-ci",)]
    assert api.get("/healthz").json()["outbox"] == 1


def test_the_migrations_run_once(clean_database):
    from service import db

    with psycopg.connect(clean_database) as conn:
        conn.row_factory = psycopg.rows.dict_row
        assert db.migrate(conn) == ["001_init.sql", "002_aliases.sql", "003_branch_origin.sql", "004_dependencies.sql", "005_slicing.sql", "006_priority.sql"]
        assert db.migrate(conn) == []
