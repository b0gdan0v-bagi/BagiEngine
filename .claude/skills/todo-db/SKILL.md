---
name: todo-db
description: Reads and maintains the todo store (the Postgres-backed service on Railway that is the source of truth for todo entries, plans and reviews, mirrored into Vikunja automatically) through tools/todo/todo.py. Use when the user asks to list or show todo entries, open the TODO dashboard, check the store or the Vikunja mirror, see the history of an entry, change an entry by hand, export a backup, or asks about "sync", "push" or "pull" of the todo list (there is none any more: the service mirrors by itself).
---

# todo-db

Wraps `python3 <engine>/tools/todo/todo.py` (standard library only; `<engine>` is `.` in the engine repository and `engine` in a game). Everything about the store, the mirror and the commands is in `<engine>/tools/todo/CLAUDE.md`. The areas are in `.claude/rules/todo-project.md`.

The store is the source of truth: entries, statuses, stages, plans, reviews. **Nothing of it is in git**, and there is no push and no pull: the service writes every change into Vikunja by itself and takes back what a person changes there (webhooks plus a reconcile pass every five minutes).

## Arguments (all optional)

| Argument | Command | Effect |
|----------|---------|--------|
| *(none)* / `list` | `list [--area A]` | The open entries (open, in progress, review, deferred) with stage and plan progress. |
| `show <id>` | `show <id>` | One entry as markdown. |
| `history <id>` | `history <id>` | Who changed what, newest first. |
| `dashboard` | `dashboard` | The address of the pages (log in with any user name and the token from `.env`; do not print the token). |
| `check` / `ping` | `ping` | The service, the token, the mirror and its outbox. |
| `reconcile` | `admin reconcile` | Compare every Vikunja project with the store now (it also runs every five minutes). |
| `export` | `export --out <dir outside git>` | A backup of every entry and plan in the old file format. |

## Workflow

1. Settings: `TODO_API_URL` and `TODO_API_TOKEN` in the environment or the git-ignored `.env` of the repository. If the CLI says they are missing, tell the user where to put them; never ask for the token in chat and never print it.
2. Reads (`list`, `show`, `history`, `ping`, `dashboard`) need no confirmation. A write (`add`, `edit`, `close`, `plan put|delete`, `area set`, `admin ...`) changes shared state and the user's Vikunja: do it only when the user asked for that change, and say what changed.
3. Statuses: `review` is set only by `todo-loop`; `close` after a merge is `todo-loop close` or `todo-merge`. By hand, an entry is deferred or reopened with `edit --status`.
4. A mirror problem (`ping` shows a mirror error or a failing outbox): `admin outbox` shows why; `admin reconcile` retries. Duplicated tasks in Vikunja: `admin dedupe` (report), `admin dedupe --apply` only after the user agreed (it deletes the newer tasks).

## Report

The command and its result, briefly; for `ping` the state of the mirror. Say plainly when a step failed.
