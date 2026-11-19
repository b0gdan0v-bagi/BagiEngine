# RemoteControl Module Guidelines

## Overview

The WebSocket transport, both halves; it knows nothing about what a message means.

- **Server half:** `WebSocketListener` (accept thread, connections, limits, handshake policy). The control API's server (`WebSocketServer`) is a listener on `127.0.0.1` whose `onMessage` is `ControlService::Post()` (the text passes as it is; checking the request is the control service's, `BECore/Control/CLAUDE.md`) and whose event sink is a broadcast. The control API is a development tool on loopback, not player networking; it is off by default and `Application` starts it with `--control` (flags: `Control/CLAUDE.md`, "Starting the API").
- **Client half:** `WebSocketClient` (`ws://`, `wss://` through `Tls/`, `Tls/CLAUDE.md`).
- Players use the same transport: `RoomServer` takes the server half, a game's `RoomClient` the client half.

Built only with `ENGINE_WITH_REMOTE_CONTROL` (default ON, OFF on the web); without it `--control` stops the game with `Control API: not available in this build`. Module order: after `TaskSystem`, before `SDL`; links `BECoreModule` and `EventsModule` only (plus the platform socket and TLS libraries); no static initializers, so no `WHOLE_ARCHIVE`.

| File | Purpose |
|------|---------|
| `Sha1` | only for the handshake |
| `WebSocketHandshake` | server side of the handshake under a `HandshakePolicy`, health check, responses, header helpers both sides share |
| `WebSocketUrl` | `ParseWebSocketUrl()` (IPv6 refused), `GetHostHeader()` |
| `WebSocketClientHandshake` | client side of the handshake |
| `WebSocketHttpHead` | `ReadHttpHead()`: reads an HTTP head under one deadline, for both sides of the handshake |
| `WebSocketFrame` | `EncodeFrame()`, `WebSocketFrameParser` (masked client frames by default; `WebSocketPeer::Server` reads a server's) |
| `Socket` | move-only IPv4 TCP socket, POSIX or Winsock behind one interface; `Listen` on every address (`SocketBind::AnyAddress`) uses a larger backlog than on the loopback; `Connect` does a blocking `getaddrinfo`, then connects in `kWakeInterval` slices that watch `cancel` and the deadline |
| `WebSocketConnection` | one client: socket, capped send queue, handshake, id, addresses |
| `WebSocketListener` | accept and refusal threads, connections and their limits, reader loop (ping, pong, close, binary and protocol errors handled here); a `WebSocketHandler` (`onOpen`, `onMessage`, `onClosed`) gets the rest |
| `WebSocketServer` | the control API's server (limits are its `k*` constants) |
| `WebSocketClient` | `Open(url, settings)`, `SendText()`, `Close()`, `Abort()`, `PollEvent()`; limits in `WebSocketClientSettings`; without a TLS backend `wss://` is refused with an error starting `tls_unsupported` |
| `Tls/` | `ITlsStream`, `ClientStream`, the backends: `Tls/CLAUDE.md` |

Handshake, frame parser and `Sha1` have no sockets: protocol rules are tested there.

## Security

The control API can play, reset and quit the game, so the default `HandshakePolicy::loopbackOnly` admits only the person at this machine:

- Binds `127.0.0.1` only. No token: any local process can connect, accepted for a tool that is off by default.
- `Host` must be `127.0.0.1`, `localhost` or `[::1]` (port allowed): defeats DNS rebinding (403).
- `Origin`, when present, must be an `http(s)://` loopback page. Browsers always send it, programs do not, so a web page cannot connect and a script can. The `null` origin (`file://`) is refused.
- At most `WebSocketServer::kMaxConnections`, handshakes included (503 beyond). `kHandshakeTimeout` is one deadline for the whole handshake, not per read; `kMaxHandshakeBytes` caps the request head.
- **Public mode (`loopbackOnly = false`), for the room server**: its secret is the room token, not the page, and it has no cookies. `Host` is only required to be present (behind a proxy the public name is unknown; rebinding gains nothing without ambient credentials). `Origin`, when present, must equal an entry of `allowedOrigins` (`IsOriginAllowed()`: scheme and host case-insensitive, default port equal to none, empty path or `/` only, no `userinfo@`, no suffix or wildcard match; `*` allows all, for development; `null` only by its own entry). An empty list refuses every browser and admits programs: **`Origin` protects against pages, never against a program**; `allowMissingOrigin = false` refuses those too. Refusals quote the offending `Host` / `Origin` through `QuoteForMessage()` (printable, bounded) so the operator can fix the list; the client quotes a server's status line the same way. `healthPath` (public mode, empty = off) answers a plain `GET <path>` without `Upgrade` with `200 ok` before `Host` and `Origin` are checked and with no `onOpen`; with `Upgrade` it is a normal handshake. All other handshake checks apply in both modes.
- **`trustedProxyHops` = N (0 = off)**: behind a proxy every socket has the proxy's address, so the per-address limit would see one client. With N > 0 the client is the N-th entry **from the right** of `X-Forwarded-For` (several headers are one list): the right entries are your proxies', the client's own left entries are never read. Set N only when exactly N appending proxies stand in front (a smaller N believes the client). A missing header, too few entries or a non-IP entry keeps the socket's address. `GetClientAddress()` is the peer until the handshake is accepted, then the forwarded one; with N > 0 the per-address check runs once, after the handshake and before the 101 (503, no `onOpen`), atomically with setting the address, counting admitted connections only.
- A message is at most `ControlService::kMaxRequestBytes`. The parser checks the length announced in the frame header, so a client cannot make it buffer more; after a violation it drops what it held and takes no more bytes. Fuzzed: `tests/Fuzz/CLAUDE.md`.

## Protocol

One text message is one request, one response is one text message, JSON (`Control/CLAUDE.md`, "Protocol"); pushed events are text messages too.

- Client frames must be masked, RSV zero, control frames at most 125 bytes and unfragmented. A text message may be fragmented; a ping between fragments is answered at once. Violation: close 1002; over the limit: 1009; binary message: 1003.
- A close frame is answered with the same code and the connection ends. Neither the listener nor the control server pings; `RoomWebSocketServer` pings silent connections and drops those that do not answer (`RoomServer/CLAUDE.md`, "Threads"), which is why `WebSocketClient` answers a ping at once.
- Responses of one connection keep request order (`ControlService` runs them in order, one writer sends them in order); events go to every open connection between them.
- **Deviation from RFC 6455 5.6:** text is not checked for UTF-8; text that is not valid JSON gets `parse_error` from the control service and the connection stays open.
- The handshake's request target is ignored: any path opens the same endpoint.

## Threads

- **Listener:** one accept thread and one refusal thread. `Accept()` polls in short slices because closing a listening socket does not wake a blocked `accept()` everywhere; `Stop()` waits at most one slice for it. A refused client is answered by the refusal thread (a bounded queue; beyond it a refused socket is closed without an answer), so one that says nothing never holds up the accept loop.
- **Closing is bounded:** a close frame has the listener's `closeTimeout` to reach the client, and the accept loop cuts a connection over it, so a client that does not read cannot keep two threads and a slot.
- **Per connection:** a reader thread (handshake, frames, then the handler) and, after the handshake, a writer thread sending from a queue. The handler runs on the reader, so its calls for one connection never overlap, and `onClosed` runs exactly once per connection that had `onOpen`, after the writer stopped (also when `Stop()` ended it). `WebSocketServer::GetClientCount()` is an atomic raised in `onOpen` and lowered in `onClosed`: valid from any thread, 0 after `Stop()`.
- Only the writer sends. A response is queued by the `ReplyFn`, which holds an `IntrusivePtrAtomic` of the connection (`RefCountedAtomic`: the last holder may be any thread), so replying after the client left is safe.
- A client that does not read fills its queue; at `maxOutboxBytes` it is dropped, and nobody else slows down.
- Connection ids are a listener counter, never reused (not even after a restart). Limits count unfinished connections, handshakes included; the 503 text is a setting (the default names no product); `maxConnectionsPerAddress` 0 is off.
- **Winsock:** `shutdown()` does not wake a thread blocked in `recv()` / `send()` there, and `closesocket()` from another thread races with the handle. So a connected `Socket` is non-blocking on Windows: `Receive()` and `SendAll()` wait with `WSAPoll` in `kWakeInterval` slices and check the flag `ShutdownBoth()` sets, and `Receive()` keeps its timeout itself. On POSIX the calls block and `shutdown()` wakes them; the interface is the same.
- `Stop()` shuts the sockets down (waking every blocked thread) and joins all threads; no handler runs after it returns, so **a handler must not call `Stop()`**, and a handler that blocks must be woken by its owner first. Call the control server's `Stop()` **before** `ControlService::Shutdown()` (`Application::Cleanup()` does; the event sink is removed first). `Start()` and `Stop()` belong to one thread (for the control server, the main thread).
- **Client:** a reader thread (connect and TLS handshake with a cancel flag, opening handshake, read loop) and a writer thread started and joined by the reader. With `wss://`, `handshakeTimeout` is one deadline for lookup, TCP, TLS and the opening request. **Nothing is called back: events (`Opened`, `Text`, `Closed`) are polled with `PollEvent()` on the owner's thread**, so a game component needs no lock.
  - Exactly one `Closed` per successful `Open()`, and it is the last event (1006 with a reason for a refused connect, failed handshake, drop or abort; else the close frame's code). The first recorded reason wins (`Close(code)` over the server's answer, an abort's reason over the read error it causes).
  - The next `Open()` or the destructor aborts, wakes every wait (socket, outbox, inbox, close wait) and joins; a `getaddrinfo` in progress is the one wait it cannot wake.
  - Frames are masked in the writer with a fresh key per frame from one `std::random_device`, used by the reader for the handshake key first and by the writer after, never concurrently.
  - A full inbox stops the reader (pings wait too); the server's queue then fills and the server drops the client at its own cap.
  - After a queued close frame nothing follows; the writer waits `closeTimeout` for the reader to end, then aborts.
- Logging from these threads is fine; lines appear at the next frame's flush.

## Using it

```bash
build/macos-debug/bin/BagiStress --control                  # ws://127.0.0.1:8765 (add --control-port=N)
.venv/bin/python -c "
import sys; sys.path.insert(0, 'tools/control')
from bagi_control_client import BagiControlClient
with BagiControlClient() as c: print(c.call('get_state'))"
```

Clients (Python client, MCP server, smoke and scenario tests): `tools/control/CLAUDE.md`. `websocat ws://127.0.0.1:8765` works too. What listens: `lsof -nP -iTCP:8765 -sTCP:LISTEN`.

## Tests

`tests/BECore/`: `Sha1Test`, `WebSocketHandshakeTest`, `WebSocketFrameTest` (pure), `SocketTest`, `WebSocketListenerTest`, `WebSocketClientTest`, `WebSocketServerTest` (on free ports; `WebSocketTestClient.h` is the shared raw client), `Tls/` (`Tls/CLAUDE.md`); the `wss://` cases of `WebSocketClientTest` run over the fake TLS backend. `WebSocketServerTest`'s thread plays the main thread and calls `Drain()` while it waits.

**Not yet run on Windows:** the client and `AnyAddress` / peer-address paths of `Socket.cpp`, `WebSocketClient`, `WebSocketListener` / `WebSocketConnection` and the room server; never checked there: `smoke.py --window` and the Vulkan backend. Schannel: `Tls/CLAUDE.md`.
