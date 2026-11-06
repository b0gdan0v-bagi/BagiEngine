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
        assert db.migrate(conn) == ["001_init.sql"]
        assert db.migrate(conn) == []
