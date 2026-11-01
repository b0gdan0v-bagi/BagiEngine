# Control Tools Guidelines

## Overview

Python tools that drive a game through the control API (`src/Modules/RemoteControl/CLAUDE.md`, protocol in `src/Modules/BECore/Control/CLAUDE.md`). They are separate processes: the engine has no MCP, Stockfish or Python dependency. Python 3.10+. Nothing here names a concrete game except the Stockfish bridge, which is Chess by nature (UCI), and the fakes used by its tests.

| File | Purpose |
|------|---------|
| `bagi_control_client.py` | WebSocket (RFC 6455) client with the standard library only: `BagiControlClient.call(method, params)`, pushed events in `events`, `ControlError` for an error response |
| `game_process.py` | `GameProcess`: starts a game executable with `--control`, retries until it listens, keeps its log; `build_game_command`, `free_port` |
| `bagi_session.py` | `GameSession`: one game, started (`launch`) or reached (`connect`), as plain Python calls. No MCP code, so it is tested without the `mcp` package. `SessionError` for what the session refuses; a refusal of the game stays a `ControlError` |
| `bagi_mcp.py` | The MCP server (stdio). Each tool is one line over `GameSession`; the only file that needs `mcp` |
| `stockfish_bridge.py` | Plays a Chess game of the engine against Stockfish over UCI (`UciEngine`, `play_game`) |
| `requirements.txt` | `mcp` (2.x) and `pytest`; nothing else is needed |
| `tests/` | pytest, plus `smoke.py` (the ctest entries `Bagi<Game>ControlSmoke`, `.claude/rules/cmake-rules.md`) |

## Running

Use the repository venv (`CI/setup_venv.*` makes `.venv`; on Windows `.venv\Scripts\python`), then once `.venv/bin/pip install -r tools/control/requirements.txt`.

```bash
.venv/bin/python -m pytest tools/control/tests                # the tests; real-Stockfish ones are skipped without Stockfish and a built BagiChess
STOCKFISH=/path/to/stockfish .venv/bin/python -m pytest tools/control/tests/test_stockfish.py   # also plays real games (BAGI_CHESS_EXE if the exe is not in build/*/)
.venv/bin/python tools/control/stockfish_bridge.py --exe build/ClangmacOS/BagiChess --side both --cross-check     # Stockfish against itself, every position checked
.venv/bin/python tools/control/stockfish_bridge.py --exe build/ClangmacOS/BagiChess --side white --opponent random --option "Skill Level=3"
.venv/bin/python tools/control/stockfish_bridge.py --side white      # a game that is already running with --control; you play black at its window (a game on the real clock)
```

## The MCP server

`claude mcp add bagi -- .venv/bin/python tools/control/bagi_mcp.py` (Windows: `.venv\Scripts\python`). The repository has no `.mcp.json`: the interpreter path differs per OS and machine, and a project file makes every contributor approve a server they may not want. Tools: `launch_game`, `connect`, `status`, `get_state`, `list_actions`, `perform_action`, `set_state`, `reset`, `step`, `screenshot` (an image), `game_log`, `quit`. `launch_game` starts the game `--headless --manual-clock`, so a screenshot works and nothing moves until `step`.

- Written for `mcp` 2.x (`MCPServer`, `ToolError`); 1.x had `FastMCP` and is not supported.
- **Every tool is wrapped in `reports_errors`.** The server shows the model only the message of a `ToolError`; any other exception becomes "Error executing tool X" and the model would never see `illegal_action` or `promotion_required`. A new tool needs the decorator; `test_dispatch.py` checks this over a real stdio connection.
- The game's stdout goes to a log file, never to the server's: stdout is the MCP protocol.
- `GameSession.call` drops the connection on a timeout, because the late answer would be taken for the answer of the next call.

## The Stockfish bridge

Reads `get_state` (`startFen` + `moves`, so Stockfish sees the repetitions), asks Stockfish for `go movetime N`, and sends the move as `perform_action`. A move Stockfish picks that `list_actions` does not have is a `RulesMismatch`, not a retry. `--cross-check` compares `go perft 1` with `list_actions` in every position: a standing check of the rules against an independent implementation. Exit codes: 0 done, 1 failure, 77 no Stockfish (`--stockfish`, `$STOCKFISH`, `PATH`).

- On the manual clock the bridge runs one frame after each move so the scene follows (`--step-frames`).
- A person at the window should play on the real clock: on the manual clock the scene reacts to a click only in a frame that someone steps (`Control/CLAUDE.md`, "Manual clock"). The bridge polls `get_state` while it waits. Not tried with a person yet.
- Measured once (Stockfish 19, macOS): 40 random-against-random games of up to 300 moves, 11,274 positions, 98 promotions, mates and a stalemate, no difference in the legal moves. The same check on a full Stockfish-against-Stockfish game (66 moves) passes.

## Tests without the real things

`tests/fake_control_server.py` (a WebSocket server answering from a Python function), `fake_game.py` (a game executable that serves it), `fake_uci_engine.py` (a UCI engine that answers from a JSON file). Executables are made as scripts with a shebang, so tests that need them are skipped on Windows. New tool code gets a test against these before it is tried on the real game.
