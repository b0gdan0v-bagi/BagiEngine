"""The automatic mirror between the store and the fake Vikunja (the worker is driven by the tests)."""

import hashlib
import hmac
import json

import pytest

from conftest import WEBHOOK_SECRET, make_client

ENTRY = {
    "id": "no-ci",
    "area": "engine",
    "title": "No CI",
    "body": "**What is missing:** a build on every push.\n\n- one\n- two\n",
    "stage": 1,
}


def add(api, **fields):
    response = api.post("/api/v1/entries", json={**ENTRY, **fields})
    assert response.status_code == 201, response.text
    return response.json()


def task_of(api, fake, entry_id="no-ci"):
    return fake.tasks[api.get(f"/api/v1/entries/{entry_id}").json()["vikunjaTaskId"]]


def labels(task):
    return sorted(label["title"] for label in task["labels"])


def project(fake, title):
    return next(p for p in fake.projects if p["title"] == title)


def send_webhook(api, event, task, secret=WEBHOOK_SECRET):
    raw = json.dumps({"event_name": event, "time": "2026-11-06T10:00:00Z", "data": {"task": task, "doer": {"id": 1}}}).encode()
    signature = hmac.new(secret.encode(), raw, hashlib.sha256).hexdigest()
    return api.post("/api/v1/vikunja/webhook", content=raw, headers={"X-Vikunja-Signature": signature, "Content-Type": "application/json"})


def test_a_new_entry_becomes_a_task_with_the_marker_body_and_labels(mirrored):
    api, mirror, fake = mirrored
    add(api)
    assert mirror.process_outbox() == 1
    task = task_of(api, fake)
    assert task["project_id"] == project(fake, "BagiEngine")["id"]
    assert task["title"] == "No CI" and not task["done"]
    assert task["description"].startswith("<p>todo-sync:id=no-ci</p><p><strong>Status:</strong> open</p><p><strong>Stage:</strong> 1: Core</p><hr>")
    assert "<li><p>one</p></li>" in task["description"]
    assert labels(task) == ["stage 1"]


def test_a_second_pass_without_a_change_writes_nothing(mirrored):
    api, mirror, fake = mirrored
    add(api)
    mirror.process_outbox()
    mirror.reconcile()  # the first pass creates the project of the other area
    before = len(fake.writes)
    api.patch("/api/v1/entries/no-ci", json={"title": "No CI"})  # the same title: no change, nothing queued
    assert mirror.process_outbox() == 0
    assert mirror.reconcile() == {"same": 1}
    mirror.process_outbox()
    assert len(fake.writes) == before


def test_the_loop_statuses_reach_the_labels_and_close_marks_the_task_done(mirrored):
    api, mirror, fake = mirrored
    add(api)
    api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci"}).raise_for_status()
    mirror.process_outbox()
    assert labels(task_of(api, fake)) == ["in progress", "stage 1"]
    api.post("/api/v1/entries/no-ci/review", json={"branch": "todo/no-ci", "note": "done"}).raise_for_status()
    mirror.process_outbox()
    task = task_of(api, fake)
    assert labels(task) == ["review", "stage 1"] and "branch todo/no-ci: done" in task["description"]
    api.post("/api/v1/entries/no-ci/close", json={}).raise_for_status()
    mirror.process_outbox()
    assert task_of(api, fake)["done"] is True and labels(task_of(api, fake)) == ["stage 1"]


def test_foreign_fields_and_labels_of_a_task_are_kept_and_the_priority_is_owned(mirrored):
    api, mirror, fake = mirrored
    add(api)
    mirror.process_outbox()
    task = task_of(api, fake)
    task["due_date"] = "2026-12-01T00:00:00Z"
    task["labels"].append(fake.add_label("urgent"))
    api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci"}).raise_for_status()
    api.patch("/api/v1/entries/no-ci", json={"priority": "high"}).raise_for_status()
    mirror.process_outbox()
    task = task_of(api, fake)
    assert task["due_date"] == "2026-12-01T00:00:00Z"
    assert task["priority"] == 3
    assert labels(task) == ["in progress", "stage 1", "urgent"]


@pytest.mark.parametrize("level,number", [("now", 5), ("high", 3), ("normal", 2), ("low", 1)])
def test_a_priority_set_in_the_store_reaches_a_new_and_an_existing_task(mirrored, level, number):
    api, mirror, fake = mirrored
    add(api, id="made-with", title="Made with", priority=level)
    add(api, id="made-after", title="Made after")
    mirror.process_outbox()
    assert task_of(api, fake, "made-with")["priority"] == number
    assert task_of(api, fake, "made-after")["priority"] == 0
    api.patch("/api/v1/entries/made-after", json={"priority": level}).raise_for_status()
    mirror.process_outbox()
    assert task_of(api, fake, "made-after")["priority"] == number
    api.patch("/api/v1/entries/made-after", json={"clear": ["priority"]}).raise_for_status()
    mirror.process_outbox()
    assert task_of(api, fake, "made-after")["priority"] == 0


@pytest.mark.parametrize("number,level", [(1, "low"), (2, "normal"), (3, "high"), (4, "now"), (5, "now"), (0, None)])
def test_a_priority_set_in_vikunja_reaches_the_entry(linked, number, level):
    api, mirror, fake, task = linked
    api.patch("/api/v1/entries/no-ci", json={"priority": "low" if number != 1 else "high"}).raise_for_status()
    mirror.process_outbox()
    task = task_of(api, fake)  # the fake replaces the dict of a task it writes
    task["priority"] = number
    assert send_webhook(api, "task.updated", task).json() == {"result": "changed"}
    assert api.get("/api/v1/entries/no-ci").json()["priority"] == level
    mirror.reconcile()
    mirror.process_outbox()
    assert task["priority"] == number  # the echo changes nothing in the task


def test_a_task_a_person_left_at_urgent_stays_there_until_the_level_changes(linked):
    api, mirror, fake, task = linked
    task["priority"] = 4
    send_webhook(api, "task.updated", task)
    assert api.get("/api/v1/entries/no-ci").json()["priority"] == "now"
    api.patch("/api/v1/entries/no-ci", json={"title": "Renamed"}).raise_for_status()
    mirror.process_outbox()
    assert task_of(api, fake)["priority"] == 4 and task_of(api, fake)["title"] == "Renamed"
    api.patch("/api/v1/entries/no-ci", json={"priority": "high"}).raise_for_status()
    mirror.process_outbox()
    assert task_of(api, fake)["priority"] == 3
    api.patch("/api/v1/entries/no-ci", json={"priority": "now"}).raise_for_status()
    mirror.process_outbox()
    assert task_of(api, fake)["priority"] == 5


def test_the_echo_of_a_priority_write_queues_nothing_and_changes_no_version(linked):
    api, mirror, fake, task = linked
    api.patch("/api/v1/entries/no-ci", json={"priority": "high"}).raise_for_status()
    mirror.process_outbox()
    version = api.get("/api/v1/entries/no-ci").json()["version"]
    assert send_webhook(api, "task.updated", task_of(api, fake)).json() == {"result": "same"}
    assert mirror.reconcile() == {"same": 1}
    assert api.get("/api/v1/entries/no-ci").json()["version"] == version
    assert api.get("/api/v1/admin/outbox").json() == []


def test_clearing_the_priority_in_vikunja_clears_it_in_the_store(linked):
    api, mirror, fake, task = linked
    api.patch("/api/v1/entries/no-ci", json={"priority": "high"}).raise_for_status()
    mirror.process_outbox()
    task = task_of(api, fake)
    task["priority"] = 0
    send_webhook(api, "task.updated", task)
    assert api.get("/api/v1/entries/no-ci").json()["priority"] is None
    event = api.get("/api/v1/entries/no-ci/events").json()[0]
    assert event["actor"] == "vikunja" and event["data"] == {"old": {"priority": "high"}, "new": {"priority": None}}


def without_priority_in_snapshot(api, entry_id="no-ci"):
    """The state of an entry written before the priority was mirrored."""
    with psycopg_connect(api) as conn:
        conn.execute("UPDATE entries SET vikunja_snapshot = vikunja_snapshot - 'priority' WHERE id = %s", (entry_id,))


def test_an_old_snapshot_adopts_the_priority_the_owner_set_in_vikunja(linked):
    api, mirror, fake, task = linked
    without_priority_in_snapshot(api)
    task["priority"] = 3
    assert send_webhook(api, "task.updated", task).json() == {"result": "changed"}
    assert api.get("/api/v1/entries/no-ci").json()["priority"] == "high"
    mirror.process_outbox()
    assert task_of(api, fake)["priority"] == 3  # not wiped


def test_an_old_snapshot_with_no_priority_anywhere_changes_nothing(linked):
    api, mirror, fake, task = linked
    without_priority_in_snapshot(api)
    version = api.get("/api/v1/entries/no-ci").json()["version"]
    assert send_webhook(api, "task.updated", task).json() == {"result": "same"}
    assert api.get("/api/v1/entries/no-ci").json()["version"] == version


def test_an_old_snapshot_keeps_the_stores_priority_and_writes_it(linked):
    api, mirror, fake, task = linked
    api.patch("/api/v1/entries/no-ci", json={"priority": "low"}).raise_for_status()
    mirror.process_outbox()
    without_priority_in_snapshot(api)
    task = task_of(api, fake)
    task["priority"] = 5
    assert send_webhook(api, "task.updated", task).json() == {"result": "same"}
    assert api.get("/api/v1/entries/no-ci").json()["priority"] == "low"
    mirror.process_outbox()  # queued by the webhook: the store's level is written
    assert task_of(api, fake)["priority"] == 1


def test_a_task_made_in_vikunja_with_a_priority_becomes_an_entry_with_it(mirrored):
    api, mirror, fake = mirrored
    mirror.reconcile()
    chess = project(fake, "BagiEngine-Chess")
    task = fake.add_task(chess["id"], "Show the clock", priority=5)
    assert send_webhook(api, "task.created", task).json() == {"result": "adopted"}
    assert api.get("/api/v1/entries/show-the-clock").json()["priority"] == "now"
    mirror.process_outbox()
    assert fake.tasks[task["id"]]["priority"] == 5
    plain = fake.add_task(chess["id"], "Plain")
    send_webhook(api, "task.created", plain)
    assert api.get("/api/v1/entries/plain").json()["priority"] is None


def test_a_plan_shows_its_checklist_in_the_task(mirrored):
    api, mirror, fake = mirrored
    add(api)
    api.put("/api/v1/plans/todo-loop-no-ci.md", json={"content": "# Slice\n## WP 1\n- [x] a\n- [ ] b\n", "entry": "no-ci", "kind": "slice"})
    mirror.process_outbox()
    description = task_of(api, fake)["description"]
    assert "<p>todo-sync:plan=todo-loop-no-ci.md</p><h3>Slice</h3><h4>WP 1</h4>" in description
    assert description.count('data-type="taskItem"') == 2


def test_a_failed_write_is_retried_later(mirrored):
    api, mirror, fake = mirrored
    add(api)
    fake.fail_next = 1
    assert mirror.process_outbox() == 0
    outbox = api.get("/api/v1/admin/outbox").json()
    assert outbox[0]["attempts"] == 1 and "500" in outbox[0]["last_error"]
    api.patch("/api/v1/entries/no-ci", json={"title": "Changed"})  # a new write makes it due again
    assert mirror.process_outbox() == 1
    assert task_of(api, fake)["title"] == "Changed"


# --- what a person does in Vikunja -------------------------------------------------------------


@pytest.fixture
def linked(mirrored):
    api, mirror, fake = mirrored
    add(api)
    api.post("/api/v1/entries/no-ci/claim", json={"branch": "todo/no-ci", "base": "main", "origin": "game@host"}).raise_for_status()
    mirror.process_outbox()
    return api, mirror, fake, task_of(api, fake)


def test_a_task_closed_in_vikunja_closes_the_entry(linked):
    api, mirror, fake, task = linked
    task["done"] = True
    assert send_webhook(api, "task.updated", task).json() == {"result": "changed"}
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["status"] == "closed" and entry["branch"] is None
    assert entry["base"] is None and entry["origin"] is None
    mirror.process_outbox()
    assert labels(task_of(api, fake)) == ["stage 1"]  # the status label goes with the closure


def test_a_title_and_a_body_edited_in_vikunja_reach_the_entry(linked):
    api, mirror, fake, task = linked
    task["title"] = "No CI (renamed)"
    task["description"] = task["description"].replace("<li><p>two</p></li>", "<li><p>two</p></li><li><p>three</p></li>")
    send_webhook(api, "task.updated", task)
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["title"] == "No CI (renamed)"
    assert entry["body"].endswith("- one\n- two\n- three\n")
    assert entry["status"] == "in progress"  # untouched
    kinds = [event["kind"] for event in api.get("/api/v1/entries/no-ci/events").json()]
    assert kinds[0] == "vikunja"


def test_labels_set_in_vikunja_change_status_and_stage_but_never_to_review(linked):
    api, mirror, fake, task = linked
    task["labels"] = [fake.add_label("deferred"), fake.add_label("stage 3")]
    send_webhook(api, "task.updated", task)
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["status"] == "deferred" and entry["stage"] == 3
    task["labels"] = [fake.add_label("review")]
    send_webhook(api, "task.updated", task)
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["status"] == "open" and entry["stage"] is None
    assert entry["branch"] is None and entry["base"] is None and entry["origin"] is None


def test_the_echo_of_the_mirrors_own_write_changes_nothing(linked):
    api, mirror, fake, task = linked
    version = api.get("/api/v1/entries/no-ci").json()["version"]
    assert send_webhook(api, "task.updated", task).json() == {"result": "same"}
    assert api.get("/api/v1/entries/no-ci").json()["version"] == version


def test_an_edit_made_before_a_pending_write_is_not_overwritten(linked):
    api, mirror, fake, task = linked
    task["title"] = "Renamed in Vikunja"  # the webhook was lost
    api.put("/api/v1/plans/p.md", json={"content": "- [ ] a", "entry": "no-ci", "kind": "slice"}).raise_for_status()
    mirror.process_outbox()
    assert api.get("/api/v1/entries/no-ci").json()["title"] == "Renamed in Vikunja"
    assert task_of(api, fake)["title"] == "Renamed in Vikunja"


def test_a_task_made_in_vikunja_becomes_an_entry(mirrored):
    api, mirror, fake = mirrored
    mirror.reconcile()  # resolves the projects
    chess = project(fake, "BagiEngine-Chess")
    task = fake.add_task(chess["id"], "Show the clock", description="<p>Both sides need a clock.</p>")
    task["labels"] = [fake.add_label("stage 2")]
    assert send_webhook(api, "task.created", task).json() == {"result": "adopted"}
    entry = api.get("/api/v1/entries/show-the-clock").json()
    assert entry["area"] == "chess" and entry["stage"] == 2 and entry["body"] == "Both sides need a clock.\n"
    mirror.process_outbox()
    assert fake.tasks[task["id"]]["description"].startswith("<p>todo-sync:id=show-the-clock</p>")
    assert len(fake.tasks_of(chess["id"])) == 1  # written back into the same task, no duplicate


def test_a_deleted_task_closes_the_entry_and_is_not_recreated(linked):
    api, mirror, fake, task = linked
    del fake.tasks[task["id"]]
    assert send_webhook(api, "task.deleted", {"id": task["id"]}).json() == {"result": "closed"}
    entry = api.get("/api/v1/entries/no-ci").json()
    assert entry["status"] == "closed" and entry["vikunjaTaskId"] is None
    assert entry["branch"] is None and entry["base"] is None and entry["origin"] is None
    mirror.process_outbox()
    assert fake.tasks_of(task["project_id"]) == []


def test_a_webhook_with_a_bad_signature_is_refused(linked):
    api, mirror, fake, task = linked
    task["done"] = True
    assert send_webhook(api, "task.updated", task, secret="wrong").status_code == 401
    assert api.get("/api/v1/entries/no-ci").json()["status"] == "in progress"


def test_reconcile_catches_a_lost_webhook_and_restores_a_lost_marker(linked):
    api, mirror, fake, task = linked
    task["description"] = task["description"].replace("<p>todo-sync:id=no-ci</p>", "")  # a stale browser tab
    task["done"] = True
    report = mirror.reconcile()
    assert report == {"changed": 1}
    assert api.get("/api/v1/entries/no-ci").json()["status"] == "closed"
    mirror.process_outbox()
    assert task_of(api, fake)["description"].startswith("<p>todo-sync:id=no-ci</p>")


def test_reconcile_links_the_tasks_of_the_old_push_by_their_marker(mirrored):
    api, mirror, fake = mirrored
    mirror.reconcile()
    engine = project(fake, "BagiEngine")
    old = fake.add_task(engine["id"], "No CI", description="<p>todo-sync:id=no-ci</p><p>old summary</p>")
    api.post("/api/v1/entries", json=ENTRY).raise_for_status()
    with_outbox_cleared = api.get("/api/v1/admin/outbox").json()
    assert with_outbox_cleared  # queued by the create; reconcile links before the worker creates a task
    assert mirror.reconcile() == {"linked": 1}
    mirror.process_outbox()
    assert len(fake.tasks_of(engine["id"])) == 1
    assert api.get("/api/v1/entries/no-ci").json()["vikunjaTaskId"] == old["id"]
    assert "<hr>" in fake.tasks[old["id"]]["description"]


def test_webhooks_are_installed_once_per_project(clean_database, fake_vikunja):
    with make_client(clean_database, fake_vikunja, public_url="https://todo.example") as api:
        api.put("/api/v1/areas/engine", json={"title": "Engine", "vikunjaProject": "BagiEngine"}).raise_for_status()
        assert api.post("/api/v1/admin/vikunja/webhooks").json() == {"lines": ["engine: webhook created"]}
        assert api.post("/api/v1/admin/vikunja/webhooks").json() == {"lines": ["engine: webhook present"]}
        hook = next(iter(fake_vikunja.webhooks.values()))[0]
        assert hook["target_url"] == "https://todo.example/api/v1/vikunja/webhook"
        assert hook["events"] == ["task.created", "task.updated", "task.deleted"] and hook["secret"] == WEBHOOK_SECRET


def test_the_worker_thread_writes_a_new_entry_by_itself(mirrored):
    import time

    api, mirror, fake = mirrored
    mirror.start()
    try:
        add(api)
        for _ in range(100):
            if api.get("/api/v1/entries/no-ci").json()["vikunjaTaskId"]:
                break
            time.sleep(0.05)
        assert task_of(api, fake)["title"] == "No CI"
    finally:
        mirror.stop()


def test_a_task_marked_with_an_old_id_links_to_the_entry_that_has_it_as_alias(mirrored):
    api, mirror, fake = mirrored
    mirror.reconcile()
    engine = project(fake, "BagiEngine")
    old = fake.add_task(engine["id"], "No CI", description="<p>todo-sync:id=no-ci-builds-and-tests-run-only-on-a-developer-machine</p>")
    api.post("/api/v1/entries", json={**ENTRY, "aliases": ["no-ci-builds-and-tests-run-only-on-a-developer-machine"]}).raise_for_status()
    assert mirror.reconcile() == {"linked": 1}
    mirror.process_outbox()
    assert len(fake.tasks_of(engine["id"])) == 1
    assert fake.tasks[old["id"]]["description"].startswith("<p>todo-sync:id=no-ci</p>")


def test_an_area_that_gets_its_project_links_the_old_tasks_instead_of_making_new_ones(clean_database, fake_vikunja):
    """The import of 2026-11-06: entries made while the area had no project, then the project set."""
    with make_client(clean_database, fake_vikunja) as api:
        mirror = api.app.state.todo["mirror"]
        engine = fake_vikunja.add_project("BagiEngine")
        old = fake_vikunja.add_task(engine["id"], "No CI", description="<p>todo-sync:id=no-ci-builds-and-tests</p>")
        api.put("/api/v1/areas/engine", json={"title": "Engine", "stages": [{"number": 1, "title": "Core"}]}).raise_for_status()
        api.post("/api/v1/entries", json={**ENTRY, "aliases": ["no-ci-builds-and-tests"]}).raise_for_status()
        assert mirror.process_outbox() == 1  # no project yet: nothing written
        assert fake_vikunja.tasks_of(engine["id"]) == [old]
        api.put(
            "/api/v1/areas/engine", json={"title": "Engine", "vikunjaProject": "BagiEngine", "stages": [{"number": 1, "title": "Core"}]}
        ).raise_for_status()
        mirror.process_outbox()  # the worker runs before any reconcile
        assert [task["id"] for task in fake_vikunja.tasks_of(engine["id"])] == [old["id"]]
        assert api.get("/api/v1/entries/no-ci").json()["vikunjaTaskId"] == old["id"]
        assert fake_vikunja.tasks[old["id"]]["description"].startswith("<p>todo-sync:id=no-ci</p>")


def test_dedupe_keeps_the_oldest_open_task_and_deletes_the_newer(mirrored):
    api, mirror, fake = mirrored
    mirror.reconcile()
    engine = project(fake, "BagiEngine")
    old = fake.add_task(engine["id"], "No CI", description="<p>todo-sync:id=no-ci-old</p>")
    done = fake.add_task(engine["id"], "No CI", description="<p>todo-sync:id=no-ci</p>", done=True)
    api.post("/api/v1/entries", json=ENTRY).raise_for_status()
    api.post("/api/v1/entries", json={**ENTRY, "id": "other", "title": "Other"}).raise_for_status()
    new = fake.add_task(engine["id"], "No CI", description="<p>todo-sync:id=no-ci</p>")
    with psycopg_connect(api) as conn:  # the state the bug left: the entry linked to the newer task
        conn.execute("UPDATE entries SET vikunja_task_id = %s WHERE id = 'no-ci'", (new["id"],))
    with psycopg_connect(api) as conn:
        conn.execute("UPDATE entries SET aliases = '{no-ci-old}' WHERE id = 'no-ci'")
    assert api.post("/api/v1/admin/vikunja/dedupe").json() == {"lines": [f"no-ci: keep task {old['id']}, delete {new['id']}"]}
    assert new["id"] in fake.tasks  # the report changes nothing
    api.post("/api/v1/admin/vikunja/dedupe", params={"apply": "true"}).raise_for_status()
    assert new["id"] not in fake.tasks and old["id"] in fake.tasks and done["id"] in fake.tasks
    mirror.process_outbox()
    assert api.get("/api/v1/entries/no-ci").json()["vikunjaTaskId"] == old["id"]
    assert fake.tasks[old["id"]]["description"].startswith("<p>todo-sync:id=no-ci</p>")
    assert api.post("/api/v1/admin/vikunja/dedupe").json() == {"lines": ["no duplicates"]}


def psycopg_connect(api):
    import psycopg

    return psycopg.connect(api.app.state.todo["pool"].conninfo, autocommit=True)
