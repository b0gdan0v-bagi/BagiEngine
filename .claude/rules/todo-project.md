# The todo areas of this repository

Read by the skills `todo-loop`, `todo-merge`, `todo-add`, `todo-db` and by the agents `todo-*` (`.claude/skills/todo-loop/SKILL.md`, "Areas and the repository rules" explains the terms). A game that carries this repository as the submodule `engine/` has a file of the same name with its own table; the skills are the same text in both.

## Areas

| Area | `<area>` (todo store) | `<R>` (git repository) | `<base>` | Vikunja project (the service's) |
|------|-----------------------|------------------------|----------|---------------------------------|
| engine | `engine` | `.` | `main` | `BagiEngine` |

`<engine>` is `.`. The CLI is `python3 tools/todo/todo.py`; `TODO_API_URL` and `TODO_API_TOKEN` come from the git-ignored `.env` (`.env.example`). The Vikunja project of an area is a setting of the store (`todo.py area set`), not of the CLI: the service mirrors by itself.

## Build and test (baseline and reviewer)

- Targets: `BagiEngineTests Match3Tests` and the games the smoke tests need (`BagiMatch3`); then `ctest --preset <p>`. The preset comes from `cmake --list-presets=build` (the `build` skill picks it).
- The build, format and include commands (product, test targets, configure triggers, the `format_sources.py` and `strip_pch_includes.py` calls) are in `build-project.md`; the targets above are the ones the loop uses.
- Read first: `CLAUDE.md` (style, DO and DON'T), the nested `CLAUDE.md` of the touched directories and the matching `.claude/rules/` file.

## Zones nobody edits

The vendored code: `src/Modules/EABase/`, `EnTT/`, `fmt/`, `imgui/`, `nlohmann/`, `pugixml/`, `RmlUi/`, `stb/`, and `external/`.

## Classifier

`eligible` is a concrete, bounded piece of engine or tool work. Examples of reasons:

- `owner decision`: renderer backends, nominal ECS, moving games to their own repositories, and similar.
- Example answer lines: `{"area": "engine", "id": "no-ci", "verdict": "eligible", "size": "M", "reason": "one line"}`, `{"area": "engine", "id": "renderer-backends", "verdict": "skip", "size": null, "reason": "owner decision: the entry waits for the choice of backends"}`.
