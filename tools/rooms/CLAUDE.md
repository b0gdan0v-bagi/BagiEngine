# Rooms Tools Guidelines

## Overview

`smoke.py`: the smoke test of the room server (`src/Modules/RoomServer/CLAUDE.md`). It starts the built `BagiRoomServer --test-games` as a process of its own and plays from this one, over real sockets, with the WebSocket client of `tools/control/` (`BagiControlClient`: only `connect`, `send_text`, `receive_text`; the engine has no Python dependency and this tool no copy of the frame code). Standard library only, Python 3.10+.

The same scenarios run in the gtest executable against the server in its own process (`RoomServerScenarioTest`); this one proves the real executable: its arguments, the listening line, the signal handling, the exit code.

With `--probe <BagiRoomClientProbe>` (`ctest` passes it when the target is built; `--preset` finds it next to the server) it also runs the C++ client of `RoomClient` from a process of its own. Without a probe those scenarios are skipped with a line.

## Running

```bash
python3 tools/rooms/smoke.py --exe build/macos-debug/bin/BagiRoomServer
python3 tools/rooms/smoke.py --preset macos-debug
python3 tools/rooms/smoke.py --exe build/macos-debug/bin/BagiRoomServer --probe build/macos-debug/bin/BagiRoomClientProbe
ctest --preset macos-debug -R BagiRoomServerSmoke
```

Exit code 0 passed, 1 failed (the message says what and, for a server that died, the tail of its log), 77 skipped (no executable; ctest reads it as skipped). Each scenario starts a fresh server on `--port 0` and reads the port it chose from its `BagiRoomServer listening on ws://...` line on stdout, so there is no port race and no retry. At the end the server gets SIGTERM and must exit 0 within 10 s (on Windows it is terminated and the exit code is not checked).

## Scenarios

A hand of two humans and two computers; a human that drops mid-hand (reconnect grace 300 ms) and returns with its token; the same hand on `cards-leaky`; a server with `--origin` (a foreign `Origin` gets 403, the listed one and a program without one get in); a server started with `PORT=0` in its environment and no `--port` (the variable is read: it listens on a port of its own and takes a client); a plain `GET /healthz` over a raw socket gets 200 and `ok`, `GET /` gets 400; the log of a running server on a piped stdout (`--stats-interval-ms 200`, a refused foreign `Origin`): the settings line, the refusal line and a stats line must show up within 5 s (block-buffered output fails it); SIGTERM with a client connected (`--shutdown-grace-ms 3000`): the client is closed with 1001 `server_stopping` and the process exits 0 inside the grace (not on Windows).

With the probe (`BagiRoomClientProbe --url ws://127.0.0.1:<port> --game cards`, prints `PROBE OK finished version=<n>` and exits 0): the probe plays a hand to the end; the same against a server whose `--origin` names only a web page (the probe sends no `Origin`, so it gets in); and a server that is killed (no close frames) after the probe's `PROBE first view` line, slowed by `--computer-delay-ms` so the hand is still running: the probe must exit non-zero with `PROBE FAILED` within 10 s (it ends on the first lost connection, it does not reconnect).

## The judge

A card id is `c<digit>_<digit>`, a whole token. A message to the client of seat S may name `c<S>_*` and the ids in its own `view.table`; any other id is a leak. Every text the client receives goes through it. The rule is also in `tests/BECore/Rooms/WireJudge.h`: **change both together**. The `cards-leaky` hand is the negative control: the smoke test fails when the judge finds nothing there. Binary and masked frames are not checked here (the gtest scenarios do).
