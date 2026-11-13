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

A hand of two humans and two computers; a human that drops mid-hand (reconnect grace 300 ms) and returns with its token; the same hand on `cards-leaky`; a server with `--origin` (a foreign `Origin` gets 403, the listed one and a program without one get in); a server started with `PORT=0` in its environment and no `--port` (the variable is read: it listens on a port of its own and takes a client); a plain `GET /healthz` over a raw socket gets 200 and `ok`, `GET /` gets 400; the log of a running server on a piped stdout (`--stats-interval-ms 200`, a refused foreign `Origin`): the settings line, the refusal line and a stats line must show up within 5 s (block-buffered output fails it); a public and a private `cards` room, a third socket lists them (`list`): only the public code is there, with host, `taken` and `seats`, `no-such-game` is `unknown_game`, the private code is on no wire of the browser, and it joins from the list and plays the hand to the end; SIGTERM with a client connected (`--shutdown-grace-ms 3000`): the client is closed with 1001 `server_stopping` and the process exits 0 inside the grace (not on Windows).

With the probe (`BagiRoomClientProbe --url ws://127.0.0.1:<port> --game cards`, prints `PROBE OK finished version=<n>` and exits 0): the probe plays a hand to the end; the same against a server whose `--origin` names only a web page (the probe sends no `Origin`, so it gets in); and a server that is killed (no close frames) after the probe's `PROBE first view` line, slowed by `--computer-delay-ms` so the hand is still running: the probe must exit non-zero with `PROBE FAILED` within 10 s (it ends on the first lost connection, it does not reconnect).

## The hostile client

`hostile.py` attacks the real executable with frames **written by hand** (it is the one place that does: `BagiControlClient` refuses to send an unmasked, oversized or fragmented frame, a byte at a time, or a handshake that never ends). The honest players, the judge and the server process are the ones of `smoke.py`; in each scenario a good table (`cards`, two humans and two computers, judged on every text) plays while the attack goes on, and at the end it must be finished with no error and no hidden card, the server must answer a new client and exit 0 on SIGTERM.

```bash
python3 tools/rooms/hostile.py --preset macos-debug          # or --exe <BagiRoomServer>; --list shows the scenarios
python3 tools/rooms/hostile.py --preset macos-debug --only address-cap --server-arg=--max-per-address --server-arg=0   # a limit switched off: the scenario has to fail
ctest --preset macos-debug -R BagiRoomServerHostile          # about 15 s; exit 77 (skipped) without the executable
```

Scenarios, each on a fresh server (`--port 0`): wrong seat numbers; wrong and malformed room codes; tokens (a third socket takes a seat over, a token with another room's code is `bad_token`); actions out of turn, stale, replayed, of another seat's card or of a card that never existed; frames (over the limit in one frame and in three fragments, a message at the limit, one byte per write, a ping between fragments, no mask, binary, a stray continuation, invalid UTF-8); stalled handshakes and 40 silent sockets cut by the deadline, a 100 KiB head; floods of `ping` and `create_room` closed with 1008, 100 sockets dropped; the per-address socket cap (a forged `X-Forwarded-For` changes nothing at hops 0); `--max-rooms-per-address 2`; `--lobby-timeout-ms 500` (pings do not keep the lobby; the socket stays); guessing codes across reconnects (the address bucket, no oracle, a member's token still works); `--trusted-proxy-hops 1` (forwarded addresses do not limit each other, a forged left entry counts as the real one). `list-flood`: a flood of `list_rooms` is answered `rate_limited` with `ref: list_rooms` and then closed 1008 by the general bucket; 100 lists from fresh sockets and between wrong codes leave the failed-join limits as they were (the fifth wrong code still closes, the address budget of 10 is whole) and no private code is in any answer. `tests/test_hostile.py` covers the frame builder and the writers. The demo game's error text repeats a card id the player named: `scenario_actions` forgives exactly those ids.

## The Docker check

`docker_check.py` checks the image of `tools/rooms/Dockerfile` the way a platform runs it. **Manual**, not a ctest entry: a cold build takes minutes (a warm one, a minute); the Linux preset builds no tests, so this is also the only run of the server on Linux.

```bash
python3 tools/rooms/docker_check.py                       # from the engine root: builds the image bagi-room-server-check, runs, checks
python3 tools/rooms/docker_check.py --image bagi-room-server   # an image that exists: no build
python3 tools/rooms/docker_check.py --context . --dockerfile engine/tools/rooms/Dockerfile   # from a game's root
python3 -m unittest discover tools/rooms/tests            # the unit test: a fake runner, no docker
```

It runs the container with `PORT=9090`, `BAGI_ROOMS_TEST_GAMES=1`, `BAGI_ROOMS_ORIGINS` and `BAGI_ROOMS_SHUTDOWN_GRACE_MS`, publishes the port on a free one of 127.0.0.1, and checks: the listening line for `PORT` and the settings line in `docker logs`; `/healthz`; the Origin rules; a `cards` room (`joined`); `docker stop` ends with exit code 0 and the connected client is closed with 1001. Exit codes as `smoke.py` (77: no `docker` or no daemon). The container is always removed, the image stays. It reuses `Human` and `http_get` of `smoke.py`; every `docker` call goes through `run_docker`, which the unit test replaces.

## The deploy without a build on Railway

`build_and_deploy.py` builds the server on this machine and uploads only the binary, so Railway compiles nothing (the build from source takes minutes and the upload of a whole checkout with its submodules is large). From the engine root or a game root (`--source-dir`, `--server-target`, `--preset` as for the Dockerfile):

```bash
python3 tools/rooms/build_and_deploy.py --dry-run                     # print the commands, run nothing
python3 tools/rooms/build_and_deploy.py --deploy --service bagi-rooms # build, stage build/rooms-deploy/, railway up
python3 engine/tools/rooms/build_and_deploy.py --server-target BagiMahjongServer --deploy --service <name>   # from a game
python3 -m unittest discover tools/rooms/tests                         # the unit test: a fake runner, no docker, no railway
```

It runs the `build` stage of `Dockerfile` (`docker build --platform linux/amd64 --target build`), takes `/out` out of it with `docker create` / `cp` / `rm`, and stages `app/` (the executable `room-server` and its shared libraries) beside `Dockerfile.prebuilt` copied as `Dockerfile` (debian-slim, `libstdc++6`); `railway up <stage> --path-as-root --service <name> --detach` uploads that folder only (a few MB). `linux/amd64` is Railway's architecture: on an arm machine the compiler runs in emulation and is slow. Its model is `tools/web/build_and_deploy.py`; it reads and writes no secret (the Railway CLI has its own login or `RAILWAY_TOKEN`). **Manual:** the real build and deploy are not run by any test.

`Dockerfile.prebuilt` and the last stage of `Dockerfile` run the same binary the same way: change one, change the other (a test compares `WORKDIR`, `ENV`, `EXPOSE`, `USER`, `ENTRYPOINT`; `libstdc++6` and the base image stay equal by hand). Both `Dockerfile` stage names (`build`) and `/out/room-server` are a contract with the script.

## The judge

A card id is `c<digit>_<digit>`, a whole token. A message to the client of seat S may name `c<S>_*` and the ids in its own `view.table`; any other id is a leak. Every text the client receives goes through it, a `rooms` answer too (it names no card; the scenarios also look for the private room's code in it). The rule is also in `tests/BECore/Rooms/WireJudge.h`: **change both together**. The `cards-leaky` hand is the negative control: the smoke test fails when the judge finds nothing there. Binary and masked frames are not checked here (the gtest scenarios do).
