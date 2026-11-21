---
paths:
  - "src/Modules/RoomServer/**/*"
  - "tests/BECore/Rooms/RoomServer*"
  - "tests/BECore/Rooms/RoomHostile*"
---

# RoomServer: limits and threat model

The module: `src/Modules/RoomServer/CLAUDE.md`.

The server is public and its clients are programs of strangers. **Trusted:** the registry's games, its own clock, and the last `--trusted-proxy-hops` entries of `X-Forwarded-For` (only what your own proxies appended; set it to the number of proxies in front, a larger N reads the client's own entry). **Never trusted:** every byte of a client, the `Origin` of a program (it only keeps web pages out), a seat number, a version, a code, a name, `options` and `action` (the game checks them). **Limited:** the Limits table below, each for a reason: sockets and handshakes (threads, memory), frames at their header and messages before the parse (memory), the rate and failed joins (CPU, guessing a 32^5 code: 64 rooms are hit by about 2 guesses in a million, and every budget cuts that further), the failed-join buckets (they stop guessing, and lock honest joins too: see Out of scope), rooms per address and the lobby timeout (one address cannot take all 64 rooms or keep them for ever), the outbox and the event queue (a client that never reads). **Out of scope:** one client that fails joins on purpose empties the server-wide bucket (100, then 1 per 6 s) and with it every `join_room` by code on the server until it refills; only token reconnects pass. With the per-address rules off (the hosted default) it is the only budget left: a lock-out of new joins for the price of one socket, traded for the guess limit. With them on, the same client first meets its own address bucket, but many addresses still do it. An IPv6 client would be keyed by its /128, not the /64. A human who stays connected, answers pings and never plays in a started game (a turn timer is the game's); a trickle of one byte per ping interval (holds a thread and a socket, bounded by the caps); a flood above the host; a client cheating inside its own view; `options` of `create_room` are the game's to validate; a token works from any address (a phone roams); a tampered proxy that does not append the header (then a forged client address passes). **Owner's settings behind a proxy:** `BAGI_ROOMS_TRUSTED_PROXY_HOPS=1` with `BAGI_ROOMS_MAX_PER_ADDRESS` above 0, otherwise the per-address rules stay off; the 15 min lobby default also drops a Mahjong host who waits without touching the screen (the lobby sends nothing on its own). **Checking:** `RoomHostileTest` (the hub in memory), `RoomServerHostileTest` (real sockets, the wire judge), and `tools/rooms/hostile.py` / ctest `BagiRoomServerHostile` against the executable; a limit scenario is shown to fail with its flag switched off (`--only <name> --server-arg=...`, `tools/rooms/CLAUDE.md`).

## Limits

| Limit | Default |
|-------|---------|
| open sockets, handshakes included (`maxConnections`) | `RoomLimits::kMaxConnections + 64`: the hub's own limit answers `server_full` first, the extra is room for connections still in the handshake; over it a 503 |
| open sockets of one address (`maxConnectionsPerAddress`) | 32; 0 is off. Behind a proxy, with `--trusted-proxy-hops` it counts forwarded addresses (checked after the handshake) |
| rooms of one address (`hub.maxRoomsPerAddress`) | 4; counted while the room exists, so closing frees a place; `too_many_rooms` |
| failed joins | per connection 5 in a row (close); per address a bucket of 10, then 1 per 30 s; for the server 100, then 1 per 6 s. An empty bucket answers `rate_limited` before the room is looked up |
| lobby with no message but `ping` (`hub.lobbyTimeout`) | 15 min; the members get `left {lobby_timeout}` and keep their sockets (then 30 s idle) |
| send queue of a connection (`maxOutboxBytes`) | 1 MiB |
| message | `hub.maxMessageBytes` (16 KiB) |
| handshake | 5 s in all, request head 8 KiB |
| silence (`pingInterval`) | a ping after 20 s, a drop after 40 s |

Two threads per connection, about 1200 at the limit: accepted for a server of friends.

The hub gets an address only when `maxConnectionsPerAddress != 0` or `trustedProxyHops != 0` (`_passAddress`); with both off every per-address rule (sockets, rooms, the address bucket) is off and only the server-wide bucket, the room cap and the lobby timeout hold. The settings line says `per-address rules on|off`. The address is compared, never logged. Two deviations on purpose: a join by a real member's token passes with an empty bucket (128 bits cannot be guessed, and it is how a phone returns); the budget makes the right code of a room unusable too while it is empty, so friends behind the same address wait for the refill (and, for the server-wide bucket, everybody).
