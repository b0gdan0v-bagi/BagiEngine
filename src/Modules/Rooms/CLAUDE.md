# Rooms Module Guidelines

## Overview

Online play in rooms by code, with an authoritative server (the entry `network-rooms` of the todo store, slice 1 of 4). The server owns the game (`ISeatedGame`, `Games/CLAUDE.md`, "Seats"); every client is a seat and sees only what the game says that seat may see. This module is **pure code**: JSON text in, JSON text out, no sockets, no clock, no randomness of its own. A transport (a later slice) feeds it. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomsModule`; `BagiEngineTests` links it with `LINK RemoteControlModule RoomsModule RuntimeModule`).

Built so far (slice 1, work package 4 of 6): the protocol, room codes and tokens, the limits and the rate limiter, and the whole life of `Room` (lobby, start, play, computers, absence and return, leaving, closing). `RoomHub` (the connection-to-room dispatcher) comes in the next work package and is not written yet; this file grows with it.

## Files

| File | Purpose |
|------|---------|
| `RoomTypes.h/.cpp` | `ConnectionId`, `PlayerId`, `RoomTime` (`steady_clock::time_point`, always passed in), `RandomFn` (`uint64_t()`, injected), `MakeSecureRandom()` (`std::random_device`), `RoomPhase` (`Lobby`, `Playing`, `Finished`), `SeatKind` (`Human`, `Computer`, `Empty`) and their wire names |
| `RoomLimits.h` | `RoomLimits::k*` (the constants of the table below) and `RoomHubSettings` (the same values as defaults, so a test overrides them) |
| `RoomErrors.h` | `RoomErrors::k*`, the stable error codes |
| `RoomProtocol.h/.cpp` | `ClientMessage` (a `std::variant` of the seven client messages), `ParseClientMessage`, `SerializeClientMessage`, `SanitizePlayerName`, the builders of the server messages, `LobbySnapshot` |
| `RoomCode.h/.cpp` | `GenerateRoomCode`, `NormalizeRoomCode`, `GenerateToken`, `IsWellFormedToken` |
| `RateLimiter.h/.cpp` | a token bucket over `RoomTime` |
| `Room.h/.cpp` | `Room`: members, seats, host, lobby snapshot and revision, phase, the game (start, actions, views, computers), absence and return; `RoomTimings` (see "Room lifecycle") |

## Protocol

JSON text, one message per WebSocket text message, `{"v":1,"type":"...", ...}` (`kRoomProtocolVersion`). A change that an old client cannot read bumps `v`; a member that a message does not define is ignored when read.

**Client to server** (`ParseClientMessage`):

| `type` | Members | Notes |
|--------|---------|-------|
| `create_room` | `game` (1 to 64 bytes), `name`, `options?` | `options` is an object or absent; anything else is `bad_message`. It is handed to `ISeatedGame::Start` as it is: untrusted |
| `join_room` | `code`, and **exactly one** of `name` (a new member) or `token` (a member returns) | the code is normalised to upper case; the token is 32 lower-case hex characters |
| `choose_seat` | `seat` (any integer) | the range is the room's to check (`invalid_seat`) |
| `start` | | the host only |
| `action` | `version` (integer, 0 or more), `action` (object) | `version` is the game version the client saw (`stale_action` when it moved) |
| `leave` | | |
| `ping` | | |

**Server to client** (the builders `Make*Message`):

| `type` | Members | When |
|--------|---------|------|
| `joined` | `room`, `player`, `token`, `seat`, `host` | to the new member, or to a returning one. The **token is a secret and goes to its owner only** |
| `lobby` | `room`, `game`, `phase` (`lobby`, `playing`, `finished`), `host` (the host's seat, -1 when no human is left), `seats: [{seat, kind: human\|computer\|empty, name?, connected?}]` | to every member when it changes. `name` and `connected` only for a human |
| `view` | `room`, `seat`, `version`, `finished`, `view`, `actions` | to a member when the game version moves or its seat changes. `view` is `ISeatedGame::GetSeatView`, `actions` is `GetSeatActions` |
| `error` | `code`, `message`, `ref?` | `ref` is the type of the message it answers |
| `left` | `reason` | the receiver is no longer in the room |
| `pong` | | answer to `ping` |

`ParseClientMessage(text, maxBytes)` never throws, aborts or asserts, whatever the bytes. It checks in this order: the size (`message_too_large`), no raw NUL byte (the JSON lexer takes NUL for the end of the text and would ignore what follows), a JSON object, `v`, `type`, then the fields of that type. It uses only the checked helpers of `BECore/Control/Json.h` (nlohmann aborts on a bad access in this build) and never interns a client string into `PoolString`.

**Error codes** (`RoomErrors`, part of the protocol; the codes of the game itself, `illegal_action`, `not_your_turn`, `invalid_seat`, are passed on as it gave them):

| Code | Meaning |
|------|---------|
| `bad_message` | not JSON, not an object, no NUL allowed, a missing or wrong-typed field, an unknown `type`, `v` missing or not an integer, a fractional or huge number, `options` not an object, a `join_room` with both or neither of `name` and `token` |
| `unsupported_version` | `v` is an integer other than 1 |
| `message_too_large` | over the size limit |
| `rate_limited` | the connection sent too fast (hub) |
| `unknown_game` | `create_room` names a game the registry does not have (hub) |
| `unknown_room` | no room has the code; also a `code` that is not 5 characters of the alphabet (a code that cannot exist is the same answer as one that does not) |
| `room_full`, `not_in_room`, `already_in_room`, `not_host`, `game_started`, `not_started`, `seat_taken`, `invalid_seat` | the state does not allow it (room, hub) |
| `stale_action`, `illegal_action`, `not_your_turn` | the action (room; the last two from the game) |
| `bad_name` | the name is empty after trimming, over 24 bytes, invalid UTF-8 or has a control character (`SanitizePlayerName`) |
| `bad_token` | the token is not 32 lower-case hex characters (parser), or no member of the room has it (room) |
| `too_many_rooms` | the room limit (hub) |
| `internal_error` | a bug on the server's side, never a client's fault |

`SanitizePlayerName`: valid UTF-8 (no overlong forms, surrogates or values above U+10FFFF), no control character (C0, DEL, C1, the line and paragraph separators, zero-width and direction-control characters, the byte order mark), then spaces at both ends removed, 1 to 24 bytes left. Names may repeat; the seat tells people apart. A name of only non-ASCII spaces (for example U+00A0 or U+3000) is not trimmed, so it is a valid name, not `bad_name`.

## Room lifecycle

`Room` is pure logic over `PlayerId` (numbered from 1, never reused in the room): no connection, no JSON, no clock of its own. Everybody who joins takes the lowest free seat at once, so every member has a seat and there are no spectators.

| Phase | Who may do what |
|-------|-----------------|
| `Lobby` | `AddMember(name, now)` (`bad_name`, `room_full`; the first member is the host), `RemoveMember(player)` (the seat is free; if it was the host, the host passes to the connected human of the lowest seat; none left: no host), `ChooseSeat(player, seat)` (`not_in_room`, `invalid_seat` for any integer outside 0..N-1, `seat_taken`; the own seat is a success that changes nothing) |
| `Playing`, `Finished` | `AddMember` and `ChooseSeat` are `game_started`, `RemoveMember` refuses (use `Leave`). `PerformAction(player, version, action)`, `GetSeatView`, `GetSeatActions`, `MarkAway`, `MarkReturned`, `StepComputers`, `Leave`, `ShouldClose` |

`Room(code, info, random, factory, timings)`: the room is given the factory of its game (from the registry) and `RoomTimings` (`reconnectGrace`, `abandonedTimeout`, `computerDelay`, the defaults of `RoomLimits`; tests pass zero or small values). `Start(host, now, options)`: `not_in_room`, `not_host`, `game_started`, then the game is made and checked (no factory, no game, or `GetSeatCount()` different from the registry's `seatCount` is `internal_error`) and started with a seed drawn from the `RandomFn`; an error of the game's `Start` is passed on. **Any error leaves the room in the lobby, unchanged.** On success the phase is `Playing` and empty seats are played by computers. `PerformAction` checks in this order: `not_in_room`, `not_started`, `stale_action` (`version` is not `GetGameVersion()`), `not_your_turn` (the member is away), then the game's own errors (`not_your_turn`, `illegal_action`, `invalid_params`); a finished game moves the phase to `Finished` (also when a computer made the last move). `GetGameVersion()` is 0 before the start.

### Absence, return and computers

| Rule | Value |
|------|-------|
| `MarkAway(player, now)` | the connection dropped (only after the start; in the lobby the hub removes the member). The seat is kept; the lobby shows `connected: false` |
| inside the grace (30 s) | nobody acts for the seat: not a computer, and `PerformAction` of the absent player is `not_your_turn` |
| from the grace on (`now - awaySince >= reconnectGrace`) | `IsSeatComputerControlled(seat, now)` is true and `StepComputers` plays it |
| `MarkReturned(player, token)` | the right token takes control back **at once** (a computer move is atomic, so there is no half-made move); `bad_token` otherwise, `not_in_room` for a player that left |
| `Leave(player)` | in a game the member is erased: the seat has no human and a computer plays it for good, the token no longer works, the host passes to the connected human of the lowest seat |
| empty seats | computer-controlled from the start |
| `StepComputers(now)` | at most one move per call: the first computer-controlled seat for which the game gives a `ChooseComputerAction`, sent through `PerformSeatAction`. Waits `computerDelay` (800 ms) from the start and from the last computer move; a time that goes back acts on nothing. A move the game refuses is not retried (returns false) |
| `ShouldClose(now)` | a lobby with no member; after the start no member left, or every member away for `abandonedTimeout` (10 min) or longer; one connected human keeps it open. A finished game with people present is not closed by the room (the hub decides) |

`AddMember` draws a new token while one is taken, at most 16 times, then `internal_error` (a broken `RandomFn` cannot hang the server). The hub does the same for room codes (work package 5).

`FindByToken(token)` compares every member's token over the whole length with no early exit (the time does not tell how much is right). `GetLobbyRevision()` grows on every change `GetLobbySnapshot()` shows (join, removal, seat change) and on nothing else, so the hub sends a `lobby` message exactly when it moved. Strings from a client are checked again in the room.

## Room codes and tokens

- A code is 5 characters of `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (32 characters: no `I`, `O`, `0`, `1`), taken 5 bits at a time from **one** word of the `RandomFn`, so every character is equally likely. `NormalizeRoomCode` upper-cases ASCII letters and refuses everything else. The code is not unique by itself: the hub retries when a room has it.
- A token is 128 bits (two words, high first) as 32 lower-case hex characters. Production uses `MakeSecureRandom()`; a test injects a fixed sequence.

## Limits

Constants in `RoomLimits.h`; `RoomHubSettings` repeats them as defaults for the hub (WP 5), so a test can use small ones.

| Limit | Value |
|-------|-------|
| client message | 16 KiB (`kMaxMessageBytes`) |
| player name | 1 to 24 UTF-8 bytes |
| game name | 1 to 64 bytes |
| rooms, connections | 64, 512 |
| per-connection rate | burst 20, then 10 per second (`RateLimiter`); the connection is closed after 5 `rate_limited` in a row |
| failed `join_room` in a row | 5, then the connection is closed (guessing a code or token) |
| idle connection that is in no room | 30 s |
| reconnect grace | 30 s |
| abandoned room | 10 min |
| computer move delay | 800 ms (0 in tests) |

`RateLimiter(burst, perSecond)`: a bucket that starts full at its first call, `TryTake(now)` takes one token. A burst below 1 is 1; a rate that is zero, negative or not a number adds nothing; a time that goes back counts as no time passing.

## Tests

`tests/BECore/Rooms/`: `RoomProtocolTest.cpp` (every message parses and round-trips, a table of malformed input with the code each must give, names, the builders), `RoomCodeTest.cpp`, `RateLimiterTest.cpp`, `RoomLobbyTest.cpp` (members, seats, host handover, revision, tokens), `RoomPlayTest.cpp` (start, actions, absence and return, computers and their delay, leaving, closing; every view is checked with `ExpectNoLeak`), `TestRandom.h` (`MakeSequence`, the shared deterministic `RandomFn`), and from WP 1 `FakeSeatedGame.*` and `SeatedGameTest.cpp` (`Games/CLAUDE.md`). Time and randomness are injected, so no test sleeps.
