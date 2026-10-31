# TODO

Known problems that are not being worked on yet. One entry per problem: where it is, what is wrong, what a fix has to cover.

## Config Editor: the sink `type` dropdown offers values the logger factory cannot resolve

**Where:** `src/Widgets/ConfigEditor/ConfigEditorWidget.cpp:22` (`schema.RegisterEnum<LogSinkType>("LoggerConfig"_intern, "sink/@type")`), `ConfigFieldSchema.h` (`RegisterEnum`), `FieldInspectors.cpp` (`RenderEnum`), `src/Modules/BECore/Logger/LogSinkType.h`.

**What is wrong** (found by reading the code, not reproduced in a running editor):

1. The dropdown options come from `EnumUtils<LogSinkType>::Names()`: `Console`, `File`, `Output`. `config/LoggerConfig.xml` stores the sink **class names**: `ConsoleSink`, `FileSink`, `OutputSink`. `RenderEnum` looks for the current value among the options, finds nothing, `selectedIndex` stays `-1` and the combo shows no current selection.
2. Choosing an option and saving writes `type="Console"` into the file. On the next load `LoadElement` (`Reflection/DataAccessor.h`) creates the sink with `AbstractFactory<ILogSink>::Create(typeString)`, which looks the type up by the hash of the class name. `"Console"` is not registered, the result is null, and `ILogSink` is abstract, so the entry is dropped without any message. If every entry is dropped, `LoggerManager` silently falls back to a `ConsoleSink`.
3. `LogSinkType` is defined twice: by hand in `BECore/Logger/LogSinkType.h` (`Console, File, Output`) and by the generator in `build/Generated/EnumLogSink.gen.hpp` (`File, Console, Output`, different order). Only the handwritten one is included anywhere; including both in one translation unit would be a redefinition, and the value order already disagrees.

**A fix has to cover:**

- the dropdown lists exactly the strings the factory accepts, and shows the current value as selected;
- one definition of the sink type enum (or none: the editor can take the registered class names from `AbstractFactory<ILogSink>::GetRegisteredTypes()`);
- a visible error when a config names a sink type that is not registered, instead of dropping the entry silently;
- a check that edit → save → restart keeps every sink working (a round-trip test of `LoggerConfig` through the editor's save path, or at least through `ConfigManager::SaveConfig` / `ReloadConfig`).

The same pattern (an enum hint whose names differ from what the loader expects) can exist for other registered fields: `LogLevel` for `sink/@minLevel` is fine because `minLevel` is read through `EnumUtils`, but check any new `RegisterEnum` call against the loader that reads the attribute.

## Scene events are global: components of inactive scenes still react to `SceneDrawEvent` / `SceneUpdateEvent`

**Where:** `src/Modules/BECore/Scene/Scene.cpp` (`Update` / `Draw` emit `SceneEvents::*`), `SceneNode.cpp` (`WireToScene` calls `OnAttached()`), `SceneManager.cpp` (`SetActiveScene` only swaps the pointer), `Scene/Components/QuadRendererComponent.cpp` and `SpriteRendererComponent.cpp` (subscribe in `OnAttached()` without checking the scene).

**What is wrong** (found by reading the code, not reproduced in a running game):

1. `SceneUpdateEvent` and `SceneDrawEvent` are `EventBase` events with one static dispatcher per type, so they are not tied to a scene.
2. Every scene is deserialized and initialized when `SceneConfig` loads (`Scene::Initialize()` is called by the reflection loader), so `OnAttached()` runs for the components of **all** scenes and each one subscribes to the global events.
3. `SceneManager::SetActiveScene()` only changes which scene emits the events. When `ChessScene` is active, the quads and sprites that belong to `MainScene` (`config/SceneConfig.xml`: 3 `QuadRendererComponent` and 12 `SpriteRendererComponent` nodes there) should still receive `SceneDrawEvent` and be drawn on top of the chess board.
4. Game components avoid this on their own: `ChessBoardComponent` / `ChessPieceComponent` do nothing unless `GameManager::GetActive()` is `ChessGame`. The built-in renderer components have no such check.

**Check first:** start the game with Chess as the default game (`config/GamesConfig.xml`) and see whether `MainScene` content is visible in the viewport. If it is not, find what filters it and update this entry and `BECore/Scene/CLAUDE.md`.

**A fix has to cover:**

- a scene-scoped dispatch: either the scene's components subscribe only while their scene is active (attach/detach on `SetActiveScene`), or the event carries the emitting scene and handlers ignore foreign scenes;
- the same for `ClickableComponent` (global `SDLEvents::MouseButtonDownEvent`) and `ChangeToRandomColorDebugComponent` (global `ActionEvent`), which also react regardless of the active scene;
- a gtest with two scenes where only the active one draws and reacts (needs a renderer double or an event counter, so it can run without a window).

## Merge `task-system-wait` into `main`

**Where:** branch `task-system-wait`, 7 commits on top of `main` (`3a93d7a5`, so a fast-forward is possible): `c5f08a8b`, `4555d3b4`, `a8515896`, `db8a467d`, `507431e1`, `655b2972`, `eeb320fe`. Nothing is pushed; `main` is 21 commits ahead of `origin/main`.

**State:**

- [x] Commit: the TaskSystem work (blocking `Wait()`, linked `CancellationToken`s, handle lifetime, derived status, `Start()`/`Cancel()` race, shutdown abandon) is committed. The only untracked file left is `.claude/plans/typed-jumping-toast.md`, which belongs to another task.
- [ ] Merge into `main`: `git switch main && git merge --ff-only task-system-wait`, then re-run `cmake --build --preset macos-debug --target MyGame BagiEngineTests` and `ctest --preset macos-debug` (136 tests expected).

**Not decided yet:** pushing `main`; what to do with `stash@{0}` ("On main: auto").
