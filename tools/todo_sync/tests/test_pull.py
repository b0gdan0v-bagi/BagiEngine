import shutil
import subprocess
from datetime import date

import pytest

from fake_vikunja import FakeVikunja
from pull import entry_text, html_to_markdown, pull
from todo_parser import parse_todo
from vikunja import VikunjaClient, marker_of, sync

PROJECT = "BagiEngine"
TODAY = date(2026, 11, 3)


@pytest.fixture
def fake():
    server = FakeVikunja()
    server.start()
    yield server
    server.stop()


@pytest.fixture
def client(fake):
    return VikunjaClient(fake.url, fake.token)


def git(folder, *args):
    subprocess.run(
        ["git", "-c", "user.email=t@example.com", "-c", "user.name=t", *args], cwd=folder, check=True, capture_output=True
    )


@pytest.fixture
def todo(tmp_path, sample_todo, sample_plans, fake, client):
    """A git repository with a copy of the sample todo/ folder, all committed and pushed to the fake tracker."""
    folder = tmp_path / "repo" / "todo"
    shutil.copytree(sample_todo, folder)
    git(folder.parent, "init", "-q")
    git(folder.parent, "add", ".")
    git(folder.parent, "commit", "-q", "-m", "entries")
    sync(client, parse_todo(folder, sample_plans), PROJECT)
    return folder


def task_of(fake, entry_id):
    return next(task for task in fake.tasks.values() if marker_of(task) == entry_id)


def run_pull(client, todo, sample_plans, apply=False):
    return pull(client, todo, sample_plans, PROJECT, apply=apply, today=TODAY)


def add_unmanaged(fake, title, description="", labels=()):
    project = fake.projects[0]
    task = fake.add_task(project["id"], title, description=description)
    task["labels"] = [fake.add_label(name) for name in labels]
    return task


def test_report_only_changes_nothing(fake, client, todo, sample_plans):
    task_of(fake, "deferred")["done"] = True
    add_unmanaged(fake, "A task made in the UI")
    writes = len(fake.writes)

    report = run_pull(client, todo, sample_plans)

    assert (report.closed, report.adopted) == (1, 1)
    assert any(line.startswith("closed") and "deferred" in line for line in report.lines)
    assert len(fake.writes) == writes
    assert (todo / "deferred.md").exists()
    assert not (todo / "a-task-made-in-the-ui.md").exists()


def test_a_done_task_deletes_a_clean_tracked_file(fake, client, todo, sample_plans):
    task_of(fake, "deferred")["done"] = True

    report = run_pull(client, todo, sample_plans, apply=True)

    assert not (todo / "deferred.md").exists()
    assert report.warnings == []


def test_a_done_task_keeps_a_modified_file(fake, client, todo, sample_plans):
    task_of(fake, "deferred")["done"] = True
    with (todo / "deferred.md").open("a", encoding="utf-8") as handle:
        handle.write("\nAn unfinished edit.\n")

    report = run_pull(client, todo, sample_plans, apply=True)

    assert (todo / "deferred.md").exists()
    assert any("uncommitted" in warning for warning in report.warnings)


def test_a_done_task_keeps_an_untracked_file(fake, client, todo, sample_plans):
    git(todo.parent, "rm", "-q", "--cached", "todo/deferred.md")
    task_of(fake, "deferred")["done"] = True

    report = run_pull(client, todo, sample_plans, apply=True)

    assert (todo / "deferred.md").exists()
    assert any("does not track" in warning for warning in report.warnings)


def test_a_done_task_whose_file_is_gone_is_consistent(fake, client, todo, sample_plans):
    task_of(fake, "deferred")["done"] = True
    (todo / "deferred.md").unlink()

    report = run_pull(client, todo, sample_plans, apply=True)

    assert (report.closed, report.adopted) == (0, 0)


def test_a_task_made_in_vikunja_is_adopted_and_gets_the_marker(fake, client, todo, sample_plans):
    add_unmanaged(
        fake,
        "A task made in the UI",
        description="<p>What is <strong>missing</strong> here</p><ul><li>one</li><li>two</li></ul>",
        labels=("deferred", "stage 2", "my own label"),
    )

    report = run_pull(client, todo, sample_plans, apply=True)

    text = (todo / "a-task-made-in-the-ui.md").read_text(encoding="utf-8")
    assert text.startswith("---\nstatus: deferred 2026-11-03\nstage: 2\n---\n# A task made in the UI\n")
    assert "What is **missing** here" in text
    assert "- one\n- two" in text
    assert report.adopted == 1
    task = task_of(fake, "a-task-made-in-the-ui")
    assert task["description"].startswith("<p>todo-sync:id=a-task-made-in-the-ui</p>")

    # the next push finds the task as it is: no duplicate, no change
    push = sync(client, parse_todo(todo, sample_plans), PROJECT, dry_run=True)
    assert push.counts["create"] == 0
    assert push.counts["update"] == 0


def test_a_name_clash_gets_a_suffix(fake, client, todo, sample_plans):
    add_unmanaged(fake, "Deferred")
    add_unmanaged(fake, "Deferred!")

    run_pull(client, todo, sample_plans, apply=True)

    assert (todo / "deferred-2.md").exists()
    assert (todo / "deferred-3.md").exists()


def test_a_second_pull_is_a_no_op(fake, client, todo, sample_plans):
    task_of(fake, "deferred")["done"] = True
    add_unmanaged(fake, "A task made in the UI", labels=("in progress",))
    run_pull(client, todo, sample_plans, apply=True)
    writes = len(fake.writes)

    report = run_pull(client, todo, sample_plans, apply=True)

    assert (report.closed, report.adopted) == (0, 0)
    assert report.lines == []
    assert len(fake.writes) == writes


def test_a_done_task_without_the_marker_is_not_adopted(fake, client, todo, sample_plans):
    task = add_unmanaged(fake, "Already finished")
    task["done"] = True

    assert run_pull(client, todo, sample_plans, apply=True).adopted == 0


def test_push_warns_before_it_reopens_a_task_closed_in_vikunja(fake, client, todo, sample_plans):
    task_of(fake, "deferred")["done"] = True

    report = sync(client, parse_todo(todo, sample_plans), PROJECT)

    assert report.counts["reopen"] == 1
    assert any("run pull" in warning and "deferred" in warning for warning in report.warnings)


def test_html_becomes_markdown():
    html = (
        "<h2>Where</h2><p>See <a href=\"http://x\">the link</a>, <code>f()</code> &amp; <em>more</em></p>"
        "<ol><li>first</li><li>second</li></ol>"
        '<ul data-type="taskList"><li data-checked="true">done</li><li data-checked="false">open</li></ul>'
    )
    assert html_to_markdown(html) == (
        "## Where\n\nSee [the link](http://x), `f()` & *more*\n\n1. first\n2. second\n\n- [x] done\n- [ ] open"
    )


def test_a_plain_text_description_stays_as_it_is():
    assert html_to_markdown("Just text") == "Just text"
    assert html_to_markdown("") == ""


def test_entry_text_without_labels_has_no_front_matter():
    task = {"title": " Plain ", "description": "<p>todo-sync:id=x</p><p>body</p>", "labels": []}
    assert entry_text(task, TODAY) == "# Plain\n\nbody\n"
