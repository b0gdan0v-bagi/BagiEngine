# TODO

The entries are not files any more. The list of known problems and ideas of the engine (area `engine`), their status, the roadmap stages, the plans and the reviews live in the **todo store**: a service on Railway over Postgres, described in [tools/todo/CLAUDE.md](../tools/todo/CLAUDE.md). Nothing of it is committed, so this folder holds only this note.

- Read: `python3 tools/todo/todo.py list`, `show <id>`, `dashboard` (the pages), or Vikunja (project `BagiEngine`), which the service keeps in step by itself in both directions.
- Add: `/todo-add <idea>` (or `todo.py add`). Work: `/todo-loop`, `/todo-merge`. Everything else: `/todo-db`.
- The direction of the engine: the owner plan `engine-roadmap.md` (`todo.py plan get engine-roadmap.md --stdout`); the stages are a setting of the area.
- A finished entry is closed, not deleted; its history stays (`todo.py history <id>`).
- `todo.py export --out <folder outside git>` writes every entry and plan in the old file format, as a backup.
