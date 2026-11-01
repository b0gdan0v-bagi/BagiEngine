# The todo as files, synced with Vikunja on Railway; model plans out of the repo, with a copy in the tracker

## Context

`TODO.md` is 397 lines with 22 entries. Each entry is prose (Where / What is wrong / A fix has to cover); `tools/todo_sync/todo_parser.py` finds the fields with regexes over `**Status:**` / `**Stage:**` / `**Plan:**` lines and the entry id is the slug of the heading, so renaming a heading makes `push` close the Vikunja task and create a new one. The export is one-way: `push` owns `title`, `description`, `done` and the labels, so a task closed in Vikunja is reopened by the next `push`, and a task created in Vikunja never reaches the repository. Model plans sit in `.claude/plans/` under random names (22 files, most finished).

Decisions (user, 2026-11-01):

1. **Storage:** one markdown file per entry in `todo/`, with front matter. JSON is rejected: the entries are paragraphs with lists and code, which become `\n`-escaped strings; `todo_sync.py parse` already emits JSON for programs.
2. **Sync:** the files stay the source of truth for the content; a new `pull` brings closed tasks and new tasks back from Vikunja. Vikunja is reached through its REST API as now; the SQLite database on the Railway volume is never opened directly.
3. **Plans are not in the repository** (the owner's decision in `move-old-plans-to-whimsical-pelican.md`): plan mode writes them to the local store `~/.claude/plans/`, finished ones are deleted. **New:** a plan also gets a copy in the Vikunja task of its entry, so its progress is visible from any machine, and the copy goes away when the plan is done. Part 0 carries out the old plan's cleanup; Part 3 replaces its step 5 (it dropped the `plan:` field and with it all plan progress; here the field stays, as a bare file name).

The dashboard (`dashboard.py`) and the export (`vikunja.py`) read only the dict that `parse_todo` returns, so the storage change stays inside `todo_parser.py`, the CLI default path, the tests and the docs.

## Target layout

```
todo/
  README.md      # the rules and the entry format (replaces the header of TODO.md)
  roadmap.md     # stages list, "Not now", "Driver" (not an entry; its `N. **Title:**` list gives the stages)
  no-ci.md       # one entry per file, 22 files
  ...
```

```
---
status: deferred 2026-10-31 free text      # optional; open | in progress | deferred, then a date, then text; default open
stage: 1                                    # optional
plan: todo-clever-emerson.md                # optional; a plan file NAME in the local plans folder (Part 3)
id: old-slug                                # optional; only to keep an already exported tracker task (Part 1, step 1)
---
# Title of the entry

**Where:** ...
**What is missing:** ...
**A fix has to cover:** ...
```

Rules: id = file name without `.md` (or `id:`); title = the first `# ` line; front matter is `key: value` lines split on the first colon, an unknown or repeated key is a `ParseError`; no front matter means an open entry with no stage and no plan. A finished entry is `git rm`'d, as before. `README.md` and `roadmap.md` are not entries. The summary still comes from the `**What is missing / wrong**` paragraph, so Vikunja descriptions do not change.

## Part 0: remove the plans from the repository (the owner's plan, unchanged)

Skip this part if `.claude/plans/` is already gone (the other plan was executed in another session).

- [ ] 0.1 Copy the 4 live plans (`buzzing-tinkering-cat`, `chess-staged-stream`, `magical-leaping-blossom`, `delightful-stirring-eagle`) to `~/.claude/plans/` with `cp`, `cmp` each. Two of them exist only in the working tree (one untracked, one with uncommitted edits), so this comes before any delete. This plan file is copied the same way.
- [ ] 0.2 `git rm -r .claude/plans` (the 16 finished plans listed in `move-old-plans-to-whimsical-pelican.md`, the 2 in `archive/`, the tracked live ones), `rm` the untracked one, this plan file and `move-old-plans-to-whimsical-pelican.md` last.
- [ ] 0.3 Stop them coming back: delete `.claude/settings.json` (its only key is `plansDirectory`; plan mode then writes to `~/.claude/plans/`), add `.claude/plans/` to `.gitignore`.
- [ ] 0.4 Docs: `CLAUDE.md` "After Making Changes" (plan mode writes to the local store, plans are not committed, link the plan from the entry with `plan:`, delete the plan when the work is done) and `src/Modules/BECore/Control/CLAUDE.md:5` (drop "and `.claude/plans/`").

## Part 1: `todo/` instead of `TODO.md`

- [ ] 1. **Find out if Vikunja holds managed tasks.** `.venv/bin/python tools/todo_sync/todo_sync.py push --dry-run` on the current `TODO.md` (read-only; `.env` exists locally). `unchanged` / `update` rows mean tasks exist: every old id must survive (an `id:` line in each file whose short name differs from the old slug). All rows `create`: file name = id, no `id:` lines.
- [ ] 2. **Snapshot.** `todo_sync.py parse --out <scratchpad>/before.json` on the current `TODO.md` (its uncommitted edits move into the new files).
- [ ] 3. **Migration script** in the scratchpad (not committed): split `TODO.md` on `## ` outside code fences; write `todo/<short-name>.md` with front matter from the `**Status:**` / `**Stage:**` / `**Plan:**` lines (removed from the body, the rest copied verbatim); Roadmap section to `todo/roadmap.md`, the header to `todo/README.md`. Short names by hand (`no-ci`, `no-audio`, `mahjong`, `control-api-remaining`, `render-output-size`, …). The old `slugify` computes `id:` per step 1.
- [ ] 4. **Parser** (`todo_parser.py`): `parse_todo(todo_dir, root)` reads `*.md` of the folder (sorted, not recursive), `_read_front_matter`, title from the `# ` line, stages from `roadmap.md` via `_parse_stages`. Reuse `_parse_status`, `_plan_progress`, `_extract_summary`; keep `slugify` (migration and `pull` use it); drop `_split_sections`, `_FIELD_RE` and the legacy prose status form. The result keeps its keys; `line` becomes `file` (nothing in `dashboard.py` / `vikunja.py` reads `line`). A duplicate id stays a `ParseError`.
- [ ] 5. **CLI** (`todo_sync.py`): `--todo` is the folder, default `todo`; `root_of` falls back to its parent. Fix the docstrings and the `Generated … from TODO.md` line in `dashboard.py`.
- [ ] 6. **Tests** (`tools/todo_sync/tests`): `fixtures/todo/` replaces `fixtures/TODO.sample.md` (README, roadmap, entries: no front matter, each field, `id:` override, a bad status, a bad date); new cases for unknown key, repeated key, missing title, duplicate id; `test_real_todo_md_parses_with_known_statuses` parses the real `todo/`; fix the fixtures in `test_dashboard.py` / `test_vikunja.py`.
- [ ] 7. **Run the migration**, then `git rm TODO.md`.
- [ ] 8. **Cross-references.** Entries point at each other ("entry below", "the CI entry", "the release entry") and the Roadmap points at entries: rewrite them as links to the new files. Other places that name `TODO.md`: root `CLAUDE.md` (lines 75, 163, 164), `tools/todo_sync/CLAUDE.md`, `.claude/skills/todo-sync/SKILL.md`, `.claude/rules/widgets-module.md:16`, `src/Modules/BECore/Control/CLAUDE.md:5`, `src/Modules/Vulkan/CLAUDE.md:32`, and the comment at `games/Match3/src/Match3/Components/Match3BoardComponent.cpp:92` (comment only, no formatting needed). The "UI authoring" entry names `.claude/plans/magical-leaping-blossom.md` in prose: say "the plan `magical-leaping-blossom.md` in the local plan store" instead.
- [ ] 9. **Docs.** `todo/README.md` carries the format; `tools/todo_sync/CLAUDE.md` says the id is the file name and that retitling an entry is safe (renaming the file is not).

## Part 2: `pull` (Vikunja to the files)

Same `VIKUNJA_*` settings and the same `GET /tasks?filter=project_id = N` listing as `push`. No local state (the existing rule), so a pull decides from what it reads. Report-only by default (it can delete files, so the reverse of `push`); `--apply` writes.

- [ ] 10. **Closed in Vikunja.** A task with the marker `todo-sync:id=X` that is `done` while `todo/X.md` exists: reported as "closed in Vikunja, entry still in the repository". With `--apply` the file is deleted (a finished entry is deleted, never marked done), but only if `git` tracks it and it has no uncommitted changes; otherwise it is kept with a warning. A done task whose file is gone is consistent already.
- [ ] 11. **Created in Vikunja.** A not-done task without the marker, in the project: listed as a candidate. With `--apply` it is adopted as `todo/<slugify(title)>.md` (a `-2` suffix on a clash): `# Title`, the labels `in progress` / `deferred` / `stage N` turned into `status:` (with today's date) and `stage:`, the description converted from HTML to markdown text as the body. Then the task is written back with `render_description` of the parsed new entry, so it carries the marker and the next `push` reports it `unchanged` instead of creating a duplicate.
- [ ] 12. **A guard in `push`:** before it reopens a done task it prints a warning naming the entry: "closed in Vikunja since the last push? run `pull`". The reopen stays (an entry that comes back must reopen its task).
- [ ] 13. **Tests** with `fake_vikunja.py`: report-only changes nothing; a done task deletes a clean tracked file (a `git init` in `tmp_path`) and keeps a modified or untracked one; an unmarked task is adopted, gets the marker, and a following `push --dry-run` is `unchanged`; a name clash gets the suffix; a second `pull --apply` is a no-op.
- [ ] 14. **Docs.** `tools/todo_sync/CLAUDE.md` loses "the export is one-way and is never edited back" and gains the `pull` section; the `todo-sync` skill gets a `pull` row and the order "pull, then push"; root `CLAUDE.md` mentions the two-way sync.

Not pulled back (stated in the docs): edits of the title, description or labels of a managed task (the next `push` overwrites them; `push --dry-run` shows which), priority, due date, assignees. A status set by the label `in progress` in Vikunja is a natural next step, left out of this version.

## Part 3: a copy of the model's plan in the Vikunja task

The working plan stays a local file written by plan mode (`~/.claude/plans/<name>.md`, never in the repo). The entry names it with `plan: <name>.md`. `push` puts a copy of it into the task's description; that is the "next to the todo" part. Direction is local to tracker only.

Design, chosen to survive the rules already in `vikunja.py` (the description is owned by `push` and compared as HTML; identity is a visible text marker, because the editor may strip attributes):

- the description is the entry's facts (as now), then a plan block that starts with the visible paragraph `todo-sync:plan=<name>.md` and runs to the end; the block is the plan converted from markdown to simple HTML, its `- [ ]` / `- [x]` lines as the editor's task-list markup, so Vikunja shows the checklist progress itself (to be confirmed in the spike);
- the plans folder is `--plans-dir`, then `TODO_SYNC_PLANS_DIR`, default `~/.claude/plans` (the tool takes the path as an argument, like `--root`; nothing in it names a game);
- **file found:** the block is rendered from it. **File not found** (another machine): the block already in the task is kept as it is, so a `push` from a machine without the plan never erases the copy. **No `plan:` line** (finished, or never set): no block, so the copy disappears at the next `push`. **A closed task** (the entry was deleted): `CLOSE` also cuts the block, so a done task does not keep a dead plan;
- `planProgress` and the dashboard read the local file when it exists, so progress shows on this machine without the network.

- [ ] 15. **Spike, before any code.** On the local Docker Vikunja (recipe in `tools/todo_sync/CLAUDE.md`), not on Railway: does a description with `<ul data-type="taskList">` items show the checklist progress in the task list and keep its markup after a `POST /tasks/{id}` round trip, how large a description may be, does the marker paragraph survive an edit in the UI. If the progress does not show, the fallback is the plan as a comment (`PUT /tasks/{id}/comments`) with no native progress; stop and ask before building that.
- [ ] 16. **Markdown to HTML** for plans in `vikunja.py` (or a small `plan_html.py`), stdlib only: headings, paragraphs, bullet / numbered / task lists, fenced code, `code` and **bold**; everything through `html.escape`. A plan longer than the size found in step 15 is cut with a visible "…cut" line.
- [ ] 17. **Parser and tool:** the `plan:` value is a bare file name (reject a path with a separator); `parse_todo` takes `plans_dir`, fills `planProgress` and a new `planFound`; `render_description` takes the plan block; `plan_actions` keeps the existing block when the file is missing (extract from the marker to the end of the task's description) and `CLOSE` cuts it. `sync` is unchanged otherwise.
- [ ] 18. **The "delete me" signal.** `check` and the dashboard warn `plan 'x.md' is complete (N/N): delete the file, remove the plan: line (the copy leaves the tracker at the next push), and delete the entry if the problem is solved`. A linked plan not found locally is a note, not a warning.
- [ ] 19. **Tests:** block rendering (task list markup, escaping); `push` twice is `unchanged`; the file missing keeps the copy; `plan:` removed drops it; `CLOSE` cuts it; a complete plan warns, an incomplete one does not; the markdown converter cases.
- [ ] 20. **Docs.** `todo/README.md` (the `plan:` field, the lifecycle: write the plan, link it, `push`, work, delete the plan and the line, delete the entry when solved); `tools/todo_sync/CLAUDE.md` (plans folder, what the copy is, local to tracker only); root `CLAUDE.md` (already touched in 0.4: add `push` after linking).

## Verification

- [ ] `todo_sync.py parse` after step 7 against `before.json`: same entries, same `title`, `status`, `date`, `stage`, `plan`, `summary`, same `stages`; only `line` → `file` differs (and `id` where step 1 allowed it). Any other difference is a migration bug.
- [ ] `.venv/bin/python -m pytest tools/todo_sync/tests` passes (Parts 1 to 3). `todo_sync.py check`: 22 entries, 6 stages, no new warnings. `todo_sync.py dashboard` shows the same rows.
- [ ] `push --dry-run` after the migration: when step 1 found tasks, every row is `unchanged` (the ids survived).
- [ ] After Part 0: `ls .claude/plans` fails; the 4 live plans and this one are byte-identical in `~/.claude/plans/`; `git status --short` shows the deletions, `.gitignore`, the removed settings file and the doc edits; no engine or game source is touched except the one comment in `Match3BoardComponent.cpp`.
- [ ] Part 3 end to end on the local Docker Vikunja first: write a throwaway plan, link it with `plan:`, `push`: the task shows the plan and its progress; tick a box in the file, `push`: progress follows; move the file away, `push`: the copy stays; remove the `plan:` line, `push`: the copy is gone.
- [ ] By hand on the Railway Vikunja, only after the user agrees to writes on the real tracker: close one task and create one in the UI; `pull` lists both; `pull --apply` deletes the closed one's file (clean and tracked) and adds a file for the new one; `push --dry-run` is all `unchanged`; a second `pull` reports nothing.
- [ ] `grep -rn 'TODO\.md'` outside the history finds nothing stale.

## Not in scope

No commit (only on request). No generated `TODO.md` index in the repository (it would go stale; the dashboard is the overview). No direct access to the Railway database. No pull of plans back from the tracker, no auto-deletion of plans (the tool only says a plan is complete). Label and status changes made in Vikunja are not pulled.
