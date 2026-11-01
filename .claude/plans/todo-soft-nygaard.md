# TODO.md as a task tracker: convention, parser, dashboard, Vikunja export

## Context

The `TODO.md` entry "TODO.md is read only by people: no link to a task tracker or a dashboard" (roadmap stage 1, "Hygiene") asks for three things: a machine-readable shape for the entries, a view of progress, and a path to the owner's personal tracker. The project owner chose the full scope and **Vikunja (self-hosted)** as the tracker. The entry's own recommendation stands: `TODO.md` stays the source of truth (models read and edit it), the export is one-way and never edited back.

No engine code, no CMake, no C++ is touched. The new tool is Python in `tools/todo_sync/`, stdlib only (the repo `.venv` is Python **3.10**, so no `tomllib`, no `datetime.UTC`). Tests are pytest (already in `requirements.txt`).

## Facts the plan rests on (read in the repo, nothing run)

- `TODO.md`: `## Roadmap` (numbered stages 1-6 with a bold title each), then 23 entries as `## Title`. Only two entries carry a status today, both as prose: CI (`**Status:** deferred by the project owner on 2026-10-31. ...`) and external control (`**Status:** in progress since 2026-11-01, plan in \`.claude/plans/buzzing-tinkering-cat.md\`. ...`). No entry has an id, a stage or a plan field.
- `.claude/plans/*.md`: only `buzzing-tinkering-cat.md` has checkboxes (`- [x]` / `- [ ]`, 11 lines, in "Прогресс"). Other plans describe status in prose, so plan progress is `null` for them.
- `CI/scripts/*.py` is the style to follow (argparse, `pathlib`, run from project root). `.venv` is created by `CI/setup_venv.*`. `build/` and `.env` are already git-ignored, so the dashboard output (`build/todo/`) and the Vikunja settings need no `.gitignore` edit.
- Vikunja REST (checked against the demo instance's `docs.json`, not against the owner's instance): `PUT /projects`, `GET /projects`, `PUT /projects/{id}/tasks`, `POST /tasks/{id}` (update, also sets `done`), `GET /tasks` and `GET /projects/{id}/views/{view}/tasks` (params `page`, `per_page`, `filter`, `filter_include_nulls`), `PUT /labels`, scoped API tokens in `Authorization: Bearer ...`. **Not confirmed:** the label-on-task endpoint (`PUT /tasks/{id}/labels/bulk` expected), whether done tasks are listed by default, and whether `GET /projects/{id}/tasks` still exists in the owner's version. Task 0 settles this.

## Design

### 1. Convention (written in the `TODO.md` header next to "one entry per problem")

- **Id:** the heading slug (lowercase, non-alphanumerics to `-`, cut at 60 characters on a word boundary). Renaming a heading makes a new id (the old task is closed, a new one is created); the parser rejects duplicate ids.
- `**Status:**` starts with one keyword, `open` (default when the line is absent), `in progress` or `deferred`, then a date `YYYY-MM-DD` and free text.
- `**Stage:**` the roadmap number 1-6. Optional: bugs that sit outside the roadmap stay without a stage and appear under "No stage".
- `**Plan:**` a path under `.claude/plans/`.
- A finished entry is deleted, not marked done (the existing rule); the tracker closes its task.

Migration of the current entries (Status / Stage / Plan lines added; text otherwise untouched):

| Entry | Stage | Other |
|-------|-------|-------|
| No CI | 1 | already `deferred`, date kept |
| TODO.md is read only by people (this one) | 1 | `in progress`, date of the first commit, `Plan: .claude/plans/todo-soft-nygaard.md` |
| No external control | 2 | already `in progress`, add `**Plan:**` |
| No animation, 2D rendering is minimal, No text rendering, No audio, Input has no mapping layer, No in-game UI, Game UI has no LLM-friendly authoring path | 3 | |
| Renderer backends, ECS is nominal | 4 | |
| No prefabs, No save games / hot-reload / asset packing, Test gaps and platforms, The reflection generator is regex-based | 5 | |
| Third game: Mahjong, No release builds | 6 | |
| Config Editor sink dropdown, Vulkan swapchain / HiDPI, Chess pieces stretched, Match3 view size, Scene editor saves only the active scene | none | bugs outside the roadmap order |

The parser also accepts the legacy prose form (keyword at the start of the status line, the first `YYYY-MM-DD` as the date, a `.claude/plans/*.md` path in the status line as the plan), so a half-migrated file still parses.

### 2. Parser (`todo_parser.py`)

`parse_todo(todo_path, root) -> dict`, no I/O besides reading files:

```
{ "stages":  [{"number": 1, "title": "Hygiene"}, ...],          # from the ## Roadmap numbered list
  "entries": [{"id", "title", "status", "date", "stage", "plan",
               "planProgress": {"done": n, "total": m} | null,   # counts - [x] / - [ ] lines in the plan file
               "summary", "line"}] }                              # summary = the "What is missing/wrong" lead, plain text
```

Tolerant: absent fields give `open` / `None`; a missing plan file keeps the path, sets progress `null` and prints a warning to stderr; unknown stage numbers are kept and shown under "No stage". Errors (exit 1): duplicate ids, a status keyword that is not one of the three, unreadable `TODO.md`.

### 3. Dashboard (`dashboard.py`)

One self-contained `build/todo/index.html` (inline CSS, no JS, no network, light/dark by `prefers-color-scheme`), all text HTML-escaped. Sections, in this order: roadmap stages with entry counts by status and the entries under each; in-progress entries with a plan progress bar (or "no checklist"); deferred entries with age in days; "No stage". It is generated locally and not published anywhere.

### 4. Vikunja export (`vikunja.py`)

- Stdlib `urllib.request` client. Settings from the process environment, then from the git-ignored repo-root `.env` (a ten-line `KEY=VALUE` reader, no dependency): `VIKUNJA_URL`, `VIKUNJA_TOKEN`, `VIKUNJA_PROJECT` (title, default `BagiEngine`, created if missing). The token is never printed or logged; the Vikunja token should be a scoped one (projects, tasks, labels).
- **Identity:** every managed task starts its description with the visible text `todo-sync:id=<id>` (a visible token survives the Vikunja editor, an HTML comment may not). No local state file, so a second machine finds the same tasks.
- **Sync, one pass:** parse; find or create the project; list all tasks of the project including done ones and map by the marker (a duplicate marker is a warning, the first wins); ensure labels `in progress`, `deferred`, `stage 1..6`; for each entry create the task or update it when title, description, labels or `done` differ; a task whose marker is no longer in `TODO.md` is marked done (never deleted); a task of a returned entry is reopened.
- **Owned fields:** title, description, done, labels. Priority, assignees, comments, due dates and any other label are never touched. Edits to owned fields in Vikunja are overwritten on the next push (one-way, stated in the tool's CLAUDE.md).
- `--dry-run` does only GETs and prints `create / update / close / reopen / unchanged` per id plus totals; a normal run prints the same totals after the writes.
- Description is a few HTML paragraphs: marker, status and date, stage title, plan and its progress, the summary, and the `TODO.md` heading name (the repo is private, so no deep link).

### 5. CLI and refresh path

`python tools/todo_sync/todo_sync.py <parse|dashboard|push> [--todo TODO.md] [--out build/todo] [--dry-run]`. The paths are arguments, so a game repository that carries engine as a submodule can run it on its own `TODO.md`; nothing names a concrete game. A skill `.claude/skills/todo-sync/SKILL.md` (same format as `build`) wraps it: default = dashboard, `push`, `push dry-run`, `check` (parse only). No git hook: it would call the network on commits.

## Files

| New | Purpose |
|-----|---------|
| `tools/todo_sync/todo_sync.py` | CLI |
| `tools/todo_sync/todo_parser.py` | `TODO.md` + plans to the dict above |
| `tools/todo_sync/dashboard.py` | dict to HTML |
| `tools/todo_sync/vikunja.py` | client, settings, sync |
| `tools/todo_sync/CLAUDE.md` | convention, commands, settings, Vikunja notes (module guidance sits next to the code) |
| `tools/todo_sync/tests/` | `conftest.py`, `test_parser.py`, `test_dashboard.py`, `test_vikunja.py`, `fixtures/` (a sample `TODO.md`, a plan with checkboxes) |
| `.claude/skills/todo-sync/SKILL.md` | refresh skill |

Edited: `TODO.md` (header convention, migrated entries, this entry at the end: dropped, roadmap stage-1 line reworded), `CLAUDE.md` (layout: `tools/`; Rules and Skills: `todo-sync`).

## Tasks

- [x] 0. Vikunja spike, before the adapter. Against the owner's instance (ask for its URL and a token if one exists): version, list-tasks endpoint, done tasks in the listing, label endpoints, description round trip of the visible marker. If there is no instance, build against the demo `docs.json` and say in the final report that the adapter was verified only against the fake server.
- [x] 1. Convention in the `TODO.md` header; migrate entries per the table.
- [x] 2. `todo_parser.py` + `test_parser.py`.
- [x] 3. `todo_sync.py` (`parse`, `dashboard`) + `dashboard.py` + `test_dashboard.py`.
- [x] 4. `vikunja.py` + `push` + `test_vikunja.py` with a fake HTTP server.
- [x] 5. Docs: `tools/todo_sync/CLAUDE.md`, the skill, `CLAUDE.md`.
- [x] 6. Close out: drop this entry from `TODO.md`, fix the roadmap stage-1 wording, run everything below.

## Tests

- **Parser** (fixtures): entry count, unique ids, the three statuses, date, stage, plan progress from a fixture plan with checkboxes, tolerance for entries without the new fields, the legacy prose status, duplicate id and bad keyword errors. Plus one smoke test on the real `TODO.md`: every entry has an id and a known status, so format drift in the repo fails the suite.
- **Dashboard:** stage counts, in-progress bar, deferred age with a fixed "today", HTML escaping of `<` and `&` in titles.
- **Vikunja** (a `http.server` fake in a thread implementing projects, tasks, labels, and checking the Bearer header): first push creates every entry; second push does zero writes; a removed entry is closed and a returned one is reopened; a status change updates the labels; `--dry-run` does zero writes; a task without a marker is never touched; a duplicate marker warns.

## Verification

1. `.venv/bin/python -m pytest tools/todo_sync/tests` (Windows: `.venv\Scripts\python`) is green.
2. `.venv/bin/python tools/todo_sync/todo_sync.py parse` on the real `TODO.md` prints all 23 entries with the stages from the table, exit 0.
3. `... dashboard` writes `build/todo/index.html`; check the structure by reading the file (the page itself cannot be looked at from the agent session, say so).
4. `... push --dry-run` against the owner's instance prints the expected creates; then one real `push`, a second `push` reports all `unchanged`. If no instance exists, only the fake-server tests cover this and the report says so.
5. `git status`: only `TODO.md`, `CLAUDE.md`, `.claude/skills/todo-sync/`, `.claude/plans/`, `tools/todo_sync/` changed; no token or `.env` staged. No commit unless asked.

Formatting and PCH-include steps do not apply (no C++ changed).

## Status (done)

- Tasks 0-6 done. The spike ran against a throwaway Vikunja 2.7.1 in Docker (removed afterwards). Differences from the plan, all found by the spike:
  - 2.7.1 has no `GET /projects/{id}/tasks`; tasks are listed with `GET /tasks?filter=project_id = N` (done tasks included, paged by `X-Pagination-Total-Pages`).
  - `POST /tasks/{id}` replaces the whole task (priority and due date are reset when left out), so an update is read-modify-write from the listing.
  - Labels are set with `POST /tasks/{id}/labels/bulk`; an update of the task leaves them alone.
  - The minimal API-token permissions are `projects` create/read_all, `tasks` create/read_all/update, `labels` create/read_all, `tasks_labels` update_bulk.
  - Added a `check` command (one line per entry) that the skill uses; the dashboard also lists entries whose stage is not on the roadmap under "No stage".
- Verified on the real server: dry run, first push (23 creates), second push (23 unchanged), a removed entry closed, a returned one reopened, priority / due date / a foreign label / a task without the marker untouched, the minimal token.
- 49 pytest cases (`tools/todo_sync/tests`), 0.4 s. Not verified: the Vikunja web editor round trip of the marker (edited by hand in the UI), Windows, the look of the dashboard page.
- The entry "TODO.md is read only by people" was dropped from `TODO.md` and the roadmap stage-1 line reworded.
