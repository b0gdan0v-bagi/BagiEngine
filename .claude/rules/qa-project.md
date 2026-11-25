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
| Editor content files | what the editor's Save writes: `QaSession` backs these files up before an editor launch and puts them back after it (`tools/qa/CLAUDE.md`, "The session"); `none` for a game with no editor charter | the member `EDITOR_CONTENT_FILES` of `<invariants>`: `tools/stress/config/SceneConfig.xml` |
| Editor charter | the command `/qa-loop` runs once per run, after the monkey sweeps; its exit codes are the sweep's (0 clean, 1 a bug, 2 a bad option, 77 skipped); `none` for a game with no editor charter | `python3 tools/stress/qa/editor_charter.py --exe <exe> --out <out> --charter all` |

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
| RmlUi scene: documents, reload and wheel | only in a build with `ENGINE_UI_RMLUI` (`macos-rmlui-debug`, where `get_state.scenes` lists `rmlui`); `reset` to `rmlui` at a small and at a large count (a few hundred documents), `set_count` up and down, `set_language` both ways, `call_method` `ReloadRmlUiDocuments`, `wheel_at` over the panel (`x` 1700, `y` 920) on both axes and with both `flipped`, a `step` of a few dozen frames after each wheel, screenshots, the counters `Stress rmlui ...` of `frame_stats`. The scene makes many contexts and documents; it does not make hundreds of textures (the glyph pages are shared by all contexts), so it does not exercise the growth of the Vulkan descriptor pool |
| The editor: edit, save and reload a scene | `editor_charter.py --charter edit`: in a headless editor (`--editor`, no window) select the node `Run` of `StressEntities`, set a number, the layer and the name in the inspector, see the unsaved-changes marker, Save, see it go; the saved `SceneConfig.xml` differs from the one before in those edits only (the findings `editor.*`); a second start on the saved file shows the renamed node |
| The editor: panels and chords, no save | `editor_charter.py --charter panels`: open every panel of the View menu, close and open the Game panel by its chord, open the command palette, filter it, close it, show the `ui` scene; a picture of each; the content file is never written (`editor.content_file_written`) and the log stays clean |

## Monkey

`tools/qa/bagi_monkey.py` (`tools/qa/CLAUDE.md`, "The monkey") walks the game at random from a seed and stops at the first bug. What it reads from the game: `list_actions` and `ui_tree` for the generic moves, `list_methods` (the `window` moves only when `set_window` is listed, `click` only with `ui_click`, `reset` only with `reset`) and the optional `monkey_moves(answer, moves)` of `<invariants>`, which drops what is too slow and adds the game's own moves. The charter text of a run is the one of the table; `--start` and `--kinds` are its columns.

| Charter | Start | Kinds |
|---------|-------|-------|
| Every scene and every language switch | `{"scene": "sprites", "count": 20}` | all |
| A count sweep up to the maximum | not a monkey charter: a walk cannot keep a big count and a Debug `ui` scene at it in a minute, so `monkey_moves` caps `set_count` | none |
| Reset and the manual clock | `{"count": 20}` | `action,reset,step` |
| RmlUi scene: documents, reload and wheel | not a monkey charter: the monkey reaches the scene through the first charter's scene list (`reset` adds `rmlui` to it in that build) and finds its moves there | none |
| The editor: edit, save and reload a scene | not a monkey charter: the walk knows `list_actions` and `ui_tree`, not ImGui; `/qa-loop` runs the field `Editor charter` once per run | none |
| The editor: panels and chords, no save | not a monkey charter, as above | none |

Budget per seed: the runner's default steps and 3 minutes (`--minutes 3`), whichever ends first. Engine, `BagiStress`: `monkey_moves` drops a `set_count` above `MONKEY_MAX_COUNT` and adds a `reset` to each scene of `state.scenes` with a small count; on the scene `rmlui` it also adds a `wheel_at` over the panel (both axes, `flipped`) and the `call_method` `ReloadRmlUiDocuments`, as kind `click` (the runner follows a `click` with a `step`). The cap needs no lower value for that scene (measured in `invariants.py`). A game's file has the same headings: its charters, start parameters and kinds, and what its `monkey_moves` does.

## Known deviations

What is not a bug. The module's `KNOWN_DEVIATIONS` holds the names; the reason for each is written here, never a name without one.

Engine, `BagiStress`: none today. These are not names but properties the editor charters meet, so they are no findings:

- The `ui` scene in the editor: its `Screen` canvas takes the shape of the viewport panel (not the stress grid's view size), while the stress grid fills a view of its own size, so its labels are `OutsideCanvas` there. The panels charter shows the scene and leaves its `ui_tree` unchecked; a name in `KNOWN_DEVIATIONS` would hide the same finding in Game mode.
- A saved stress scene holds the nodes its component made (`EDITOR_GENERATED`) and the defaults the writer adds to a transform (`EDITOR_ADDED_DEFAULTS`); the second start then shows the generated nodes twice (the file's and the component's).

## The editor

How `/qa` and `/qa-loop` use the editor (`tools/qa/CLAUDE.md`, "The session"; the moves are `tools/qa/bagi_qa_editor.py`, which names every label the editor shows):

- **No scene or editor method exists on the control API.** A scene is opened by the game's `reset`, "reload" is a fresh launch, and Save is a click on the Save button; the dirty marker is read from the picture, since "(unsaved changes)" is text, not an item.
- **Save writes the checkout's content file** (the content roots are compiled into the executable, so a copy cannot be edited instead): the executable must be this checkout's, the library puts the file back after every session (also a lost game, `replay` and every `shrink` probe), and `git status --short` is checked at the end of the run.
- **A saved file holds more than the edits** (the live tree, the writer's defaults, a comment with `--` rewritten): `compare_scene_files` takes the game's allow lists, never a loose comparison.
- **The screenshot set takes the editor's panels** (`tools/stress/qa/editor_shots.py`, "Screenshots"): a scene is the crop of one panel's window at the one default layout (1920x1080), in the state of the edit charter (the node `Run` selected, `TransformComponent` open). It never clicks Save, and the session still guards the content file. What made two runs differ and is kept out of every picture: the status bar (a free control port each run, the frame rate), the pointer inside the window (hover colour, tooltip), the editor's folder of an earlier run (`imgui.ini`; the session's is a temporary one); a panel that draws a clock or a counter (Profiler, Console, Game) is not a scene.
- **A build without the editor** answers `status.runMode` Game: the charter is skipped (exit 77), not a finding.
- **A reproduction with `--editor` in its header never goes into the `Scenario folder`**: `be_add_game_scenario_tests` runs every `*.jsonl` there through `bagi_play.py`, which starts the game with the header's flags, so `--editor` reaches the game and `bagi_play.py` has no guard for the content file: one that clicks Save would write the checkout. The pytest `test_no_scenario_of_the_stress_folder_starts_the_editor` of `tools/qa/tests/test_stress_editor.py` fails for such a file. The regression test of a fix is a gtest (`SceneEditorSaveTest` is the pattern) and the plan `bug-<short>-repro.md` keeps the scenario and the command of the charter.

## Screenshots

A session's own pictures are `<out>/shots/<NN>-<label>.png` (`QaSession.shot(label)`: two frames after the last change, the manual clock, the long side 640). The skill looks at every one with the `Read` tool and says what it saw in the report; the pictures stay outside the repositories.

The optional set below is what `todo-loop` takes before / after pictures from (`tools/shots/CLAUDE.md`, "The screenshot set of a game"): the first fenced `json` block under this heading, with the keys `preset`, `targets`, `exe`, `command`, `scenes`, `sizes`, `defaultSize`. The engine's set is the editor's panels, taken by `tools/stress/qa/editor_shots.py` (each scene is the crop of one panel at the default layout, in the state of the edit charter; `editor-frame` is the whole window); a slice that changes how the editor looks writes `**Shots:**` with them, a slice of a game's own look has none and takes no pictures:

```json
{
  "preset": "macos-debug",
  "targets": ["BagiStress"],
  "exe": "BagiStress",
  "command": ["{python}", "{root}/tools/stress/qa/editor_shots.py", "--exe", "{exe}", "--label", "{label}", "--out", "{out}", "--scenes", "{scenes}", "--window", "{window}"],
  "scenes": {
    "editor-frame": "the editor window at the default layout: Scene Editor with the node Run selected, TransformComponent open",
    "editor-hierarchy": "the Scene Editor's hierarchy of StressEntities",
    "editor-inspector": "the Scene Editor's inspector of the node Run: TransformComponent open",
    "editor-viewport": "the Viewport panel showing the sprites scene at a small count"
  },
  "sizes": ["1920x1080"],
  "defaultSize": "1920x1080"
}
```

A game that has a set writes it as the block in the form (the game's command writes `{out}/{label}/<scene>.png`):

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
