# Control Tools Guidelines

## Overview

Python tools that drive a game through the control API (`src/Modules/RemoteControl/CLAUDE.md`, protocol in `src/Modules/BECore/Control/CLAUDE.md`). They are separate processes: the engine has no MCP or Python dependency. Python 3.10+. Nothing here names a concrete game apart from Match3 in examples; a game with its own tools (the Stockfish bridge of BagiEngine-Chess) keeps them in its own repository and imports the client from here.

| File | Purpose |
|------|---------|
| `bagi_control_client.py` | WebSocket (RFC 6455) client with the standard library only: `BagiControlClient.call(method, params)`, pushed events in `events`, `ControlError` for an error response |
| `game_process.py` | `GameProcess`: starts a game executable with `--control`, retries until it listens, keeps its log; `build_game_command`, `free_port`, `preset_executable` |
| `scenario.py` | The scenario file ("Scenario files"): `load` / `dump`, the expectations (`evaluate`), what a recording writes (`minimal_expect`, `full_expect`) and `--update` writes (`updated_expect`). Standard library only, no I/O besides the two |
| `bagi_session.py` | `GameSession`: one game, started (`launch`) or reached (`connect`), as plain Python calls. No MCP code, so it is tested without the `mcp` package. `SessionError` for what the session refuses; a refusal of the game stays a `ControlError` |
| `bagi_mcp.py` | The MCP server (stdio). Each tool is one line over `GameSession`; the only file that needs `mcp` |
| `requirements.txt` | `mcp` (2.x) and `pytest`; nothing else is needed |
| `tests/` | pytest, plus `smoke.py` (the ctest entries `Bagi<Game>ControlSmoke`, `.claude/rules/cmake-rules.md`) |

## Running

Use the repository venv (`CI/setup_venv.*` makes `.venv`; on Windows `.venv\Scripts\python`, made by `CI\setup_venv.bat`, which needs `uv`), then once `.venv/bin/pip install -r tools/control/requirements.txt` (Windows: `uv pip install --python .venv\Scripts\python.exe -r tools/control/requirements.txt`).

```bash
.venv/bin/python -m pytest tools/control/tests                # the tests
```

## Scenario files

A session recorded as JSON lines (UTF-8, one object per line, no comments, blank lines skipped) that a player replays as a test. `scenario.py` holds the format; a file that breaks it is a `ScenarioError` naming file and line.

- **Line 1, the header:** `{"scenario": 1, "game": "Match3", "flags": ["--headless", "--manual-clock"], "title": "..."}`. `scenario` is the format version (a tool refuses a number it does not know and says so); `game` is the active game's name (`list_games.active`, checked after the start); `flags` are the start flags besides `--control` / `--control-port` (`--headless` and `--manual-clock` map onto the launch options, any other goes to the game as it is, so a flag of a later entry fits); `title` is free text.
- **Every other line is one control call:** `{"method": "perform_action", "params": {...}, "expect": {...}}`, sent in order on one connection. `params` and `expect` are optional, `note` (free text) is ignored.
- **`expect`**, every member optional, all must hold, checked in this order:
  - `error`: the error code the call must answer. An error without this member fails the line (the shrinker's `lenient` mode tolerates it); an `error` that does not happen fails too.
  - `result`: the whole answer equals the value. `equal`: each member of the answer (top level) equals the value. `pointers`: JSON pointer (RFC 6901, `/state/score`, `/a/0/b`) to value; a missing path is its own "absent", not `null`. Equality is JSON's: `true` is not `1`.
  - `versionStep`: `answer.version` minus the last `version` seen in an earlier answer of the run (any object result with an integer `version`). Relative on purpose: a scenario does not break when a counter starts elsewhere. Not checked when no version was seen before.
  - `event`: `{"name": "state_changed", "equal": {"version": 5}, "timeout": 5}`: a push of that name whose `data` has these members arrives (already received, or within `timeout` seconds, default 5); the matching push is consumed.
  - `logClean`: `true`: no line of the game's log since its start has the level Warning, Error or Fatal.
- **What is written.** A recording (mode `minimal`, the default) writes `error` and `versionStep` only, so a scenario survives a change of the game's data and fails on what the API promises. Mode `full` adds `result`; the picture of a `screenshot` is never written, its `width` and `height` go to `equal` instead. `equal`, `pointers`, `event` and `logClean` are added by hand, never by default. `--update` of the player rewrites the values of `error`, `result`, `equal`, `pointers` and `versionStep` that a line has, drops what no longer applies, adds the minimal ones to a line with none of the five, and keeps `event` and `logClean`.
- `dump` writes the stable form (members in a fixed order, `ensure_ascii=False`), so a loaded and dumped file does not change and a diff shows the real change.

## Naming the game

A caller gives the executable as a path, or as a preset and a game name: `preset_executable(preset, game, root)` is `<root>/build/<preset>/bin/<game>[.exe]` (a multi-config preset: the newest `bin/<Config>/`; `root` defaults to the repository holding these tools, so a game that carries the engine passes its own). `GameSession.launch` and `launch_game` take `exe` or `preset` + `game`, exactly one of the two; `smoke.py` takes `--exe` or `--preset` + `--game` (the ctest entries pass `--exe $<TARGET_FILE:...>`). The tools never hard-code a build folder: the layout belongs to `cmake/BagiEngine.cmake`, and a path in a docstring is a placeholder or goes through `preset_executable`.

## The smoke test and the free port

`tests/smoke.py` (`run_smoke`, `play`) starts the game on a port from `free_port()`, which binds port 0, closes the probe and hands the number over, so another process may take the port before the game binds it (the unit tests of a parallel `ctest` bind port 0 too). `play_with_retry` covers that window: when the game fails to bring the API up and its log tail holds `cannot listen on 127.0.0.1:` (what `Application::StartControlServer` logs; `PortTaken`), the scenario is played once more on a new port and the retry is announced on stdout. Any other failure is not retried, and neither is a port given with `--port`. A port held by a live listener costs about 30 s (the startup timeout) before the retry. A game's own `SCRIPT` that calls `run_smoke` gets this for free. In ctest the entry takes the `bagi_control_smoke` lock, so two smoke tests of one run do not start together (`.claude/rules/cmake-rules.md`).

## The MCP server

`claude mcp add bagi -- .venv/bin/python tools/control/bagi_mcp.py` (Windows: `.venv\Scripts\python`). The repository has no `.mcp.json`: the interpreter path differs per OS and machine, and a project file makes every contributor approve a server they may not want. Tools: `launch_game`, `connect`, `status`, `get_state`, `list_actions`, `perform_action`, `set_state`, `reset`, `step`, `screenshot` (an image), `ui_tree`, `ui_click`, `frame_stats`, `profiler_dump`, `zone_allocs` (the profiler's methods, `src/Modules/BECore/Profiler/CLAUDE.md`), `game_log`, `quit`. `launch_game` starts the game `--headless --manual-clock`, so a screenshot works and nothing moves until `step`. `ui_tree` reports the `box` of an element that has a box style (rounded, border, gradient, shadow; `.claude/rules/ui-shapes.md`), a button's `action`, `disabled`, `value` and `selected`, a choice group's (`UISegmented`) `bind` and `selectedValue`, a dropdown's `bind`, `selectedValue`, `open`, `action` and `disabled` (its options are its children, hidden while the list is closed), a toggle's `bind`, `onValue`, `offValue`, `checked`, `action` and `disabled`, a progress bar's `bind`, `minValue`, `maxValue` and `fraction`, and a slider's `bind`, `minValue`, `maxValue`, `step`, `value` (a number), `fraction`, `action` and `disabled` (all as of the call). `ui_click` answers with the `action` and `value` it emitted (a toggle: the value it asks for; a slider: the value of the press, "" when nothing was emitted) and takes an optional `at` (a number from 0 to 1, the horizontal position inside the element, default 0.5): that is how a slider is set, `at` 0 for its minimum and 1 for its maximum, one click and no drag. A click on a dropdown's header answers with no `action` (it opens the list, so `ui_click`, `step`, `ui_tree`, then click an option: the answer holds the dropdown's `action` and the option's `value`). A click on a segment, a toggle or a slider changes nothing itself (the game sets the variable), so it is checked with `ui_tree`, `ui_click`, `step`, `ui_tree`; a progress bar cannot be clicked.

- Written for `mcp` 2.x (`MCPServer`, `ToolError`); 1.x had `FastMCP` and is not supported.
- **Every tool is wrapped in `reports_errors`.** The server shows the model only the message of a `ToolError`; any other exception becomes "Error executing tool X" and the model would never see `illegal_action` or `promotion_required`. A new tool needs the decorator; `test_dispatch.py` checks this over a real stdio connection.
- The game's stdout goes to a log file, never to the server's: stdout is the MCP protocol.
- `GameSession.call` drops the connection on a timeout, because the late answer would be taken for the answer of the next call.

## Tests without the real things

`tests/fake_control_server.py` (a WebSocket server answering from a Python function), `fake_game.py` (a game executable that serves it). The `fake_game_exe` fixture of `test_dispatch.py` makes the executable: a script with a shebang, and on Windows a `FakeGame.bat` that runs `fake_game.py` with `%*`, so no test is skipped there. Killing the `.bat` (`GameProcess.stop`) ends `cmd.exe` and not the Python process behind it, so `fake_game.py` ends by itself after `WATCHDOG_SECONDS` (60 s) if nobody sent `quit`. A game that is gone can show as a reset connection (`WinError 10053` / `10054`) as well as a close frame; `GameSession.call` reports both as "the game closed the connection". New tool code gets a test against these before it is tried on the real game.
