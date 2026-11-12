# RoomClient Module Guidelines

## Overview

The client of the rooms (the entry `network-rooms` of the todo store, slice 3 of 4): what a game links to host or join a room on a room server (`Rooms/CLAUDE.md`, `RoomServer/CLAUDE.md`) and to play its seat. Two layers: `IRoomTransport`, one polled connection (the desktop and the browser each have one), and `RoomClient`, the protocol client over it. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomClientModule`). Built in **every** configuration, the web build included; with `ENGINE_WITH_REMOTE_CONTROL` it links `RemoteControlModule` for the desktop transport, and on Emscripten the SDK library `websocket.js` for the browser one (a game that links the module inherits both).

**How a game uses it:** a component owns one `RoomClient`, calls `Poll()` once per frame on the main thread, then `DrainEvents(events)` (a member vector, so it keeps its capacity) and redraws from `GetState()`, `GetLobby()`, `GetView()` and `GetActions()` when an event says they changed. Commands (`Host`, `Join`, `Resume`, `ChooseSeat`, `Start`, `SendAction`, `Leave`, `Disconnect`) are asked of the server; the outcome is an event. A command returns an error itself only when it cannot be sent (`wrong_state`, `transport_unavailable`, `connect_failed`, `unknown_room` for a code that cannot exist, `invalid_session`). The game saves `GetSession()` (room, token, address; **the token is the player's secret**) and calls `Resume()` with it to come back later (see "The session"). The client keeps `view` and `actions` as the server sent them and reads no game.

| File | Purpose |
|------|---------|
| `RoomTransport.h` | `IRoomTransport` (`Open`, `SendText`, `Close`, `Abort`, `PollEvent`), `RoomTransportEvent` (`Opened`, `Text`, `Closed`), `RoomTransportFactory`, `MakeDefaultRoomTransport()` |
| `NativeRoomTransport.h/.cpp` | the desktop transport: a `WebSocketClient` (RemoteControl) behind `IRoomTransport`; empty without `BE_WITH_REMOTE_CONTROL` |
| `EmscriptenRoomTransport.h/.cpp` | the browser transport: the page's WebSocket (`emscripten_websocket_*`) behind `IRoomTransport`; empty without `PLATFORM_WEB` |
| `RoomTransportFactory.cpp` | `MakeDefaultRoomTransport()`: the transport of this platform, nullptr where there is none |
| `RoomClient.h/.cpp` | `RoomClient`, `RoomClientSettings`, `RoomClientState`, `RoomClientEvent`, `RoomSession`, `RoomClientErrors`, `RoomClientCloseReasons` |

## The transport contract

Everything is delivered on the calling thread by `PollEvent()`; **no callback ever runs on a network thread**. One transport serves one connection; after `Closed` the client makes a new one from the factory. Exactly one `Closed` per `Open()` that returned a value (a refused connect included: code 1006 and a reason), always the last event, after every text that arrived before it. `Open` returning an error means no `Closed` follows. The destructor ends the connection without waiting and is safe in every state. `PollEvent` overwrites the event it is given, so its buffers keep their capacity, and does not allocate when there is nothing to take.

**Desktop (`NativeRoomTransport`).** `ws://host[:port][/path]` only: a `wss://` address makes `Open` fail with an error that starts with `tls_unsupported` and names the browser build as the way to `wss://` today (the command returns it as `connect_failed`). No `Origin` header is sent, so a server whose `allowedOrigins` names only web pages still admits it. The limits and the threads are `WebSocketClientSettings` defaults (`RemoteControl/CLAUDE.md`); `PollEvent` copies into the caller's event. The Windows branch of `Socket::Connect` is written and not run on the development machine.

**Browser (`EmscriptenRoomTransport`).** **Main thread only**: the callbacks of `emscripten_websocket_*` run in the page's event loop on its main thread (also in a threaded page, where the registration is proxied there) and push into a member queue without a lock, so `Open` and every other call must come from the thread of the game's frame; the debug build asserts it. The address goes to the browser as it is after a plain check (scheme `ws://` or `wss://`, no space or control character, 2048 bytes); `wss://` is the browser's own TLS, and an `https://` page may open `wss://` only. The page's `Origin` is sent by the browser, so the server's `--origin` list has to name it. The browser tells a script nothing of the closing handshake or of the server's pings, and nothing of why a connect failed: a refused connect is `Closed` 1006 with the reason `connection failed` (`connection lost` after it was open). A script may close with 1000 or 3000..4999 only, so a binary message or one over `kMaxServerMessageBytes` ends the connection with 1000 and is reported as `Closed` 1003 / 1009; `Abort` is the same with 1006. A text is NUL-terminated by the library and `numBytes` counts the NUL; the transport copies it before the callback returns. `SendText` refuses a text with a NUL and one that would put more than 1 MiB into the browser's send buffer. **Checked by compile and link only**: `RoomClientModule` builds with emsdk 4.0.23 (threaded and single-thread) and a game executable that links it links `websocket.js` (`libwebsocket.js` read for the facts above); no browser ran it.

## States and events

| State | Meaning |
|-------|---------|
| `Disconnected` | no connection |
| `Connecting` | the transport is opening; the first message (`create_room` or `join_room`) goes out when it opens |
| `Connected` | open, no room: before `joined`, after `left`, after an `error` that answered `create_room` / `join_room` (the same connection serves another `Host` / `Join` to the same address) |
| `InRoom` | `joined` came; `lobby` and `view` follow |
| `Reconnecting` | the connection of a room was lost, or `Resume()` is under way: waiting for the backoff, or a new connection is opening or waiting for `joined` |

Events (`DrainEvents`): `Connected`, `Joined`, `LobbyChanged`, `ViewChanged`, `Error {code, message, ref}`, `Left {code = reason}`, `ConnectionLost {closeCode, message, willRetry}`, `Resumed`, `Closed {code, closeCode, message}`. A `lobby` or `view` that is announced and not yet taken is not announced again (the getters hold the latest); the queue holds at most `kMaxQueuedRoomClientEvents`, the oldest is dropped. A text that is not a well-formed server message is dropped with a log line and an `Error bad_server_message`; a message of a type this client does not know is ignored (a newer server). A `lobby` or `view` outside the room, or for another room code, is dropped.

- **Which seat is mine.** `joined` names it, and after the start every `view` does; the `lobby` message does not say who the receiver is. So `ChooseSeat(n)` takes seat `n` at once and goes back to the old seat if the server answers with an error that has `ref: choose_seat`; the next `view` is the truth. `IsHost()` is `lobby.host == my seat`.
- **Stale actions.** `SendAction` stamps the version of the last `view`; a `stale_action` error and the fresh `view` come as events and the client never resends.
- **Leave and Disconnect.** `Leave()` asks the server (`left` follows, the connection stays open, the session is forgotten at once); while Reconnecting there is no connection to ask on, so it is `Disconnect()` plus the forgotten session. `Disconnect()` closes in any state, stops any retry, queues `Closed` at once and lets the transport finish in the background (it is polled until its own `Closed`, then freed); a new `Host` does not wait for it. It keeps the session. A connection that ends and is not brought back keeps the session (the seat is held for the grace) but forgets the room, lobby and view.

## Heartbeat and reconnect

Time is the `now` of `Poll(now)`; a command knows no time, so a clock that a command starts (the connect timeout, the first loss of a `Resume`) is armed by the next `Poll`. The numbers are `RoomClientSettings` (a test passes small ones).

- **Heartbeat.** The room protocol `ping` / `pong`, not WebSocket's: a browser answers the server's pings itself and shows a script nothing of them. After `pingInterval` (10 s) without any text a `ping` goes out, one at a time (the next waits for something to arrive); after `silenceLimit` (35 s) without any text the connection is lost and aborted. **Any** text is life, one that does not parse too. Both run whenever the transport is open, also while an attempt waits for `joined`. 0 turns either off.
- **Lost.** The transport's `Closed` outside our own `Disconnect()`, silence, or a connect that did not open within `connectTimeout` (15 s). In a room it is `ConnectionLost {willRetry}` and the state `Reconnecting`; the room, lobby and view stay readable (the game may show them frozen) while commands that need the room are `wrong_state`. Without a room (`Connecting`, `Connected`) there is no reconnect: the attempt ends with `Closed` (`connect_timeout` for the timeout), and the game shows it.
- **Attempts.** After a loss the first new connection is `backoffStart` (0.5 s) later, then it doubles per failed attempt up to `backoffMax` (4 s) and stays there; the transport is made by the factory again and `join_room {code, token}` goes out when it opens. No `Connected` event: the player was told once with `ConnectionLost`. `joined` is `Resumed` and the lobby and view are replaced by the ones that follow (a returning client gets `joined`, `lobby`, `view`). A seat that was held is the same seat; one a computer played comes back to the player at once (`Rooms/CLAUDE.md`).
- **Where it stops** (event `Closed`, state `Disconnected`; the code is in `RoomClientCloseReasons`):

| Cause | `Closed.code` | Session | Why it stops |
|-------|---------------|---------|--------------|
| `error` `bad_token` / `unknown_room` as the answer to the join | `room_gone` | forgotten | the server has no such room or member; asking again gives the same answer |
| any other `error` as the answer to the join | the error's code | kept | not a thing waiting can mend |
| `left {replaced}` (then the server's close) | `replaced` | forgotten | another device took the seat; two devices must not fight over it |
| close code 1008 | the server's reason (`rate_limited`, `too_many_failed_joins`) | kept | the client broke the limits; retrying is the same abuse |
| `reconnectGiveUp` (10 min = the server's abandoned-room time) after the first loss | `gave_up` | forgotten | the room is gone by now |
| an address or platform that cannot connect (`Open` fails) | `connect_failed` or `transport_unavailable` | kept | it does not get better by waiting |
| `Disconnect()`, `Leave()` | empty | `Disconnect` keeps, `Leave` forgets | the player's own decision |

Every other close retries, 1013 (server full) and a normal 1000 from the server included. `reconnect = false` in the settings turns the retry off: a lost connection is `ConnectionLost {willRetry = false}` then `Closed`.

## The session

`RoomSession {url, room, token}` is plain data, valid from `joined` and kept through losses and `Disconnect()`; the game saves it where its platform keeps things (the token is a secret of the player) and forgets it on `Closed` with `room_gone`, `replaced` or `gave_up` (the client has already cleared its own copy). `Resume(session)` makes the state `Reconnecting` at once, so the retry and stop rules above apply to a restart as they do to a drop, and a server that is not up yet is waited for. A session that cannot be one (no address, a code that is not a room code, a token that is not 32 hex characters) is `invalid_session` and makes no connection; a server that no longer has the room gives `Closed {room_gone}`. A second device that resumes the same session replaces the first (`replaced` above).

## Allocations

`Poll()` with nothing to do allocates nothing: the polled event is a member, the event queue keeps its capacity, the clocks are plain values, and the transports' `PollEvent` does not allocate when empty. A message that arrives allocates, as any parse does, and so does the `ping` that goes out every ten quiet seconds. `RoomClientTest.PollingWithNothingToDoAllocatesNothing` counts the allocations of the thread in the profile build (`ENGINE_PROFILER_ALLOC`, where the global `operator new` is replaced); the other builds keep the capacity check.

## Tests

`tests/BECore/Rooms/FakeRoomTransport.h` is the scripted transport (a link the test pushes server texts into and reads what was sent from); `RoomClientTest.cpp` drives the client over it on a `ManualClock`, so the heartbeat, the backoff times and every stop rule are exact to the millisecond. `HubTransport.h` is an `IRoomTransport` that talks to a real `RoomHub` in memory (`HubWorld`: the demo games, a manual clock, `Sever`, `SetRefusing`, a `blackhole` for a silent network) and `JudgedTransport.h` runs the wire judge on every text a client receives; `RoomClientHubTest.cpp` plays whole hands of `DemoCardGame` with four clients, drops one inside and past the grace, and has the negative control on `cards-leaky`.

`RoomClientSocketTest.cpp` is the same over the native transport and a real `RoomWebSocketServer` on port 0: hands with two and with four humans, the negative control, a connection aborted mid-hand (the client reconnects by itself), a client kept away past the grace (the server's clock is manual: a computer plays the seat, the client takes it back with its token), `Resume` from a new object, a server that stops (the client retries and gives up at a small `reconnectGiveUp`), `wss://`, a port nobody listens on, a server that admits only a web page's `Origin`, and the heartbeat `ping` answered by `pong`. The clients run on the real clock with a short backoff, so a test waits for the real reconnect (the server's computers use the server's clock: with a manual one a `computerDelay` above 0 never elapses, so only the test that needs the grace uses it). The suite is run 20 times in a row when it changes (`--gtest_repeat=20`).
