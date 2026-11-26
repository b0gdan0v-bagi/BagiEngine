# Control Tools Guidelines

## Overview

Python 3.10+ tools that drive a game through the control API (`src/Modules/RemoteControl/CLAUDE.md`; protocol: `src/Modules/BECore/Control/CLAUDE.md`), as separate processes: the engine has no MCP or Python dependency. Nothing here names a game but `BagiStress`; a game's own tools (the Stockfish bridge of BagiEngine-Chess) live in its repository and import the client from here.

| File | Purpose |
|------|---------|
| `bagi_control_client.py` | Standard-library WebSocket client: `BagiControlClient.call(method, params)`, pushes in `events`, `ControlError` for an error answer |
| `game_process.py` | `GameProcess` starts an executable with `--control`, retries until it listens, keeps its log; `build_game_command`, `free_port`, `preset_executable`, `log_problems` (Warning and above), `PORT_TAKEN_MARKER` |
| `scenario.py` | The scenario format: `load` / `dump`, `evaluate`, `minimal_expect` / `full_expect` / `updated_expect`. No other I/O |
| `bagi_session.py` | `GameSession`: one game, started (`launch`) or reached (`connect`), as plain calls; records scenarios, keeps pushes (`take_event`). No MCP code. `SessionError` is the session's refusal; the game's stays a `ControlError` |
| `scenario_shrink.py` | `shrink(items, fails, max_probes)`: delta debugging to the 1-minimal failing sublist. Pure |
| `bagi_play.py` | The scenario player: `play(exe, header, steps, update, lenient)` and the command line |
| `bagi_reference.py` | `check_reference`: the `reference` expectation of a screenshot; `default_failures_dir` |
| `bagi_backends.py` | The backend comparison (`PROFILES`, `capture_profile`) |
| `bagi_image.py` | `run_image_diff` (the `image_diff` tool without MCP), `ImageError`; the comparison is `tools/shots/png_diff.py` |
| `bagi_mcp.py` | The MCP server (stdio), one line per tool over `GameSession`; the only file that needs `mcp` |
| `bagi_bridge.py` | The bridge for a game in a browser page: the page dials it, and while the page is linked it offers the control endpoint; `Bridge` runs it in a thread |
| `tests/` | pytest and `smoke.py`, the game-neutral smoke (common methods, screenshot, UI tree, `set_window`) behind `be_add_game_control_tests`; a game with rules writes its own `SCRIPT` calling `run_smoke()`. `BagiStress`: `tools/stress/tests/stress_smoke.py` |

**A game in a browser page** cannot listen, so it dials `bagi_bridge.py`, and the tools here connect to the bridge as to a desktop game.
The link protocol, its security rules and the command line: `.claude/rules/control-web-link.md`; its tests play the page with `tests/fake_page.py`.

## Running

The repository venv (`CI/setup_venv.*`; Windows `.venv\Scripts\python`, needs `uv`), once `.venv/bin/pip install -r tools/control/requirements.txt` (`mcp` 2.x, `pytest`). Tests: `.venv/bin/python -m pytest tools/control/tests`.

## Scenario files

A session as JSON lines (UTF-8, blank lines skipped) replayed as a test; a broken file is a `ScenarioError` naming file and line.

- **Header (line 1):** `{"scenario": 1, "game": "Stress", "flags": [...], "title": "..."}`. An unknown format version is refused; `game` is checked against `list_games.active`; `flags` are the start flags besides `--control*` (`--headless` / `--manual-clock` map onto launch options, the rest go to the game as they are).
- **Other lines:** one call each, `{"method", "params"?, "expect"?, "note"?}`, sent in order on one connection.
- **`expect`** (all members optional, all must hold):
  - `error`: the code the call must answer. An unexpected error fails the line (except in `lenient` mode), so does a missing one.
  - `result` (the whole answer), `equal` (top-level members), `pointers` (RFC 6901 pointer to value; a missing path is "absent", not `null`). JSON equality: `true` is not `1`.
  - `reference` (screenshot only): `{"png", "threshold"?, "maxShare"?, "region"?}`, `png` relative to the scenario; tolerance as in `tools/shots/CLAUDE.md`, "Comparing two pictures". Another size fails: nothing is scaled silently.
  - `versionStep`: `version` minus the last `version` seen earlier in the run, relative so a counter starting elsewhere breaks nothing.
  - `event`: `{"name", "equal"?, "timeout"? (5)}`: a matching push arrives; it is consumed.
  - `logClean: true`: no Warning or above in the game's log since start.
- **Written by a recording:** mode `minimal` (default) only `error` and `versionStep`, so a scenario survives data changes and fails on what the API promises; `full` adds `result` (a screenshot gets `width` / `height` in `equal`, never the picture). `equal`, `pointers`, `reference`, `event`, `logClean` are written by hand. `--update` rewrites `error`, `result`, `equal`, `pointers`, `versionStep`, and keeps `reference`, `event`, `logClean`.
- **Recording** (`launch(record=)`, `connect(record=)`, `start_recording` / `stop_recording`) hooks `GameSession.call` alone: an answered or refused call is a line; not the session's own `list_games` / `status`, a `SessionError` or a trailing `quit`. `connect` records `--manual-clock` from `status`, never `--headless`. The file is written at `stop_recording`, `quit` or `close`, so a crash keeps it. A recording that begins late or on a connected game says so: a scenario replays from a fresh start.
- Pushes are kept in a bounded deque (oldest dropped). `dump` writes a stable form, so a round trip changes nothing.

### Playing a scenario

`bagi_play.py <exe> <scenario.jsonl>` (or `--preset P --game G`) stops at the first failing line and prints `FAILED <file>:<line>  <method> <params>` (params cut at `PARAMS_SHOWN`), the expectation and the actual value; a pass prints `PASSED <file>: N calls`. Exit codes: 0 passed; 1 failed (also a game that did not start, was lost or exited non-zero); 2 a bad file or argument (judged first, so a broken scenario fails where the game is not built); 77 no executable (ctest: skipped). A taken port is retried once.

- A `step` gets the timeout of its frames.
- **`logClean` is judged after `quit`**: redirected stdout is block-buffered, so a Warning may not be in the file at its line. Put it on the last line.
- **`--update`** plays everything, rewrites the expectations and writes the file only after a complete run and prints the changed lines; `event` is not waited for; it also writes missing or changed reference PNGs (threshold 0). A reference it cannot write fails the run.
- **Reference pictures:** `references/` next to the scenario, committed, small (fixed `maxSize`, long side about 400, manual clock, after a `step`). A failure writes `<name>.actual.png` and `<name>.diff.png` into `<temp>/bagi-reference/<scenario>/` (`--failures DIR`, never inside a git work tree) and names both in the failure. A change on purpose is `--update`, reviewed like code. Example: `tools/stress/tests/scenarios/ui_reference.jsonl`.

`be_add_game_scenario_tests(<ExeTarget> DIR <folder>)` makes every `*.jsonl` of the folder a ctest entry `<ExeTarget>Scenario.<file>`; a game adds its own folder in its CMakeLists, and a new file needs no CMake edit.

### Shrinking

`bagi_play.py <exe> <scenario> --shrink OUT` cuts a failing scenario to the calls the failure needs (exit 2 if it passes). Each probe plays a candidate list with every `expect` stripped but the failing line's, in `lenient` mode, since dropped calls move the versions and answers of later ones; the target line is strict (`strict_step`), because an error there is another failure. A probe fails when that line fails with the same expectation name. `--max-probes` bounds launches; at the limit the best list so far is written. The output keeps the header and the surviving calls with their original `expect`; `shrink` tries chunks of halving size, then single calls.

## Comparing backends

`bagi_backends.py <exe> <scenario> --out DIR [--backends software,window] [--threshold N] [--max-share X]` plays the calls on each profile of `PROFILES` without expectations, keeps every screenshot in `DIR/<profile>/` and diffs each with the first available profile. `software` (`--headless`) always runs; `window` uses the config's renderer and is "not available" under Vulkan, which has no capture; `vulkan` and `browser` are never started. Informational: exit 0 whatever differs, 1 only when `--max-share` is exceeded, 2 bad input, 77 no executable. `--out` is refused inside a git work tree.

## By-name methods and naming the game

`get_property`, `set_property`, `call_method`, `list_types`, `describe_type`, `schema` (`Reflection/CLAUDE.md`, "Reflection methods") have no MCP tool: use `client.call(...)`. A game is a path or a preset and a name: `preset_executable(preset, game, root)` is `<root>/build/<preset>/bin/<game>[.exe]` (newest `bin/<Config>/` for a multi-config preset); every entry point takes exactly one of the two forms.

## The smoke test and the free port

`free_port()` binds port 0 and releases it, so another process may take the number first. `play_with_retry` replays once on a new port when the log holds `cannot listen on 127.0.0.1:` (`PortTaken`); other failures and a `--port` given by hand are not retried. In ctest the `bagi_control_smoke` lock keeps two smoke tests apart (`.claude/rules/cmake-rules.md`).

## The MCP server

`claude mcp add bagi -- .venv/bin/python tools/control/bagi_mcp.py`; no `.mcp.json`, the interpreter path is per machine. The tools mirror `GameSession` and the API: `launch_game`, `connect`, `status`, `get_state`, `list_actions`, `perform_action`, `set_state`, `reset`, `step`, `screenshot`, `image_diff`, `ui_tree`, `ui_click`, `click_at`, `imgui_find`, `imgui_click`, `imgui_type`, `imgui_key`, `set_window`, `frame_stats`, `profiler_dump`, `zone_allocs`, `log_mark`, `log_since`, `log_subscribe`, `batch`, `run_until`, `editor_state`, `game_log`, `record_start`, `record_stop`, `quit`. `launch_game` starts `--headless --manual-clock` (screenshots work, nothing moves until `step`); `window` / `density` set its start window, kept in a recording's header; `editor=True` adds `--editor` (also kept in the header, so a scenario replays in the editor), and a screenshot is then the whole editor window (`status.runMode` says which mode runs; a build without the editor runs Game); `content_overlay=<dir>` adds `--content-overlay=` (not kept in a recording's header; a folder inside a git work tree is a `SessionError`) and `editor_state` reads the unsaved changes (`.claude/rules/control-editor-api.md`; outside the editor a `SessionError` says to launch with `editor=True`); `launch_game` and `connect` take `record=`. Contracts: `.claude/rules/ui-control-api.md`, `control-qa-api.md`, `control-window-api.md`.

- **`image_diff(a, b, out?, threshold?, region?)`:** an operand is a PNG or `screen` (the game's picture now, `step` first). Answers stats and a preview map of at most `PREVIEW_SIZE`; `out` writes the full map outside every git work tree. An `ImageError` reaches the model like a refusal.
- **ImGui (editor only):** `GameSession.imgui_find` / `imgui_input` and the helpers `imgui_click` (by label, in a `window`: zero or several visible matches are a `SessionError` listing them; or a point), `imgui_type`, `imgui_key`, `imgui_wheel` mirror the methods of `.claude/rules/control-imgui-api.md`; MCP has `imgui_find`, `imgui_click`, `imgui_type`, `imgui_key`. They wait for the game's own frames (`IMGUI_WAIT_FRAMES`), need no `step`, are `no_imgui` in Game mode, and a recording keeps them (a replay needs the editor).
- **Every tool is wrapped in `reports_errors`:** the server shows the model only a `ToolError`'s message, so without it `illegal_action` would read "Error executing tool X". `test_dispatch.py` checks it over real stdio.
- `mcp` 2.x only (`MCPServer`, `ToolError`).
- The game's stdout goes to a log file: the server's stdout is the protocol.
- `GameSession.call` drops the connection on a timeout, or the late answer would be taken for the next call's.

## Tests without the real things

`tests/fake_control_server.py` answers from a Python function (`push` before or after the answer); `fake_game.py` serves it as a game executable: a growing version with a `state_changed` push per action, log lines and counts, `batch`, `run_until`, a `FAKE_GAME_PICTURE` screenshot, `set_window` with the engine's limits, `imgui_find` / `imgui_input` on a fixed list of matches with `--editor`. The `fake_game_exe` fixture makes it runnable on Windows too (`FakeGame.bat`); since killing the `.bat` leaves Python running, `fake_game.py` exits after `WATCHDOG_SECONDS` without a `quit`.
