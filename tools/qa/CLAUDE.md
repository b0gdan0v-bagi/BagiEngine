# tools/qa

The library behind the skill `/qa` (`.claude/skills/qa/SKILL.md`): a charter script plays a game over the control API on the manual clock, the library checks every step, keeps what it covered and turns what it finds into reproductions and bug drafts. Standard library only, Python 3.10+, no MCP. It builds on `tools/control` (`GameSession`, `scenario.py`, `scenario_shrink.py`), which each file puts on `sys.path`; it never starts a window and never writes inside a git work tree.

| File | Purpose |
|------|---------|
| `bagi_qa_checks.py` | `Finding`, `Violation` and the generic checks as pure functions over control answers (no process, no I/O); the loader of a game's invariants module |
| `bagi_qa.py` | `QaSession`, the session a charter script drives; the command line (`replay`, `shrink`, `report`, `drafts`) |
| `bagi_qa_repro.py` | replay of a recorded scenario with the checks, the cut of it (`scenario_shrink.shrink`), the written reproduction |
| `bagi_monkey_moves.py` | the monkey's candidate moves and the weighted `pick`, pure functions over control answers |
| `bagi_monkey.py` | the monkey runner: `run_monkey` and the command line (see "The monkey") |
| `bagi_monkey_log.py` | the seed log of the QA loop as markdown (`parse`, `render`, `add_run`, `next_seeds`, `next_charter`): pure text, no file or store |
| `bagi_monkey_dupes.py` | the signature of a reproduction (finding name, one string per call, last call) and `similar`: the rule of "already filed" |
| `bagi_monkey_loop.py` | the command line of the QA loop: `next-seeds`, `log-add`, `dupes`, `sweep` (see "The loop") |
| `bagi_qa_scene_file.py` | the saved `SceneConfig.xml` against the one before (`compare_scene_files`: the findings `editor.*`) and `ContentGuard`, which puts content files back after a session that may Save; pure files and bytes |
| `bagi_qa_editor.py` | the editor's moves over a `QaSession` ("The editor"); every label the editor shows is a constant there |
| `bagi_qa_report.py` | the report (`report.md`, `report.json`) and the bug drafts; never calls the todo store |
| `tests/` | pytest (`.venv/bin/python -m pytest tools/qa/tests`); `qa_fake_game.py` is the game of the session tests (knobs by environment variable, listed in its docstring) |

## Findings

`Finding {kind, name, message, call_index, details, expect, shots}`. `kind` is `log`, `ui_issue`, `version_back`, `action_refused` or `invariant`. `name` is the stable key a reproduction is matched by (`generic.version_back`, a game's `score_decreased`). `call_index` is the 1-based number of the recorded call after which it showed; the pure checks leave it 0 and the session sets it (`Finding.at`). `expect` is the scenario expectation (`scenario.py`) that would have caught it on that line: `None` when the format cannot say it (a version that went back: `versionStep` is exact, not a bound), `{}` when a plain line fails by itself (a call with no `error` expectation that the game refused).

## Generic checks

| Check | Finding name | Expectation |
|-------|--------------|-------------|
| `check_log(problem_lines, seen)`: log lines after the first `seen` of `GameSession.log_problems()` (the log only grows; the caller keeps `seen`) | `generic.log_<level>` | `logClean` |
| `check_ui_tree(tree)`: a canvas not `laidOut`, or with `issues` (one finding per canvas, the issue text in the message) | `generic.ui_not_laid_out`, `generic.ui_issues` | a `pointers` on the canvas |
| `check_version(previous, current)`: the version of `get_state`, `list_actions` and action answers never goes back (equal is fine) | `generic.version_back` | none |
| `check_action_accepted(action, listed, error_code)`: an action `list_actions` offered is never refused, with any code | `generic.action_refused` | `{}` |

The game's stdout is block-buffered, so a Warning may show only after `quit`: the session checks the log once more then and says so.

## A game's invariants module

A Python file whose path is in the game's `.claude/rules/qa-project.md`, loaded by `load_invariants(path)` by path, never through `sys.path` and without a byte-code cache. Every member is optional:

- `check_state(answer, previous) -> list[Violation]`: `answer` is a full `get_state` result `{game, version, state}`, `previous` the last one or `None`.
- `check_action(action, result, before, after) -> list[Violation]`: `result` is the answer of the action (`None` when refused), `before` / `after` the `get_state` answers around it.
- `monkey_moves(answer, moves) -> list[Move]`: the monkey's candidates (`answer` a full `get_state` result, `moves` the generic ones) with what is too slow dropped and the game's own added; `Move(kind, method, params, label)` is importable from `bagi_qa_checks`. `run_monkey_moves` gives back `moves` unchanged for a module that raises or returns anything but a list of `Move`, with the `qa.module_error` finding.
- `EDITOR_CONTENT_FILES`, `EDITOR_GENERATED` (scene to the nodes whose children a component makes), `EDITOR_ADDED_DEFAULTS` (component type to the attributes Save adds, with values).
- `KNOWN_DEVIATIONS`: a set of names that are not bugs. `apply_known(findings, known)` splits them off; the report keeps them under "known".

A `Violation` is `(name, message)` (a plain pair does too); `bagi_qa_checks.Violation` is importable from the module. A module that raises, or returns something else, is a finding of kind `invariant` named `qa.module_error` (the end of the traceback in `details`), not a crash of the session. A module that cannot be loaded at all (missing, a syntax error, a member of the wrong shape) is an `InvariantsError` before the session starts.

## The session

`QaSession(out_dir, charter, invariants=None, ui_every=10)`: `out_dir` is given by the script and must be outside every git work tree (refused at construction); `invariants` is a path or a loaded `Invariants`. `launch(exe | preset + game, extra=(), editor=False, content_files=None)` starts the game headless on the manual clock, records to `<out>/session.jsonl` (mode `minimal`), runs two frames and checks, `ui_tree` included. `editor=True` (or `--editor` in `extra`, which a replayed header gives, so replay and every `shrink` probe take this path too) starts the editor with `BAGI_EDITOR_DIR=<out>/editor-dir` for the start only and needs `status.runMode` `Editor`, else `NoEditorError` (a `SessionError`: skip, not a finding). The `content_files` (default: `EDITOR_CONTENT_FILES`, relative to the folder above the invariants module that holds `.claude/rules/qa-project.md`) are backed up before the start and put back by `finish()` (a lost or failed game too; `Finished.restored`); the executable must be of their checkout. `finish(content_writes_expected=False)` makes each restore the finding `editor.content_file_written`. `finish()` quits, checks the log once more, writes the report and the drafts and returns `Finished {scenario, report, report_json, drafts}` (paths); it can be called twice and works after a lost game.

| Method | What it does, what is checked after it |
|--------|----------------------------------------|
| `step(frames, dt)` | the state (version, `check_state`) and the log; the `ui_tree` only every `ui_every` steps (0: never), since it is the dearest check |
| `act(action, expect_refusal=False)` | `perform_action`; a listed action that is refused, the version of the answer, `check_action`, the state, the log. A refusal is an `ActOutcome`, never an exception; `expect_refusal` is for an action the charter invented. `list_actions` is asked before it when the version moved since the last list |
| `imgui_find`, `imgui_click`, `imgui_input`, `imgui_type`, `imgui_key` | the `GameSession` helpers, each control call recorded and counted, then the log check; a click on a label that is not there or is there twice is a `SessionError` (kept in `session_errors`), nothing is clicked |
| `reset(**params)` | the game's `reset` (a refusal is a `ControlError`), then the state and the log |
| `call(method, params)` | any other control call (`ui_click`, `set_window`, a method the game may refuse): a refusal is an `ActOutcome` with the error code, never an exception and never a finding; the version of the answer, then `check` (with the `ui_tree` after a `step`). `replay_call` is `act` for an action and this for the rest |
| `add_findings(found)`, `current_state()` | keep findings a script got from the checks' functions (`run_monkey_moves`); the newest `get_state` answer, with no call of its own once the session has one |
| `state()`, `listed_actions()`, `check(ui=False)` | read and check; the `ui_tree` needs a frame since the last change, so `step` first |
| `shot(label, max_size=640)` | two frames, then `shots/<NN>-<label>.png`; an editor charter asks for 1280 |
| `flag(name, message, details, expect)` | a finding of the script's own, for what no check sees (a HUD against the state): kind `invariant`, `details["flagged"]`; replay never shows it (see below) |
| `note(text)` | what the charter covered, for the report |

`calls` counts every control call and is the number of the line in the recording, so a finding's `call_index` points at it: the `ui_tree` call for a ui finding, the `perform_action` call for a refused or a going-back version, the `get_state` call for a `check_state` finding, the last call so far for a log line. The same name and message again is not a new finding: it adds to `details["repeats"]` of the first. A `SessionError` is raised to the script and kept in `session_errors`. `bugs` and `known` split `findings` by the module's `KNOWN_DEVIATIONS`.

## The editor

`bagi_qa_editor.py` is the engine editor's vocabulary, so a game's charter uses it as it is; `tools/stress/qa/editor_charter.py` is the worked charter (`.claude/rules/qa-project.md`, "The editor"). What the real editor taught: no scene or editor method is on the control API (a scene is opened by the game's `reset`, "reload" is a new launch); a header takes a press only after the pointer was over it in an earlier frame, so a click is a move first; the inspector names a number by text after a field labelled `##<index>`, so a number is found by component and index and set by Ctrl+click, select all, text, Enter; the default dock leaves the inspector clipped until `widen_left_dock`; "(unsaved changes)" is text, so `dirty` looks for the warning colour beside Save; a node under a collapsed one is reached through the hierarchy's search.

- **Findings** `editor.*`: `compare_scene_files(before, after, scene, edits, ignore_children_of, added_defaults)` (`bagi_qa_scene_file.py`; comments are cut out first, the shipped file has a `--` in one) names `saved_file_unreadable`, `saved_edit_missing`, `saved_scene_changed`, `saved_other_scene_changed`, `saved_root_changed`; the session adds `content_file_written`.
- **The guard** (`ContentGuard`) restores the content files in `finish()` whatever happened; a script that needs the saved bytes for a second launch writes them itself under a guard of its own.

## Reproducing a finding

`python3 tools/qa/bagi_qa.py replay|shrink --exe E [--invariants M] SESSION.jsonl` (`--preset` + `--game` instead of `--exe`; `--flag` adds a start flag; `shrink` also takes `--finding NAME`, `--out FILE` (default `repro/<finding name>.jsonl` next to the scenario) and `--max-probes N`). Exit codes: `replay` 0 nothing found (known deviations are printed and do not count), 1 findings, 2 a bad file or option, 77 no executable; `shrink` 0 nothing found, 1 the finding recurs and the file is written, 2 a bad file or option or a named finding that does not come back on the whole scenario (nothing written), 77.

- **Replay** (`replay_steps`): a fresh game from the header's flags (they must hold `--headless` and `--manual-clock`; the game must be the header's), every call sent as it was through `QaSession.replay_call` (no launch frames, no recording), the same checks after every call, the `ui_tree` after every `step`. The checks add read-only calls, which change nothing in the game. A refusal is tolerated, as in the lenient mode of `bagi_play`. A finding's `call_index` is the 1-based number of the scenario call it showed after (a late log line: the last call played).
- **Shrink** (`shrink_finding`): the calls after the one the finding showed at are dropped, then `scenario_shrink.shrink` cuts. A probe is a fresh game and the candidate calls; it fails when a finding with the same `name` appears. The last play of the result must show the finding again, else it is "not deterministic" (an error, nothing written).
- **The written file** (`write_reproduction` runs shrink, `settle`, `write_repro` and `check_written` in one call, for `shrink` and for scripts; ReproError and no file on a problem): the expectations of the recording are replaced by those of a run of the cut calls (a cut moves versions and refusals). The finding's `expect` goes on the last line: `logClean` for a log line, a `pointers` on the canvas for a ui finding (a `ui_tree` call is added when the cut dropped it), `{}` (the refused call fails by itself) for a listed action that was refused. A finding with `expect` None (`version_back`, a game's invariant) leaves the lines as the run gave them: the file passes under `bagi_play.py` and only `bagi_qa.py replay` reproduces it; the output says so. Before the command succeeds it plays the file with `bagi_play` and removes it when it does not fail on the last line.

A **flagged** finding is not replayed or shrunk: its draft names the recording and says so, until the script writes `repro/<finding name>.jsonl` (the calls to the state, the last one with the finding's `expect`) and `bagi_qa.py drafts` picks it up; `bagi_play.py` then fails on it.

**What a run costs** (Debug, Match3 at 8x8): about 500 control calls take 4 to 10 s; `shrink` of 476 calls to 5 took 50 probes and 25 s. A big state is the slow case (tens of seconds per call in BagiStress): a script keeps a time budget and stops any game process it started.

## Report and drafts

`finish()` writes into the out folder: `session.jsonl` (the recording), `report.md` (short, for a person: charter, game, counts, covered, shots, bugs, known deviations, session errors), `report.json` (the same; `bagi_qa.py report DIR` prints the markdown again), `shots/`, `bugs/<id>.md` (the entry body in the `todo-add` form: `**Where:**`, `**What is wrong:**`, `**Steps:**`, `**Reproduction:**`) and `bugs/<id>-repro.md` (a markdown plan: the commands and the scenario in a `jsonl` block, which `read_repro_markdown` reads back). The library only writes files; filing them is the skill's step.

- **One name, one bug:** findings with the same `name` make one draft (the first one is named, the count is in the body). The id is `bug-` + the words of the name and of the message, about 40 characters, unique in the run (`-2`, ...).
- **The reproduction in a draft** is `repro/<finding name>.jsonl` when `shrink` wrote it, else the recording up to the finding, which the text says is not cut down. After a `shrink`, `bagi_qa.py drafts DIR` writes the drafts again with the cut file.
- **Nothing private:** paths in the files are relative to the out folder; the out folder, the home folder and the login name are replaced (`scrub`), so a draft can be pasted into the store.

## The monkey

`python3 tools/qa/bagi_monkey.py (--exe E | --preset P --game G) --seed S [--steps N] [--minutes M] [--invariants MODULE] [--start JSON] [--kinds a,b] [--charter TEXT] [--out DIR] [--max-probes N] [--no-shrink] [--flag F]` is a seeded random walk on a `QaSession` (`ui_every=5`), so the generic checks, the recording, the report, `replay` and the shrinker are the session's. `run_monkey(...)` is the same as a function (`clock` is injected for the tests) and gives a `MonkeyResult` (`stopped` is `steps`, `minutes`, `bug` or `lost`; `repros`, `unshrunk`, `drafts`).

- **Moves** (`bagi_monkey_moves.candidate_moves`, sorted, a weight per kind): `action` (a listed action), `click` (`ui_click` on a named, visible, interactive, enabled element of a visible canvas; a slider gets `at` from a fixed grid), `step` (a frame count from a fixed set), `reset` (the `--start` parameters; only when listed) and `window` (`set_window` from a fixed matrix; only when listed). A click or a window move is followed by a `step`, since nothing changes until a frame; the `ui_tree` is read only when a frame has run since the last change.
- **The game's say**: `monkey_moves(answer, moves)` of the invariants module returns the list to pick from. The runner keeps of its list only the moves of the five kinds (`WEIGHTS`) that `--kinds` allows, sorted and without duplicates (so a list built from a set is still repeatable); a kind the module invents is dropped, and when nothing is left the walk steps one frame.
- **Determinism**: the walk depends only on the seed and the game's answers (sorted candidates, `rng.random()` and integers, never `choice`), so the same seed on the same build writes the same `session.jsonl`.
- **Stopping**: the budget (`--steps`, `--minutes`) is checked between moves; the walk ends at the first move after which `session.bugs` is not empty (also one the launch or `--start` showed). Then each of the first three distinct bug names gets `repro/<name>.jsonl` through `write_reproduction` (a bug that does not come back, or `--no-shrink`, is in `unshrunk` and its draft keeps the recording), and the drafts are written again.
- **A lost game** (`SessionError` on a move) is the finding `monkey.game_lost`, flagged like `QaSession.flag`: it is not shrunk, the draft names the recording.
- **Exit codes**: 0 the budget ended with no bug (known deviations do not count), 1 a bug, 2 a bad option, a start the game refused or a session folder inside a git work tree, 77 the executable is missing. The default `--out` is `~/.claude/plans/qa/<date>-monkey-s<seed>/`.

## The loop

`/qa-loop` (the skill moves the files to and from the store; `bagi_monkey_loop.py` never touches it) walks the monkey over seeds, charter after charter. The log is the store plan `qa-loop-log-<product>.md`: a title `# QA loop log: <product>`, free text, and one table `| date | charter | seeds | moves | build | result |` with a row per sweep (`a..b`, both walked). The next seeds of a charter go after the highest one any row of it covered; the next charter is the first with no row, else the one walked longest ago.

`python3 tools/qa/bagi_monkey_loop.py <command>`, exit 0 nothing found, 1 a bug (`dupes`: a match), 2 a bad option or file or a start the game refused, 77 no executable (`sweep`):

- `next-seeds --log F --charter T [--charter T ...] --count N` prints `{"charter", "first", "last"}`; a missing log is empty.
- `log-add --log F --product P (--sweep sweep.json | --charter T --seeds A..B --moves N) [--build] [--result] [--date]` appends a row (the result is `clean`, `bugs: <names>` or `error: <text>`); the file is written whole through a temp file and a rename.
- `dupes --repro F [--known F...]` (no known file: no match, exit 0) prints `DUPLICATE <known>: <finding>` per similar file and `SKIPPED <file>: <why>` for an unreadable one. The rule (`bagi_monkey_dupes.similar`): the same finding name, and most of the calls in common or the same last call; a file with no finding name never matches.
- `sweep (--exe E | --preset P --game G) --charter T --first S --count N` runs `run_monkey` for the seeds in order, seed `s` into `<out-root>/s<s>`, and writes and prints `<out-root>/sweep.json` (`charter`, `first`, `last`, `moves`, `stopped`, `seeds[]` with `bugs`, `known`, `repros`, `drafts`). `--minutes` is the total, checked between seeds and also the limit of the seed that runs; it ends on `count`, `minutes`, `bugs` (`--stop-after-bugs`, distinct finding names, default 3) or `error` (a game that does not start, exit 2: the seeds before it stay). An `--out-root` inside a git work tree is refused (exit 2) before anything is written.
