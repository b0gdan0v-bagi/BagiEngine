# RoomServer Module Guidelines

## Overview

The `Rooms` hub on the network (the entry `network-rooms` of the todo store, slice 2 of 4): a WebSocket server that feeds every text message of a client to `RoomHub` and sends what the hub says back. `Rooms` is the logic and the protocol (`Rooms/CLAUDE.md`), `RemoteControl` the transport (`WebSocketListener`, `RemoteControl/CLAUDE.md`); this module is the glue between them and knows no game. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomServerModule`; `BagiEngineTests` links it).

Built only with `ENGINE_WITH_REMOTE_CONTROL` (the web build has no sockets). Module order: after `Rooms`. Its `CMakeLists.txt` also builds the executable `BagiRoomServer` and registers the ctest entry `BagiRoomServerSmoke` (`tools/rooms/CLAUDE.md`; it plays from Python and also runs the C++ client `BagiRoomClientProbe` of `RoomClient` against the real executable); in a game's build, which does not build the executable, `ctest` lists it as skipped (exit code 77): expected.

| File | Purpose |
|------|---------|
| `RoomServerSettings.h` | `RoomServerSettings` and `kRoomServerHealthPath`: port, bind scope, handshake policy, transport limits, the hub's `RoomHubSettings`, the tick interval, the ping interval, the injected clock and `RandomFn` |
| `RoomWebSocketServer.h/.cpp` | `RoomWebSocketServer(registry, settings)`: `Start()`, `Stop()`, `GetPort()`, `IsRunning()` |
| `RoomServerArgs.h/.cpp` | `RoomServerArgs` (what a server is started with: a plain struct), `LoadRoomServerArgs(argc, argv)` (environment, then flags), `ParseRoomServerArgs(argc, argv, base)`, `ReadRoomServerEnvironment(lookup)`, `GetRoomServerUsage()`, `ToSettings(args)` |
| `RoomServerMain.h/.cpp` | `RunRoomServer(registry, args)`: logging, the server, the listening line, the loop until SIGINT / SIGTERM |
| `DemoCardGame.h/.cpp` | `DemoCardGame`, the card game of the tests and of `--test-games` (not for a product), `LeakyDemoCardGame` (negative control), `FindHiddenCards` |
| `server/main.cpp` | the executable `BagiRoomServer`; outside `src/`, so the module glob does not take it |

## A server main

A game's own server is a `main` that fills a `SeatedGameRegistry` and calls `RunRoomServer(registry, args)`; `server/main.cpp` is the model (`be_add_headless_executable(<Name> SOURCES main.cpp LINK RoomServerModule ...)`: the engine core, no window, no `Application`). `args` comes from `LoadRoomServerArgs(argc, argv)` (the environment of a hosting platform, then the command line over it) or is filled by hand. `RunRoomServer` makes a console log sink in code (a server has no `LoggerConfig.xml`), prints `BagiRoomServer listening on ws://0.0.0.0:<port>` to stdout (flushed: a program that starts the server waits for that line; `127.0.0.1` with `--bind loopback`), drains the log every 50 ms on the calling thread, and ends with exit code 0 on SIGINT or SIGTERM (1 when it cannot listen, the message on stderr). `BagiRoomServer` registers `cards` and `cards-leaky` only with `--test-games`; without it the registry is empty and it exits 1.

Run: `build/<preset>/bin/BagiRoomServer --test-games --port 0`.

| Flag | Meaning (default) |
|------|-------------------|
| `--port N` | 0..65535, 0 picks a free port (8080) |
| `--bind loopback\|any` | the machine's own address, or every address (`any`) |
| `--origin O` | a web page that may connect, repeatable, `*` is every page (none: browsers refused, programs let in). The policy is always the public one, also on the loopback: `Host` is not checked |
| `--max-connections N` | connections the hub admits, 1..4096 (512); the transport allows 64 more |
| `--max-per-address N` | open sockets of one address, 0 is off (32) |
| `--computer-delay-ms N`, `--reconnect-grace-ms N` | the hub's times (800, 30000) |
| `--ping-interval-ms N` | liveness pings, 0 is off (20000) |
| `--stats-interval-ms N` | the connections-and-rooms log line at most this often, 0 is off (30000) |
| `--test-games`, `--help` | |

**Environment** (a hosting platform sets no flags): `PORT` and `BAGI_ROOMS_BIND`, `_ORIGINS` (comma-separated, items trimmed, empty ones skipped), `_MAX_CONNECTIONS`, `_MAX_PER_ADDRESS`, `_COMPUTER_DELAY_MS`, `_RECONNECT_GRACE_MS`, `_PING_INTERVAL_MS`, `_STATS_INTERVAL_MS` and `_TEST_GAMES` (`1|0|true|false`; the first live check of the demo server only), each with the range of its flag. The order is default, then variable, then flag (`--origin` on the command line replaces the list of the variable); an empty variable is unset. `ReadRoomServerEnvironment` takes the lookup as a function so a test needs no real environment.

Numbers are range-checked; an unknown flag or a bad number or variable is an error with a message (naming the flag or the variable) and exit code 1.

**Log** (what a hosting platform shows): `RunRoomServer` makes stdout line-buffered first, since a piped stdout is block-buffered and the platform would see the lines in chunks or at exit. Lines: the listening line, `Room server settings: ...` (the effective values, nothing secret), a line per connection, refusal and close, and `Room server: <n> connections, <m> rooms` from the hub thread when the pair changed and `statsLogInterval` (the server's `clock`) has passed since the last such line. No name, code or token is ever logged.

**Health:** `ToSettings` sets `policy.healthPath` to `/healthz`, so a plain `GET /healthz` gets 200 `ok` (`RemoteControl/CLAUDE.md`, "Security"); the platform's health check needs it.

## Threads

- The listener's threads (`Room accept`, a reader and a writer per connection) are `RemoteControl/CLAUDE.md`, "Threads". The reader threads call the handler: `onOpen` puts the connection in a map by id, then every one of `onOpen`, `onMessage` and `onClosed` pushes an event (`Opened`, `Message`, `Closed`) into the queue.
- **One hub thread** (`Room hub`) owns the `RoomHub`, which is single-threaded: the hub is a local of the thread function, so nothing else can reach it. It waits for an event or for `tickInterval`, takes the whole queue, feeds the hub in the order of the queue, then ticks it when the interval has passed (so a flood cannot starve the tick). The tick interval is real time; the time the hub sees is `settings.clock`.
- **The queue is bounded**: `kMaxQueuedEvents` (4096) events and `kMaxQueuedBytes` (4 MiB) of text. A reader that finds it full waits in `PushEvent`, so it stops reading its socket, TCP slows the sender and memory stays bounded; the other connections are only as slow as the hub. `Stop()` wakes the waiting readers first (they return false and end their connection), because the listener joins them.
- The `HubSink` runs on the hub thread. `send` looks the connection up (a map under a mutex of the server) and queues a text frame: it never writes to a socket, so a slow client cannot stall the hub, and a client whose queue passes `maxOutboxBytes` is dropped by its connection (the hub learns it from the `Closed` event). `close` queues a close frame. For one connection the frames go out in the order the hub produced them. A message for a connection that is gone is dropped.
- A connection is reported to the hub (`Opened`) **after** its handshake succeeded; a socket that never completes it is cut when the handshake timeout (5 s, one deadline for the whole request head, not per read) runs out, and the hub never hears of it. The hub's `Closed` handling ignores a connection it closed itself.
- **Liveness** (`pingInterval`, 20 s; 0 is off). On every tick the hub thread compares the byte counter of each open connection (`GetBytesReceived()`) with the one it saw last. A change is life (any byte: a message, a pong); a connection silent for `pingInterval` gets one WebSocket ping, and one silent for twice that is aborted, which the hub learns as a `Closed` event, so the seat goes through the reconnect rules. Without it a half-open connection (a cable pulled, a phone out of range) would keep a seat "connected" for ever and the table would wait for a dead player. The time is `settings.clock`, so a test moves it by hand. **A client has to answer pings** (a browser does by itself; the desktop `WebSocketClient` does, and `RoomClient` leaves the WebSocket pings to its transport and keeps its own `ping` / `pong` heartbeat, `RoomClient/CLAUDE.md`).
- `Stop()`: wake the readers, stop the listener (which ends every connection and joins the readers, so every `onClosed` has run), stop and join the hub thread. After it returns no callback runs and nothing is sent. Safe twice and when never started; the server can be started again (it makes a new hub, so the rooms are gone).

## Close codes

What `HubSink::close(reason)` becomes on the wire (the reason is the text of the close frame, `Rooms/CLAUDE.md`, "RoomHub"):

| Reason | Code |
|--------|------|
| `rate_limited`, `too_many_failed_joins` | 1008 (policy violation) |
| `server_full` | 1013 (try again later) |
| anything else (`idle`, `replaced`) | 1000 |

The transport adds its own: 1009 for a message over `hub.maxMessageBytes` (checked at the frame header, before the message is buffered; the hub's check is the second line), 1003 for a binary message, 1002 for a protocol error.

## Limits

| Limit | Default |
|-------|---------|
| open sockets, handshakes included (`maxConnections`) | `RoomLimits::kMaxConnections + 64`: the hub's own limit answers `server_full` first, the extra is room for connections still in the handshake; over it a 503 |
| open sockets of one address (`maxConnectionsPerAddress`) | 32; 0 is off (behind a proxy every client has the proxy's address) |
| send queue of a connection (`maxOutboxBytes`) | 1 MiB |
| message | `hub.maxMessageBytes` (16 KiB) |
| handshake | 5 s in all, request head 8 KiB |
| silence (`pingInterval`) | a ping after 20 s, a drop after 40 s |

Two threads per connection, so about 1200 threads at the limit: accepted for a server of friends.

## Origin

In public mode (`policy.loopbackOnly = false`) `Origin` keeps foreign **web pages** out of the server: a browser always sends it, and a page that is not in `policy.allowedOrigins` gets a 403. It does not protect against a program, which sends no `Origin` (or any it likes); what protects a room is its code and the token of a member. An empty list refuses every browser. Details: `RemoteControl/CLAUDE.md`, "Security".

## For later slices

- Hosting (slice 4): the Docker image and the log and drain work are still to come in this slice.
- **TLS is not here**: the server speaks plain `ws://`; `wss://` is the proxy's job (a hosting platform terminates it). Behind a proxy every client has the proxy's address, so `--max-per-address 0`, and `X-Forwarded-For` is not read: the per-address limit has nothing better to count until it is.
- IPv4 only (`Socket`). Windows and Linux are not run for this module on the development machine: the POSIX code is the macOS code, the Winsock branch of `Socket.cpp` is written and unchecked.
- The lobby has no timeout of its own (`Rooms/CLAUDE.md`).

## Tests

`tests/BECore/Rooms/RoomServerArgsTest.cpp`: the flags, the environment (a fake lookup) and `ToSettings`. `tests/BECore/Rooms/RoomWebSocketServerTest.cpp`: the server on port 0 against real sockets (`RoomSocketClient.h`: the raw client of `WebSocketTestClient.h` plus the messages of the protocol, with a cursor to read forward) and `FakeSeatedGame`. Covers the exchange (create, join by code, order of replies), what the hub's close and the transport's limits look like on the wire, an abrupt drop, the origin and per-address checks, `Stop()` under load, a client that never reads, the liveness pings on a manual clock, two servers. The bounded queue itself has no test of its own.

`tests/BECore/Rooms/RoomServerScenarioTest.cpp`: the scenarios of `RoomsScenarioTest` over real sockets and the real server on port 0 with `DemoCardGame` (`cards`) and `LeakyDemoCardGame` (`cards-leaky`): two humans and two computers, four humans, a human that drops mid-hand (manual clock: nothing is played inside the grace, a computer plays past it, the token brings the human back with `joined`, `lobby`, `view`; a second socket with the token replaces the first with `replaced`; the token is refused after `leave`), two rooms at once, and hostile clients (garbage bytes, junk text, an oversize message, a flood, a half-open handshake) while another room's hand finishes. **The wire judge** (`WireJudge.h`) is the test's only eye, since the game is inside the server: a message to seat S may name the card ids `c<S>_*` and those of its own `view.table`, any other id is a leak. It judges every text frame of every client, and the clients also count masked and binary frames from the server (there must be none). The negative control plays the same hand on `cards-leaky` and fails if the judge finds nothing; the judge's own rule is tested on hand-made texts. The half-open case waits for the handshake timeout (5 s), so that test takes that long. `tools/rooms/smoke.py` plays the same kinds of hands against the executable from another process, with the judge ported to Python (`tools/rooms/CLAUDE.md`).
