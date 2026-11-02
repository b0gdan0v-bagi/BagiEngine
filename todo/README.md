# TODO

Known problems that are not being worked on yet. One file per problem: where it is, what is wrong, what a fix has to cover. A finished entry is deleted (`git rm`), not marked done.

`tools/todo_sync/` reads this folder to build a progress dashboard and to keep a task tracker (Vikunja) in step with it (its `CLAUDE.md` has the commands). `roadmap.md` is the direction for the engine and the list of stages; it is not an entry. This file is not an entry either.

## Entry format

```
---
status: deferred 2026-10-31 free text
stage: 1
plan: magical-leaping-blossom.md
id: old-slug
---
# Title of the entry

**Where:** ...
**What is missing:** ...
**A fix has to cover:** ...
```

- The entry id is the file name without `.md`. The tracker task is found by it, so **renaming a file makes a new entry** for the tracker (the old task is closed, a new one is created); retitling an entry is safe. `id:` keeps an id that differs from the file name (only for a task that was exported before the file got its name).
- The title is the first `# ` line.
- The front matter is optional, and so is every key in it. A line is `key: value`, split on the first colon. An unknown or repeated key is an error. No front matter means an open entry with no stage and no plan.
- `status:` starts with one keyword, `open` (the default), `in progress` or `deferred`, then a date `YYYY-MM-DD` and free text.
- `stage:` is the roadmap number (1 to 6); a problem outside the roadmap order has none.
- `plan:` is the **name** of a plan file in the local plan store (`~/.claude/plans/`, where plan mode writes; no folder in it). Plans are not committed. The `- [ ]` / `- [x]` lines of the file give the progress.
- The summary on the dashboard and in the tracker comes from the `**What is missing**` / `**What is wrong**` paragraph.

## Lifecycle of a plan

Write the plan in plan mode, link it with `plan: NAME.md`, run `todo-sync push` (the tracker task gets the plan's checklist, so the progress is visible from any machine), do the work, delete the plan file and the `plan:` line (the copy leaves the tracker at the next push), and delete the entry when the problem is solved. The tool says so when a linked plan has all its boxes ticked.

## Links

Link other entries with relative links, `[Test gaps and platforms](test-gaps-platforms.md)`, so a moved file shows up as a broken link.
