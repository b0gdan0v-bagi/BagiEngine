import copy

import pytest

import vikunja
from fake_vikunja import FakeVikunja
from todo_parser import parse_todo
from vikunja import (
    CLOSE,
    CREATE,
    REOPEN,
    UNCHANGED,
    UPDATE,
    VikunjaClient,
    VikunjaError,
    load_settings,
    marker_of,
    read_env_file,
    sync,
)

PROJECT = "BagiEngine"


@pytest.fixture
def fake():
    server = FakeVikunja()
    server.start()
    yield server
    server.stop()


@pytest.fixture
def client(fake):
    return VikunjaClient(fake.url, fake.token)


@pytest.fixture
def data(sample_todo, sample_plans):
    return parse_todo(sample_todo, sample_plans)


def entry(data, entry_id):
    return next(item for item in data["entries"] if item["id"] == entry_id)


def task_of(fake, entry_id):
    return next(task for task in fake.tasks.values() if marker_of(task) == entry_id)


def label_titles(task):
    return sorted(label["title"] for label in task["labels"])


def test_first_sync_creates_the_project_labels_and_a_task_per_entry(fake, client, data):
    report = sync(client, data, PROJECT)

    assert report.counts[CREATE] == len(data["entries"])
    assert [project["title"] for project in fake.projects] == [PROJECT]
    # one label per status and per stage in use, the unknown stage 9 of the fixture included
    assert {label["title"] for label in fake.labels.values()} == {
        "deferred",
        "in progress",
        "review",
        "stage 1",
        "stage 2",
        "stage 3",
        "stage 9",
    }
    tasks = fake.tasks_of(fake.projects[0]["id"])
    assert len(tasks) == len(data["entries"])
    assert all(task["description"].startswith("<p>todo-sync:id=") for task in tasks)
    assert label_titles(task_of(fake, "deferred")) == ["deferred", "stage 1"]
    assert label_titles(task_of(fake, "bare-entry")) == []


def test_second_sync_changes_nothing(fake, client, data):
    sync(client, data, PROJECT)
    fake.requests.clear()

    report = sync(client, data, PROJECT)

    assert fake.writes == []
    assert report.counts[UNCHANGED] == len(data["entries"])
    assert report.lines == []


def test_removed_entry_is_closed_not_deleted_and_reopened_when_it_returns(fake, client, data):
    sync(client, data, PROJECT)
    shorter = copy.deepcopy(data)
    shorter["entries"] = [item for item in shorter["entries"] if item["id"] != "deferred"]

    report = sync(client, shorter, PROJECT)

    assert report.counts[CLOSE] == 1
    assert task_of(fake, "deferred")["done"] is True
    assert len(fake.tasks) == len(data["entries"])  # nothing deleted
    assert sync(client, shorter, PROJECT).counts[CLOSE] == 0  # a closed task is not closed again

    report = sync(client, data, PROJECT)

    assert report.counts[REOPEN] == 1
    assert task_of(fake, "deferred")["done"] is False


def test_status_change_swaps_the_managed_labels_and_keeps_foreign_ones(fake, client, data):
    sync(client, data, PROJECT)
    task = task_of(fake, "open-with-stage")
    foreign = fake.add_label("my own label")
    task["labels"].append(foreign)
    changed = copy.deepcopy(data)
    entry(changed, "open-with-stage")["status"] = "in progress"

    report = sync(client, changed, PROJECT)

    assert report.counts[UPDATE] == 1
    assert label_titles(task_of(fake, "open-with-stage")) == ["in progress", "my own label", "stage 3"]
    changed_back = sync(client, data, PROJECT)
    assert changed_back.counts[UPDATE] == 1
    assert label_titles(task_of(fake, "open-with-stage")) == ["my own label", "stage 3"]


def test_review_label_is_set_and_removed_with_the_status(fake, client, data):
    sync(client, data, PROJECT)
    assert label_titles(task_of(fake, "in-review")) == ["review", "stage 3"]
    merged = copy.deepcopy(data)
    entry(merged, "in-review")["status"] = "open"

    report = sync(client, merged, PROJECT)

    assert report.counts[UPDATE] == 1
    assert label_titles(task_of(fake, "in-review")) == ["stage 3"]
    back = sync(client, data, PROJECT)
    assert back.counts[UPDATE] == 1
    assert label_titles(task_of(fake, "in-review")) == ["review", "stage 3"]


def test_update_keeps_priority_and_due_date_set_in_the_tracker(fake, client, data):
    sync(client, data, PROJECT)
    task = task_of(fake, "open-with-stage")
    task.update(priority=5, due_date="2027-05-05T09:00:00Z")
    changed = copy.deepcopy(data)
    entry(changed, "open-with-stage")["title"] = "A renamed entry"

    sync(client, changed, PROJECT)

    task = task_of(fake, "open-with-stage")
    assert task["title"] == "A renamed entry"
    assert (task["priority"], task["due_date"]) == (5, "2027-05-05T09:00:00Z")


def test_closing_and_reopening_keep_the_tracker_fields_too(fake, client, data):
    sync(client, data, PROJECT)
    task_of(fake, "deferred").update(priority=2)
    shorter = copy.deepcopy(data)
    shorter["entries"] = [item for item in shorter["entries"] if item["id"] != "deferred"]

    sync(client, shorter, PROJECT)
    sync(client, data, PROJECT)

    assert task_of(fake, "deferred")["priority"] == 2


def test_dry_run_on_an_empty_tracker_reports_creates_and_writes_nothing(fake, client, data):
    report = sync(client, data, PROJECT, dry_run=True)

    assert fake.writes == []
    assert report.counts[CREATE] == len(data["entries"])
    assert any("would create" in line for line in report.lines)


def test_dry_run_reports_a_change_and_writes_nothing(fake, client, data):
    sync(client, data, PROJECT)
    changed = copy.deepcopy(data)
    entry(changed, "deferred")["status"] = "open"
    fake.requests.clear()

    report = sync(client, changed, PROJECT, dry_run=True)

    assert fake.writes == []
    assert report.counts[UPDATE] == 1
    assert report.lines == ["update  deferred (description, labels)"]
    assert label_titles(task_of(fake, "deferred")) == ["deferred", "stage 1"]  # still as before


def test_a_task_without_the_marker_is_never_touched(fake, client, data):
    project = fake.add_project(PROJECT)
    own = fake.add_task(project["id"], "My own task", description="<p>notes</p>")

    sync(client, data, PROJECT)
    sync(client, copy.deepcopy(data) | {"entries": []}, PROJECT)  # every entry gone

    assert fake.tasks[own["id"]]["done"] is False
    assert fake.tasks[own["id"]]["title"] == "My own task"


def test_an_existing_project_is_reused(fake, client, data):
    fake.add_project(PROJECT)

    sync(client, data, PROJECT)

    assert ("PUT", "/api/v1/projects") not in fake.requests
    assert len(fake.projects) == 1


def test_a_duplicate_marker_warns_and_the_first_task_wins(fake, client, data):
    project = fake.add_project(PROJECT)
    fake.add_task(project["id"], "Old copy", description="<p>todo-sync:id=deferred</p>")
    fake.add_task(project["id"], "Newer copy", description="<p>todo-sync:id=deferred</p>")

    report = sync(client, data, PROJECT)

    assert any("more than one task" in warning for warning in report.warnings)
    titles = {task["title"] for task in fake.tasks.values()}
    assert "Newer copy" in titles  # left alone
    assert "Old copy" not in titles  # renamed to the entry title


def test_every_page_of_a_long_listing_is_read(fake, client, data, monkeypatch):
    monkeypatch.setattr(vikunja, "PAGE_SIZE", 2)
    sync(client, data, PROJECT)
    fake.requests.clear()

    report = sync(client, data, PROJECT)

    assert report.counts[UNCHANGED] == len(data["entries"])  # a missed page would show up as creates
    assert fake.writes == []


def test_text_from_todo_md_is_escaped_in_the_description(fake, client, data):
    evil = copy.deepcopy(data)
    entry(evil, "bare-entry")["summary"] = "<script>alert(1)</script> & more"

    sync(client, evil, PROJECT)

    description = task_of(fake, "bare-entry")["description"]
    assert "<script>" not in description
    assert "&lt;script&gt;alert(1)&lt;/script&gt; &amp; more" in description


def test_description_carries_status_stage_plan_and_progress(fake, client, data):
    sync(client, data, PROJECT)

    description = task_of(fake, "in-progress-with-plan")["description"]

    assert description.startswith("<p>todo-sync:id=in-progress-with-plan</p>")
    assert "in progress since 2026-11-01" in description
    assert "2: Control API (in progress)" in description
    assert "<strong>Plan:</strong> sample-plan.md</p>" in description
    assert "<p>todo-sync:plan=sample-plan.md</p><h3>Sample plan</h3>" in description
    assert description.count('data-checked="true"') == 2
    assert description.count('data-checked="false"') == 2


def test_a_wrong_token_is_an_error_that_does_not_show_the_token(fake, data):
    client = VikunjaClient(fake.url, "tk_secret_value")

    with pytest.raises(VikunjaError) as error:
        sync(client, data, PROJECT)

    assert "401" in str(error.value)
    assert "tk_secret_value" not in str(error.value)


def test_an_unreachable_server_is_a_vikunja_error(data):
    client = VikunjaClient("http://127.0.0.1:9", "tk_x", timeout=2)  # the discard port: nothing listens

    with pytest.raises(VikunjaError):
        sync(client, data, PROJECT)


def test_the_cli_pushes_with_settings_from_the_environment(fake, data, sample_todo, sample_root, sample_plans, monkeypatch, capsys):
    import todo_sync

    monkeypatch.setenv("VIKUNJA_URL", fake.url)
    monkeypatch.setenv("VIKUNJA_TOKEN", fake.token)
    monkeypatch.setenv("VIKUNJA_PROJECT", "From env")
    monkeypatch.setattr("sys.argv", ["todo_sync.py", "--todo", str(sample_todo), "--root", str(sample_root), "--plans-dir", str(sample_plans), "push", "--dry-run"])

    assert todo_sync.main() == 0

    output = capsys.readouterr().out
    assert f"dry run, nothing was changed: {len(data['entries'])} create" in output
    assert "project 'From env'" in output
    assert fake.writes == []


def test_the_cli_reports_missing_settings(sample_todo, sample_root, monkeypatch, capsys, tmp_path):
    import todo_sync

    for name in ("VIKUNJA_URL", "VIKUNJA_TOKEN", "VIKUNJA_PROJECT"):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setattr(
        "sys.argv",
        ["todo_sync.py", "--todo", str(sample_todo), "--root", str(sample_root), "push", "--env-file", str(tmp_path / "none.env")],
    )

    assert todo_sync.main() == 1
    assert "VIKUNJA_URL, VIKUNJA_TOKEN not set" in capsys.readouterr().err


def test_settings_prefer_the_environment_over_the_env_file(tmp_path):
    env_file = tmp_path / ".env"
    env_file.write_text(
        '# a comment\nLIBCLANG_PATH=/x\nVIKUNJA_URL="http://from-file"\nexport VIKUNJA_TOKEN=tk_file\nVIKUNJA_PROJECT=From file\n',
        encoding="utf-8",
    )

    settings = load_settings(env={"VIKUNJA_URL": "http://from-env"}, env_file=env_file)

    assert settings.url == "http://from-env"
    assert settings.token == "tk_file"
    assert settings.project == "From file"
    assert "tk_file" not in repr(settings)


def test_settings_default_project_and_explicit_override(tmp_path):
    env = {"VIKUNJA_URL": "http://x", "VIKUNJA_TOKEN": "tk"}

    assert load_settings(env=env).project == "BagiEngine"
    assert load_settings(env=env, project="Other").project == "Other"


def test_missing_settings_name_what_is_missing(tmp_path):
    with pytest.raises(VikunjaError, match="VIKUNJA_URL, VIKUNJA_TOKEN"):
        load_settings(env={}, env_file=tmp_path / "missing.env")


def test_read_env_file_skips_comments_and_strips_quotes(tmp_path):
    path = tmp_path / ".env"
    path.write_text("# FOO=1\n\nA=1\nB='two words'\nC = \"3\"\n", encoding="utf-8")

    assert read_env_file(path) == {"A": "1", "B": "two words", "C": "3"}
    assert read_env_file(tmp_path / "absent") == {}


# --- the copy of a plan in the task ---------------------------------------------------------------


def plan_of(task):
    return vikunja.plan_block_of(task)


def test_a_plan_block_is_a_task_list_and_escapes_the_text():
    checklist = {
        "title": "A <b>plan</b>",
        "sections": [
            {"heading": "A <b>plan</b>", "items": [{"done": True, "text": "uses `code` & **bold**"}]},
            {"heading": "Later", "items": [{"done": False, "text": "<script>x</script>"}]},
        ],
    }

    block = vikunja.render_plan_block("p.md", checklist)

    assert block.startswith("<p>todo-sync:plan=p.md</p><h3>A &lt;b&gt;plan&lt;/b&gt;</h3><ul data-type=\"taskList\">")
    assert '<li data-checked="true" data-type="taskItem">' in block
    assert "<code>code</code> &amp; <strong>bold</strong>" in block
    assert "<h4>Later</h4>" in block  # a heading equal to the title is not repeated
    assert "<script>" not in block


def test_a_long_plan_is_cut_with_a_visible_line():
    items = [{"done": False, "text": f"step {number}"} for number in range(vikunja.PLAN_MAX_ITEMS + 5)]

    block = vikunja.render_plan_block("p.md", {"title": "T", "sections": [{"heading": None, "items": items}]})

    assert block.count("data-type=\"taskItem\"") == vikunja.PLAN_MAX_ITEMS
    assert f"cut: {vikunja.PLAN_MAX_ITEMS} of {vikunja.PLAN_MAX_ITEMS + 5} items shown" in block


def test_pushing_a_plan_twice_changes_nothing(fake, client, data):
    sync(client, data, PROJECT)
    writes = len(fake.writes)

    report = sync(client, data, PROJECT)

    assert report.counts[UNCHANGED] == len(data["entries"])
    assert len(fake.writes) == writes


def test_a_ticked_box_updates_the_copy(fake, client, data):
    sync(client, data, PROJECT)
    ticked = copy.deepcopy(data)
    item = entry(ticked, "in-progress-with-plan")["planChecklist"]["sections"][0]["items"][2]
    item["done"] = True

    report = sync(client, ticked, PROJECT)

    assert report.lines == ["update  in-progress-with-plan (description)"]
    assert plan_of(task_of(fake, "in-progress-with-plan"))[1].count('data-checked="true"') == 3


def test_a_missing_plan_file_keeps_the_copy(fake, client, data):
    sync(client, data, PROJECT)
    before = task_of(fake, "in-progress-with-plan")["description"]
    other_machine = copy.deepcopy(data)
    this = entry(other_machine, "in-progress-with-plan")
    this.update(planFound=False, planProgress=None, planChecklist=None)

    report = sync(client, other_machine, PROJECT)

    assert report.counts[UNCHANGED] == len(data["entries"])
    assert task_of(fake, "in-progress-with-plan")["description"] == before


def test_removing_the_plan_line_drops_the_copy(fake, client, data):
    sync(client, data, PROJECT)
    finished = copy.deepcopy(data)
    entry(finished, "in-progress-with-plan").update(plan=None, planFound=False, planProgress=None, planChecklist=None)

    sync(client, finished, PROJECT)

    description = task_of(fake, "in-progress-with-plan")["description"]
    assert "todo-sync:plan" not in description
    assert "<strong>Plan:</strong>" not in description
    assert description.startswith("<p>todo-sync:id=in-progress-with-plan</p>")


def test_a_copy_of_another_plan_is_not_kept_when_the_file_is_missing(fake, client, data):
    sync(client, data, PROJECT)
    renamed = copy.deepcopy(data)
    entry(renamed, "in-progress-with-plan").update(plan="other.md", planFound=False, planProgress=None, planChecklist=None)

    sync(client, renamed, PROJECT)

    assert "todo-sync:plan" not in task_of(fake, "in-progress-with-plan")["description"]


def test_closing_an_entry_cuts_the_copy_but_keeps_the_text(fake, client, data):
    sync(client, data, PROJECT)
    shorter = copy.deepcopy(data)
    shorter["entries"] = [item for item in shorter["entries"] if item["id"] != "in-progress-with-plan"]

    report = sync(client, shorter, PROJECT)

    task = task_of(fake, "in-progress-with-plan")
    assert task["done"] is True
    assert "todo-sync:plan" not in task["description"]
    assert "A transport." in task["description"]
    assert "close   in-progress-with-plan (plan copy removed)" in report.lines


def test_one_plan_can_stand_behind_three_entries(fake, client, data):
    shared = copy.deepcopy(data)
    template = entry(shared, "in-progress-with-plan")
    for number in (1, 2):
        extra = copy.deepcopy(template)
        extra.update(id=f"shared-{number}", title=f"Shared {number}")
        shared["entries"].append(extra)

    sync(client, shared, PROJECT)

    blocks = {plan_of(task_of(fake, entry_id))[1] for entry_id in ("in-progress-with-plan", "shared-1", "shared-2")}
    assert len(blocks) == 1
    assert sync(client, shared, PROJECT).counts[UNCHANGED] == len(shared["entries"])
