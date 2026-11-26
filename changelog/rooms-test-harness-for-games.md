# The room test harness takes a game's own games

- Version: 0.29.5
- Entry: rooms-test-harness-for-games
- Date: 2026-11-26
- Bump: patch

## Added
- `HubWorld` and `HubHarness` (`tests/BECore/Rooms/`) take a game registrar (`GameRegistrar`) and an observer of the sent texts (`TextObserver` / `MessageObserver`) as trailing optional parameters; without them they keep the demo and fake games. Adapt: a game's tests may drop their own copy of the hub transport and pass their registrar; the test target links `RoomClientModule RoomServerModule`.
