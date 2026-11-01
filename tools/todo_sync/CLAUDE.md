# tools/todo_sync

Reads `TODO.md`, builds a progress dashboard and exports the entries to a Vikunja project. `TODO.md` stays the source of truth: the export is one-way and is never edited back. Python 3.10+, standard library only (pytest for the tests). No engine code is involved, and nothing here names a concrete game: the paths are arguments, so a game repository that carries the engine as a submodule can run it on its own `TODO.md`.

## Entry format

The fields are described in the header of `TODO.md` (`**Status:**`, `**Stage:**`, `**Plan:**`, all optional). The entry id is the slug of its heading, so renaming a heading makes a new entry for the tracker (the old task is closed, a new one is created). The parser fails on a duplicate id, a status that does not start with `open`, `in progress` or `deferred`, and an impossible date. A plan's progress is the count of `- [ ]` / `- [x]` lines in the plan file; a plan without checkboxes shows "no checklist". A finished entry is deleted from `TODO.md`, not marked done.

## Commands

Run from the project root with the repo venv (`CI/setup_venv.*` creates `.venv`; `pytest` comes from `requirements.txt`). On Windows use `.venv\Scripts\python`.

```bash
.venv/bin/python tools/todo_sync/todo_sync.py check                # one line per entry, exit 1 on a format error
.venv/bin/python tools/todo_sync/todo_sync.py parse [--out FILE]   # the parsed data as JSON
.venv/bin/python tools/todo_sync/todo_sync.py dashboard            # build/todo/index.html (git-ignored)
.venv/bin/python tools/todo_sync/todo_sync.py push --dry-run       # what would change in Vikunja
.venv/bin/python tools/todo_sync/todo_sync.py push                 # export
.venv/bin/python -m pytest tools/todo_sync/tests                   # tests
```

`--todo FILE` and `--root DIR` (where plan paths and `.env` are looked up) apply to every command. The `todo-sync` skill wraps these.

## Vikunja settings

`VIKUNJA_URL` (for example `http://localhost:3456`), `VIKUNJA_TOKEN` and optionally `VIKUNJA_PROJECT` (title, default `BagiEngine`, created when missing) come from the process environment, then from `.env` in the root (already git-ignored; `--env-file` overrides the path). The token is never printed, logged or committed.

Use a scoped API token (Vikunja: Settings, API tokens). These permissions are enough, checked on Vikunja 2.7.1:

| Group | Actions |
|-------|---------|
| `projects` | `create`, `read_all` |
| `tasks` | `create`, `read_all`, `update` |
| `labels` | `create`, `read_all` |
| `tasks_labels` | `update_bulk` |

## What the export does

- **Identity:** a managed task's description starts with the visible text `todo-sync:id=<entry id>`. Nothing is stored locally, so another machine finds the same tasks. Do not delete that line in Vikunja: the task would be created again. Tasks without the marker are never touched.
- **Owned fields:** title, description, done and the labels `in progress`, `deferred`, `stage N`. Edits to these in Vikunja are overwritten by the next push. Everything else (priority, due date, assignees, comments, other labels) is kept: an update is read-modify-write, because `POST /tasks/{id}` replaces the whole task.
- **Lifecycle:** a new entry creates a task; a changed entry updates it; an entry that left `TODO.md` marks its task done (never deleted); an entry that comes back reopens it. A second push with no change does no writes.
- `--dry-run` reads from Vikunja and prints `create` / `update` / `close` / `reopen` per entry plus totals; it writes nothing.

Endpoints used (REST `/api/v1`): `GET|PUT /projects`, `GET /tasks?filter=project_id = N` (lists done tasks too, paged by `X-Pagination-Total-Pages`), `PUT /projects/{id}/tasks`, `POST /tasks/{id}`, `GET|PUT /labels`, `POST /tasks/{id}/labels/bulk`. Vikunja 2.7.1 has no `GET /projects/{id}/tasks`. If a newer version changes one of these, `tests/fake_vikunja.py` is the place that records what the adapter relies on.

## Trying it on a local Vikunja

```bash
docker run -d --name vikunja -p 127.0.0.1:3456:3456 \
  -e VIKUNJA_SERVICE_JWTSECRET=change-me -e VIKUNJA_SERVICE_PUBLICURL=http://localhost:3456/ \
  -e VIKUNJA_DATABASE_TYPE=sqlite -e VIKUNJA_DATABASE_PATH=/db/vikunja.db \
  -v "$PWD/vikunja-data:/db" vikunja/vikunja
```

Open `http://localhost:3456`, register, create an API token with the permissions above, and put `VIKUNJA_URL` and `VIKUNJA_TOKEN` into `.env`. Mount the data folder outside the repository, or the database ends up in the checkout.

## Hosting on Railway

The tracker lives on Railway. One service from the Docker image `vikunja/vikunja`, SQLite on a volume: enough for one person, no database service to run. Not run end to end from this repository; the values come from the Vikunja config docs and from Railway's community Vikunja templates.

1. New project, Deploy from Docker image `vikunja/vikunja` (pin a version tag instead of `latest`; the adapter is checked on 2.7.1).
2. Volume mounted at `/db`. The image's default `VIKUNJA_DATABASE_PATH=/db/vikunja.db` then lands on it. Uploaded files (`/app/vikunja/files`) stay on the container disk, which is fine because the export never attaches files; Railway allows one volume per service.
3. Variables:

   | Variable | Value |
   |----------|-------|
   | `RAILWAY_RUN_UID` | `0` (Railway mounts volumes as root, the image runs as UID 1000, so SQLite cannot write without it) |
   | `VIKUNJA_SERVICE_PUBLICURL` | `https://${{RAILWAY_PUBLIC_DOMAIN}}/` |
   | `VIKUNJA_SERVICE_SECRET` | a long random value (`openssl rand -hex 32`); without it Vikunja generates a new one on each start and logs everyone out |
   | `VIKUNJA_DATABASE_TYPE` | `sqlite` |

4. Networking: generate a public domain. If it does not answer, set the target port to `3456`.
5. Open the domain and register the account **right away**, then add `VIKUNJA_SERVICE_ENABLEREGISTRATION=false` and redeploy. Until then anyone with the URL can register.
6. In Vikunja create the API token with the permissions above. Put `VIKUNJA_URL=https://<the domain>` (no `/api/v1`) and `VIKUNJA_TOKEN` into the local `.env`, then run `push --dry-run`, `push`, and `push` again: the second run must report everything `unchanged`.

Keep one replica: Vikunja runs its scheduled jobs inside the web process.

## Tests

`tests/` has a sample `TODO.md` with a sample plan (`fixtures/`), the parser and dashboard tests, and `fake_vikunja.py`, an in-process HTTP server that copies the Vikunja behaviour the adapter depends on (full replace on update, paging headers, labels in the listing). `test_real_todo_md_parses_with_known_statuses` parses the repository's own `TODO.md`, so a format slip in the file fails the suite.
