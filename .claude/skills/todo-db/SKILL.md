---
name: todo-db
description: Reads and maintains the todo store (the service of the owner's BagiTodo that is the source of truth for todo entries, plans and reviews) through tools/todo/todo.py, the loader of the CLI the service hands out. Use when the user asks to list or show todo entries, open the TODO dashboard, check the store or the token, see the history of an entry, change an entry by hand, export a backup, or asks about "sync", "push" or "pull" of the todo list (there is none: the store is the only copy).
---

# todo-db

Wraps `python3 <engine>/tools/todo/todo.py` (the loader: it downloads the CLI from the service and runs it; `<engine>` is in section `Areas` of `.claude/rules/todo-project.md`: for example `.` in the engine repository and in a workspace, `engine` in a game). Everything about the loader, its settings and the commands is in the store's docs that section `Areas` names (the loader, the settings, the commands). The areas and where `.env` comes from are in that section too.

The store is the source of truth: entries, statuses, plans, reviews. **Nothing of it is in git**, and there is no push and no pull: the store is the only copy, and its pages (`dashboard`) show it to a person.

## Arguments (all optional)

| Argument | Command | Effect |
|----------|---------|--------|
| *(none)* / `list` | `list [--area A]` | The open entries (open, in progress, review, deferred) with priority and plan progress. |
| `show <id>` | `show <id>` | One entry as markdown. |
| `history <id>` | `history <id>` | Who changed what, newest first. |
| `dashboard` | `dashboard` | The address of the pages (log in with any user name and the token from `.env`; do not print the token). |
| `check` / `ping` | `ping` | The service and the token. |
| `export` | `export --out <dir outside git>` | A backup of every entry and plan in the old file format. |

## Workflow

1. Settings: `TODO_API_URL` and `TODO_API_TOKEN` in the environment or the git-ignored `.env` that section `Areas` names (of the repository, or of the workspace folder). If the CLI says they are missing, tell the user where to put them; never ask for the token in chat and never print it. The token is the checkout's (or the workspace's) own, limited to its areas, and issued by the owner: a refusal for an area outside it is the owner's to fix.
2. Reads (`list`, `show`, `history`, `ping`, `dashboard`) need no confirmation. A write (`add`, `edit`, `close`, `plan put|delete`, `area set`) changes shared state: do it only when the user asked for that change, and say what changed.
3. Statuses: `review` is set only by `todo-loop`; `close` after a merge is `todo-loop close` or `todo-merge`. By hand, an entry is deferred or reopened with `edit --status`.
4. The service does not answer: the loader runs the last copy it downloaded, with a line on stderr; a write then still fails. Say so, do not retry in a loop.

## Report

The command and its result, briefly; for `ping` what it answered. Say plainly when a step failed.
