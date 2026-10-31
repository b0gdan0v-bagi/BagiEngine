# Games Module Guidelines

## Overview

The contract between the engine and a game. `Games` provides `IGame` and `GameManager`; each concrete game (`Chess`, `Match3`) lives outside the engine in `games/<Name>/`, derives from `IGame` and registers itself in `AbstractFactory<IGame>` through `BE_CLASS` reflection. There is no hand-written registrar. One executable links one game; `GameManager` still handles several registered games (switching, `GetAll()`), but no widget exposes it.

## IGame

`IGame` is a `RefCounted` `FACTORY_BASE`:

| Member | Meaning |
|--------|---------|
| `GetName()` | Unique id, also the display label and the key in `GamesConfig.xml` |
| `GetSceneName()` | Name of the `Scene` in the game's `config/SceneConfig.xml` that the game wants active |
| `Start()` / `Stop()` / `Reset()` | Game became active / is being replaced / restart the session without changing the scene |

## GameManager

`Singleton<GameManager>`, driven by `Application`: `Initialize()` runs in `Application::Initialize()`, `Shutdown()` on exit.

- `Initialize()` creates one instance of every registered `IGame`, reads an optional `GamesConfig.xml` (`<defaultGame name="Chess"/>`) and activates the default, or the first registered game when none is configured. A game executable has exactly one registered game, so no `GamesConfig.xml` is needed.
- `SwitchTo(name)`: `Stop()` on the old game → `SceneManager::SetActiveScene(GetSceneName())` → `Start()` on the new one. Unknown or already-active names are a no-op.
- `ResetActive()` calls `IGame::Reset()`; `GetActive()` / `GetAll()` for lookups.

## Adding a game

A game is a directory `games/<Name>/` that works both inside this repository and as the root of its own repository (the engine then sits next to it as the git submodule `engine/`). Model it on `games/Chess/`:

```
games/<Name>/
├── CMakeLists.txt   # be_add_module + be_add_game_reflection + be_add_game_executable + be_add_game_tests
├── CLAUDE.md
├── src/<Name>/      # Game/ (plain C++ rules), Components/ (scene glue), <Name>Game.{h,cpp}
├── config/          # SceneConfig.xml with the game's scene(s), TextureLibrary.xml, ...
├── assets/
└── tests/           # gtest sources, built as <Name>Tests
```

1. `CMakeLists.txt`: start with the standalone block (`if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) ... include(engine/cmake/BagiEngine.cmake)`), then `be_add_module(<Name> LINK BECoreModule EventsModule GamesModule SDLModule)`, `be_add_game_reflection(<Name>Module)`, `be_add_game_executable(Bagi<Name> GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR} MODULES <Name>Module)` and, under `ENGINE_BUILD_TESTS`, `be_add_game_tests(<Name>Tests SOURCES ... LINK <Name>Module)`. The functions are documented in `cmake/BagiEngine.cmake`.
2. Add `add_subdirectory(<Name>)` to `games/CMakeLists.txt`.
3. Derive `IGame` with `BE_CLASS(<Name>Game)`, include `<Generated/<Name>Game.gen.hpp>` in the `.cpp`. The game's reflection needs no entry in `reflection_targets.json` (that file lists engine directories only): `be_add_game_reflection` scans the game's own `src/` and treats the engine's reflected headers as reference.
4. Put a `Scene` with the name returned by `GetSceneName()` into `games/<Name>/config/SceneConfig.xml`. It replaces the engine's `SceneConfig.xml`, so it must contain every scene the game uses.
5. Re-run `cmake --preset ...`, add `Bagi<Name>` to the `targets` of the build presets in `CMakePresets.json`.
6. Keep the rules in a plain-C++ `Game/` folder, scene glue in `Components/`, tests in `tests/` (see `games/Chess/CLAUDE.md`).

The engine must stay free of game names: no engine code, CMake or config outside `games/` refers to a concrete game.

## Gotchas

- Scene events are global (see `BECore/Scene/CLAUDE.md`), so a game's components should check that their game is the active one before acting on clicks or drawing. Chess does it with `GameManager::GetInstance().GetActive()->Cast<ChessGame>()`.
- `IGame` has no per-frame hook: per-frame work belongs in a component on the game's scene.
