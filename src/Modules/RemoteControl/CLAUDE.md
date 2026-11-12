# RemoteControl Module Guidelines

## Overview

The WebSocket transport. The server half is one reusable piece, `WebSocketListener` (accept thread, connections, limits, handshake policy), which knows nothing about what a message means. The control API's server (`BECore/Control/CLAUDE.md`) is that listener plus a few lines of glue: a server on `127.0.0.1` that turns every text message of a client into `ControlService::Post()` and sends the responses and the pushed events back; the text goes through as it is, the checks of the request are the control service's. The control API is a development tool (a model or a program plays a game), not player networking: the module also holds the server half that the room server (`RoomServer`) uses for players. The control API is off by default; `Application` starts it when the game is run with `--control` (flags and attributes: "Starting the API" in `Control/CLAUDE.md`).

The module is built only with `ENGINE_WITH_REMOTE_CONTROL` (default ON; `OFF` in the web build): without it `Application` has no server and a game started with `--control` stops with `Control API: not available in this build`.

Module order: after `TaskSystem`, before `SDL`. It links `BECoreModule` and `EventsModule` only (and `ws2_32` on Windows); `RuntimeModule` links it. No static initializers, so it needs no `WHOLE_ARCHIVE`.

| File | Purpose |
|------|---------|
| `Sha1` | `Sha1()`, only for the handshake |
| `WebSocketHandshake` | `ParseHandshakeRequest(text, HandshakePolicy)` (RFC 6455 section 4.2 plus the loopback checks or the origin allow-list), `IsOriginAllowed()`, `ComputeAcceptKey()`, `BuildAcceptResponse()`, `BuildRejectResponse()`, the header-text helpers both sides share (`HeaderHasToken()` ...) |
| `WebSocketUrl` | `ParseWebSocketUrl()` (`ws`/`wss`, name or IPv4 literal, IPv6 refused, no fragment or userinfo) and `GetHostHeader()`; the client side, no sockets |
| `WebSocketClientHandshake` | the client side of the opening handshake, no sockets: `GenerateClientKey()`, `BuildClientHandshakeRequest()` (`Origin` only when given), `CheckServerHandshakeResponse()` (101, `Upgrade`, `Connection`, the accept key, no extension or sub-protocol the request did not ask for) |
| `WebSocketFrame` | `EncodeFrame()` and friends, `WebSocketFrameParser` (bytes in, messages out; reads a client's masked frames by default, `WebSocketPeer::Server` makes it read a server's unmasked ones, the client role) |
| `Socket` | `Socket`: move-only IPv4 TCP socket, POSIX or Winsock behind one interface; `Listen(port, SocketBind)` binds the loopback (default) or every address (backlog 128 instead of 8), `Accept(timeout, &peer)` tells the client's address as dotted text; `Connect(host, port, timeout, cancel)` resolves a name or IPv4 literal (`getaddrinfo`, blocking) and waits for the connect in `kWakeInterval` slices that look at the `cancel` flag and the deadline (errors `cancelled`, `timed out`; the text names `host:port`), `Connect(port)` is the loopback shortcut |
| `WebSocketConnection` | one client: the socket, the send queue with its byte cap, the handshake, `QueueText/Ping/Pong/Close`, `Abort()`, the id, the peer address and a received-bytes counter |
| `WebSocketListener` | the accept thread, the connections and their limits (total, per address), the reader loop (ping, pong, close, binary and protocol errors are answered here); a `WebSocketHandler` (`onOpen`, `onMessage` for text, `onClosed`) gets the rest; `Start(port)`, `Stop()`, `ForEachOpenConnection()` |
| `WebSocketServer` | the control API's server: a listener (loopback, 4 connections, 1 MiB messages, 16 MiB outbox) whose `onMessage` is `ControlService::Post()` and whose event sink is a broadcast |

The handshake, the frame parser and `Sha1` are pure functions and classes with no sockets: that is where the protocol rules are tested.

## Security

The control API lets any client play, reset and quit the game, so by default (`HandshakePolicy::loopbackOnly`) the server is closed to everything but the person at this machine:

- It binds `127.0.0.1` only. There is no token: any local process can connect, which is accepted for a tool that is off by default.
- `Host` has to be `127.0.0.1`, `localhost` or `[::1]` (a port is allowed). A web page of another site whose name resolves to 127.0.0.1 (DNS rebinding) sends its own name there and gets a 403.
- `Origin`, when the request has one, has to be an `http://` or `https://` page of the loopback. Browsers always send it, programs do not, so a page from the internet cannot open the connection, a script can. The `null` origin of a `file://` page is refused.
- At most `WebSocketServer::kMaxConnections` (4) connections, handshakes included; the next client gets a 503. A client has `kHandshakeTimeout` (5 s in all, one deadline, not a timeout per read) to finish its handshake and `kMaxHandshakeBytes` (8 KiB) for its request head.
- **The second mode, `loopbackOnly = false`, is for a public server** (the room server: it is not a tool of the person at this machine and has no cookies, so the secret is its room token, not the page). `Host` is not checked beyond being present (behind a proxy the public name is not known, and DNS rebinding gains nothing without ambient credentials). `Origin` is what keeps a foreign web page out: when present it has to equal an entry of `HandshakePolicy::allowedOrigins` (`IsOriginAllowed()` in the header: scheme and host in any case, `https://a.com` equals `https://a.com:443`, an empty path or `/` only, no `userinfo@`, no suffix or wildcard match; `*` allows all, for development; `null` only with its own entry). An empty list refuses every browser and admits programs, which send no `Origin`, so **`Origin` does not protect against a program**, only against pages; `allowMissingOrigin = false` refuses those too. A 403 names the refused origin (cut to 100 bytes, printable ASCII) so the operator can fix the list. Every other check of the handshake applies in both modes.
- A message is at most `ControlService::kMaxRequestBytes` (1 MiB). The frame parser checks the length announced in the frame header, so a client cannot make the server buffer more.

## Protocol

One text message is one request, and one response is one text message, in JSON (`Control/CLAUDE.md`, "Protocol"). Pushed events are text messages as well.

- Client frames have to be masked (RFC 6455), RSV bits zero, control frames at most 125 bytes and not fragmented. A text message may be fragmented; a ping may come between fragments and is answered at once. A violation closes the connection with 1002, a message over the limit with 1009.
- A binary message closes the connection with 1003.
- A close frame is answered with a close frame carrying the same code, and the connection ends. The server sends no pings of its own.
- Responses of one connection keep the order of its requests, because `ControlService` runs them in order and one writer thread sends them in order. Events go to every open connection between them.
- **Deviation from RFC 6455 (section 5.6):** the parser does not check that a text message is UTF-8. Text that is not valid JSON, invalid UTF-8 included, is answered with `parse_error` by the control service, and the connection stays open.
- The request target of the handshake (`GET /control`) is not used: any path opens the same endpoint. The end of the request head is `\r\n\r\n`.

## Threads

- The listener's: one accept thread. `Accept()` is a 50 ms poll, because closing or shutting down a listening socket does not wake a blocked `accept()` on every system; `Stop()` waits for it for at most that long.
- Per connection a **reader** thread (handshake, frame parsing, then the handler: for the control server `ControlService::Post()`) and, after the handshake, a **writer** thread that sends from a queue. The handler runs on the reader thread, so its calls for one connection never overlap and `onClosed` runs once per connection for which `onOpen` ran, after the writer stopped (also when `Stop()` ended it). Nothing else sends: a response is queued by the `ReplyFn`, which holds a `shared_ptr` of the connection, so it is safe when the client is long gone.
- A client that does not read fills its queue; at the cap (`maxOutboxBytes`; 16 MiB for the control server) the connection is dropped, and nobody else is slowed down.
- Connection ids are a counter of the listener, never reused (not even after a restart). The limits count connections that have not finished, handshakes included; a refused client gets a 503 whose text is a setting (the default names no product). `maxConnectionsPerAddress` 0 is off.
- **Winsock:** `shutdown()` does not wake a thread blocked in `recv()` or `send()` there (only the peer closing, or `closesocket()` from another thread, does; a plain `closesocket()` races with the handle). So a connected `Socket` is non-blocking on Windows, `Receive()` and `SendAll()` wait with `WSAPoll` in slices of `Socket::kWakeInterval` (50 ms) and look at the flag that `ShutdownBoth()` sets, and `Receive()` keeps its timeout itself (`SetReceiveTimeout`). On POSIX the calls block and `shutdown()` wakes them. The interface and the listener are the same on both.
- `Stop()` shuts the sockets down (which wakes every blocked thread) and joins all threads; no handler function runs after it returns, so a handler function must not call it, and a handler that blocks has to be woken by its owner before `Stop()`. Call the control server's `Stop()` **before** `ControlService::Shutdown()`; `Application::Cleanup()` does. The event sink is removed first.
- `Start()` and `Stop()` belong to one thread (for the control server the main thread, like everything of `ControlService` except `Post()`).
- Logging from these threads is fine (the ring buffer takes entries from any thread); the lines show up at the next frame's flush.

## Using it

```bash
build/macos-debug/bin/BagiMatch3 --control                  # ws://127.0.0.1:8765 (add --control-port=N)
.venv/bin/python -c "
import sys; sys.path.insert(0, 'tools/control')
from bagi_control_client import BagiControlClient
with BagiControlClient() as c: print(c.call('get_state'))"
```

`tools/control/bagi_control_client.py` is a client with the Python standard library only (`call(method, params)`, pushed events in `events`, `ControlError` for an error response). `websocat ws://127.0.0.1:8765` works too: type `{"id":1,"method":"ping"}`. A model reaches the API through the MCP server `tools/control/bagi_mcp.py`, a program through `bagi_control_client.py` (a game repository adds its own tools on top of it, like the Stockfish bridge of BagiEngine-Chess): they are separate processes over this WebSocket (`tools/control/CLAUDE.md`). Check what listens with `lsof -nP -iTCP:8765 -sTCP:LISTEN`.

## Tests

- `tests/BECore/Sha1Test.cpp` (FIPS 180 vectors, the lengths around the block edges), `WebSocketHandshakeTest.cpp` (the key of RFC 6455, every refusal, `Host` and `Origin`, the origin allow-list with its look-alikes), `WebSocketFrameTest.cpp` (the examples of RFC 6455 section 5.7, lengths, fragments, every protocol violation, the limits).
- `tests/BECore/SocketTest.cpp`: `Socket` alone on a loopback pair (a `Receive()` and a `SendAll()` that wait are woken by `ShutdownBoth()`, the receive timeout, data and the end of the stream).
- `tests/BECore/WebSocketTestClient.h`: the raw client shared by the transport tests (`Socket` plus hand-written frames; a `WaitPump` runs while it waits).
- `tests/BECore/WebSocketListenerTest.cpp`: the listener with a recording handler (order of the calls, `onClosed` once however a connection ends, the limits, 1009, the public policy, `ForEachOpenConnection`, `Stop()`, ids, the byte counter).
- `tests/BECore/WebSocketServerTest.cpp`: the control server on a free port against that client. The test thread plays the main thread and calls `Drain()` while it waits. Covers handshake, pipelining and order, garbage, close, binary, oversize, foreign `Origin` and `Host`, the connection limit and the slot being freed, events, `Stop()` with open and half-open connections, a client that leaves with a request pending.
- `tools/control/tests/test_frames.py` (pytest, the frames of the Python client), `tools/control/tests/smoke.py` (starts a game executable with `--control` and plays a few moves; the ctest entry `BagiMatch3ControlSmoke` from `be_add_game_control_tests`, `.claude/rules/cmake-rules.md`; a game with its own scenario calls `run_smoke()` from its script, as `tests/control_smoke.py` of BagiEngine-Chess does for `BagiChessControlSmoke`). The smoke test runs the game `--headless`, once on the real clock and once with `--manual-clock`, and looks at screenshots (decoded by `tools/control/tests/png_reader.py`, tested in `test_png_reader.py`); `--window` runs it in a real window instead. Recorded sessions (scenario files, `tools/control/CLAUDE.md`) run as `BagiMatch3Scenario.<file>` from `be_add_game_scenario_tests` over `games/Match3/tests/scenarios/*.jsonl`.

The `SocketBind::AnyAddress`, peer-address and client-connect code of `Socket.cpp` (`INADDR_ANY`, `accept()` with an address, `inet_ntop`, `getaddrinfo`, the non-blocking `connect` with `WSAPoll`) was written for Windows as well but not run there; the POSIX branch is tested on macOS.

**Checked on Windows** (Windows 11, MSVC Debug, preset `windows-vs2022-debug`): `Sha1Test`, `WebSocketFrameTest`, `WebSocketHandshakeTest`, `SocketTest` and `WebSocketServerTest` (20 repeats in a row, and each case in its own process under ctest), `ControlServiceTest`, `SDLCaptureTest`, the pytest suite of `tools/control/tests` (`.venv\Scripts\python -m pytest`, nothing skipped, with `mcp` 2.x) and the control smoke of `BagiMatch3` (`--control`, `--headless`, `--manual-clock` through the `WinMain` start-up, `ws2_32`; 3 runs in a row and `ctest -R BagiMatch3ControlSmoke`). `WebSocketServerTest` and the control smoke ran before the server moved into `WebSocketListener` and `WebSocketConnection`; on the moved code, and for the room server (`WebSocketListenerTest`, `RoomWebSocketServerTest`, `BagiRoomServer`), nothing has run on Windows yet. The one Winsock difference is under "Threads". Not checked: `smoke.py --window` (a real window), and the Vulkan backend on Windows.
