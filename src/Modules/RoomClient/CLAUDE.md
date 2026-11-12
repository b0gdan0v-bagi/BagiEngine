# RoomClient Module Guidelines

## Overview

The client of the rooms (the entry `network-rooms` of the todo store, slice 3 of 4): what a game links to host or join a room on a room server (`Rooms/CLAUDE.md`, `RoomServer/CLAUDE.md`) and to play its seat. Two layers: `IRoomTransport`, one polled connection (the desktop and the browser each have one), and `RoomClient`, the protocol client over it. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomClientModule`). Built in **every** configuration, the web build included; with `ENGINE_WITH_REMOTE_CONTROL` it links `RemoteControlModule` for the desktop transport.

**How a game uses it:** a component owns one `RoomClient`, calls `Poll()` once per frame on the main thread, then `DrainEvents(events)` (a member vector, so it keeps its capacity) and redraws from `GetState()`, `GetLobby()`, `GetView()` and `GetActions()` when an event says they changed. Commands (`Host`, `Join`, `ChooseSeat`, `Start`, `SendAction`, `Leave`, `Disconnect`) are asked of the server; the outcome is an event. A command returns an error itself only when it cannot be sent (`wrong_state`, `transport_unavailable`, `connect_failed`, `unknown_room` for a code that cannot exist). The game saves `GetSession()` (room, token, address; **the token is the player's secret**) to come back later. The client keeps `view` and `actions` as the server sent them and reads no game.

| File | Purpose |
|------|---------|
| `RoomTransport.h` | `IRoomTransport` (`Open`, `SendText`, `Close`, `Abort`, `PollEvent`), `RoomTransportEvent` (`Opened`, `Text`, `Closed`), `RoomTransportFactory`, `MakeDefaultRoomTransport()` |
| `RoomTransportFactory.cpp` | `MakeDefaultRoomTransport()`: the transport of this platform, nullptr where there is none |
| `RoomClient.h/.cpp` | `RoomClient`, `RoomClientSettings`, `RoomClientState`, `RoomClientEvent`, `RoomSession`, `RoomClientErrors` |

## The transport contract

Everything is delivered on the calling thread by `PollEvent()`; **no callback ever runs on a network thread**. One transport serves one connection; after `Closed` the client makes a new one from the factory. Exactly one `Closed` per `Open()` that returned a value (a refused connect included: code 1006 and a reason), always the last event, after every text that arrived before it. `Open` returning an error means no `Closed` follows. The destructor ends the connection without waiting and is safe in every state. `PollEvent` overwrites the event it is given, so its buffers keep their capacity, and does not allocate when there is nothing to take.

## States and events

| State | Meaning |
|-------|---------|
| `Disconnected` | no connection |
| `Connecting` | the transport is opening; the first message (`create_room` or `join_room`) goes out when it opens |
| `Connected` | open, no room: before `joined`, after `left`, after an `error` that answered `create_room` / `join_room` (the same connection serves another `Host` / `Join` to the same address) |
| `InRoom` | `joined` came; `lobby` and `view` follow |

Events (`DrainEvents`): `Connected`, `Joined`, `LobbyChanged`, `ViewChanged`, `Error {code, message, ref}`, `Left {code = reason}`, `Closed {closeCode, message}`. A `lobby` or `view` that is announced and not yet taken is not announced again (the getters hold the latest); the queue holds at most `kMaxQueuedRoomClientEvents`, the oldest is dropped. A text that is not a well-formed server message is dropped with a log line and an `Error bad_server_message`; a message of a type this client does not know is ignored (a newer server). A `lobby` or `view` outside the room, or for another room code, is dropped.

- **Which seat is mine.** `joined` names it, and after the start every `view` does; the `lobby` message does not say who the receiver is. So `ChooseSeat(n)` takes seat `n` at once and goes back to the old seat if the server answers with an error that has `ref: choose_seat`; the next `view` is the truth. `IsHost()` is `lobby.host == my seat`.
- **Stale actions.** `SendAction` stamps the version of the last `view`; a `stale_action` error and the fresh `view` come as events and the client never resends.
- **Leave and Disconnect.** `Leave()` asks the server (`left` follows, the connection stays, the session is forgotten at once). `Disconnect()` closes in any state, queues `Closed` at once and lets the transport finish in the background (it is polled until its own `Closed`, then freed); a new `Host` does not wait for it. A connection that ends keeps the session (the seat is held for the grace) but forgets the room, lobby and view.

## Allocations

`Poll()` with nothing to do allocates nothing: the polled event is a member, the event queue keeps its capacity, and the transports' `PollEvent` does not allocate when empty. A message that arrives allocates, as any parse does.

## Tests

`tests/BECore/Rooms/FakeRoomTransport.h` is the scripted transport (a link the test pushes server texts into and reads what was sent from); `RoomClientTest.cpp` drives the client over it.
