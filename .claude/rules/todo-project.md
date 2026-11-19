---
paths:
  - "tools/todo/**/*"
  - ".claude/agents/**/*"
---

# The todo areas of this repository

Read by the skills `todo-loop`, `todo-merge`, `todo-add`, `todo-db` and by the agents `todo-*` (`.claude/skills/todo-loop/SKILL.md`, "Areas and the repository rules" explains the terms). A game that carries this repository as the submodule `engine/` has a file of the same name with its own table; the skills are the same text in both.

## Areas

| Area | `<area>` (todo store) | `<R>` (git repository) | `<base>` |
|------|-----------------------|------------------------|----------|
| engine | `engine` | `.` | `main` |

`<engine>` is `.`. The CLI is `python3 tools/todo/todo.py` (the loader of the CLI the service hands out: `tools/todo/CLAUDE.md`); `TODO_API_URL` and `TODO_API_TOKEN` come from the git-ignored `.env` (`.env.example`); the token is this checkout's own, limited to its areas.

## Build and test (baseline and reviewer)

- Targets: `BagiEngineTests StressTests` and `BagiStress`, which the smoke tests need; then `ctest --preset <p>`. The preset comes from `cmake --list-presets=build` (the `build` skill picks it); its test preset leaves out the label `extended`, which only `alltests` runs through `<p>-all` (`build-project.md`).
- The build, format and include commands (product, test targets, configure triggers, the `format_sources.py` and `strip_pch_includes.py` calls) are in `build-project.md`; the targets above are the ones the loop uses.
- Read first: `CLAUDE.md` (style, DO and DON'T), the nested `CLAUDE.md` of the touched directories and the matching `.claude/rules/` file.

## Workers

Parallel loops run in worker folders (`.claude/skills/todo-loop/SKILL.md`, "Parallel workers"; `tools/todo/CLAUDE.md`, "Workers"): `python3 tools/todo/worker.py create 3` in the main checkout makes `../BagiEngine.workers/w1` ... `w3`, `/todo-loop-parallel` (or `worker.py start`) runs `/todo-loop` in all of them as background sessions a minute apart (`worker.py list` shows them, `worker.py run <k>` opens one in a terminal). Three suit the owner's machine (14 cores; a full build takes about 17 s there, and `worker.py run` gives each session a share of the cores).

- **Shared files**, which do not count alone when the loop compares busy files: `CLAUDE.md`, `README.md` (every entry touches them a little; the developer keeps its edits to its own lines).
- Everything else counts as it is: two entries that change the same source, config or test file wait for each other.

## Zones nobody edits

The vendored code: `src/Modules/EABase/`, `EnTT/`, `fmt/`, `imgui/`, `nlohmann/`, `pugixml/`, `RmlUi/`, `stb/`, `tracy/`, and `external/`.

## Classifier

`eligible` is a concrete, bounded piece of engine or tool work. Examples of reasons:

- `owner decision`: an entry that names what is "to be decided with the owner first" (for example the binary form of sound assets, `audio-binarization`), and similar.
- `owner step`: the work is in Railway, Google, a bank, Telegram, a phone, a check on production or a deploy. When an open entry of the area `owner` is that step, name it: `owner step: <owner id>` (the conductor links the entry to it).
- Example answer lines: `{"area": "engine", "id": "no-ci", "verdict": "eligible", "size": "M", "reason": "one line"}`, `{"area": "engine", "id": "audio-binarization", "verdict": "skip", "size": null, "reason": "owner decision: the binary form of sound assets is to be decided with the owner"}`.
