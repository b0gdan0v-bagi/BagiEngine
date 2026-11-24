# Games Module Guidelines

## Overview

The contract between the engine and a game. `Games` provides `IGame` and `GameManager`; each concrete game (`Chess` in the repository BagiEngine-Chess) lives outside the engine, in a repository of its own, derives from `IGame` and registers itself in `AbstractFactory<IGame>` through `BE_CLASS` reflection. There is no hand-written registrar. One executable links one game; `GameManager` still handles several registered games (switching, `GetAll()`), but no widget exposes it.

## IGame

`IGame` is a `RefCounted` `FACTORY_BASE`:

| Member | Meaning |
|--------|---------|
| `GetName()` | Unique id, also the display label and the key in `GamesConfig.xml` |
| `GetSceneName()` | Name of the `Scene` in the game's `config/SceneConfig.xml` that the game wants active |
| `Start()` / `Stop()` / `Reset()` | Game became active / is being replaced / restart the session without changing the scene |
| `AsControllable()` | The game's `IControllableGame` for the external control API, or `nullptr` (the default) |

## Control API (`IControllableGame`, `GameControl`)

A program or a model plays a game through `IControllableGame` (`Games/IControllableGame.h`), which the game hands out from `IGame::AsControllable()`. The engine asks through that call, never through a cast, so no engine code names a game. `GameManager::Initialize()` calls `RegisterGameControl()`, which registers the methods below in `CoreManager::GetControlService()` (protocol and threading: `BECore/Control/CLAUDE.md`). They act on the active game, looked up on every call, and every one writes its answer in place with a `JsonWriter`: `list_games`, `switch_game`, `reset`, `set_state` and `run_until` are the reflected structs of `GameControlAnswers.h` (`call.ReplyObject(answer)`; `run_until` derives `ControlClock`), and `get_state`, `list_actions`, `perform_action` and the push are reflected structs too (`GameStateAnswer`, `GameActionsAnswer`, `GameActionAnswer`) whose game document is a `JsonPayload`: the streamed form over `WriteState` / `WriteActions`, the `outcome` as the text the game wrote, and the text of the state for the push. `run_until` writes the state document `{"game","state","version"}` from `WriteState` into a kept text, tapes it again only when the game or the version moved, and evaluates its `pointer` over the view (`JsonViewOps.h`); its `equals` and the `equal` of an `event` are copies of the request's text with tapes of their own, and the observer of `event` compares over the pushed view. `RegisterGameControl()` also installs a frame end hook that pushes `{"event": "state_changed", "data": {"game", "version", "state"}}` when the state version of the active game moved since the previous frame, whatever moved it (a client, a reset, a person's click); the first frame only records the version, and a game that is not controllable never pushes (`Control/CLAUDE.md`).

`RegisterGameControl` also registers the control root `game` (`Control/CLAUDE.md`, `ControlRoots`): the active game, resolved on every request to its most derived type, empty when no game is active. It keeps `allowInterning` off, so a client cannot grow the `PoolString` pool through it. A game adds what a client may do beyond the table below by marking a method of its `IGame` `BE_FUNCTION` (`call_method` with the root `game`) and a field `BE_REFLECT_FIELD` (`get_property` / `set_property`); `BagiStress` is the engine's example (`tools/stress/CLAUDE.md`), Mahjong the next user. Parameters, codes and limits: `BECore/Reflection/CLAUDE.md`, "Reflection methods".

| Method | Params | Result |
|--------|--------|--------|
| `list_games` | none | `{"active": name, "games": [{"name", "scene", "controllable"}]}`; `active` is left out when no game is active |
| `switch_game` | `{"name"}` | `{"active"}` |
| `reset` | the game's options (e.g. a seed) | `{"game", "version"}`; a game that is not controllable gets a plain `IGame::Reset()` and no version |
| `get_state` | none | `{"game", "version", "state"}` |
| `list_actions` | none | `{"game", "version", "actions": [..]}` |
| `perform_action` | the action object itself, one of `actions` | `{"game", "version", "outcome"}` (`version` is the one after the action) |
| `set_state` | the game's state (Chess: `{"fen"}`, in BagiEngine-Chess) | `{"game", "version"}` |
| `run_until` | `{condition, maxFrames?, dt?}`: a state pointer equals a value, the version moved, or an event arrived | `{met, frames, reason?, game?, version?}` and the clock of `step`; the limit is `met: false` (`.claude/rules/control-qa-api.md`) |

`IControllableGame`: `WriteState()` and `WriteActions()` (a JSON array; every element is accepted by `PerformAction` as is; what `get_state`, `list_actions` and the push call), `PerformAction(JsonView action, JsonWriter& outcome)`, `Reset(JsonView options)`, `SetState(JsonView state)` (default: `unsupported`), `WriteReplayStart(JsonWriter&)` (default `{}`), `GetStateVersion()`. Every writing method writes **exactly one value** (`outcome.Null()` for "nothing to say") and returns success, or returns an error and what it wrote is dropped; a `PerformAction` that succeeds without one complete value is an `internal_error`, nothing is recorded or sent. Errors are `std::expected<.., ControlError>`: `GameControlErrors` (codes in `IControllableGame.h`), a bad parameter is `ControlErrors::kInvalidParams`. `Games/GameText.h` gives the text forms (`StateText`, `ActionsText`, `PerformActionText`, `ResetText`, ...) to code with no writer or view at hand: the Game panel and tests; they never record.

The engine writes the clock, window, status and every plain answer through reflected structs, and a game writes its state, actions and outcome into the writer it is given. A game that wants the same for its own control methods writes a `BE_CLASS` answer and `call.ReplyObject(answer)`, reading `call.Params()`; `ISeatedGame` answers with reflected structs too (Seats). A game's tool that reads `schema` follows `isConst` (the old `const`) and a type with no `parent` has no `parent` member.

Writing the adapter of a game:

- Keep the rules free of JSON: the adapter is a small class next to the game that the `IGame` owns and returns from `AsControllable()`. It may reach a model that lives in a scene component through a provider function, which is also what lets a test drive it without a scene; a model that the game owns itself is handed over by reference (Chess, BagiEngine-Chess).
- `GetStateVersion()` must grow on every change a controller has to see (a new game, every committed action) and never go back; `GameControl` compares it at the end of every frame to publish `state_changed`.
- The parameters of `PerformAction`, `Reset` and `SetState` come from a client. Read the `JsonView` with the checked helpers of `BECore/Control/ControlParams.h` (`GetParamString` decodes an escape, `GetParamInt` refuses a non-integer and a value past `int64_t`, `FindParam` tells absent from wrong); a bad value is an error, not an assert, and a view is valid for the call only. `illegal_action` means the action is well-formed but not legal now.
- The state and the actions are for a model to read: short, self-explanatory names, a board as strings, no internal ids.
- `switch_game` looks the name up among the games; do not intern a client's string into `PoolString`.
- Tests: the engine-level methods are covered with fake games in `tests/BECore/GameControlTest.*` (`FakeControllableGame`, `FakePlainGame`); the adapter of a game is tested in the game's own tests.

## Record and replay

A recording is JSON lines of the accepted `reset`, `perform_action` and `set_state` calls; a game opts in with `IsReplayable()` and `WriteReplayStart()`, and is replayable only when all its randomness comes from the seed and its delayed tasks follow game time. `record_start` and `record_stop` are the reflected structs of `ReplayAnswers.h`; `replay` is a reflected `ReplayAnswer` (counts, clock and the `mismatch`, a `ReplayMismatchAnswer` of views of the mismatch's own texts: the entry, the outcome of the file and what the game wrote are JSON text; `ReplayMismatchText` writes it for tests, `ReplayWireTest` pins its bytes). The file format, the methods, the mismatch kinds and the adoption steps: `.claude/rules/games-replay.md`.

## Moving a game to the view interface

A game written against the old DOM interface (`Json`) changes in five places and nowhere else:

- delete `GetState()` and `GetActions()`; keep `WriteState` / `WriteActions` (they are pure virtual now);
- `PerformAction(JsonView action, JsonWriter& outcome)`: read with `GetParamString` / `GetParamInt` / `FindParam` of `<BECore/Control/ControlParams.h>` instead of `GetJson*`, write one value (`outcome.Null()` when there is nothing to say);
- `Reset(JsonView options)` and `SetState(JsonView state)` read the same way;
- `GetReplayStart()` becomes `WriteReplayStart(JsonWriter&)`;
- calls of `ControlledReset` / `ControlledSetState` take a `JsonView`, and `ControlledAction(game, view, String& outcomeText)` writes the outcome into a text; a game's own control methods read `call.Params()`.

A seated game (`ISeatedGame`) moves the same way: `Start(seed, JsonView options)` and `PerformSeatAction(seat, JsonView action)` read with `ControlParams.h`, and `ChooseComputerAction(seat, JsonWriter&)` writes one object and returns true (false when the seat cannot act); its tests drive the calls with a text (`tests/Common/JsonDoc.h`, `StartText` / `PerformText` / `ChooseComputerText` of `tests/BECore/Rooms/FakeSeatedGame.h`). A game with a `RoomClient` passes its options and actions as JSON text and reads `GetViewText()` / `GetActionsText()`.

A game's tests use `<Games/GameText.h>` for the text forms and `EXPECT_REPLAY_FINAL_STATE(game, path, expectedStateJson)` takes the final state as JSON text. A recording lists the keys of `params` in the order the caller sent them (the reader does not care, older files read unchanged).

## Seats (`ISeatedGame`)

For a turn-based game with several people and hidden information (cards, tile racks) that a server runs for clients in rooms (the entry `network-rooms`; the room logic itself is the module `Rooms`). `Games/ISeatedGame.h` is the contract, `Games/SeatedGameRegistry.h` how a server creates a game by name. Not a `BE_CLASS` and no scene: the server owns the game object, every client is a seat. A game in its own repository writes an adapter over its rules (like the control adapter of `IControllableGame`).

How it differs from `IControllableGame`:

| `IControllableGame` | `ISeatedGame` |
|---------------------|---------------|
| one controller sees and does everything, in the process of the game | every call names a **seat**; the game checks the seat (`invalid_seat`) and whose turn it is (`not_your_turn`) |
| `WriteState()` is the whole state | `GetSeatView(seat)` is what that seat may see, a `UniquePtr<SeatedView>`; **free of hidden information** of the other seats, field by field (the server writes it to the client as it is, so a count of the cards of the others is fine, the cards are not) |
| `WriteActions()` | `GetSeatActions(seat)`: what the seat may send now, a `UniquePtr<SeatedActions>`, empty when it cannot act |
| `PerformAction()`, `Reset()` (a view and a writer) | `PerformSeatAction(seat, JsonView action)` (answers a `UniquePtr<SeatedOutcome>`), `Start(seed, JsonView options)`: the seed makes the deal reproducible; a view is valid for the call, copy what is kept |
| nothing for a computer | `ChooseComputerAction(seat, JsonWriter& action)`: writes the action a computer plays for an empty or abandoned seat (exactly one object, true; false: the seat cannot act, nothing counts). The room tapes that text and hands the view to `PerformSeatAction` like a person's, and that one way in is why a computer cannot do what a seat may not; a game that returns true with no complete object is logged and its seat skipped |

Rules of the adapter:

- The seat number, `options` and `action` come from a client: untrusted. Read them with `GetParamString` / `GetParamInt` / `FindParam` of `BECore/Control/ControlParams.h` (an absent key, a wrong type or an integer past `int64_t` reads as nothing), range-check numbers as `int64_t` before narrowing, answer `invalid_params` for a malformed value, `illegal_action` (`GameControlErrors`) for a well-formed move the rules refuse, `not_your_turn` and `invalid_seat` (`SeatedGameErrors`) as above. An error changes nothing.
- **The answers are the game's own `BE_CLASS` structs** derived from the bases of `Games/SeatedAnswers.h` (`SeatedView`, `SeatedAction`, `SeatedOutcome`; reflected there, no field), returned as `UniquePtr<Base>` with `MakeUnique`. The room holds the base pointer and writes it through the virtual generated `WriteJson` inside `JsonPayload::Streamed` (no tree, no game type in `Rooms`); a derived struct writes its keys merged with the base's, sorted. Fields are plain `Vector` / `String`, never the arena types: the answer crosses calls, from the game to the hub. A game's structs show up in `describe_type` under `derived` of the base; its reflection target scans the folder of its structs. The wire `actions` is an array and a reflected struct writes an object, so a game returns a `SeatedActionList<TAction>` (a `Vector<TAction>`, `TAction` derived from `SeatedAction`), which writes `[]` for no action.
- The elements of `actions` are what a client sends back: once written as JSON, each is accepted as is by `PerformSeatAction`, and its keys are the ones that function reads with the checked helpers. The seat rule holds per field of the view: write the test per field (the demo game's `SeatedGameTest` compares own hand, counts and table with the truth at every point of a played game), not only a text search for hidden ids.
- `GetStateVersion()` grows with every committed action and with `Start()` and never goes back; the server compares `action.version` with it to refuse a stale action.
- The outcome of `PerformSeatAction` may be sent to every seat: public information only. The `message` of an error goes back to the one client that sent the action, but keep it free of hidden information too (it may quote what the client itself sent, as the fake game's "seat 0 does not hold 'c3_1'" does, never what the other seats hold).
- The module that runs rooms of seated games is `Rooms` (`src/Modules/Rooms/CLAUDE.md`: protocol, room logic, hub).
- `SeatedGameRegistry` is a plain class that the caller owns and fills (no singleton, no static initializer); names are looked up in a `HashMap` of `String` and never interned into `PoolString`, because they come from a client. `SeatedGameInfo::publicOptions` names the keys of `create_room`'s options every member may see in the lobby before the start (booleans and integers only; the rest stays the game's). `listsStartedRooms` (default false) makes `list_rooms` also list the game's started public rooms that still have a free seat (nobody can join them: `game_started`); a finished room is never listed.
- Tests: `tests/BECore/Rooms/` (`FakeSeatedGame` is a four-seat card game with hidden hands (the `DemoCardGame` of `RoomServer`), `LeakyFakeSeatedGame` its negative control, `ExpectNoLeak` the check that a text does not name a hidden card; `SeatedGameTest.cpp`); the scenario tests over the whole hub are `RoomsScenarioTest.cpp`.

## GameManager

`Singleton<GameManager>`, driven by `Application`: `Initialize()` runs in `Application::Initialize()`, `Shutdown()` on exit.

- `Initialize()` creates one instance of every registered `IGame`, reads an optional `GamesConfig.xml` (`<defaultGame name="Chess"/>`) through the reflected `GamesConfig` (`Games/GamesConfig.h`; errors of the file are logged as `Config 'GamesConfig': ...`) and activates the default, or the first registered game when none is configured. A game executable has exactly one registered game, so no `GamesConfig.xml` is needed.
- `SwitchTo(name)`: `Stop()` on the old game → `SceneManager::SetActiveScene(GetSceneName())` → `Start()` on the new one. Unknown or already-active names are a no-op.
- `ResetActive()` calls `IGame::Reset()`; `GetActive()` / `GetAll()` for lookups.

## Adding a game

A game is a repository of its own with the engine as the git submodule `engine/`; model it on BagiEngine-Chess (`CMakePresets.json` that includes the engine's). The repository around a game (the git identity of the commits, the submodule on `games/<area>`, the linked skills, the rules files, the todo area) is set up by [NEW_GAME.md](../../../NEW_GAME.md) in the engine root (the engine branch is `games/<area>`; `main` is the showcase that only `games/*` reach, and the owner syncs them with `/sync-engine`):

```
<repo>/
├── CMakeLists.txt   # be_add_module + be_add_game_reflection + be_add_game_executable + be_add_game_tests
├── CLAUDE.md
├── src/<Name>/      # Game/ (plain C++ rules), Components/ (scene glue), <Name>Game.{h,cpp}
├── config/          # ApplicationConfig.xml (mode="Game"), SceneConfig.xml with the game's scene(s), TextureLibrary.xml, ...
├── assets/
└── tests/           # gtest sources, built as <Name>Tests
```

1. `CMakeLists.txt`: start with the standalone block (`if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) ... include(engine/cmake/BagiEngine.cmake)`), then `be_add_module(<Name> LINK BECoreModule EventsModule GamesModule SDLModule)`, `be_add_game_reflection(<Name>Module)`, `be_add_game_executable(Bagi<Name> GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR} MODULES <Name>Module)` and, under `ENGINE_BUILD_TESTS`, `be_add_game_tests(<Name>Tests SOURCES ... LINK <Name>Module)`. The functions are documented in `cmake/BagiEngine.cmake`.
2. Derive `IGame` with `BE_CLASS(<Name>Game)`, include `<Generated/<Name>Game.gen.hpp>` in the `.cpp`. The game's reflection needs no entry in `reflection_targets.json` (that file lists engine directories only): `be_add_game_reflection` scans the game's own `src/` and treats the engine's reflected headers as reference.
3. Put a `Scene` with the name returned by `GetSceneName()` into `config/SceneConfig.xml`. It replaces the engine's `SceneConfig.xml`, so it must contain every scene the game uses.
4. Copy the engine's `config/ApplicationConfig.xml` into the game's `config/` and set `mode="Game"`: the executable then starts without the editor (see "Run mode" in `BECore/CLAUDE.md`). Without the file the game starts in Editor mode. The game's file replaces the engine's, so it keeps `type`, `renderer` and `path`.
5. Re-run `cmake --preset ...`.
6. Keep the rules in a plain-C++ `Game/` folder, scene glue in `Components/`, tests in `tests/` (the layout of BagiEngine-Chess).

The engine must stay free of game names: no engine code, CMake or config refers to a concrete game.

## Gotchas

- Scene and input events are global (see `BECore/Scene/CLAUDE.md`): a game's components subscribe with `SubscribeInScene`, so they react only while their scene is active. A component that reads the game's shared model must still check that the game is the active one. A game that keeps its model in the `IGame` (Chess does) does it with `GameManager::GetInstance().GetActive()->Cast<TheGame>()`.
- `IGame` has no per-frame hook: per-frame work belongs in a component on the game's scene.
- In Game mode there is no ImGui context. A game component must not call `ImGui::` without checking `ImGui::GetCurrentContext()`. A game's HUD and menus are UI canvases of the engine (`BECore/UI/CLAUDE.md`): they draw in both run modes, take input through `UIInput` and emit `InputEvents::ActionEvent`s that a component of the game answers with `SubscribeInScene`.
- A game can open on a menu scene: `GetSceneName()` is the scene the game wants active when it starts, and a button of the menu switches scenes through `CoreManager::GetSceneManager().SetActiveScene()` (a component of the menu scene).
