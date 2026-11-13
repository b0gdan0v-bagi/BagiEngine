# BagiStress: the engine's own 2D stress app

## Overview

`BagiStress` is an executable of the engine repository with an `IGame` of its own (`StressGame`, name `Stress`, namespace `BECore::Stress`) and no game rules: three scenes that load the engine in different ways, chosen by argument. It is the target of the engine's benchmarks (`tools/perf`) and of a control smoke, so a change to the 2D renderer, the scene or the UI is measured without Match3. It lives in `tools/`, not in `games/`: `games/` goes away when Match3 leaves. How a game plugs into the engine: `src/Modules/Games/CLAUDE.md`.

| Scene (`SceneConfig.xml`) | Flag value | Loads |
|---------------------------|------------|-------|
| `StressSprites` | `sprites` | N spinning sprites, one `IRenderer` call each |
| `StressEntities` | `entities` | N nodes with a transform and a tween, nothing drawn |
| `StressUi` | `ui` | one canvas of N text labels, English and Russian |

Each scene is 1920 x 1080 and has a node `Field` for the scene's component.

## Layout

```
CMakeLists.txt        be_add_module(Stress), the reflection, BagiStress, StressTests (no standalone block: not a game of its own repository)
config/               ApplicationConfig.xml (Game mode), SceneConfig.xml: kept at the engine's content version by ContentFilesVersionTest
src/Stress/           StressSettings (the flags), StressState (what is shown now), StressGame (the IGame), StressGameControl (the control adapter)
tests/                gtest sources of StressTests
```

## The arguments and the state

- **The flags** reach the game through `CoreManager::GetCommandLine()` (`BECore/Application/CommandLine.h`; `Application::SetArguments` fills it), never through `Application`, so a test sets them. `StressGame` parses them in its constructor, because `GameManager` asks `GetSceneName()` before `Start()`. `ParseStressSettings` documents every flag; a wrong one is an error that names the flag (a bad value, an empty value, a value on a bare flag, no value on a valued one), the game logs it once as a warning and starts with the defaults. Nothing is defaulted silently inside the parser.
- **`StressState`** is the live scene, count and language. Every component and the control adapter read it and rebuild what they show only when `Version()` moved; a change to an equal value does not move it, and it never goes back (it starts at 1, so a component's own 0 means "nothing built yet"). A count is `0..kMaxCount` here (the command line and the control API ask for at least 1).
- A component reaches the game through `StressGame::GetActive()` and does nothing when it is null (the game is not the active one).

## Control API

`StressGameControl` (`AsControllable()`, owned by `StressGame`) drives the `StressState` by reference; there is no model in a scene. State `{"scene", "count", "language", "maxCount", "scenes"}`, actions `set_scene`, `set_count`, `set_language` (the exact shapes are in the header). Rules that a controller can lean on:

- `list_actions` shows the other scenes, the half and the double of the count and the other language; `perform_action` takes any `count` in `1..maxCount`, not only the listed ones.
- A malformed or out-of-range value is `invalid_params`; an action that is well-formed and changes nothing is accepted (the version does not move); `illegal_action` is only for an unknown `type`.
- A scene change makes the game switch the active scene (`SetActiveScene` through a callback, so a test needs no scene manager). `reset` takes an optional `{"scene", "count"}` and puts the rest to the defaults, so a measurement starts from a known state; the options are checked before anything changes.
- `get_state` version is `StressState::Version()`.
