---
paths:
  - "src/Modules/Games/**/*"
  - "tests/BECore/Replay*"
  - "tests/BECore/Session*"
---

# Record and replay of a game session

The module: `src/Modules/Games/CLAUDE.md`.

A recording is the control API form of a session: the start (the seed included), then every accepted `reset`, `perform_action` and `set_state` in order. It is the game's own record (it replays headless or in a window and checks outcomes); the client's session of `tools/control/scenario.py` is another thing (`.claude/rules/control-qa-api.md`). The file is JSON lines, UTF-8, written and flushed line by line, so a crashed game keeps it:

```
{"replay":1,"game":"ReplayTest","engine":"0.1.5","title":"replay test game"}
{"op":"reset","params":{"seed":8},"gap":{"frames":0,"seconds":0.0}}
{"op":"action","params":{"type":"roll"},"outcome":{"value":5},"gap":{"frames":1,"seconds":0.25}}
```

Line 1 is the header; `replay` is the format version (a reader refuses another number). An entry is `op` (`reset`, `action`, `state`), `params` (a `reset`'s are `GetReplayStart()`, the others what the caller sent), `outcome` (what `PerformAction` answered) and `gap`. The file starts at a `reset`: entries before the first are dropped (one log line), so the game's own start must go through `ControlledReset` or it is not in the file. A refused call is never written. `ReplayFile.h` reads and writes it; a reader treats the file as untrusted input (a person or a model edits it), so every refusal names its line.

- **`gap`** is the game time since the previous entry (`Time` frames and total seconds). A game with animations or delayed tasks refuses an action that comes too early, so the playback lets the same game time pass first (the manual clock steps `frames` frames of `seconds / frames`).
- **`outcome`** is the cheap check that the replay did the same thing, not only that the game accepted the same action.
- **Opt-in:** `IControllableGame::IsReplayable()` (default false) and `GetReplayStart()` (the `Reset` options that start the same game again, the drawn seed included; default `{}`). A game that is not replayable answers `unsupported` to `record_start` and `replay`.
- **The rule:** a game is replayable only when all its randomness comes from the seed of `reset` and its delayed tasks follow game time (`DelayGame*`, tweens), never `Delay*` / `RunDelayed`; otherwise the same file plays differently.
- **Who reports:** the handlers of `reset`, `perform_action` and `set_state` call `ControlledReset` / `ControlledAction` / `ControlledSetState` (`GameControl.h`), which call the adapter and, on success, `SessionRecorder` (`Singleton`, main thread only). A person's input that reaches the game another way calls the same functions, or `SessionRecorder::Report*` when it does not pass `PerformAction`; a game never reports a call that a handler has reported. `switch_game` ends a recording: a file is of one game.

| Where | What |
|-------|------|
| `--record=FILE` | records the active game from the start; needs no control API. A bad value or a game that cannot is one log error and no recording; a recording that still runs is closed in `GameManager::Shutdown()` |
| `record_start` `{path, title?}` | to `{path, game}`; `no_active_game`, `not_controllable`, `unsupported`, `invalid_params` (a bad path, a recording runs) |
| `record_stop` | to `{entries}`; `invalid_params` when none runs |
| `replay` `{path}` | plays the file on the manual clock: for every entry it steps the gap, then applies the entry through the adapter (never through the recorder, so a replay does not record). `{ok: true, entries, frames}` and the clock of `step`, or `{ok: false, mismatch}`: a mismatch is a result like `met: false` of `run_until`, not an error. `invalid_params` (no path, a file that cannot be read or parsed, with its line; a file of another game; a recording runs), `no_active_game`, `not_controllable`, `unsupported`, `replay_needs_manual_clock` |
| `--replay=FILE`, `--replay-exit` | `Application` posts `replay` once the game is up; they need `--control` and `--manual-clock`, and `--replay-exit` needs `--replay=`. The exit code is 0 for a played replay, 1 for a mismatch or an error; with `--replay-exit` the application then quits, without it the game stays up on the manual clock to look at |

`mismatch` is `{index, line, kind, entry, error?, expected?, actual?}` for the first entry that differs, `index` 0-based among the entries and `line` of the file: `refused` (the game answered `error` to a call it accepted), `outcome_differs` (`expected` and `actual` outcome) or `start_differs` (after a `reset` `GetReplayStart()` is not the params of the entry). `record_start` and `replay` take any path of the host: a control client can overwrite or read any file the process can; the API is a loopback tool of the person at the machine (`RemoteControl/CLAUDE.md`), so no sandbox.

Files (`Games/`): `ReplayFile` (format, parser), `SessionRecorder`, `ReplayPlayer` (no frames of its own: `PeekGap()`, let it pass, `Apply()`), `ReplayControl` (the three methods, `--record`).

Adopting it, in four steps: (1) draw the seed in `Reset` when none is given and keep it, `GetReplayStart()` answers `{"seed": ..}`; (2) keep every delayed or animated step on game time; (3) `IsReplayable()` returns true, the game's own start (a new game from a menu) goes through `ControlledReset`, and a person's swap or click through `ControlledAction`; (4) record a session with `--record=FILE`, commit the file next to the game's tests and add `EXPECT_REPLAY_FINAL_STATE(game, path, expectedState, step?)` of `<Common/ReplayCheck.h>` (`PlayReplayFile` returns the first mismatch; its default frame advances the clock and updates the active scene, a game with more to advance passes its own `ReplayStepFn`). The engine's case is `tests/BECore/ReplayCheckTest.cpp` with `tests/BECore/replays/replay_test_game.jsonl`, recorded from `ReplayTestGame` (a dice game with an `arm` lock that runs down on game time).
