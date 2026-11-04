# The todo areas of this repository

Read by the skills `todo-loop`, `todo-merge`, `todo-add`, `todo-sync` and by the agents `todo-*` (`.claude/skills/todo-loop/SKILL.md`, "Areas and the repository rules" explains the terms). A game that carries this repository as the submodule `engine/` has a file of the same name with its own table; the skills are the same text in both.

## Areas

| Area | `<todo>` | `<R>` (git repository) | `<base>` | `<tracker>` (Vikunja project) | Env file |
|------|----------|------------------------|----------|-------------------------------|----------|
| engine | `todo/` | `.` | `main` | `BagiEngine` | `.env` (the tool's default) |

`<engine>` is `.`.

## Build and test (baseline and reviewer)

- Targets: `BagiEngineTests Match3Tests` and the games the smoke tests need (`BagiMatch3`); then `ctest --preset <p>`. The preset comes from `cmake --list-presets=build` (the `build` skill picks it).
- Read first: `CLAUDE.md` (style, DO and DON'T), the nested `CLAUDE.md` of the touched directories and the matching `.claude/rules/` file.

## Zones nobody edits

The vendored code: `src/Modules/EABase/`, `EnTT/`, `fmt/`, `imgui/`, `nlohmann/`, `pugixml/`, `RmlUi/`, `stb/`, and `external/`.

## Classifier

`eligible` is a concrete, bounded piece of engine or tool work. Examples of reasons:

- `owner decision`: renderer backends, nominal ECS, moving games to their own repositories, and similar.
- Example answer lines: `{"area": "engine", "id": "no-ci", "verdict": "eligible", "size": "M", "reason": "one line"}`, `{"area": "engine", "id": "renderer-backends", "verdict": "skip", "size": null, "reason": "owner decision: the entry waits for the choice of backends"}`.
