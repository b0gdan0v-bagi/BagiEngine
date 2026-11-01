---
name: todo-sync
description: Refreshes the TODO.md progress dashboard and exports TODO.md entries to the Vikunja task tracker through tools/todo_sync. Use when the user asks to update, open or regenerate the TODO dashboard, to sync or push TODO to Vikunja or the task tracker, or to check that TODO.md still parses.
---

# TODO sync (BagiEngine)

Wraps `tools/todo_sync/todo_sync.py`. Format of the entries, Vikunja settings and what the export owns are in `tools/todo_sync/CLAUDE.md`; the entry fields are described in the header of `TODO.md`. `TODO.md` is the source of truth: edit it, never the tracker.

## Arguments (all optional)

| Argument | Command | Effect |
|----------|---------|--------|
| *(none)* / `dashboard` | `dashboard` | Writes `build/todo/index.html` and prints its path. |
| `check` | `check` | Parses `TODO.md`, one line per entry, exit 1 on a format error. |
| `push dry-run` | `push --dry-run` | Reads Vikunja, prints what would be created, updated, closed or reopened; writes nothing. |
| `push` | `push` | Exports to Vikunja. |

## Workflow

Run from the project root. The Python is the repo venv (`.venv/bin/python`, on Windows `.venv\Scripts\python`); if it is missing, point the user at `CI/setup_venv.*` and stop.

1. Run `check` first. A format error is a mistake in `TODO.md`: show the message and fix the entry only if asked.
2. For `push`, always run `push --dry-run` first and show the totals. Run the real `push` only when the user asked for it. It writes to the user's tracker, and a wrong `VIKUNJA_PROJECT` would fill the wrong project.
3. If the settings are missing, the tool says which of `VIKUNJA_URL` / `VIKUNJA_TOKEN` is not set. Do not ask for the token in chat and never print it: the user puts it into the git-ignored `.env`.
4. After a `dashboard`, give the file path. The page cannot be looked at from the agent session, so do not describe how it looks.

## Report

State the command, the totals (`N create, N update, N close, N reopen, N unchanged`) and any `[warn]` lines. Say plainly when a step failed or was skipped.
