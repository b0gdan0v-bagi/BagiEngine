# Rooms Module Guidelines

## Overview

Online play in rooms by code, with an authoritative server (the entry `network-rooms` of the todo store, slice 1 of 4). The server owns the game (`ISeatedGame`, `Games/CLAUDE.md`, "Seats"); every client is a seat and sees only what the game says that seat may see. This module is **pure code**: JSON text in, JSON text out, no sockets, no clock, no randomness of its own. A transport (a later slice) feeds it. Namespace `BECore`; no static initializers, so it is linked as an ordinary static library (`RoomsModule`; `BagiEngineTests` links it with `LINK RemoteControlModule RoomsModule RuntimeModule`).

Built so far (slice 1, work package 2 of 6): the protocol, room codes and tokens, the limits and the rate limiter. `Room` (lobby, seats, host, start, computers, absence) and `RoomHub` (the connection-to-room dispatcher) come in the next work packages and are not written yet; this file grows with them.

## Files

| File | Purpose |
|------|---------|
| `RoomTypes.h/.cpp` | `ConnectionId`, `PlayerId`, `RoomTime` (`steady_clock::time_point`, always passed in), `RandomFn` (`uint64_t()`, injected), `MakeSecureRandom()` (`std::random_device`), `RoomPhase` (`Lobby`, `Playing`, `Finished`), `SeatKind` (`Human`, `Computer`, `Empty`) and their wire names |
| `RoomLimits.h` | `RoomLimits::k*` (the constants of the table below) and `RoomHubSettings` (the same values as defaults, so a test overrides them) |
| `RoomErrors.h` | `RoomErrors::k*`, the stable error codes |
| `RoomProtocol.h/.cpp` | `ClientMessage` (a `std::variant` of the seven client messages), `ParseClientMessage`, `SerializeClientMessage`, `SanitizePlayerName`, the builders of the server messages, `LobbySnapshot` |
| `RoomCode.h/.cpp` | `GenerateRoomCode`, `NormalizeRoomCode`, `GenerateToken`, `IsWellFormedToken` |
| `RateLimiter.h/.cpp` | a token bucket over `RoomTime` |

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

`SanitizePlayerName`: valid UTF-8 (no overlong forms, surrogates or values above U+10FFFF), no control character (C0, DEL, C1, the line and paragraph separators, zero-width and direction-control characters, the byte order mark), then spaces at both ends removed, 1 to 24 bytes left. Names may repeat; the seat tells people apart.

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

`tests/BECore/Rooms/`: `RoomProtocolTest.cpp` (every message parses and round-trips, a table of malformed input with the code each must give, names, the builders), `RoomCodeTest.cpp`, `RateLimiterTest.cpp`, and from WP 1 `FakeSeatedGame.*` and `SeatedGameTest.cpp` (`Games/CLAUDE.md`). Time and randomness are injected, so no test sleeps.
