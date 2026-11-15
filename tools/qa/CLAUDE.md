# tools/qa

The library behind the skill `/qa` (`.claude/skills/qa/SKILL.md`): a charter script plays a game over the control API on the manual clock, the library checks every step, keeps what it covered and turns what it finds into reproductions and bug drafts. Standard library only, Python 3.10+, no MCP. It builds on `tools/control` (`GameSession`, `scenario.py`, `scenario_shrink.py`), which each file puts on `sys.path`; it never starts a window and never writes inside a git work tree.

| File | Purpose |
|------|---------|
| `bagi_qa_checks.py` | `Finding`, `Violation` and the generic checks as pure functions over control answers (no process, no I/O); the loader of a game's invariants module |
| `bagi_qa.py` | `QaSession`, the session a charter script drives; the command line (`replay`, `shrink`, `report`, `drafts`) |
| `bagi_qa_repro.py` | replay of a recorded scenario with the checks, the cut of it (`scenario_shrink.shrink`), the written reproduction |
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

The game's stdout is block-buffered into its log, so a Warning may show only after `quit`: a session checks the log once more after the game has quit and says so in the finding.

## A game's invariants module

A Python file whose path is in the game's `.claude/rules/qa-project.md`, loaded by `load_invariants(path)` by path, never through `sys.path` and without a byte-code cache. Every member is optional:

- `check_state(answer, previous) -> list[Violation]`: `answer` is a full `get_state` result `{game, version, state}`, `previous` the last one or `None`.
- `check_action(action, result, before, after) -> list[Violation]`: `result` is the answer of the action (`None` when refused), `before` / `after` the `get_state` answers around it.
- `KNOWN_DEVIATIONS`: a set of names that are not bugs. `apply_known(findings, known)` splits them off; the report keeps them under "known".

A `Violation` is `(name, message)` (a plain pair does too); `bagi_qa_checks.Violation` is importable from the module. A module that raises, or returns something else, is a finding of kind `invariant` named `qa.module_error` (the end of the traceback in `details`), not a crash of the session. A module that cannot be loaded at all (missing, a syntax error, a member of the wrong shape) is an `InvariantsError` before the session starts.

## The session

`QaSession(out_dir, charter, invariants=None, ui_every=10)`: `out_dir` is given by the script and must be outside every git work tree (refused at construction: the recording and the pictures are never committed); `invariants` is a path or a loaded `Invariants`. `launch(exe | preset + game, extra=())` starts the game headless on the manual clock, records to `<out>/session.jsonl` (mode `minimal`), runs two frames and checks, `ui_tree` included. `finish()` quits, checks the log once more, writes the report and the drafts and returns `Finished {scenario, report, report_json, drafts}` (paths); calling it twice gives the same result, and it works after a lost game.

| Method | What it does, what is checked after it |
|--------|----------------------------------------|
| `step(frames, dt)` | the state (version, `check_state`) and the log; the `ui_tree` only every `ui_every` steps (0: never), since it is the dearest check |
| `act(action, expect_refusal=False)` | `perform_action`; a listed action that is refused, the version of the answer, `check_action`, the state, the log. A refusal is an `ActOutcome`, never an exception; `expect_refusal` is for an action the charter invented. `list_actions` is asked before it when the version moved since the last list |
| `reset(**params)` | the game's `reset` (a refusal is a `ControlError`), then the state and the log |
| `state()`, `listed_actions()`, `check(ui=False)` | read and check; the `ui_tree` needs a frame since the last change, so `step` first |
| `shot(label)` | two frames, then `shots/<NN>-<label>.png` at `maxSize` 640 |
| `note(text)` | what the charter covered, for the report |

`calls` counts every control call the session made and is the number of the line in the recording, so a finding's `call_index` points at it: the `ui_tree` call for a ui finding, the `perform_action` call for a refused or a going-back version, the `get_state` call for a `check_state` finding, the last call so far for a log line. The same name and message again is not a new finding: it adds to `details["repeats"]` of the first. A `SessionError` is raised to the script and kept in `session_errors`. `bugs` and `known` split `findings` by the module's `KNOWN_DEVIATIONS`.

## Reproducing a finding

`python3 tools/qa/bagi_qa.py replay|shrink --exe E [--invariants M] SESSION.jsonl` (`--preset` + `--game` instead of `--exe`; `--flag` adds a start flag; `shrink` also takes `--finding NAME`, `--out FILE` (default `repro/<finding name>.jsonl` next to the scenario) and `--max-probes N`). Exit codes: `replay` 0 nothing found (known deviations are printed and do not count), 1 findings, 2 a bad file or option, 77 no executable; `shrink` 0 nothing found, 1 the finding recurs and the file is written, 2 a bad file or option or a named finding that does not come back on the whole scenario (nothing written), 77.

- **Replay** (`replay_steps`): a fresh game from the header's flags (they must hold `--headless` and `--manual-clock`; the game must be the header's), every call sent as it was through `QaSession.replay_call` (no launch frames, no recording), the same checks after every call, the `ui_tree` after every `step`. The checks add read-only calls, which change nothing in the game. A refusal is tolerated, as in the lenient mode of `bagi_play`. A finding's `call_index` is the 1-based number of the scenario call it showed after (a late log line: the last call played).
- **Shrink** (`shrink_finding`): the calls after the one the finding showed at are dropped, then `scenario_shrink.shrink` cuts. A probe is a fresh game and the candidate calls; it fails when a finding with the same `name` appears. The last play of the result must show the finding again, else it is "not deterministic" (an error, nothing written).
- **The written file** (`settle`, `write_repro`, `check_written`): the expectations of the recording are replaced by those of a run of the cut calls (a cut moves versions and refusals, so the recorded ones fail on lines that have nothing to do with the finding). The finding's `expect` goes on the last line: `logClean` for a log line, a `pointers` on the canvas for a ui finding (a `ui_tree` call is added at the end when the cut dropped it), `{}` (no expectation, so the refused call fails by itself) for a listed action that was refused. A finding with `expect` None (`version_back`, a game's invariant) leaves the lines as the run gave them: the file passes under `bagi_play.py` and only `bagi_qa.py replay` reproduces it; the output says so. Before the command succeeds it plays the file with `bagi_play` and removes it when it does not fail on the last line.

## Report and drafts

`finish()` writes into the out folder: `session.jsonl` (the recording), `report.md` (short, for a person: charter, game, counts, what was covered, shots, bugs, known deviations, session errors), `report.json` (the same, `bagi_qa.py report DIR` prints the markdown from it again), `shots/`, `bugs/<id>.md` (the entry body in the `todo-add` form: `**Where:**`, `**What is wrong:**`, `**Steps:**`, `**Reproduction:**`) and `bugs/<id>-repro.md` (a markdown plan: the commands and the scenario in a `jsonl` block, which `read_repro_markdown` reads back). The library only writes files; filing them is the skill's step.

- **One name, one bug:** findings with the same `name` make one draft (the first one is named, the count is in the body). The id is `bug-` + the words of the name and the first letters-only words of the message, at most about 40 characters, unique in the run (`-2`, ...).
- **The reproduction in a draft** is `repro/<finding name>.jsonl` when `shrink` wrote it, else the recording up to the finding, which the text says is not cut down. After a `shrink`, `bagi_qa.py drafts DIR` writes the drafts again with the cut file.
- **Nothing private:** paths in the files are relative to the out folder; the out folder, the home folder and the login name are replaced (`scrub`), so a draft can be pasted into the store.
