# No record and replay of a game's control API actions: a bug seen by a person cannot be handed over as a file

- Version: 0.1.11
- Entry: replay
- Date: 2026-11-16
- Bump: patch

## Added
- A replay file (JSON lines, versioned): the `reset` parameters with the seed, then every accepted action of a session in order. Recorded with `--record=FILE` or the control methods `record_start` / `record_stop`; a game opts in with `IControllableGame::IsReplayable()` and `GetReplayStart()` (the seed drawn at `reset`).
- Playback: the control method `replay` and the flags `--replay=FILE` / `--replay-exit` (need the API and the manual clock); the first action that differs from the recording is named, the exit code is 0 or 1.
- `tests/Common/ReplayCheck.h`: a ctest helper that plays a recorded file and checks the final state. The rule of a replayable game is in `Games/CLAUDE.md`, "Record and replay".
