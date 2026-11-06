"""
Reads and writes of the todo store. Every function takes an open connection and runs inside the
caller's transaction; a write that changes what a Vikunja task shows queues the entry in the outbox.
"""

import json
from datetime import date, datetime, timezone

from psycopg import Connection
from psycopg.types.json import Jsonb

from todo_markdown import ID_RE, PLAN_NAME_RE, STATUSES, checklist_progress, extract_summary, read_checklist

PLAN_KINDS = ("owner", "slice", "review")
ENTRY_FIELDS = ("title", "body", "status", "status_date", "status_note", "branch", "stage", "plan")
MIRRORED_FIELDS = ("title", "body", "status", "status_date", "status_note", "branch", "stage", "plan")


class StoreError(Exception):
    """A request the store refuses. `code` is the HTTP status the API answers with."""

    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code


# --- events and the outbox ---------------------------------------------------------------------


def add_event(conn: Connection, actor: str, kind: str, entry_id: str | None = None, plan: str | None = None, data: dict | None = None) -> None:
    conn.execute(
        "INSERT INTO events (entry_id, plan, actor, kind, data) VALUES (%s, %s, %s, %s, %s)",
        (entry_id, plan, actor, kind, Jsonb(data or {})),
    )


def enqueue(conn: Connection, entry_id: str) -> None:
    """The entry's task has to be written (again). One row per entry; a new write makes it due now."""
    conn.execute(
        """INSERT INTO vikunja_outbox (entry_id) VALUES (%s)
           ON CONFLICT (entry_id) DO UPDATE SET due_at = now(), attempts = 0, last_error = NULL""",
        (entry_id,),
    )


# --- areas -------------------------------------------------------------------------------------


def list_areas(conn: Connection) -> list[dict]:
    areas = conn.execute("SELECT id, title, vikunja_project, vikunja_project_id FROM areas ORDER BY id").fetchall()
    stages = conn.execute("SELECT area, number, title FROM stages ORDER BY area, number").fetchall()
    for area in areas:
        area["stages"] = [{"number": s["number"], "title": s["title"]} for s in stages if s["area"] == area["id"]]
    return areas


def put_area(conn: Connection, area_id: str, title: str, vikunja_project: str | None, stages: list[dict] | None, actor: str) -> dict:
    if not ID_RE.match(area_id):
        raise StoreError(422, f"area id '{area_id}' may hold only letters, digits, '_' and '-'")
    conn.execute(
        """INSERT INTO areas (id, title, vikunja_project) VALUES (%s, %s, %s)
           ON CONFLICT (id) DO UPDATE SET title = EXCLUDED.title, vikunja_project = EXCLUDED.vikunja_project,
               vikunja_project_id = CASE WHEN areas.vikunja_project IS DISTINCT FROM EXCLUDED.vikunja_project
                                         THEN NULL ELSE areas.vikunja_project_id END""",
        (area_id, title, vikunja_project),
    )
    if stages is not None:
        conn.execute("DELETE FROM stages WHERE area = %s", (area_id,))
        for stage in stages:
            conn.execute("INSERT INTO stages (area, number, title) VALUES (%s, %s, %s)", (area_id, int(stage["number"]), stage["title"]))
        for row in conn.execute("SELECT id FROM entries WHERE area = %s AND stage IS NOT NULL", (area_id,)).fetchall():
            enqueue(conn, row["id"])  # the stage title is part of the description
    add_event(conn, actor, "area", data={"area": area_id, "title": title, "vikunjaProject": vikunja_project, "stages": stages})
    return next(area for area in list_areas(conn) if area["id"] == area_id)


def _require_area(conn: Connection, area_id: str) -> None:
    if conn.execute("SELECT 1 FROM areas WHERE id = %s", (area_id,)).fetchone() is None:
        raise StoreError(422, f"no area '{area_id}'")


# --- entries -----------------------------------------------------------------------------------


def status_line(entry: dict) -> str:
    """The status as the old front matter wrote it: `review 2026-11-06 branch todo/x: what was done`."""
    parts = [entry["status"]]
    if entry["status_date"]:
        parts.append(entry["status_date"].isoformat() if isinstance(entry["status_date"], date) else str(entry["status_date"]))
    if entry["branch"]:
        parts.append(f"branch {entry['branch']}")
    line = " ".join(parts)
    if entry["status_note"]:
        line += (": " if entry["branch"] else " ") + entry["status_note"]
    return line


def plan_progress(conn: Connection, entries: list[dict]) -> dict[str, dict]:
    """entry id -> {plan, progress}: the owner's plan when the entry names one, else its newest slice plan."""
    if not entries:
        return {}
    ids = [entry["id"] for entry in entries]
    names = [entry["plan"] for entry in entries if entry["plan"]]
    rows = conn.execute(
        """SELECT name, entry_id, kind, content, updated_at FROM plans
           WHERE name = ANY(%s) OR (entry_id = ANY(%s) AND kind = 'slice') ORDER BY updated_at DESC""",
        (names, ids),
    ).fetchall()
    by_name = {row["name"]: row for row in rows}
    newest_slice: dict[str, dict] = {}
    for row in rows:
        if row["kind"] == "slice" and row["entry_id"] not in newest_slice:
            newest_slice[row["entry_id"]] = row
    result = {}
    for entry in entries:
        plan = by_name.get(entry["plan"]) if entry["plan"] else newest_slice.get(entry["id"])
        if plan is not None:
            checklist = read_checklist(plan["content"], plan["name"])
            result[entry["id"]] = {"name": plan["name"], "checklist": checklist, "progress": checklist_progress(checklist)}
    return result


def to_json(entry: dict, plan: dict | None = None, with_body: bool = True) -> dict:
    data = {
        "id": entry["id"],
        "area": entry["area"],
        "title": entry["title"],
        "status": entry["status"],
        "statusDate": entry["status_date"].isoformat() if entry["status_date"] else None,
        "statusNote": entry["status_note"],
        "statusLine": status_line(entry),
        "branch": entry["branch"],
        "stage": entry["stage"],
        "plan": entry["plan"],
        "planShown": plan["name"] if plan else None,
        "planProgress": plan["progress"] if plan else None,
        "summary": extract_summary(entry["body"]),
        "version": entry["version"],
        "createdAt": entry["created_at"].isoformat(),
        "updatedAt": entry["updated_at"].isoformat(),
        "closedAt": entry["closed_at"].isoformat() if entry["closed_at"] else None,
        "vikunjaTaskId": entry["vikunja_task_id"],
        "aliases": entry["aliases"],
    }
    if with_body:
        data["body"] = entry["body"]
    return data


def get_entry(conn: Connection, entry_id: str, lock: bool = False) -> dict | None:
    query = "SELECT * FROM entries WHERE id = %s" + (" FOR UPDATE" if lock else "")
    return conn.execute(query, (entry_id,)).fetchone()


def entry_by_marker(conn: Connection, marker: str) -> dict | None:
    """The entry whose id or old id (alias) a Vikunja marker names, locked."""
    return conn.execute("SELECT * FROM entries WHERE id = %s OR %s = ANY(aliases) ORDER BY id = %s DESC LIMIT 1 FOR UPDATE", (marker, marker, marker)).fetchone()


def require_entry(conn: Connection, entry_id: str, lock: bool = False) -> dict:
    entry = get_entry(conn, entry_id, lock)
    if entry is None:
        raise StoreError(404, f"no entry '{entry_id}'")
    return entry


def list_entries(conn: Connection, area: str | None = None, statuses: list[str] | None = None) -> list[dict]:
    clauses, params = [], []
    if area:
        clauses.append("area = %s")
        params.append(area)
    if statuses:
        clauses.append("status = ANY(%s)")
        params.append(statuses)
    where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
    return conn.execute(f"SELECT * FROM entries {where} ORDER BY area, stage NULLS FIRST, id", params).fetchall()


def entries_json(conn: Connection, entries: list[dict], with_body: bool = False) -> list[dict]:
    plans = plan_progress(conn, entries)
    return [to_json(entry, plans.get(entry["id"]), with_body) for entry in entries]


def entry_json(conn: Connection, entry: dict) -> dict:
    return entries_json(conn, [entry], with_body=True)[0]


def _check_fields(changes: dict) -> None:
    if "status" in changes and changes["status"] not in STATUSES:
        raise StoreError(422, f"status must be one of {', '.join(STATUSES)}")
    if "title" in changes and not str(changes["title"]).strip():
        raise StoreError(422, "the title is empty")
    if changes.get("plan") and not PLAN_NAME_RE.match(changes["plan"]):
        raise StoreError(422, f"plan '{changes['plan']}' must be a bare file name like 'my-plan.md'")
    if changes.get("stage") is not None and not isinstance(changes["stage"], int):
        raise StoreError(422, "stage must be a number")


def create_entry(conn: Connection, data: dict, actor: str) -> dict:
    entry_id = data.get("id") or ""
    if not ID_RE.match(entry_id):
        raise StoreError(422, f"id '{entry_id}' may hold only letters, digits, '_' and '-'")
    _require_area(conn, data["area"])
    fields = {key: data[key] for key in ENTRY_FIELDS if key in data and data[key] is not None}
    fields.setdefault("status", "open")
    aliases = [alias for alias in data.get("aliases") or [] if alias != entry_id]
    if any(not ID_RE.match(alias) for alias in aliases):
        raise StoreError(422, "an alias may hold only letters, digits, '_' and '-'")
    if aliases:
        fields["aliases"] = aliases
    _check_fields(fields)
    if get_entry(conn, entry_id) is not None:
        raise StoreError(409, f"entry '{entry_id}' exists")
    if fields["status"] != "open" and "status_date" not in fields:
        fields["status_date"] = date.today()
    columns = ["id", "area", *fields]
    values = [entry_id, data["area"], *fields.values()]
    if fields["status"] == "closed":
        columns.append("closed_at")
        values.append(datetime.now(timezone.utc))
    placeholders = ", ".join(["%s"] * len(values))
    entry = conn.execute(f"INSERT INTO entries ({', '.join(columns)}) VALUES ({placeholders}) RETURNING *", values).fetchone()
    add_event(conn, actor, "create", entry_id, data={key: _jsonable(value) for key, value in fields.items() if key != "body"})
    enqueue(conn, entry_id)
    return entry


def _jsonable(value):
    return value.isoformat() if isinstance(value, date) else value


def update_entry(
    conn: Connection, entry_id: str, changes: dict, actor: str, kind: str = "update", expected_version: int | None = None, mirror: bool = True
) -> dict:
    """
    Apply `changes` (columns of ENTRY_FIELDS, plus area). A changed status gets today's date unless one is given;
    `closed` stamps closed_at. Raises 409 when expected_version is not the current one.
    """
    entry = require_entry(conn, entry_id, lock=True)
    if expected_version is not None and entry["version"] != expected_version:
        raise StoreError(409, f"entry '{entry_id}' is at version {entry['version']}, not {expected_version}: read it again")
    _check_fields(changes)
    if "area" in changes:
        _require_area(conn, changes["area"])
    changes = {key: value for key, value in changes.items() if entry.get(key) != value}
    if "status" in changes and "status_date" not in changes:
        changes["status_date"] = date.today()
    if not changes:
        return entry
    sets = [f"{key} = %s" for key in changes]
    values = list(changes.values())
    if "status" in changes:
        sets.append("closed_at = %s")
        values.append(datetime.now(timezone.utc) if changes["status"] == "closed" else None)
    sets += ["version = version + 1", "updated_at = now()"]
    updated = conn.execute(f"UPDATE entries SET {', '.join(sets)} WHERE id = %s RETURNING *", [*values, entry_id]).fetchone()
    old = {key: _jsonable(entry[key]) for key in changes if key in entry and key != "body"}
    new = {key: _jsonable(value) for key, value in changes.items() if key != "body"}
    if "body" in changes:
        old["body"], new["body"] = "(changed)", f"{len(changes['body'])} characters"
    add_event(conn, actor, kind, entry_id, data={"old": old, "new": new})
    if mirror and any(key in changes for key in (*MIRRORED_FIELDS, "area")):
        enqueue(conn, entry_id)
    return updated


# --- the life of an entry in the loop ----------------------------------------------------------


def claim(conn: Connection, entry_id: str, branch: str, actor: str) -> dict:
    entry = require_entry(conn, entry_id, lock=True)
    if entry["status"] != "open":
        raise StoreError(409, f"entry '{entry_id}' is '{status_line(entry)}', not open")
    return update_entry(conn, entry_id, {"status": "in progress", "branch": branch, "status_note": ""}, actor, "claim")


def review(conn: Connection, entry_id: str, branch: str, note: str, actor: str) -> dict:
    entry = require_entry(conn, entry_id, lock=True)
    if entry["status"] != "in progress" or entry["branch"] != branch:
        raise StoreError(409, f"entry '{entry_id}' is '{status_line(entry)}', not in progress on {branch}")
    return update_entry(conn, entry_id, {"status": "review", "status_note": note}, actor, "review")


def stop(conn: Connection, entry_id: str, reason: str, actor: str) -> dict:
    entry = require_entry(conn, entry_id, lock=True)
    if entry["status"] != "in progress":
        raise StoreError(409, f"entry '{entry_id}' is '{status_line(entry)}', not in progress")
    return update_entry(conn, entry_id, {"status_note": f"stopped: {reason}", "status_date": date.today()}, actor, "stop")


def close(conn: Connection, entry_id: str, reopen: bool, actor: str) -> dict:
    """After the merge: closed, or open again (the next slice) with the branch cleared."""
    entry = require_entry(conn, entry_id, lock=True)
    if entry["status"] == "closed":
        raise StoreError(409, f"entry '{entry_id}' is closed already")
    status = "open" if reopen else "closed"
    return update_entry(conn, entry_id, {"status": status, "branch": None, "status_note": ""}, actor, "reopen" if reopen else "close")


# --- plans -------------------------------------------------------------------------------------


def plan_json(plan: dict, with_content: bool = True) -> dict:
    checklist = read_checklist(plan["content"], plan["name"])
    data = {
        "name": plan["name"],
        "entry": plan["entry_id"],
        "kind": plan["kind"],
        "version": plan["version"],
        "progress": checklist_progress(checklist),
        "updatedAt": plan["updated_at"].isoformat(),
    }
    if with_content:
        data["content"] = plan["content"]
    return data


def get_plan(conn: Connection, name: str) -> dict | None:
    return conn.execute("SELECT * FROM plans WHERE name = %s", (name,)).fetchone()


def list_plans(conn: Connection, entry_id: str | None = None, kind: str | None = None) -> list[dict]:
    clauses, params = [], []
    if entry_id:
        clauses.append("(entry_id = %s OR name IN (SELECT plan FROM entries WHERE id = %s AND plan IS NOT NULL))")
        params += [entry_id, entry_id]
    if kind:
        clauses.append("kind = %s")
        params.append(kind)
    where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
    return conn.execute(f"SELECT * FROM plans {where} ORDER BY updated_at DESC", params).fetchall()


def put_plan(conn: Connection, name: str, content: str, entry_id: str | None, kind: str | None, actor: str) -> dict:
    if not PLAN_NAME_RE.match(name):
        raise StoreError(422, f"plan '{name}' must be a bare file name like 'my-plan.md'")
    existing = get_plan(conn, name)
    kind = kind or (existing["kind"] if existing else None)
    if kind not in PLAN_KINDS:
        raise StoreError(422, f"kind must be one of {', '.join(PLAN_KINDS)}")
    if entry_id is not None:
        require_entry(conn, entry_id)
    if existing is None:
        plan = conn.execute(
            "INSERT INTO plans (name, entry_id, kind, content) VALUES (%s, %s, %s, %s) RETURNING *", (name, entry_id, kind, content)
        ).fetchone()
    else:
        if existing["content"] == content and existing["kind"] == kind and (entry_id is None or entry_id == existing["entry_id"]):
            return existing
        plan = conn.execute(
            """UPDATE plans SET content = %s, kind = %s, entry_id = COALESCE(%s, entry_id), version = version + 1, updated_at = now()
               WHERE name = %s RETURNING *""",
            (content, kind, entry_id, name),
        ).fetchone()
    add_event(conn, actor, "plan", plan["entry_id"], name, {"kind": kind, "progress": checklist_progress(read_checklist(content, name))})
    for row in conn.execute(
        "SELECT id FROM entries WHERE plan = %s OR (id = %s AND plan IS NULL)", (name, plan["entry_id"])
    ).fetchall():
        enqueue(conn, row["id"])
    return plan


def delete_plan(conn: Connection, name: str, actor: str) -> None:
    plan = get_plan(conn, name)
    if plan is None:
        raise StoreError(404, f"no plan '{name}'")
    conn.execute("DELETE FROM plans WHERE name = %s", (name,))
    add_event(conn, actor, "plan deleted", plan["entry_id"], name)
    for row in conn.execute("SELECT id FROM entries WHERE plan = %s OR id = %s", (name, plan["entry_id"])).fetchall():
        enqueue(conn, row["id"])


def events_of(conn: Connection, entry_id: str, limit: int = 200) -> list[dict]:
    rows = conn.execute(
        "SELECT at, actor, kind, plan, data FROM events WHERE entry_id = %s ORDER BY at DESC, id DESC LIMIT %s", (entry_id, limit)
    ).fetchall()
    return [{"at": row["at"].isoformat(), "actor": row["actor"], "kind": row["kind"], "plan": row["plan"], "data": row["data"]} for row in rows]


def dumps(value) -> str:
    return json.dumps(value, ensure_ascii=False, default=_jsonable)
