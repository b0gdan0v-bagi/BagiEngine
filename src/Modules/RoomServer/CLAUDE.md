# RoomServer Module Guidelines

## Overview

The `Rooms` hub on the network (the entry `network-rooms` of the todo store, slice 2 of 4): a WebSocket server that feeds every text message of a client to `RoomHub` and sends what the hub says back. `Rooms` is the logic and the protocol (`Rooms/CLAUDE.md`), `RemoteControl` the transport (`WebSocketListener`, `RemoteControl/CLAUDE.md`); this module is the glue between them and knows no game. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomServerModule`; `BagiEngineTests` links it).

Built only with `ENGINE_WITH_REMOTE_CONTROL` (the web build has no sockets). Module order: after `Rooms`. Its `CMakeLists.txt` also builds the executable `BagiRoomServer` and registers the ctest entries `BagiRoomServerSmoke` (`tools/rooms/CLAUDE.md`; it plays from Python and also runs the C++ client `BagiRoomClientProbe` of `RoomClient` against the real executable) and `BagiRoomServerHostile` (`hostile.py`, "Threat model"); in a game's build, which does not build the executable, `ctest` lists it as skipped (exit code 77): expected.

| File | Purpose |
|------|---------|
| `RoomServerSettings.h` | `RoomServerSettings` and `kRoomServerHealthPath`: port, bind scope, handshake policy, transport limits, the hub's `RoomHubSettings`, the tick interval, the ping interval, the injected clock and `RandomFn` (`MoveOnlyFunction`s: the settings are move-only) |
| `RoomWebSocketServer.h/.cpp` | `RoomWebSocketServer(registry, settings)`: `Start()`, `Stop()`, `GetPort()`, `IsRunning()` |
| `RoomServerArgs.h/.cpp` | `RoomServerArgs` (what a server is started with: a plain struct), `LoadRoomServerArgs(argc, argv)` (environment, then flags), `ParseRoomServerArgs(argc, argv, base)`, `ReadRoomServerEnvironment(lookup)`, `GetRoomServerUsage()`, `ToSettings(args)` |
| `RoomServerMain.h/.cpp` | `RunRoomServer(registry, args)`: logging, the server, the listening line, the loop until SIGINT / SIGTERM |
| `DemoCardGame.h/.cpp` | `DemoCardGame`, the card game of the tests and of `--test-games` (not for a product), `LeakyDemoCardGame` (negative control), `FindHiddenCards` |
| `server/main.cpp` | the executable `BagiRoomServer`; outside `src/`, so the module glob does not take it |

## A server main

A game's own server is a `main` that fills a `SeatedGameRegistry` and calls `RunRoomServer(registry, args)`; `server/main.cpp` is the model (`be_add_headless_executable(<Name> SOURCES main.cpp LINK RoomServerModule ...)`: the engine core, no window, no `Application`). `args` comes from `LoadRoomServerArgs(argc, argv)` (the environment of a hosting platform, then the command line over it) or is filled by hand. `RunRoomServer` makes a console log sink in code (a server has no `LoggerConfig.xml`), prints `BagiRoomServer listening on ws://0.0.0.0:<port>` to stdout (flushed: a program that starts the server waits for that line; `127.0.0.1` with `--bind loopback`), drains the log every 50 ms on the calling thread, and ends with exit code 0 on SIGINT or SIGTERM (1 when it cannot listen, the message on stderr). `BagiRoomServer` registers `cards` and `cards-leaky` only with `--test-games`; without it the registry is empty and it exits 1.

Run: `build/<preset>/bin/BagiRoomServer --test-games --port 0`.

**Profiling:** `RunRoomServer` names its thread `Main` and starts the profiler (`args.profiler`, resolved by `ParseRoomServerArgs` through `ResolveProfilerSettings`, `BECore/Profiler/CLAUDE.md`) before anything listens; the trace is written at SIGTERM, after `Stop()` has joined every thread, and a file that cannot be written makes a clean run exit 1. A build without the profiler refuses either flag (exit 1, the message on stderr). There are no environment variables for it: a hosting platform profiles nothing.

| Flag | Meaning (default) |
|------|-------------------|
| `--port N` | 0..65535, 0 picks a free port (8080) |
| `--bind loopback\|any` | the machine's own address, or every address (`any`) |
| `--origin O` | a web page that may connect, repeatable, `*` is every page (none: browsers refused, programs let in). The policy is always the public one, also on the loopback: `Host` is not checked |
| `--max-connections N` | connections the hub admits, 1..4096 (512); the transport allows 64 more |
| `--max-per-address N` | open sockets of one address, 0 is off (32) |
| `--max-rooms-per-address N` | rooms one address may have at once, 0..64, 0 is off (4) |
| `--lobby-timeout-ms N` | a lobby with no message but `ping` for this long is closed, 0..86 400 000, 0 is off (900 000) |
| `--trusted-proxy-hops N` | proxies in front that append to `X-Forwarded-For`, 0..4, 0 reads the socket's address (0) |
| `--computer-delay-ms N`, `--reconnect-grace-ms N` | the hub's times (800, 30000) |
| `--ping-interval-ms N` | liveness pings, 0 is off (20000) |
| `--stats-interval-ms N` | the connections-and-rooms log line at most this often, 0 is off (30000) |
| `--shutdown-grace-ms N` | on SIGTERM how long to wait for the clients to end after close 1001, 0..60000, 0 sends no close frame (1000; `RoomServerSettings` itself has 0 so tests do not wait) |
| `--trace=FILE` | record with the Chrome trace profiler and write FILE at the end (profile build only) |
| `--profiler=none\|chrome\|tracy` | the profiler backend; `chrome` without `--trace` records for nothing (none; Tracy in a Tracy build) |
| `--test-games`, `--help` | |

**Environment** (a hosting platform sets no flags): `PORT` and `BAGI_ROOMS_BIND`, `_ORIGINS` (comma-separated, items trimmed, empty ones skipped), `_MAX_CONNECTIONS`, `_MAX_PER_ADDRESS`, `_MAX_ROOMS_PER_ADDRESS`, `_LOBBY_TIMEOUT_MS`, `_TRUSTED_PROXY_HOPS`, `_COMPUTER_DELAY_MS`, `_RECONNECT_GRACE_MS`, `_PING_INTERVAL_MS`, `_STATS_INTERVAL_MS`, `_SHUTDOWN_GRACE_MS` and `_TEST_GAMES` (`1|0|true|false`; the first live check of the demo server only), each with the range of its flag. The order is default, then variable, then flag (`--origin` on the command line replaces the list of the variable); an empty variable is unset. `ReadRoomServerEnvironment` takes the lookup as a function so a test needs no real environment.

Numbers are range-checked; an unknown flag or a bad number or variable is an error with a message (naming the flag or the variable) and exit code 1.

**Log** (what a hosting platform shows): `RunRoomServer` makes stdout line-buffered first and flushes it after each delivery of the log (the Windows CRT has no line buffering): a piped stdout is block-buffered, so the platform sees the lines late. Lines: the listening line, `Room server settings: ...` (the effective values, no secret), a line per connection, refusal and close, and `Room server: <n> connections, <m> rooms` from the hub thread when the pair changed and `statsLogInterval` (the server's `clock`) has passed since the last such line. No name, code or token is ever logged.

**Health:** `ToSettings` sets `policy.healthPath` to `/healthz`, so a plain `GET /healthz` gets 200 `ok` (`RemoteControl/CLAUDE.md`, "Security"); the platform's health check needs it.

## Threads

- The listener's threads (`Room accept`, a reader and a writer per connection) are `RemoteControl/CLAUDE.md`, "Threads". The reader threads call the handler: `onOpen` puts the connection in a map by id, then every one of `onOpen`, `onMessage` and `onClosed` pushes an event (`Opened`, `Message`, `Closed`) into the queue.
- **One hub thread** (`Room hub`) owns the `RoomHub`, which is single-threaded: the hub is a local of the thread function, so nothing else can reach it. It waits for an event or for `tickInterval`, takes the whole queue, feeds the hub in the order of the queue, then ticks it when the interval has passed (so a flood cannot starve the tick). The tick interval is real time; the time the hub sees is `settings.clock`.
- **Zones on the hub thread:** `Room open`, `Room message` and `Room close` for one event of that kind, `Room tick` for the whole tick part of the loop (the hub's tick, liveness, stats), `Room send` for `SendTo` / `CloseConnection` (inside the event or tick zone that caused it). The idle wait is outside every zone. The transport's `WS` zones are `RemoteControl/CLAUDE.md`, "Threads".
- **The queue is bounded**: `kMaxQueuedEvents` (4096) events and `kMaxQueuedBytes` (4 MiB) of text. A reader that finds it full waits in `PushEvent`, so it stops reading its socket, TCP slows the sender and memory stays bounded; the other connections are only as slow as the hub. `Stop()` wakes the waiting readers first (they return false and end their connection), because the listener joins them.
- The `HubSink` runs on the hub thread. `send` looks the connection up (a map under a mutex of the server) and queues a text frame from the hub's text (valid for the call only; `QueueText` copies it): it never writes to a socket, so a slow client cannot stall the hub; a client whose queue passes `maxOutboxBytes` is dropped by its connection (the hub learns it from the `Closed` event). `broadcast` (the hub's one text for several connections) encodes one `WebSocketSharedFrame`, finds the connections of the ids under one lock and queues the frame on each outside it (`QueueShared`), so the text is encoded once whatever the number of recipients. The text of a received message is copied once, from the parser's view into the `Event` that crosses to the hub thread; `OnMessage` reads it in place. `close` queues a close frame. For one connection the frames go out in the order the hub produced them. A message for a gone connection is dropped.
- A connection is reported to the hub (`Opened`, carrying the client address: `GetClientAddress()`, the forwarded one behind trusted proxies) **after** its handshake succeeded; a socket that never completes it is cut when the handshake timeout (5 s, one deadline for the whole request head, not per read) runs out, and the hub never hears of it. The hub's `Closed` handling ignores a connection it closed itself.
- **Liveness** (`pingInterval`, 20 s; 0 is off). On every tick the hub thread compares the byte counter of each open connection (`GetBytesReceived()`) with the one it saw last. A change is life (any byte, a pong too); a connection silent for `pingInterval` gets one WebSocket ping, and one silent for twice that is aborted, which the hub learns as a `Closed` event, so the seat goes through the reconnect rules. Without it a half-open connection would keep a seat "connected" and the table waiting for a dead player. The time is `settings.clock`, so a test moves it by hand. **A client has to answer pings** (a browser does by itself; the desktop `WebSocketClient` does, and `RoomClient` leaves the WebSocket pings to its transport and keeps its own `ping` / `pong` heartbeat, `RoomClient/CLAUDE.md`).
- `Stop()`: with `shutdownGrace` > 0 it first **drains**: it sets `_draining`, queues close 1001 `server_stopping` on every open connection and waits on the real clock (a condition variable that `onClosed` signals) until the map of connections is empty or the grace has passed. The hub thread and the readers run meanwhile, so the hub takes the `Closed` events. A connection that opens during the drain is closed the same way (`onOpen` sets it under the lock of the map, so it is either in the snapshot or sees the flag). The client's answer is not awaited: after the close frame the connection shuts its socket and ends; the grace only matters for a client that does not read (its writer is stuck in a full send buffer), which is then cut. Then the old sequence: wake the readers, stop the listener (which ends every connection and joins the readers, so every `onClosed` has run), stop and join the hub thread. After it returns no callback runs and nothing is sent. Safe twice and when never started; the server can be started again (it makes a new hub, so the rooms are gone). A restart loses the rooms: the client reconnects and gets `room_gone`.

## Close codes

What `HubSink::close(reason)` becomes on the wire (`GetRoomCloseCode`, also used by the tests' `HubWorld`; the reason is the close frame's text, `Rooms/CLAUDE.md`, "RoomHub"):

| Reason | Code |
|--------|------|
| `rate_limited`, `too_many_failed_joins` | 1008 (policy violation) |
| `server_full` | 1013 (try again later) |
| the server is stopping (`Stop()`, not the hub: `server_stopping`) | 1001 (going away) |
| anything else (`idle`, `replaced`) | 1000 |

The transport adds its own: 1009 for a message over `hub.maxMessageBytes` (checked at the frame header, before the message is buffered; the hub's check is the second line), 1003 for a binary message, 1002 for a protocol error.

## Limits

The table of defaults (sockets, rooms and failed joins per address, lobby timeout, outbox, message, handshake, silence) and what `_passAddress` turns on are in `.claude/rules/rooms-server-threats.md`; the hub's own limits are in `Rooms/CLAUDE.md`.

## Threat model

The server is public and every byte of a client is untrusted; each limit of the Limits table has a reason (threads, memory, CPU, code guessing). What is trusted, limited and out of scope, and how it is checked: `.claude/rules/rooms-server-threats.md`.

## Origin

In public mode (`policy.loopbackOnly = false`) `Origin` keeps foreign **web pages** out of the server: a browser always sends it, and a page that is not in `policy.allowedOrigins` gets a 403. It does not protect against a program, which sends no `Origin` (or any it likes); what protects a room is its code and the token of a member. An empty list refuses every browser. Details: `RemoteControl/CLAUDE.md`, "Security".

## For later slices

- **Hosting** is the Docker image of `tools/rooms/Dockerfile` (a Linux build of the target, `nobody`, no `CMD`) and the platform notes (the variables in a table, Railway's settings) in the README, "Hosting the room server"; `tools/rooms/docker_check.py` is its check. A game reuses the file with `--build-arg SERVER_TARGET`. The image sets `BAGI_ROOMS_BIND=any` and `BAGI_ROOMS_MAX_PER_ADDRESS=0`.
- **TLS is not here**: the server speaks plain `ws://`; `wss://` is the proxy's job (a hosting platform terminates it). Behind a proxy every client has the proxy's address, so the image keeps `--max-per-address 0` and `--trusted-proxy-hops 0` (a header any client can send is a lie unless the proxy is known to set it): the owner turns the per-address rules on with `BAGI_ROOMS_TRUSTED_PROXY_HOPS` once the platform is known to append the real client as the last entry.
- IPv4 only (`Socket`). Linux is built and run in the Docker image (`docker_check.py`; the gtest executables are not built there, so the stuck-client shutdown tests rely on macOS socket buffer sizes and are unchecked on Linux). Windows is not run: the Winsock branch of `Socket.cpp` is written and unchecked.

## Tests

`tests/BECore/Rooms/RoomServerArgsTest.cpp`: the flags, the environment (a fake lookup) and `ToSettings`. `tests/BECore/Rooms/RoomWebSocketServerTest.cpp`: the server on port 0 against real sockets (`RoomSocketClient.h`: the raw client of `WebSocketTestClient.h` plus the messages of the protocol, with a cursor to read forward) and `FakeSeatedGame`. Covers the exchange (create, join by code, order of replies), what the hub's close and the transport's limits look like on the wire, an abrupt drop, the origin and per-address checks, `Stop()` under load and its drain (close 1001, the grace, a connection opening meanwhile), a client that never reads, the liveness pings on a manual clock, two servers. The bounded queue itself has no test of its own. It also has the per-address rooms, the lobby timeout on the manual clock and the forwarded addresses (a raw handshake with `X-Forwarded-For`).

`tests/BECore/Rooms/RoomServerScenarioTest.cpp`: the scenarios of `RoomsScenarioTest` over real sockets and the real server on port 0 with `DemoCardGame` (`cards`) and `LeakyDemoCardGame` (`cards-leaky`): two humans and two computers, four humans, a human that drops mid-hand (manual clock: nothing is played inside the grace, a computer plays past it, the token brings the human back with `joined`, `lobby`, `view`; a second socket with the token replaces the first with `replaced`; the token is refused after `leave`), two rooms at once, and hostile clients (garbage bytes, junk text, an oversize message, a flood, a half-open handshake) while another room's hand finishes. **The wire judge** (`WireJudge.h`) is the test's only eye, since the game is inside the server: a message to seat S may name the card ids `c<S>_*` and those of its own `view.table`, any other id is a leak. It judges every text frame of every client, and the clients also count masked and binary frames from the server (there must be none). The negative control plays the same hand on `cards-leaky` and fails if the judge finds nothing; the judge's own rule is tested on hand-made texts. The half-open case waits for the handshake timeout (5 s), so that test takes that long. `tools/rooms/smoke.py` plays the same kinds of hands against the executable from another process, with the judge ported to Python (`tools/rooms/CLAUDE.md`); its scenario `trace` starts the server with `--trace=` (the file in a profile build, the refusal in any other).

`RoomHostileTest.cpp` (the hub in memory) and `RoomServerHostileTest.cpp` (real sockets, the `cards` game, the judge on every text) hold the attacks that are not a hand: tokens across rooms, nothing of a room after `leave` / `replaced` / `room_closed`, fragments over the limit, a frame one byte at a time, silent sockets while a table plays, floods, guessing across reconnects. Both play through `RoomTableDriver.h` (the shared table of humans over sockets).
