# BECore Module Guidelines

## CoreManager

Central service locator (`GameManager/CoreManager.h`): one static getter per subsystem (`GetFileSystem()`, `GetConfigManager()`, `GetLoggerManager()`, `GetResourceManager()`, `GetTaskManager()`, `GetSceneManager()`, `GetWidgetManager()`, `GetRenderer()`, `GetTextureLibrary()`, `GetTime()`, ...). A new global service goes here, not into another `Singleton`.

**Lifecycle** (called from `Application`, each takes `PassKey<Application>`):

| Call | Does |
|------|------|
| `OnApplicationPreInit` | `InitializeCoreServices()`, then runs the enabled runtime `ITest`s through `TestManager` |
| `OnApplicationInit` | creates the widgets of the run mode (see below), builds `SceneManager` from `SceneConfig` |
| `OnGameCycle` | events → control commands (`ControlService::Drain`) → `TaskManager::Update` → scene update → widgets update → queued events → `DrawScreenAll` → `DrawAll` |
| `OnIdleCycle` | the manual clock between steps instead of `OnGameCycle`: events → `Drain` → wait for a request (up to 8 ms). No frame runs |
| `OnApplicationDeinit` | `ControlService::Shutdown`, `TaskManager::Shutdown` |

**`InitializeCoreServices()` order:** CRT debug hooks → temporary `OutputSink` + `StackTraceHandler` → `FileSystem` → `TaskManager` → `ConfigManager` → `LoggerManager` → `AssertHandlerManager` → `ResourceManager` → `TextureLibrary` → `FontAtlasLibrary` → `UIEnvironment` → the builtin control methods (`RegisterControlBuiltins`).

## Run mode

`mode` in `config/ApplicationConfig.xml` (`RunMode`, `Application/RunMode.h`) picks the widget set that `WidgetManager::CreateWidgets()` creates; a missing or unknown value is `Editor` (logged as a warning).

| Mode | Widgets from | What runs |
|------|--------------|-----------|
| `Editor` (engine default) | `config/WidgetsConfig.xml` | ImGui, the editor panels, the scene in `ViewportWidget` (offscreen target) |
| `Game` | `config/GameWidgetsConfig.xml` | `ClearScreenWidget` and `SceneRenderWidget`: no ImGui, the scene is drawn straight into the back buffer |

`--headless` (`Control/CLAUDE.md`) needs Game mode: `Application::Initialize` refuses the Editor mode with it. In Game mode `SDLEventsProvider` forwards every mouse event unchanged (window coordinates) because there is no ImGui context to capture it; in Editor mode `ViewportWidget` re-emits them with viewport-local coordinates. Both go through `CoreManager::GetUIInput()` first, so a UI canvas can consume an event before game code sees it (`SDL/CLAUDE.md`, "Input and window metrics"). Either way the position is in render output pixels, not scene units: a scene with a view size is scaled to the output, so a handler maps the position with `Scene::ScreenToSceneX/Y` (`Scene/CLAUDE.md`, "View size"). Code that touches ImGui outside a widget must check `ImGui::GetCurrentContext()` first. A game's HUD is not ImGui: it is a UI canvas drawn by the engine (`UI/CLAUDE.md`), so it works in both run modes.

A game starts in Game mode by shipping its own `config/ApplicationConfig.xml` with `mode="Game"`. A game's file replaces the engine's whole file, so it repeats `type`, `renderer` and `path`. To open a game in the editor, set `mode="Editor"` there. A game can also ship its own `GameWidgetsConfig.xml` (for example to add `ImGuiWidget` for a HUD). Tests: `tests/BECore/RunModeTest.cpp`, `tests/BECore/SDLEventsProviderTest.cpp`.

## Frame order and time

`Application::Run` repeats this once per frame:

| # | Stage | Where | Does |
|---|-------|-------|------|
| 0 | Clock | `Time::Tick` (real clock) or `Time::Advance(step seconds)` (manual clock) | Measures or sets the frame time; every value below is fixed for the rest of the frame |
| 1 | Input | `EventsProviderManager::ProcessEvents` | Window, mouse and keyboard events become `SDLEvents`, the mouse through `UIInput` first; `InputEvents::ActionEvent` comes from components such as `ClickableComponent` |
| 1b | Commands | `ControlService::Drain` | Requests of the external control API run here, so they see this frame's input (`Control/CLAUDE.md`) |
| 2 | Tasks | `TaskManager::Update` | Delayed tasks that are due and the tasks queued for the main thread |
| 3 | Logic | `SceneManager::UpdateAll` | `SceneFixedUpdateEvent` (zero or more, a constant step), then `SceneUpdateEvent` (once, with the frame time) |
| 4 | Render | `WidgetManager::UpdateAll`, `DrawScreenAll`, `DrawAll` | Editor: UI and `Scene::Draw()` through `ViewportWidget`, then ImGui render. Game: `Scene::Draw()` through `SceneRenderWidget::DrawScreen`. `Scene::Draw()` calls `IComponent::OnDraw()` in layer order (`Scene/CLAUDE.md`). Present at the end of the frame |

Logic reads input and writes state; render only reads state. Do not draw from `SceneUpdateEvent` handlers and do not change game state from `OnDraw()`.

After the present and `LogEvent::Flush()` the loop calls `ControlService::EndFrame()`: it runs the frame end hooks and counts down a control call that waits for frames (a `step`, a `screenshot`).

With the manual clock (`--manual-clock`, `Control/CLAUDE.md`) `Application::Run` skips the whole frame unless a `step` or another deferred control call asks for one, and calls `CoreManager::OnIdleCycle` instead. The frame it does run is the same as above, so game logic cannot tell the clocks apart except that `deltaTime` is what the client asked for.

**Time** (`CoreManager::GetTime()`, `Time/Time.h`): `GetDeltaTime()` is the seconds the last frame took, `GetTotalTime()` the game time since the first frame, `GetFrameCount()` counts frames from 1. The frame time is clamped to `Time::kMaxFrameTime` (0.25 s), so a pause in the debugger is one long frame, not a jump. Scale movement and animation by `SceneUpdateEvent::deltaTime`, never by "per frame" constants. Logic that must give the same result at any frame rate (physics-like integration, replays) goes into `SceneFixedUpdateEvent`: its `deltaTime` is always `Time::GetFixedDeltaTime()` (1/60 s by default), and the leftover time carries over to the next frame, so the number of steps per second is constant.

`Time::Advance(seconds)` takes the frame time as a number, so a test drives a component with a fake clock at any frame rate: `CoreManager::GetTime().Advance(1.0 / 30)`, then `SceneManager::UpdateAll()`. Reset the clock in `TearDown` (`Time::Reset()`, and `SetFixedDeltaTime` back if the test changed it). Example: `tests/BECore/TimeTest.cpp`.

The GoogleTest executable (`tests/`) brings up the same services through `OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>)`. That path skips `TestManager` and disables `DebugBreakHandler`, so a failed assert is reported by gtest instead of trapping the process. See `.claude/rules/tests-module.md`.

## Subsystem guides

Nested `CLAUDE.md` files in `BECore/`: `Config/` (ConfigManager, `XmlNode`, `MakeFromConfig`), `Control/` (external control API core, JSON rules), `FileSystem/` (virtual paths and mount points), `Scene/` (scene graph and components), `Logger/`, `Resource/`, `Text/` (fonts, layout, glyph atlases, `TextComponent`), `UI/` (in-game UI: canvases, adaptive layout, validator). Always read configs through `ConfigManager`, never by creating loaders by hand.

## Assert (CRTDebugHook, StackTrace)

- **CRTDebugHook** (`InstallCRTDebugHooks()`): captures CRT debug reports (heap corruption, invalid params) with a stack trace. Windows Debug builds only; installed first in `InitializeCoreServices()`.
- **StackTrace** (`CaptureAndPrintStackTrace()`): cross-platform stack capture for assertion failures; uses DbgHelp/PDB on Windows.
- Macros, handlers and `AssertEvent`: `.claude/rules/tests-module.md`.

## Logging before the sinks exist

`LogEvent::Emit` only pushes into a lock-free global ring buffer; sinks get entries when `LogEvent::Flush()` runs on the main thread (once per frame, and after `Fatal`). Logs written during early init therefore reach the sinks once `LoggerManager` has created them. Details in `Logger/CLAUDE.md`.

## File Organization

- One class per file: `ClassName.h` + `ClassName.cpp`.
- GoogleTest tests live in `tests/<Module>/` (a game's in `games/<Name>/tests/`); only runtime `ITest`s live in `BECore/Tests/`.
- Reflected engine classes (`BE_CLASS`, `BE_EVENT`) need their directory in `source_dirs` / `scan_dirs` of `CI/meta_generator/reflection_targets.json`, then re-run `cmake --preset ...`. A game's reflected classes are covered by its own `be_add_game_reflection`.
- Document the public API with Doxygen comments.
