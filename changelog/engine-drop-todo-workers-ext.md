# The engine still keeps the todo-workers VS Code extension, which now lives in BagiTodo

- Version: 0.6.7
- Entry: engine-drop-todo-workers-ext
- Date: 2026-11-19
- Bump: patch

## Changed
- `tools/vscode/todo-workers/` is gone: the Todo Workers panel (extension id `bagiengine.todo-workers`, unchanged) lives in BagiTodo, and the service builds its `.vsix` and hands it out as it hands out the CLI. `tools/vscode/` keeps only `bagi-dev`.
  Adapt: install or update the panel with `python3 engine/tools/todo/worker.py ext install` (`ext status` shows the installed and the offered version), then Developer: Reload Window; a version installed by hand from the engine's copy updates the same way, without `--force`. Remove any call of `engine/tools/vscode/todo-workers/pack.py` and any mention of that folder from the game's docs, scripts and settings. A change of the panel is an entry of the area `bagitodo`.
- `tools/todo/CLAUDE.md` has the section "The Todo Workers panel (VS Code)": where the panel comes from and that it reads `worker.py list --json` and `buildslots.py status --json` of `tools/todo/`, so those scripts keep their names.
