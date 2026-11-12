# Rooms Tools Guidelines

## Overview

`smoke.py`: the smoke test of the room server (`src/Modules/RoomServer/CLAUDE.md`). It starts the built `BagiRoomServer --test-games` as a process of its own and plays from this one, over real sockets, with the WebSocket client of `tools/control/` (`BagiControlClient`: only `connect`, `send_text`, `receive_text`; the engine has no Python dependency and this tool no copy of the frame code). Standard library only, Python 3.10+.

The same scenarios run in the gtest executable against the server in its own process (`RoomServerScenarioTest`); this one proves the real executable: its arguments, the listening line, the signal handling, the exit code.

## Running

```bash
python3 tools/rooms/smoke.py --exe build/macos-debug/bin/BagiRoomServer
python3 tools/rooms/smoke.py --preset macos-debug
ctest --preset macos-debug -R BagiRoomServerSmoke
```

Exit code 0 passed, 1 failed (the message says what and, for a server that died, the tail of its log), 77 skipped (no executable; ctest reads it as skipped). Each scenario starts a fresh server on `--port 0` and reads the port it chose from its `BagiRoomServer listening on ws://...` line on stdout, so there is no port race and no retry. At the end the server gets SIGTERM and must exit 0 within 10 s (on Windows it is terminated and the exit code is not checked).

## Scenarios

A hand of two humans and two computers; a human that drops mid-hand (reconnect grace 300 ms) and returns with its token; the same hand on `cards-leaky`; a server with `--origin` (a foreign `Origin` gets 403, the listed one and a program without one get in).

## The judge

A card id is `c<digit>_<digit>`, a whole token. A message to the client of seat S may name `c<S>_*` and the ids in its own `view.table`; any other id is a leak. Every text the client receives goes through it. The rule is also in `tests/BECore/Rooms/WireJudge.h`: **change both together**. The `cards-leaky` hand is the negative control: the smoke test fails when the judge finds nothing there. Binary and masked frames are not checked here (the gtest scenarios do).
