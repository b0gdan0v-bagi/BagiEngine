# Games Module Guidelines

## Overview

The contract between the engine and a game. `Games` provides `IGame` and `GameManager`; each concrete game (`Match3` here, `Chess` in the repository BagiEngine-Chess) lives outside the engine, in `games/<Name>/` or in a repository of its own,, derives from `IGame` and registers itself in `AbstractFactory<IGame>` through `BE_CLASS` reflection. There is no hand-written registrar. One executable links one game; `GameManager` still handles several registered games (switching, `GetAll()`), but no widget exposes it.

## IGame

`IGame` is a `RefCounted` `FACTORY_BASE`:

| Member | Meaning |
|--------|---------|
| `GetName()` | Unique id, also the display label and the key in `GamesConfig.xml` |
| `GetSceneName()` | Name of the `Scene` in the game's `config/SceneConfig.xml` that the game wants active |
| `Start()` / `Stop()` / `Reset()` | Game became active / is being replaced / restart the session without changing the scene |
| `AsControllable()` | The game's `IControllableGame` for the external control API, or `nullptr` (the default) |

## Control API (`IControllableGame`, `GameControl`)

A program or a model plays a game through `IControllableGame` (`Games/IControllableGame.h`), which the game hands out from `IGame::AsControllable()`. The engine asks through that call, never through a cast, so no engine code names a game. `GameManager::Initialize()` calls `RegisterGameControl()`, which registers the methods below in `CoreManager::GetControlService()` (protocol and threading: `BECore/Control/CLAUDE.md`). They act on the active game, looked up on every call. `RegisterGameControl()` also installs a frame end hook that pushes `{"event": "state_changed", "data": {"game", "version", "state"}}` when the state version of the active game moved since the previous frame, whatever moved it (a client, a reset, a person's click); the first frame only records the version, and a game that is not controllable never pushes (`Control/CLAUDE.md`).

| Method | Params | Result |
|--------|--------|--------|
| `list_games` | none | `{"active": name or null, "games": [{"name", "scene", "controllable"}]}` |
| `switch_game` | `{"name"}` | `{"active"}` |
| `reset` | the game's options (Match3: `{"seed"}`) | `{"game", "version"}`; a game that is not controllable gets a plain `IGame::Reset()` and no version |
| `get_state` | none | `{"game", "version", "state"}` |
| `list_actions` | none | `{"game", "version", "actions": [..]}` |
| `perform_action` | the action object itself, one of `actions` | `{"game", "version", "outcome"}` (`version` is the one after the action) |
| `set_state` | the game's state (Chess: `{"fen"}`, in BagiEngine-Chess) | `{"game", "version"}` |

`IControllableGame`: `GetState()`, `GetActions()` (a JSON array; every element is accepted by `PerformAction` as is), `PerformAction(action)`, `Reset(options)`, `SetState(state)` (default: `unsupported`), `GetStateVersion()`. Errors are `std::expected<.., ControlError>`; `GameControlErrors` has `no_active_game`, `not_controllable`, `unknown_game`, `no_active_model`, `illegal_action`, `unsupported`, and a bad parameter is `ControlErrors::kInvalidParams`.

Writing the adapter of a game:

- Keep the rules free of JSON: the adapter is a small class next to the game (`Match3GameControl`) that the `IGame` owns and returns from `AsControllable()`. It may reach a model that lives in a scene component through a provider function (Match3), which is also what lets a test drive it without a scene; a model that the game owns itself is handed over by reference (Chess, BagiEngine-Chess).
- `GetStateVersion()` must grow on every change a controller has to see (a new game, every committed action) and never go back; it is how a client knows the state moved, and `GameControl` compares it at the end of every frame to publish `state_changed`.
- The parameters of `PerformAction`, `Reset` and `SetState` come from a client. Read them with the checked helpers of `BECore/Control/Json.h` (nlohmann aborts the process on a bad access in this build) and range-check numbers as `int64_t` before narrowing. A bad value is an error, not an assert. `illegal_action` means the action is well-formed but not legal now.
- The state and the actions are for a model to read: short, self-explanatory names, a board as strings, no internal ids.
- `switch_game` looks the name up among the games; do not intern a client's string into `PoolString` (`GameControlTest` checks this with `PoolString::Find`).
- Tests: the engine-level methods are covered with fake games in `tests/BECore/GameControlTest.*` (`FakeControllableGame`, `FakePlainGame`); the adapter of a game is tested in the game's own tests.

## Seats (`ISeatedGame`)

For a turn-based game with several people and hidden information (cards, tile racks) that a server runs for clients in rooms (the entry `network-rooms`; the room logic itself is the module `Rooms`). `Games/ISeatedGame.h` is the contract, `Games/SeatedGameRegistry.h` how a server creates a game by name. Not a `BE_CLASS` and no scene: the server owns the game object, every client is a seat. A game in its own repository writes an adapter over its rules (like the control adapter of `IControllableGame`).

How it differs from `IControllableGame`:

| `IControllableGame` | `ISeatedGame` |
|---------------------|---------------|
| one controller sees and does everything, in the process of the game | every call names a **seat**; the game checks the seat (`invalid_seat`) and whose turn it is (`not_your_turn`) |
| `GetState()` is the whole state | `GetSeatView(seat)` is what that seat may see; **free of hidden information** of the other seats (the server sends it to the client as it is, so a count of the cards of the others is fine, the cards are not) |
| `GetActions()` | `GetSeatActions(seat)`: what the seat may send now, empty when it cannot act |
| `PerformAction()`, `Reset()` | `PerformSeatAction(seat, action)`, `Start(seed, options)`: the seed makes the deal reproducible |
| nothing for a computer | `ChooseComputerAction(seat)`: the action a computer plays for an empty or abandoned seat; the server sends it through `PerformSeatAction` like a person's, so a computer cannot do what a seat may not |

Rules of the adapter:

- The seat number, `options` and `action` come from a client: untrusted. Read them with the checked helpers of `BECore/Control/Json.h`, range-check numbers as `int64_t` before narrowing, answer `invalid_params` for a malformed value, `illegal_action` (`GameControlErrors`) for a well-formed move the rules refuse, `not_your_turn` and `invalid_seat` (`SeatedGameErrors`) as above. An error changes nothing.
- `GetStateVersion()` grows with every committed action and with `Start()` and never goes back; the server compares `action.version` with it to refuse a stale action.
- The outcome of `PerformSeatAction` may be sent to every seat: public information only. The `message` of an error goes back to the one client that sent the action, but keep it free of hidden information too (it may quote what the client itself sent, as the fake game's "seat 0 does not hold 'c3_1'" does, never what the other seats hold).
- The module that runs rooms of seated games is `Rooms` (`src/Modules/Rooms/CLAUDE.md`: protocol, room logic, hub).
- `SeatedGameRegistry` is a plain class that the caller owns and fills (no singleton, no static initializer); names are looked up in a hash map of `eastl::string` and never interned into `PoolString`, because they come from a client.
- Tests: `tests/BECore/Rooms/` (`FakeSeatedGame` is a four-seat card game with hidden hands, `LeakyFakeSeatedGame` its negative control, `ExpectNoLeak` the check that a text does not name a hidden card; `SeatedGameTest.cpp`); the scenario tests over the whole hub are `RoomsScenarioTest.cpp`.

## GameManager

`Singleton<GameManager>`, driven by `Application`: `Initialize()` runs in `Application::Initialize()`, `Shutdown()` on exit.

- `Initialize()` creates one instance of every registered `IGame`, reads an optional `GamesConfig.xml` (`<defaultGame name="Match3"/>`) through the reflected `GamesConfig` (`Games/GamesConfig.h`; errors of the file are logged as `Config 'GamesConfig': ...`) and activates the default, or the first registered game when none is configured. A game executable has exactly one registered game, so no `GamesConfig.xml` is needed.
- `SwitchTo(name)`: `Stop()` on the old game → `SceneManager::SetActiveScene(GetSceneName())` → `Start()` on the new one. Unknown or already-active names are a no-op.
- `ResetActive()` calls `IGame::Reset()`; `GetActive()` / `GetAll()` for lookups.

## Adding a game

A game is a directory `games/<Name>/` that works both inside this repository and as the root of its own repository (the engine then sits next to it as the git submodule `engine/`). Model it on `games/Match3/`, or on the repository BagiEngine-Chess (the engine as the submodule `engine/`, `CMakePresets.json` that includes the engine's). The repository around a game of its own (the git identity of the commits, the submodule on `games/<area>`, the linked skills, the rules files, the todo area) is set up by [NEW_GAME.md](../../../NEW_GAME.md) in the engine root:

```
games/<Name>/
├── CMakeLists.txt   # be_add_module + be_add_game_reflection + be_add_game_executable + be_add_game_tests
├── CLAUDE.md
├── src/<Name>/      # Game/ (plain C++ rules), Components/ (scene glue), <Name>Game.{h,cpp}
├── config/          # ApplicationConfig.xml (mode="Game"), SceneConfig.xml with the game's scene(s), TextureLibrary.xml, ...
├── assets/
└── tests/           # gtest sources, built as <Name>Tests
```

1. `CMakeLists.txt`: start with the standalone block (`if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) ... include(engine/cmake/BagiEngine.cmake)`), then `be_add_module(<Name> LINK BECoreModule EventsModule GamesModule SDLModule)`, `be_add_game_reflection(<Name>Module)`, `be_add_game_executable(Bagi<Name> GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR} MODULES <Name>Module)` and, under `ENGINE_BUILD_TESTS`, `be_add_game_tests(<Name>Tests SOURCES ... LINK <Name>Module)`. The functions are documented in `cmake/BagiEngine.cmake`.
2. Add `add_subdirectory(<Name>)` to `games/CMakeLists.txt`.
3. Derive `IGame` with `BE_CLASS(<Name>Game)`, include `<Generated/<Name>Game.gen.hpp>` in the `.cpp`. The game's reflection needs no entry in `reflection_targets.json` (that file lists engine directories only): `be_add_game_reflection` scans the game's own `src/` and treats the engine's reflected headers as reference.
4. Put a `Scene` with the name returned by `GetSceneName()` into `games/<Name>/config/SceneConfig.xml`. It replaces the engine's `SceneConfig.xml`, so it must contain every scene the game uses.
5. Copy the engine's `config/ApplicationConfig.xml` into `games/<Name>/config/` and set `mode="Game"`: the executable then starts without the editor (see "Run mode" in `BECore/CLAUDE.md`). Without the file the game starts in Editor mode. The game's file replaces the engine's, so it keeps `type`, `renderer` and `path`.
6. Re-run `cmake --preset ...`, add `Bagi<Name>` to the `targets` of the build presets in `CMakePresets.json`.
7. Keep the rules in a plain-C++ `Game/` folder, scene glue in `Components/`, tests in `tests/` (see `games/Match3/CLAUDE.md`).

The engine must stay free of game names: no engine code, CMake or config outside `games/` refers to a concrete game.

## Gotchas

- Scene and input events are global (see `BECore/Scene/CLAUDE.md`): a game's components subscribe with `SubscribeInScene`, so they react only while their scene is active. A component that reads the game's shared model must still check that the game is the active one. A game that keeps its model in the `IGame` (Chess does) does it with `GameManager::GetInstance().GetActive()->Cast<TheGame>()`.
- `IGame` has no per-frame hook: per-frame work belongs in a component on the game's scene.
- In Game mode there is no ImGui context. A game component must not call `ImGui::` without checking `ImGui::GetCurrentContext()`. A game's HUD and menus are UI canvases of the engine (`BECore/UI/CLAUDE.md`, an example in `games/Match3/`): they draw in both run modes, take input through `UIInput` and emit `InputEvents::ActionEvent`s that a component of the game answers with `SubscribeInScene`.
- A game can open on a menu scene: `GetSceneName()` is the scene the game wants active when it starts, and a button of the menu switches scenes through `CoreManager::GetSceneManager().SetActiveScene()` (Match3's `Match3MenuComponent`).
