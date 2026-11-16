---
paths:
  - "tools/qa/**/*"
  - "tools/shots/**/*"
  - ".claude/skills/qa/**/*"
  - ".claude/skills/qa-loop/**/*"
---

# QA facts of this repository

Read by the skills `qa` (`.claude/skills/qa/SKILL.md`) and `qa-loop` (`.claude/skills/qa-loop/SKILL.md`; the library they drive: `tools/qa/CLAUDE.md`) and by `tools/shots/shots.py` (the `## Screenshots` set below). A game that carries this repository as the submodule `engine/` has a file of the same name with the same headings and fields: this file is its template. A game without the file is not tested by `/qa`. The build facts are in `build-project.md`, the todo areas in `todo-project.md`, not here.

## Terms

| Term | Meaning |
|------|---------|
| `<engine>` | `.` in the engine repository, `engine` in a game: the folder with `tools/qa/`. The same term as in `build-project.md`. |
| `<R>` | the repository whose product is tested: the one the session runs in. |
| `<product>` | the executable of `<R>`, from the field `Product` (the file is `build/<preset>/bin/<name>[.exe]`). |
| `<invariants>` | the game's invariants module, from the field `Invariants module`. |
| `<out>` | the folder of one session: `Out folder` + `<date>-<slug>/`. |

## Session

Fields the `qa` and `qa-loop` skills read. A game fills every row.

| Field | Meaning | Engine value |
|-------|---------|--------------|
| Product | the executable, built through the `build` skill if `build/<preset>/bin/` has none | `BagiStress` |
| Preset | the preset of the session, as the `build` skill picks it (`cmake --list-presets=build`) | the Debug preset of this OS (`macos-debug`) |
| Invariants module | the Python file of `tools/qa/CLAUDE.md` ("A game's invariants module"), relative to `<R>` | `tools/stress/qa/invariants.py` |
| Loop preset | what the unattended `/qa-loop` builds when the game is too slow in Debug for a long walk | the Debug `Preset` (`monkey_moves` keeps the slow scenes out) |
| Extra start flags | what `launch(extra=...)` adds to `--headless --manual-clock` | none |
| First scene | where the game is after `launch` | `sprites` with the default count (`get_state` says it) |
| Known state | how a session gets back to a start (`QaSession.reset(**params)`, the game's `reset`) | `reset(scene=..., count=...)`: both optional, the rest (the language) goes to its defaults; `scene` is one of `get_state`'s `scenes` |
| Out folder | where sessions write; outside every repository (`QaSession` refuses a git work tree) | `~/.claude/plans/qa/` |
| Bug area | the area `todo add --area` takes; in a game its own area, or `engine` when the cause is in the engine (the skill decides by where the cause is) | `engine` |
| Bug id prefix | the id of an entry is `bug-<short>`, as the drafts name it | `bug-` |
| Scenario folder | the product's recorded scenarios, relative to `<R>`: the fix of a `bug-*` entry leaves its reproduction here as `<short>.jsonl`, and the ctests of `be_add_game_scenario_tests` glob it | `tools/stress/tests/scenarios/` |

## Invariants

The generic ones (no new Warning / Error / Fatal line, every `ui_tree` canvas laid out with no `issues`, a version that never goes back, a listed action that is accepted) hold for every game and are in `tools/qa/CLAUDE.md`. The game's are in `<invariants>`; this section says them in words, the module is the truth.

Engine, `BagiStress` (the rules of `tools/stress/CLAUDE.md`, "Control API"):

- `scene` is one of `scenes`, `count` is an integer in `0..maxCount`, `language` is `en` or `ru`.
- The state version moves exactly when the state changes: an action that changes nothing leaves it, one that changes something moves it.
- An accepted `set_scene`, `set_count` or `set_language` shows what was asked, in its outcome and in the next `get_state`.
- A refused action (`invalid_params`, `illegal_action`) changes nothing.

## Standing charters

A charter is one sentence; the skill takes the first one when the user names none. The line after it is what the session has to cover.

| Charter | Covers |
|---------|--------|
| Every scene and every language switch | `set_scene` to each scene and `set_language` both ways in every scene, a few frames after each, a screenshot of each scene in both languages, the `ui_tree` check on the `ui` scene |
| A count sweep up to the maximum | `set_count` through small counts, the listed half and double, and `maxCount`, in each scene; a refused `0` and `maxCount + 1`; at a large count the `ui` scene needs tens of seconds in a Debug build, so take it one scene at a time and keep the session to a few hundred calls |
| Reset and the manual clock | `reset` with and without `scene` / `count` from every scene, `step` with several `frames` and `dt`, the state the same after a `reset` as after a start, no frame advancing between two calls that do not `step` |

## Monkey

`tools/qa/bagi_monkey.py` (`tools/qa/CLAUDE.md`, "The monkey") walks the game at random from a seed and stops at the first bug. What it reads from the game: `list_actions` and `ui_tree` for the generic moves, `list_methods` (the `window` moves only when `set_window` is listed, `click` only with `ui_click`, `reset` only with `reset`) and the optional `monkey_moves(answer, moves)` of `<invariants>`, which drops what is too slow and adds the game's own moves. The charter text of a run is the one of the table; `--start` and `--kinds` are its columns.

| Charter | Start | Kinds |
|---------|-------|-------|
| Every scene and every language switch | `{"scene": "sprites", "count": 20}` | all |
| A count sweep up to the maximum | not a monkey charter: a walk cannot keep a big count and a Debug `ui` scene at it in a minute, so `monkey_moves` caps `set_count` | none |
| Reset and the manual clock | `{"count": 20}` | `action,reset,step` |

Budget per seed: the runner's default steps and 3 minutes (`--minutes 3`), whichever ends first. Engine, `BagiStress`: `monkey_moves` drops a `set_count` above `MONKEY_MAX_COUNT` and adds a `reset` to each scene of `state.scenes` with a small count. A game's file has the same headings: its charters, start parameters and kinds, and what its `monkey_moves` does.

## Known deviations

What is not a bug. The module's `KNOWN_DEVIATIONS` holds the names; the reason for each is written here, never a name without one.

Engine, `BagiStress`: none today.

## Screenshots

A session's own pictures are `<out>/shots/<NN>-<label>.png` (`QaSession.shot(label)`: two frames after the last change, the manual clock, the long side 640). The skill looks at every one with the `Read` tool and says what it saw in the report; the pictures stay outside the repositories.

The optional set below is what `todo-loop` takes before / after pictures from (`tools/shots/CLAUDE.md`, "The screenshot set of a game"): the first fenced `json` block under this heading, with the keys `preset`, `targets`, `exe`, `command`, `scenes`, `sizes`, `defaultSize`. The engine has none: `python3 tools/shots/shots.py scenes` exits 77 here, and a slice of the engine takes no pictures. A game that has a set writes it as the block in the form (the game's command writes `{out}/{label}/<scene>.png`):

```jsonc
{
  "preset": "macos-debug",
  "targets": ["<Game>"],
  "exe": "<Game>",
  "command": ["{python}", "{root}/tools/<take-the-shots>.py", "--exe", "{exe}", "--label", "{label}", "--out", "{out}", "--scenes", "{scenes}", "--window", "{window}"],
  "scenes": {"<scene>": "what it shows"},
  "sizes": ["1920x1080"],
  "defaultSize": "1920x1080"
}
```

A game's block is fenced with `json` and no `c` after it: the reader takes the first `json` fence after the heading, so the form above is not one.
