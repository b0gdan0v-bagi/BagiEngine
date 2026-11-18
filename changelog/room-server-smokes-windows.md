# Windows: BagiRoomServerSmoke and BagiRoomServerHostile always fail, so every ctest (and the panel's Verify) is red

- Version: 0.3.4
- Entry: room-server-smokes-windows
- Date: 2026-11-18
- Bump: patch

## Added
- `Socket::FinishSending(limit)`: ends the sending direction, then reads and drops what the peer still sends until it closes, `ShutdownBoth()` is called or the limit runs out.

## Changed
- The room server flushes stdout on every round of its loop, so on Windows, where the CRT treats `_IOLBF` as full buffering, the log reaches a pipe while the server runs.
- A WebSocket connection that sends a close frame finishes sending before it closes (for at most 1 s), so a client that is still sending receives the frame instead of a connection reset. Data that arrives after the close frame is no longer parsed.
