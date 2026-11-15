# tools/qa

The library behind the skill `/qa` (`.claude/skills/qa/SKILL.md`): a charter script plays a game over the control API on the manual clock, the library checks every step, keeps what it covered and turns what it finds into reproductions and bug drafts. Standard library only, Python 3.10+, no MCP. It builds on `tools/control` (`GameSession`, `scenario.py`, `scenario_shrink.py`), which each file puts on `sys.path`; it never starts a window and never writes inside a git work tree.

| File | Purpose |
|------|---------|
| `bagi_qa_checks.py` | `Finding`, `Violation` and the generic checks as pure functions over control answers (no process, no I/O); the loader of a game's invariants module |
| `bagi_qa.py` | `QaSession`, the session a charter script drives |
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

`QaSession(out_dir, charter, invariants=None, ui_every=10)`: `out_dir` is given by the script and must be outside every git work tree (refused at construction: the recording and the pictures are never committed); `invariants` is a path or a loaded `Invariants`. `launch(exe | preset + game, extra=())` starts the game headless on the manual clock, records to `<out>/session.jsonl` (mode `minimal`), runs two frames and checks, `ui_tree` included. `finish()` quits, checks the log once more and returns the path of the recording; calling it twice is safe, and it works after a lost game.

| Method | What it does, what is checked after it |
|--------|----------------------------------------|
| `step(frames, dt)` | the state (version, `check_state`) and the log; the `ui_tree` only every `ui_every` steps (0: never), since it is the dearest check |
| `act(action, expect_refusal=False)` | `perform_action`; a listed action that is refused, the version of the answer, `check_action`, the state, the log. A refusal is an `ActOutcome`, never an exception; `expect_refusal` is for an action the charter invented. `list_actions` is asked before it when the version moved since the last list |
| `reset(**params)` | the game's `reset` (a refusal is a `ControlError`), then the state and the log |
| `state()`, `listed_actions()`, `check(ui=False)` | read and check; the `ui_tree` needs a frame since the last change, so `step` first |
| `shot(label)` | two frames, then `shots/<NN>-<label>.png` at `maxSize` 640 |
| `note(text)` | what the charter covered, for the report |

`calls` counts every control call the session made and is the number of the line in the recording, so a finding's `call_index` points at it: the `ui_tree` call for a ui finding, the `perform_action` call for a refused or a going-back version, the `get_state` call for a `check_state` finding, the last call so far for a log line. The same name and message again is not a new finding: it adds to `details["repeats"]` of the first. A `SessionError` is raised to the script and kept in `session_errors`. `bugs` and `known` split `findings` by the module's `KNOWN_DEVIATIONS`.
