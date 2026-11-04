# Control Tools Guidelines

## Overview

Python tools that drive a game through the control API (`src/Modules/RemoteControl/CLAUDE.md`, protocol in `src/Modules/BECore/Control/CLAUDE.md`). They are separate processes: the engine has no MCP or Python dependency. Python 3.10+. Nothing here names a concrete game apart from Match3 in examples; a game with its own tools (the Stockfish bridge of BagiEngine-Chess) keeps them in its own repository and imports the client from here.

| File | Purpose |
|------|---------|
| `bagi_control_client.py` | WebSocket (RFC 6455) client with the standard library only: `BagiControlClient.call(method, params)`, pushed events in `events`, `ControlError` for an error response |
| `game_process.py` | `GameProcess`: starts a game executable with `--control`, retries until it listens, keeps its log; `build_game_command`, `free_port` |
| `bagi_session.py` | `GameSession`: one game, started (`launch`) or reached (`connect`), as plain Python calls. No MCP code, so it is tested without the `mcp` package. `SessionError` for what the session refuses; a refusal of the game stays a `ControlError` |
| `bagi_mcp.py` | The MCP server (stdio). Each tool is one line over `GameSession`; the only file that needs `mcp` |
| `requirements.txt` | `mcp` (2.x) and `pytest`; nothing else is needed |
| `tests/` | pytest, plus `smoke.py` (the ctest entries `Bagi<Game>ControlSmoke`, `.claude/rules/cmake-rules.md`) |

## Running

Use the repository venv (`CI/setup_venv.*` makes `.venv`; on Windows `.venv\Scripts\python`), then once `.venv/bin/pip install -r tools/control/requirements.txt`.

```bash
.venv/bin/python -m pytest tools/control/tests                # the tests
```

## The MCP server

`claude mcp add bagi -- .venv/bin/python tools/control/bagi_mcp.py` (Windows: `.venv\Scripts\python`). The repository has no `.mcp.json`: the interpreter path differs per OS and machine, and a project file makes every contributor approve a server they may not want. Tools: `launch_game`, `connect`, `status`, `get_state`, `list_actions`, `perform_action`, `set_state`, `reset`, `step`, `screenshot` (an image), `ui_tree`, `ui_click`, `game_log`, `quit`. `launch_game` starts the game `--headless --manual-clock`, so a screenshot works and nothing moves until `step`. `ui_tree` reports a button's `action`, `disabled`, `value` and `selected` and a choice group's (`UISegmented`) `bind` and `selectedValue`; `ui_click` answers with the `action` and `value` it emitted. A click on a segment selects nothing itself (the game sets the variable), so a choice is checked with `ui_tree`, `ui_click`, `step`, `ui_tree`.

- Written for `mcp` 2.x (`MCPServer`, `ToolError`); 1.x had `FastMCP` and is not supported.
- **Every tool is wrapped in `reports_errors`.** The server shows the model only the message of a `ToolError`; any other exception becomes "Error executing tool X" and the model would never see `illegal_action` or `promotion_required`. A new tool needs the decorator; `test_dispatch.py` checks this over a real stdio connection.
- The game's stdout goes to a log file, never to the server's: stdout is the MCP protocol.
- `GameSession.call` drops the connection on a timeout, because the late answer would be taken for the answer of the next call.

## Tests without the real things

`tests/fake_control_server.py` (a WebSocket server answering from a Python function), `fake_game.py` (a game executable that serves it). Executables are made as scripts with a shebang, so tests that need them are skipped on Windows. New tool code gets a test against these before it is tried on the real game.
