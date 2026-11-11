# RoomServer Module Guidelines

## Overview

The `Rooms` hub on the network (the entry `network-rooms` of the todo store, slice 2 of 4): a WebSocket server that feeds every text message of a client to `RoomHub` and sends what the hub says back. `Rooms` is the logic and the protocol (`Rooms/CLAUDE.md`), `RemoteControl` the transport (`WebSocketListener`, `RemoteControl/CLAUDE.md`); this module is the glue between them and knows no game. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomServerModule`; `BagiEngineTests` links it).

Built only with `ENGINE_WITH_REMOTE_CONTROL` (the web build has no sockets). Module order: after `Rooms`.

Not here yet (later WPs and slices): liveness pings, the demo game and the executable `BagiRoomServer`, the client side, hosting.

| File | Purpose |
|------|---------|
| `RoomServerSettings.h` | `RoomServerSettings`: port, bind scope, handshake policy, transport limits, the hub's `RoomHubSettings`, the tick interval, the injected clock and `RandomFn` |
| `RoomWebSocketServer.h/.cpp` | `RoomWebSocketServer(registry, settings)`: `Start()`, `Stop()`, `GetPort()`, `IsRunning()` |

## Threads

- The listener's threads (`Room accept`, a reader and a writer per connection) are `RemoteControl/CLAUDE.md`, "Threads". The reader threads call the handler: `onOpen` puts the connection in a map by id, then every one of `onOpen`, `onMessage` and `onClosed` pushes an event (`Opened`, `Message`, `Closed`) into the queue.
- **One hub thread** (`Room hub`) owns the `RoomHub`, which is single-threaded: the hub is a local of the thread function, so nothing else can reach it. It waits for an event or for `tickInterval`, takes the whole queue, feeds the hub in the order of the queue, then ticks it when the interval has passed (so a flood cannot starve the tick). The tick interval is real time; the time the hub sees is `settings.clock`.
- **The queue is bounded**: `kMaxQueuedEvents` (4096) events and `kMaxQueuedBytes` (4 MiB) of text. A reader that finds it full waits in `PushEvent`, so it stops reading its socket, TCP slows the sender and memory stays bounded; the other connections are only as slow as the hub. `Stop()` wakes the waiting readers first (they return false and end their connection), because the listener joins them.
- The `HubSink` runs on the hub thread. `send` looks the connection up (a map under a mutex of the server) and queues a text frame: it never writes to a socket, so a slow client cannot stall the hub, and a client whose queue passes `maxOutboxBytes` is dropped by its connection (the hub learns it from the `Closed` event). `close` queues a close frame. For one connection the frames go out in the order the hub produced them. A message for a connection that is gone is dropped.
- A connection is reported to the hub (`Opened`) **after** its handshake succeeded; a socket that never completes it is cut by the handshake timeout (5 s) and the hub never hears of it. The hub's `Closed` handling ignores a connection it closed itself.
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
| handshake | 5 s, request head 8 KiB |

Two threads per connection, so about 1200 threads at the limit: accepted for a server of friends.

## Origin

In public mode (`policy.loopbackOnly = false`) `Origin` keeps foreign **web pages** out of the server: a browser always sends it, and a page that is not in `policy.allowedOrigins` gets a 403. It does not protect against a program, which sends no `Origin` (or any it likes); what protects a room is its code and the token of a member. An empty list refuses every browser. Details: `RemoteControl/CLAUDE.md`, "Security".

## Tests

`tests/BECore/Rooms/RoomWebSocketServerTest.cpp`: the server on port 0 against real sockets (`RoomSocketClient.h`: the raw client of `WebSocketTestClient.h` plus the messages of the protocol, with a cursor to read forward) and `FakeSeatedGame`. Covers the exchange (create, join by code, order of replies), what the hub's close and the transport's limits look like on the wire, an abrupt drop, the origin and per-address checks, `Stop()` under load, a client that never reads, two servers. The bounded queue itself has no test of its own.
