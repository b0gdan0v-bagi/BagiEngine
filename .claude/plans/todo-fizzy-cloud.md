# TODO: entry on MCP, WebSocket and API control of a game

## Context

The project owner wants a `TODO.md` entry for one goal: a whole game can be developed, played and tested by a model (through MCP) or by an external program over WebSocket + API, with no human at the window. Chess against Stockfish is the example given.

Only `TODO.md` changes. No code, no build, no commit (the working tree already holds unrelated changes: Time, Scene events; leave them alone).

## What the code shows (the facts the entry rests on)

- `src/Application/Application.cpp`: `Run()` is a blocking loop on the real clock; input comes only from the SDL window.
- `src/Modules/Games/CLAUDE.md`: `IGame` has `Start` / `Stop` / `Reset` only, nothing to read state or make a move.
- `games/Chess/src/Chess/Game/ChessGameModel.h`: the only way to move is `OnSquareClicked(Square)`, a two-step select / move that imitates clicks. `games/Match3/.../Match3GameModel.h` already has `TrySwap(a, b)`.
- Chess has no FEN and no move notation (grep finds only the word "algebraic" in a comment); rules are pseudo-legal (`games/Chess/CLAUDE.md`), so Stockfish and the model would disagree on legality.
- No `websocket`, `mcp`, `uci`, `headless` or video-driver handling anywhere in `src`, `games`, `cmake`, `config`; `external/` has no socket or JSON library.
- `IRenderer` has no pixel readback (no screenshot). Scene state can be dumped through `ISerializer`.
- `TaskManager` main-thread queue (`SwitchToMainThread()`, drained at frame stage 2) is the existing way to hand work from another thread to the frame.
- `Time::Advance(seconds)` already exists (used by tests) and is the lever for a stepped, deterministic clock.
- Dependency: headless needs the game-only run mode (existing TODO entry), so the new entry goes right after it.

## Edits to `TODO.md`

### 1. Roadmap, stage 2 (line 10)

Replace "The game-only run mode is next." with
"The game-only run mode is next, then the external control API (MCP, WebSocket) that lets a model or a program play a game."

No renumbering: stage 3 and the Driver paragraph keep their references.

### 2. Roadmap, "Not now" (line 15)

Replace "networking" with
"networking for players (servers, multiplayer; the control API in stage 2 is a local development tool, not this)".

### 3. New entry, inserted after "No game-only run mode" and before "No animation"

```markdown
## No external control: a game can be played only by a human at the window

**Where:** `src/Application/Application.cpp` (`Run()` is a blocking loop on the real clock, input comes only from the SDL window), `src/Modules/Games/` (`IGame` has `Start` / `Stop` / `Reset` and nothing to read the state or make a move), `games/Chess/src/Chess/Game/ChessGameModel.h` (`OnSquareClicked(Square)` is the only way to move), `games/Match3/src/Match3/Game/Match3GameModel.h` (`TrySwap` takes a move directly), `src/Modules/TaskSystem/` (`SwitchToMainThread()` is how another thread hands work to the frame), `external/` (no socket, WebSocket or JSON library).

**What is missing** (found by reading the code and searching `src`, `games` and CMake, not by running a game):

1. No control channel: nothing listens on a socket or a pipe, there is no MCP server, no WebSocket or JSON dependency.
2. No way for a program to read the game state or make a move. Match3 has `TrySwap(a, b)`, but Chess moves only through `OnSquareClicked`, a two-step select / move that imitates mouse clicks. Nothing lists the actions a game offers in its current state.
3. Chess cannot talk to a chess engine: no FEN, no move notation (UCI `e2e4`), and the rules are pseudo-legal (no check, castling, en passant or promotion, see `games/Chess/CLAUDE.md`), so Stockfish and the model would disagree on what a legal move is.
4. No run without a person: the window and the renderer are always created, and the scene is drawn only through `ViewportWidget` (see the game-only run mode entry). The loop ticks the real clock and has no "advance N frames" step; `Time::Advance(seconds)` exists, but only tests call it.
5. No observation beyond the window: `IRenderer` cannot read pixels back, so there is no screenshot. The scene state can be dumped through `ISerializer`.

**A fix has to cover** (the goal: a whole game can be developed, played and tested by a model or by an external program with no human at the window; Chess against Stockfish is the acceptance example):

- a control service in `CoreManager`, independent of any transport: commands (list games, switch, reset, get state, list actions, perform an action, step N frames, screenshot, quit) queued from any thread and run on the main thread at a fixed stage of the frame, never on the socket thread;
- a game-side interface that the engine finds without knowing game names (state snapshot, legal actions, apply an action), implemented by Chess and Match3; no engine code refers to a concrete game;
- a WebSocket server (JSON, request id and response, state-change push) in a new module placed in the module order of `CLAUDE.md` without a cycle; localhost only, off by default, enabled by config or a command-line flag. This is a development tool, not the player networking from "Not now";
- an MCP server so Claude can play and debug a game. Recommendation: a thin separate process in `tools/` that maps MCP tools (`get_state`, `perform_action`, `step`, `screenshot`, `reset`) onto the WebSocket, so the engine has no MCP dependency;
- a headless mode on top of the game-only run mode: a hidden or absent window, offscreen rendering for screenshots, an optional manual clock (`Time::Advance`) for deterministic runs; a screenshot needs a pixel readback in `IRenderer` and in both backends;
- Chess rules and notation in `Game/`, each with gtests: FEN in and out, UCI move parse and format, full legal moves (check, mate, castling, en passant, promotion) checked against known perft counts, and a model-level move that does not go through clicks;
- a Stockfish bridge as an external script in `tools/`: it reads the position over the WebSocket, asks Stockfish over UCI and sends the move back, so Stockfish is never linked into the engine;
- tests: command dispatch with a fake transport, a smoke test that starts a game headless and plays a few moves through a WebSocket client, and a Chess ↔ Stockfish game as an optional check that is skipped when Stockfish is not installed.
```

## Verification

- Re-read `TODO.md`: the entry sits between "No game-only run mode" and "No animation", headings and bold labels match the neighbours, the roadmap lines read correctly.
- `git diff --stat` shows `TODO.md` only from this task (the other modified files were already modified before).
- Nothing to build or test: documentation change. Formatting and PCH-include steps do not apply (no C++ touched).
