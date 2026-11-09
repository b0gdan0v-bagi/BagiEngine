"""
The automatic mirror between the store and Vikunja. Nobody runs a push or a pull any more.

- Store -> Vikunja: every write that changes what a task shows queues the entry in `vikunja_outbox`;
  the worker thread writes the task (with retries and backoff) and keeps what it wrote as the
  entry's `vikunja_snapshot`.
- Vikunja -> store: a project webhook (task.created / task.updated / task.deleted) and a reconcile
  pass every few minutes read the task and compare it with the snapshot. What differs was changed by
  a person and is applied to the entry: done (closed / open), the title, the body, the labels
  `in progress` / `deferred` / `stage N`. A task made in Vikunja without the marker becomes a new entry.
  `review` never comes from Vikunja: only the loop sets it.

The entry row is locked (SELECT ... FOR UPDATE) while its task is written and while a webhook is
applied, so the echo of the mirror's own write always meets the new snapshot and changes nothing.
"""

import hashlib
import hmac
import logging
import threading
import time
from datetime import datetime, timedelta, timezone

from psycopg import Connection
from psycopg.types.json import Jsonb
from psycopg_pool import ConnectionPool

from todo_markdown import html_to_markdown, markdown_to_html, slugify

from . import store
from .vikunja import (
    MANAGED_LABEL_RE,
    WEBHOOK_EVENTS,
    VikunjaClient,
    VikunjaError,
    body_of,
    managed_labels,
    marker_of,
    normalize_html,
    render_task,
    snapshot_of,
    stage_from_labels,
    status_from_labels,
)

log = logging.getLogger("todo.mirror")

ACTOR = "vikunja"
POLL_SECONDS = 2.0
BATCH_SIZE = 20
MAX_BACKOFF_SECONDS = 600


def verify_signature(secret: str, body: bytes, signature: str | None) -> bool:
    """Vikunja signs a webhook with HMAC-SHA256 of the raw body, hex, in X-Vikunja-Signature."""
    if not signature:
        return False
    expected = hmac.new(secret.encode("utf-8"), body, hashlib.sha256).hexdigest()
    return hmac.compare_digest(expected, signature.strip().lower())


def _same_text(left: str, right: str) -> bool:
    return " ".join(left.split()) == " ".join(right.split())


class Mirror:
    def __init__(
        self,
        pool: ConnectionPool,
        client: VikunjaClient | None,
        webhook_secret: str = "",
        public_url: str = "",
        reconcile_seconds: float = 300.0,
    ):
        self.pool = pool
        self.client = client
        self.webhook_secret = webhook_secret
        self.public_url = public_url.rstrip("/")
        self.reconcile_seconds = reconcile_seconds
        self._label_ids: dict[str, int] = {}
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._last_reconcile = 0.0
        self._marked: dict[int, dict[str, list[dict]]] = {}  # per outbox pass: project -> marker -> its tasks
        self.last_error: str | None = None

    @property
    def enabled(self) -> bool:
        return self.client is not None

    # --- the worker thread ---

    def start(self) -> None:
        if not self.enabled or self._thread is not None:
            return
        self._last_reconcile = time.monotonic() - self.reconcile_seconds + 5  # the first pass shortly after start
        self._thread = threading.Thread(target=self._run, name="todo-mirror", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()
        if self._thread is not None:
            self._thread.join(timeout=30)
            self._thread = None

    def wake(self) -> None:
        self._wake.set()

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self.process_outbox()
                if self.reconcile_seconds > 0 and time.monotonic() - self._last_reconcile >= self.reconcile_seconds:
                    self._last_reconcile = time.monotonic()
                    self.reconcile()
                self.last_error = None
            except Exception as error:  # the thread must survive a dead network or database
                self.last_error = str(error)
                log.exception("mirror pass failed")
            self._wake.wait(POLL_SECONDS)
            self._wake.clear()

    # --- Vikunja helpers ---

    def _area_of_project(self, conn: Connection, project_id: int | None) -> dict | None:
        if project_id is None:
            return None
        return conn.execute("SELECT * FROM areas WHERE vikunja_project_id = %s", (project_id,)).fetchone()

    def project_id(self, conn: Connection, area: dict) -> int | None:
        """The id of the area's project, found by title (created when missing) and kept in the area."""
        if not area["vikunja_project"]:
            return None
        if area["vikunja_project_id"]:
            return area["vikunja_project_id"]
        project = self.client.find_project(area["vikunja_project"])
        if project is None:
            project, _ = self.client.request("PUT", "/projects", {"title": area["vikunja_project"]})
        conn.execute("UPDATE areas SET vikunja_project_id = %s WHERE id = %s", (project["id"], area["id"]))
        area["vikunja_project_id"] = project["id"]
        return project["id"]

    def _resolve_projects(self, conn: Connection) -> list[dict]:
        areas = conn.execute("SELECT * FROM areas WHERE vikunja_project IS NOT NULL ORDER BY id").fetchall()
        for area in areas:
            self.project_id(conn, area)
        return areas

    def _labels(self, titles: list[str]) -> dict[str, int]:
        missing = [title for title in titles if title not in self._label_ids]
        if missing:
            self._label_ids = {label["title"]: label["id"] for label in self.client.get_all("/labels")}
            for title in missing:
                if title not in self._label_ids:
                    created, _ = self.client.request("PUT", "/labels", {"title": title})
                    self._label_ids[title] = created["id"]
        return {title: self._label_ids[title] for title in titles}

    def _set_labels(self, task: dict, wanted: list[str]) -> None:
        foreign = [label["id"] for label in task.get("labels") or [] if not MANAGED_LABEL_RE.match(label["title"])]
        ids = self._labels(wanted)
        self.client.request("POST", f"/tasks/{task['id']}/labels/bulk", {"labels": [{"id": i} for i in foreign + [ids[t] for t in wanted]]})

    def _tasks_by_marker(self, project_id: int) -> dict[str, list[dict]]:
        """marker -> the project's tasks that carry it, oldest first (listed once per outbox pass)."""
        if project_id not in self._marked:
            index: dict[str, list[dict]] = {}
            for task in sorted(self.client.project_tasks(project_id), key=lambda task: task["id"]):
                marker = marker_of(task)
                if marker:
                    index.setdefault(marker, []).append(task)
            self._marked[project_id] = index
        return self._marked[project_id]

    def find_marked_task(self, conn: Connection, entry: dict, project_id: int) -> dict | None:
        """
        An unlinked task of the project whose marker names this entry (its id or an old id): the task
        the old export made, or one whose link was lost. An open one wins over a done one, the oldest first.
        """
        index = self._tasks_by_marker(project_id)
        tasks = [task for marker in (entry["id"], *entry["aliases"]) for task in index.get(marker, [])]
        tasks.sort(key=lambda task: (bool(task.get("done")), task["id"]))
        for task in tasks:
            if conn.execute("SELECT 1 FROM entries WHERE vikunja_task_id = %s", (task["id"],)).fetchone() is None:
                return task
        return None

    # --- store -> Vikunja ---

    def process_outbox(self) -> int:
        """Write the tasks of every due entry. Returns how many were written."""
        if not self.enabled:
            return 0
        self._marked = {}
        written = 0
        with self.pool.connection() as conn:
            due = conn.execute(
                "SELECT entry_id FROM vikunja_outbox WHERE due_at <= now() ORDER BY due_at LIMIT %s", (BATCH_SIZE,)
            ).fetchall()
            conn.commit()
            for row in due:
                entry_id = row["entry_id"]
                try:
                    with conn.transaction():
                        # the row is locked first, so a write that queues the entry meanwhile waits and queues it again
                        if conn.execute("SELECT 1 FROM vikunja_outbox WHERE entry_id = %s FOR UPDATE", (entry_id,)).fetchone() is None:
                            continue
                        self.write_entry(conn, entry_id)
                        conn.execute("DELETE FROM vikunja_outbox WHERE entry_id = %s", (entry_id,))
                    written += 1
                except (VikunjaError, OSError) as error:
                    with conn.transaction():
                        attempts = conn.execute(
                            "UPDATE vikunja_outbox SET attempts = attempts + 1, last_error = %s WHERE entry_id = %s RETURNING attempts",
                            (str(error), entry_id),
                        ).fetchone()
                        if attempts:
                            delay = min(5 * 2 ** (attempts["attempts"] - 1), MAX_BACKOFF_SECONDS)
                            conn.execute(
                                "UPDATE vikunja_outbox SET due_at = %s WHERE entry_id = %s",
                                (datetime.now(timezone.utc) + timedelta(seconds=delay), entry_id),
                            )
                    log.warning("task of %s not written: %s", entry_id, error)
        return written

    def write_entry(self, conn: Connection, entry_id: str) -> str:
        """Make the entry's task show the entry. Runs inside the caller's transaction, with the entry locked."""
        entry = store.get_entry(conn, entry_id, lock=True)
        if entry is None:
            return "gone"
        area = conn.execute("SELECT * FROM areas WHERE id = %s", (entry["area"],)).fetchone()
        project_id = self.project_id(conn, area)
        if project_id is None:
            return "no mirror"

        task = self.client.get_task(entry["vikunja_task_id"]) if entry["vikunja_task_id"] else None
        if task is None:
            task = self.find_marked_task(conn, entry, project_id)  # never a second task for an entry that has one
            if task is not None:
                entry = {**entry, "vikunja_snapshot": None}
        if task is not None and entry["vikunja_snapshot"] is not None and self.apply_task(conn, entry, task):
            entry = store.get_entry(conn, entry_id, lock=True)  # a person's edit came first; show it, do not overwrite it

        plan = store.plan_progress(conn, [entry]).get(entry_id)
        stage = conn.execute("SELECT title FROM stages WHERE area = %s AND number = %s", (entry["area"], entry["stage"])).fetchone()
        desired = render_task(entry, store.status_line(entry), stage["title"] if stage else None, plan)

        if task is None:
            if entry["status"] == "closed":
                conn.execute("UPDATE entries SET vikunja_task_id = NULL, vikunja_snapshot = NULL WHERE id = %s", (entry_id,))
                return "closed, no task"
            task, _ = self.client.request(
                "PUT", f"/projects/{project_id}/tasks", {"title": desired["title"], "description": desired["description"], "done": desired["done"]}
            )
            task["labels"] = []
            self._tasks_by_marker(project_id).setdefault(entry_id, []).append(task)
            action = "create"
        else:
            current = snapshot_of(task)
            fields_differ = (
                current["title"] != desired["title"]
                or current["description"] != normalize_html(desired["description"])
                or current["done"] != desired["done"]
                or task.get("project_id") != project_id
            )
            action = "unchanged"
            if fields_differ:
                body = {key: value for key, value in task.items() if key != "labels"}
                body.update(title=desired["title"], description=desired["description"], done=desired["done"], project_id=project_id)
                written, _ = self.client.request("POST", f"/tasks/{task['id']}", body)
                task = {**(written or body), "labels": task.get("labels") or []}
                action = "update"
        if managed_labels(task) != desired["labels"]:
            self._set_labels(task, desired["labels"])
            action = "update" if action == "unchanged" else action
        snapshot = {**snapshot_of(task), "labels": desired["labels"]}
        conn.execute("UPDATE entries SET vikunja_task_id = %s, vikunja_snapshot = %s WHERE id = %s", (task["id"], Jsonb(snapshot), entry_id))
        return action

    # --- Vikunja -> store ---

    def apply_task(self, conn: Connection, entry: dict, task: dict) -> bool:
        """Apply what a person changed in the task since the mirror last wrote it. True when the entry changed."""
        before = entry["vikunja_snapshot"]
        current = snapshot_of(task)
        if before is None or current == before:
            return False
        changes: dict = {}
        if current["title"] != before["title"] and current["title"].strip():
            changes["title"] = current["title"].strip()
        if current["done"] != before["done"]:
            if current["done"]:
                changes["status"] = "closed"
            elif entry["status"] == "closed":
                changes["status"] = "open"
        if current["labels"] != before["labels"]:
            new_status, old_status = status_from_labels(current["labels"]), status_from_labels(before["labels"])
            if "status" not in changes and new_status != old_status:
                if new_status:
                    changes["status"] = new_status
                elif entry["status"] in ("in progress", "deferred"):
                    changes["status"] = "open"
            new_stage = stage_from_labels(current["labels"])
            if new_stage != stage_from_labels(before["labels"]):
                changes["stage"] = new_stage
        if current["description"] != before["description"]:
            body = body_of(task.get("description") or "")
            if not _same_text(body, html_to_markdown(markdown_to_html(entry["body"]))):
                changes["body"] = body
        area = self._area_of_project(conn, task.get("project_id"))
        if area is not None and area["id"] != entry["area"]:
            changes["area"] = area["id"]
        if changes.get("status") in ("open", "closed"):
            changes.update(branch=None, base=None, origin=None, status_note="")
        conn.execute("UPDATE entries SET vikunja_snapshot = %s WHERE id = %s", (Jsonb(current), entry["id"]))
        if not changes:
            return False
        store.update_entry(conn, entry["id"], changes, ACTOR, kind="vikunja", mirror=True)
        return True

    def adopt(self, conn: Connection, task: dict, area: dict) -> dict:
        """A task made in Vikunja becomes an entry; the worker then writes the marker into it."""
        base = slugify(task.get("title") or "") or f"task-{task['id']}"
        entry_id, suffix = base, 1
        while store.get_entry(conn, entry_id) is not None:
            suffix += 1
            entry_id = f"{base}-{suffix}"
        labels = managed_labels(task)
        entry = store.create_entry(
            conn,
            {
                "id": entry_id,
                "area": area["id"],
                "title": (task.get("title") or entry_id).strip(),
                "body": body_of(task.get("description") or ""),
                "status": status_from_labels(labels) or "open",
                "stage": stage_from_labels(labels),
            },
            ACTOR,
        )
        conn.execute("UPDATE entries SET vikunja_task_id = %s, vikunja_snapshot = NULL WHERE id = %s", (task["id"], entry_id))
        return entry

    def process_task(self, conn: Connection, task: dict) -> str:
        """One task as Vikunja has it now: apply a person's edit, link a marked task, adopt a new one."""
        entry = conn.execute("SELECT * FROM entries WHERE vikunja_task_id = %s FOR UPDATE", (task["id"],)).fetchone()
        if entry is not None:
            return "changed" if self.apply_task(conn, entry, task) else "same"
        marked = marker_of(task)
        if marked:
            entry = store.entry_by_marker(conn, marked)
            if entry is None:
                return "marker of no entry"
            if entry["vikunja_task_id"] not in (None, task["id"]):
                return "duplicate marker"
            conn.execute("UPDATE entries SET vikunja_task_id = %s, vikunja_snapshot = NULL WHERE id = %s", (task["id"], entry["id"]))
            store.enqueue(conn, entry["id"])
            return "linked"
        area = self._area_of_project(conn, task.get("project_id"))
        if area is not None and not task.get("done"):
            self.adopt(conn, task, area)
            return "adopted"
        return "ignored"

    def task_deleted(self, conn: Connection, task_id: int) -> str:
        entry = conn.execute("SELECT * FROM entries WHERE vikunja_task_id = %s FOR UPDATE", (task_id,)).fetchone()
        if entry is None:
            return "ignored"
        conn.execute("UPDATE entries SET vikunja_task_id = NULL, vikunja_snapshot = NULL WHERE id = %s", (entry["id"],))
        if entry["status"] != "closed":
            store.update_entry(conn, entry["id"], {"status": "closed", "branch": None, "base": None, "origin": None, "status_note": "task deleted in Vikunja"}, ACTOR, "vikunja", mirror=False)
        return "closed"

    def on_webhook(self, payload: dict) -> str:
        event = payload.get("event_name") or ""
        data = payload.get("data") or {}
        task = data.get("task") if isinstance(data.get("task"), dict) else None
        task_id = (task or {}).get("id")
        if event not in WEBHOOK_EVENTS or not isinstance(task_id, int):
            return "ignored"
        with self.pool.connection() as conn, conn.transaction():
            self._resolve_projects(conn)
            if event == "task.deleted":
                return self.task_deleted(conn, task_id)
            conn.execute("SELECT 1 FROM entries WHERE vikunja_task_id = %s FOR UPDATE", (task_id,))  # wait for a write in flight
            fresh = self.client.get_task(task_id)
            if fresh is None:
                return self.task_deleted(conn, task_id)
            result = self.process_task(conn, fresh)
        self.wake()
        return result

    def reconcile(self) -> dict:
        """Compare every project with the store: the safety net for a webhook that did not arrive."""
        report: dict[str, int] = {}
        if not self.enabled:
            return report

        def count(result: str) -> None:
            report[result] = report.get(result, 0) + 1

        with self.pool.connection() as conn:
            with conn.transaction():
                areas = self._resolve_projects(conn)
            for area in areas:
                tasks = self.client.project_tasks(area["vikunja_project_id"])
                seen = set()
                for task in tasks:
                    seen.add(task["id"])
                    with conn.transaction():
                        count(self.process_task(conn, task))
                linked = conn.execute(
                    "SELECT id, vikunja_task_id FROM entries WHERE area = %s AND vikunja_task_id IS NOT NULL", (area["id"],)
                ).fetchall()
                for row in linked:
                    if row["vikunja_task_id"] in seen:
                        continue
                    fresh = self.client.get_task(row["vikunja_task_id"])
                    with conn.transaction():
                        count(self.task_deleted(conn, row["vikunja_task_id"]) if fresh is None else self.process_task(conn, fresh))
                with conn.transaction():
                    missing = conn.execute(
                        """SELECT id FROM entries WHERE area = %s AND vikunja_task_id IS NULL AND status <> 'closed'
                           AND id NOT IN (SELECT entry_id FROM vikunja_outbox)""",
                        (area["id"],),
                    ).fetchall()
                    for row in missing:
                        store.enqueue(conn, row["id"])
                        count("queued")
        self.wake()
        return report

    def dedupe(self, apply: bool = False) -> list[str]:
        """
        Entries with more than one open task carrying their marker (id or old id): the entry keeps the
        oldest one (relinked and rewritten) and the newer ones are deleted. Without apply: only the report.
        """
        lines: list[str] = []
        doomed: list[int] = []
        with self.pool.connection() as conn:
            with conn.transaction():
                areas = self._resolve_projects(conn)
            for area in areas:
                tasks = sorted(self.client.project_tasks(area["vikunja_project_id"]), key=lambda task: task["id"])
                for entry in conn.execute("SELECT * FROM entries WHERE area = %s ORDER BY id", (area["id"],)).fetchall():
                    names = {entry["id"], *entry["aliases"]}
                    open_tasks = [task for task in tasks if not task.get("done") and marker_of(task) in names]
                    if len(open_tasks) < 2:
                        continue
                    keep, extra = open_tasks[0], open_tasks[1:]
                    lines.append(f"{entry['id']}: keep task {keep['id']}, delete {', '.join(str(task['id']) for task in extra)}")
                    if not apply:
                        continue
                    with conn.transaction():
                        conn.execute(
                            "UPDATE entries SET vikunja_task_id = NULL WHERE vikunja_task_id = %s AND id <> %s", (keep["id"], entry["id"])
                        )
                        conn.execute("UPDATE entries SET vikunja_task_id = %s, vikunja_snapshot = NULL WHERE id = %s", (keep["id"], entry["id"]))
                        store.enqueue(conn, entry["id"])
                        store.add_event(conn, ACTOR, "dedupe", entry["id"], data={"kept": keep["id"], "deleted": [task["id"] for task in extra]})
                    doomed.extend(task["id"] for task in extra)
        for task_id in doomed:  # after the commit: no entry is linked to them any more, so a webhook changes nothing
            try:
                self.client.request("DELETE", f"/tasks/{task_id}")
            except VikunjaError as error:
                lines.append(f"task {task_id} not deleted: {error}")
        self.wake()
        return lines or ["no duplicates"]

    def install_webhooks(self) -> list[str]:
        """A webhook on every area's project pointing at this service (once; an existing one is kept)."""
        if not self.enabled or not self.public_url or not self.webhook_secret:
            return ["skipped: VIKUNJA_URL, TODO_PUBLIC_URL and VIKUNJA_WEBHOOK_SECRET are needed"]
        target = f"{self.public_url}/api/v1/vikunja/webhook"
        lines = []
        with self.pool.connection() as conn, conn.transaction():
            for area in self._resolve_projects(conn):
                project_id = area["vikunja_project_id"]
                hooks, _ = self.client.request("GET", f"/projects/{project_id}/webhooks")
                if any(hook.get("target_url") == target for hook in hooks or []):
                    lines.append(f"{area['id']}: webhook present")
                    continue
                self.client.request(
                    "PUT", f"/projects/{project_id}/webhooks", {"target_url": target, "events": WEBHOOK_EVENTS, "secret": self.webhook_secret}
                )
                lines.append(f"{area['id']}: webhook created")
        return lines
