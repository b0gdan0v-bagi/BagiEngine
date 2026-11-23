# Windows: RoomClientSocketTest.AServerThatStopsIsRetriedUntilTheGiveUpTime sees 2 connections in the give-up time, not 3

- Version: 0.9.4
- Entry: room-client-retry-test-windows
- Date: 2026-11-23
- Bump: patch

## Changed
- `RoomClientSocketTest.AServerThatStopsIsRetriedUntilTheGiveUpTime` asks for at least one retry after the server stopped, not 3 connections in 700 ms: Windows answers a connect to a closed loopback port after about 2 s of its own retries. The client itself is unchanged (it gives up on its own clock while a connect hangs).
  Adapt: nothing; a game's test of reconnects on Windows does not count on a refused loopback connect being fast.
