# Seated games answer with typed structs instead of a Json tree per call

- Version: 0.16.0
- Entry: seated-game-typed-answers
- Date: 2026-11-23
- Bump: minor

## Added
- `Games/SeatedAnswers.h`: the base types `SeatedView`, `SeatedAction` and `SeatedOutcome` (a game derives its own `BE_CLASS` structs from them; `describe` lists them by derivation) and `SeatedActions` / `SeatedActionList<TAction>` for the actions array. The room writes a game's answer through its generated `WriteJson`, with no `Json` built. Adapt: nothing beyond the Breaking item below.

## Changed
- The room, the hub and the demo card game use the typed answers. The `view` and `actions` messages on the wire and `kRoomProtocolVersion` are unchanged. Adapt: nothing.

## Breaking
- `ISeatedGame::GetSeatView` returns `std::expected<UniquePtr<SeatedView>, ControlError>`, `GetSeatActions` returns `std::expected<UniquePtr<SeatedActions>, ControlError>` and `PerformSeatAction` returns `std::expected<UniquePtr<SeatedOutcome>, ControlError>`, instead of `std::expected<Json, ControlError>`. Adapt: declare the game's view, action and outcome as `BE_CLASS` structs derived from `SeatedView`, `SeatedAction` and `SeatedOutcome` with the same field names the `Json` had, fill them instead of the `Json`, return the actions as a `SeatedActionList<YourAction>`, and add the folder of those structs to the game's reflection (`be_add_game_reflection`); `RoomServer/src/RoomServer/DemoCardGame.h` is the example. Mahjong follows in `mahjong-seated-typed-answers`.
