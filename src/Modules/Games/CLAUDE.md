# Games Module Guidelines

## Overview

Infrastructure for several self-contained games in one executable. `Games` provides `IGame` and `GameManager`; each concrete game (`Chess`, `Match3`) is its own module that derives from `IGame` and registers itself in `AbstractFactory<IGame>` through `BE_CLASS` reflection. There is no hand-written registrar.

## IGame

`IGame` is a `RefCounted` `FACTORY_BASE`:

| Member | Meaning |
|--------|---------|
| `GetName()` | Unique id, also the display label and the key in `config/GamesConfig.xml` |
| `GetSceneName()` | Name of the `Scene` in `config/SceneConfig.xml` that the game wants active |
| `Start()` / `Stop()` / `Reset()` | Game became active / is being replaced / restart the session without changing the scene |

## GameManager

`Singleton<GameManager>`, driven by `Application`: `Initialize()` runs in `Application::Initialize()`, `Shutdown()` on exit.

- `Initialize()` creates one instance of every registered `IGame`, reads `config/GamesConfig.xml` (`<defaultGame name="Chess"/>`) and activates the default, or the first registered game when none is configured.
- `SwitchTo(name)`: `Stop()` on the old game → `SceneManager::SetActiveScene(GetSceneName())` → `Start()` on the new one. Unknown or already-active names are a no-op.
- `ResetActive()` calls `IGame::Reset()`; `GetActive()` / `GetAll()` for lookups. `GameSelectorWidget` (`src/Widgets/GameSelector/`) is the UI over these calls.

## Adding a game

1. New module `src/Modules/<Name>/` with a `CMakeLists.txt` modeled on `Chess/CMakeLists.txt`: `STATIC` library, `REUSE_FROM BECoreModule` plus the `BECore/pch.h` force-include, `REFLECTION_GENERATED=1`, links `BECoreModule`, `EventsModule`, `GamesModule`, `SDLModule`.
2. `add_subdirectory(src/Modules/<Name>)` after `Games` in the root `CmakeLists.txt`, and link it into `MyGame` with `WHOLE_ARCHIVE` (otherwise the linker drops the registration). Add it to `tests/CMakeLists.txt` if it has tests.
3. Put the module's source directories into `scan_dirs` of `CI/meta_generator/reflection_targets.json` (and the include dir into `include_dirs`), then re-run `cmake --preset ...`.
4. Derive `IGame` with `BE_CLASS(<Name>Game)`, include `<Generated/<Name>Game.gen.hpp>` in the `.cpp`.
5. Add a `Scene` with the name returned by `GetSceneName()` to `config/SceneConfig.xml`.
6. Keep the rules in a plain-C++ `Game/` folder, scene glue in `Components/`, tests in `tests/<Name>/` (see `Chess/CLAUDE.md`).

## Gotchas

- Scene events are global (see `BECore/Scene/CLAUDE.md`), so a game's components should check that their game is the active one before acting on clicks or drawing. Chess does it with `GameManager::GetInstance().GetActive()->Cast<ChessGame>()`.
- `IGame` has no per-frame hook: per-frame work belongs in a component on the game's scene.
