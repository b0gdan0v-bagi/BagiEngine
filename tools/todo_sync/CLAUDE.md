# tools/todo_sync

Reads the `todo/` folder (one markdown file per entry), builds a progress dashboard and keeps a Vikunja project in step with it. The files are the source of truth for the content: `push` writes entries to the tracker, `pull` brings back only a task closed there and a task created there. Python 3.10+, standard library only (pytest for the tests). No engine code is involved, and nothing here names a concrete game: the paths are arguments, so a game repository that carries the engine as a submodule can run it on its own `todo/`.

## Entry format

The format is described in `todo/README.md`: front matter with `status:`, `stage:`, `plan:` and `id:` (all optional), then a `# Title` line and the body. The entry id is the file name without `.md` (or `id:`), so **renaming a file makes a new entry for the tracker** (the old task is closed, a new one is created); retitling an entry is safe. `README.md` and `roadmap.md` are not entries; the numbered list of `roadmap.md` gives the stages. The parser fails on a duplicate id, an unknown or repeated front matter key, a missing title, a status that does not start with a keyword of `STATUSES` (`open`, `in progress`, `review`, `deferred`), an impossible date and a `plan:` that is not a bare file name. A finished entry is deleted (`git rm`), not marked done. `review` means a branch `todo/<id>` waits for the owner: only the `todo-loop` skill sets it, only in that branch, and the entry stays until the owner has merged the branch and deleted the file.

A plan is a file of the local plan store (`~/.claude/plans/`, where plan mode writes; plans are not committed). Its progress is the count of `- [ ]` / `- [x]` lines outside code fences; a plan without boxes shows "no checklist". A plan file that is not on this machine is a note, not a warning; a plan with every box ticked is a warning ("delete the file and the `plan:` line").

## Commands

Run from the project root with the repo venv (`CI/setup_venv.*` creates `.venv`; `pytest` comes from `requirements.txt`). On Windows use `.venv\Scripts\python`.

```bash
.venv/bin/python tools/todo_sync/todo_sync.py check                # one line per entry, exit 1 on a format error
.venv/bin/python tools/todo_sync/todo_sync.py parse [--out FILE]   # the parsed data as JSON
.venv/bin/python tools/todo_sync/todo_sync.py dashboard            # build/todo/index.html (git-ignored)
.venv/bin/python tools/todo_sync/todo_sync.py pull [--apply]       # what changed in Vikunja (report only without --apply)
.venv/bin/python tools/todo_sync/todo_sync.py push --dry-run       # what would change in Vikunja
.venv/bin/python tools/todo_sync/todo_sync.py push                 # export
.venv/bin/python -m pytest tools/todo_sync/tests                   # tests
```

`--todo DIR` (the entry folder, default `todo`), `--root DIR` (where `.env` is looked up, default the parent of `--todo`) and `--plans-dir DIR` (then `TODO_SYNC_PLANS_DIR`, default `~/.claude/plans`) apply to every command. The `todo-sync` skill wraps these. Order: `pull`, then `push`.

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

- **Identity:** a managed task's description starts with the visible text `todo-sync:id=<entry id>`. Nothing is stored locally, so another machine finds the same tasks. Do not delete that line in Vikunja: the task would be created again. Tasks without the marker are never touched by `push`.
- **Owned fields:** title, description, done and the labels `in progress`, `review`, `deferred`, `stage N`. Edits to these in Vikunja are overwritten by the next push (`push --dry-run` shows which). Everything else (priority, due date, assignees, comments, other labels) is kept: an update is read-modify-write, because `POST /tasks/{id}` replaces the whole task.
- **Lifecycle:** a new entry creates a task; a changed entry updates it; an entry that left `todo/` marks its task done (never deleted); an entry that comes back reopens it, with a warning "closed in Vikunja since the last push? run pull". A second push with no change does no writes.
- **The copy of a plan:** an entry with `plan: NAME.md` gets, at the end of its description, a block that starts with the visible paragraph `todo-sync:plan=NAME.md`, then the plan's title and, under each heading that has boxes, its boxes as the editor's task list (`<ul data-type="taskList">`), so Vikunja shows the checklist progress itself (the frontend counts `data-checked`; not seen in a browser from here). The prose of the plan is not copied; an item is cut at 240 characters and a block at 300 items, with a visible "…cut" line. The direction is local to tracker only. Plan file found: the block is rendered from it. Plan file not found (another machine): the block already in the task stays, so a push from a machine without the plan never erases the copy. No `plan:` line: no block, so the copy disappears at the next push. The entry deleted: the task is closed and the block is cut, the rest of the text stays.
- **`review`:** an entry in this status reaches the tracker only after its branch is merged into `main`; the `todo-loop` skill never runs `push` or `pull`. `pull` does not turn the `review` label of a task made in Vikunja into a status (it adopts only `in progress` and `deferred`): `review` comes from the loop, not from the tracker.
- `--dry-run` reads from Vikunja and prints `create` / `update` / `close` / `reopen` per entry plus totals; it writes nothing.

## What `pull` does

Same settings and the same `GET /tasks?filter=project_id = N` listing as `push`; no local state, so a pull decides from what it reads. Report only by default (it can delete files); `--apply` writes.

- **Closed in Vikunja:** a task with the marker that is done while `todo/<id>.md` exists. With `--apply` the file is deleted (a finished entry is deleted, never marked done), but only if git tracks it and it has no uncommitted changes; otherwise it is kept with a warning. A done task whose file is gone is consistent already.
- **Created in Vikunja:** a task without the marker that is not done is adopted as `todo/<slug of the title>.md` (a `-2` suffix on a clash): `# Title`, the labels `in progress` / `deferred` / `stage N` as `status:` (with today's date) and `stage:`, the description converted from HTML to markdown as the body. The task is then written back with the marker, so the next `push` reports it `unchanged` instead of creating a duplicate.
- **A stale browser tab loses the marker:** the Vikunja page saves the whole task it loaded. If a task page was open before `push` or `pull --apply` wrote the marker, closing the task from that tab sends the old description back and the marker is gone (seen on Vikunja 2.7.1). `pull` then takes the task for a foreign one and sees no closure, and `push` would create the entry again. Reload the task page before closing or editing a task right after a sync; a task that lost its marker can be repaired by writing `todo-sync:id=<id>` as the first line of its description.
- **Not pulled back:** edits of the title, description or labels of a managed task (the next `push` overwrites them), priority, due date, assignees, and a status set by a label. The direct database of the tracker is never opened; everything goes through the REST API.

Endpoints used (REST `/api/v1`): `GET|PUT /projects`, `GET /tasks?filter=project_id = N` (lists done tasks too, paged by `X-Pagination-Total-Pages`), `PUT /projects/{id}/tasks`, `POST /tasks/{id}`, `GET|PUT /labels`, `POST /tasks/{id}/labels/bulk`. Vikunja 2.7.1 has no `GET /projects/{id}/tasks`. If a newer version changes one of these, `tests/fake_vikunja.py` is the place that records what the adapter relies on.

## Trying it on a local Vikunja

```bash
mkdir -p vikunja-data vikunja-files && chmod 777 vikunja-data vikunja-files
docker run -d --name vikunja -p 127.0.0.1:3456:3456 \
  -e VIKUNJA_SERVICE_SECRET=change-me -e VIKUNJA_SERVICE_PUBLICURL=http://localhost:3456/ \
  -e VIKUNJA_DATABASE_TYPE=sqlite -e VIKUNJA_DATABASE_PATH=/db/vikunja.db \
  -v "$PWD/vikunja-data:/db" -v "$PWD/vikunja-files:/app/vikunja/files" vikunja/vikunja:2.7.1
```

The image runs as UID 1000 and cannot create `/app/vikunja/files` itself (checked on 2.7.1: "permission denied" and the server exits), hence the second mount. Open `http://localhost:3456`, register, create an API token with the permissions above, and put `VIKUNJA_URL` and `VIKUNJA_TOKEN` into `.env`. Mount the data folders outside the repository, or the database ends up in the checkout.

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

`tests/` has a sample `todo/` folder with a sample plan (`fixtures/todo`, `fixtures/plans`), the parser, dashboard and pull tests, and `fake_vikunja.py`, an in-process HTTP server that copies the Vikunja behaviour the adapter depends on (full replace on update, paging headers, labels in the listing). The pull tests make a git repository in `tmp_path`. `test_real_todo_folder_parses_with_known_statuses` parses the repository's own `todo/`, so a format slip in an entry fails the suite.
