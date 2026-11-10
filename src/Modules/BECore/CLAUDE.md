# BECore Module Guidelines

## CoreManager

Central service locator (`GameManager/CoreManager.h`): one static getter per subsystem (`GetFileSystem()`, `GetConfigManager()`, `GetLoggerManager()`, `GetResourceManager()`, `GetTaskManager()`, `GetSceneManager()`, `GetWidgetManager()`, `GetRenderer()`, `GetTextureLibrary()`, `GetTime()`, `GetTweens()`, ...). A new global service goes here, not into another `Singleton`.

**Lifecycle** (called from `Application`, each takes `PassKey<Application>`):

| Call | Does |
|------|------|
| `OnApplicationPreInit` | `InitializeCoreServices()`, then runs the enabled runtime `ITest`s through `TestManager` |
| `OnApplicationInit` | creates the widgets of the run mode (see below), builds `SceneManager` from `SceneConfig` |
| `OnGameCycle` | events → control commands (`ControlService::Drain`) → `TaskManager::Update` → `TweenManager::Advance` → scene update → widgets update → queued events → `DrawScreenAll` → `DrawAll` |
| `OnIdleCycle` | the manual clock between steps instead of `OnGameCycle`: events → `Drain` → wait for a request (up to 8 ms). No frame runs |
| `OnApplicationDeinit` | `TweenManager::Clear` (no callbacks), `ControlService::Shutdown`, `TaskManager::Shutdown` |

`Application::Cleanup` runs before `OnApplicationDeinit`, in this order: stop the control server (when `--control` started one), `GameManager::Shutdown`, `FontAtlasLibrary::Clear()`, `WidgetManager::DestroyAll()`, then `ApplicationCleanUpEvent` (its handlers: the renderer and the window, and the other cleanup handlers such as `RmlUiRuntime::Shutdown` and `RenderProbeComponent`), then `OnApplicationDeinit`. The font atlas and the widgets own GPU objects (glyph pages, the viewport's target, ImGui's descriptors), so they are direct calls and not handlers of `ApplicationCleanUpEvent`: EnTT calls the handlers in the reverse order of the subscription, so the renderer's `Destroy` would run first and free the device under them. `DestroyAll` waits for the GPU, then destroys the widgets last-created-first (the viewport's target needs the ImGui context of `ImGuiWidget`, which is created before it).

**`InitializeCoreServices()` order:** CRT debug hooks → temporary `OutputSink` + `StackTraceHandler` → `FileSystem` → `TaskManager` → `ConfigManager` → `LoggerManager` → `AssertHandlerManager` → `ResourceManager` → `TextureLibrary` → `FontAtlasLibrary` → `UIEnvironment` → `UIStrings` (loads `config/UIStrings.xml` when a game ships one) → the builtin control methods (`RegisterControlBuiltins`, then `RegisterUIControl`).

## Run mode

`mode` in `config/ApplicationConfig.xml` (`RunMode`, `Application/RunMode.h`) picks the widget set that `WidgetManager::CreateWidgets()` creates; a missing or unknown value is `Editor`. The mode is a field of the reflected `ApplicationConfig` (`Application/ApplicationConfig.h`, `ApplicationConfig::_mode`, read by `LoadApplicationConfig()`): the **requested** mode, exactly what the config says; an unknown value keeps `Editor` and is one `LOG_ERROR` (`Config 'ApplicationConfig': mode: Invalid enum value: 'x'`) that `Application::Initialize` writes once. `ResolveRunMode` returns the mode that **runs** in this build.

| Mode | Widgets from | What runs |
|------|--------------|-----------|
| `Editor` (engine default) | `config/WidgetsConfig.xml` | ImGui, the editor panels, the scene in `ViewportWidget` (offscreen target) |
| `Game` | `config/GameWidgetsConfig.xml` | `ClearScreenWidget` and `SceneRenderWidget`: no ImGui, the scene is drawn straight into the back buffer |

**The build decides.** The editor panels are compiled only with `BE_WITH_EDITOR` (`kEditorBuilt` in `RunMode.h`; the CMake option and its defaults: root `CLAUDE.md`, "Build Commands"; Debug builds always have the editor, a single-config Release does not). A build without the editor has only `Game`: `Editor` requested there is `Game`, with one `LOG_WARNING` ("... this build has no editor (BE_WITH_EDITOR=OFF), starting in Game mode"), not an error and not a refusal, so the engine's own default `mode="Editor"` starts a Release build. `CreateWidgets()` resolves the mode and logs `Run mode: <resolved mode>`.

`--headless` (`Control/CLAUDE.md`) needs Game mode: `Application::Initialize` refuses the Editor mode with it, but only in a build with the editor (`kEditorBuilt && config._mode == Editor`, so the warning of `ResolveRunMode` is logged once, by `CreateWidgets`; without the editor the request falls back to Game and `--headless` starts). In Game mode `SDLEventsProvider` forwards every mouse event (SDL's window units scaled by the pixel density into output pixels) because there is no ImGui context to capture it; in Editor mode `ViewportWidget` re-emits them with viewport-local coordinates. Both go through `CoreManager::GetUIInput()` first, so a UI canvas can consume an event before game code sees it (`SDL/CLAUDE.md`, "Input and window metrics"). Either way the position is in render output pixels, not scene units: a scene with a view size is scaled to the output, so a handler maps the position with `Scene::ScreenToSceneX/Y` (`Scene/CLAUDE.md`, "View size"). Code that touches ImGui outside a widget must check `ImGui::GetCurrentContext()` first. A game's HUD is not ImGui: it is a UI canvas drawn by the engine (`UI/CLAUDE.md`), so it works in both run modes.

A game starts in Game mode by shipping its own `config/ApplicationConfig.xml` with `mode="Game"`. A game's file replaces the engine's whole file, so it repeats `type`, `renderer` and `path`. To open a game in the editor, set `mode="Editor"` there (in a build with the editor). A game can also ship its own `GameWidgetsConfig.xml` (for example to add `ImGuiWidget` for a HUD). Tests: `tests/BECore/RunModeTest.cpp`, `tests/BECore/WidgetManagerTest.cpp`, `tests/BECore/SDLEventsProviderTest.cpp`.

**Switching at run time** (only in a build with the editor; `WidgetManager::RequestMode` / `RequestToggle` / `ApplyPendingMode` exist only under `#if BE_WITH_EDITOR`, `GetMode()` always):

- **Ctrl+Shift+E** (`IsEditorToggle`: key `SDLK_E`, left or right Ctrl and Shift, no Alt or Cmd, no key repeat; constants in `RunMode.h`) switches Game and Editor. `Application` reads it from the raw `SDLEvents::SDLEventWrapper`, which is emitted before the ImGui capture check, so the chord also works while an ImGui text field has the keyboard. It only stores a request (`RequestToggle`; a later request wins, asking for the running mode clears the request, two toggles in a row cancel out).
- The switch is applied at the end of the frame, after the present and before `LogEvent::Flush()` (`Application::RunFrame`), never inside `UpdateAll` / `DrawScreenAll` / `DrawAll`: ImGui's frame is open there and a widget cannot be destroyed in the middle of its own call. The manual-clock idle branch has no frame, so it applies nothing.
- The order of `ApplyPendingMode`: (1) `IRenderer::WaitIdle()` (the GPU may still read what the widgets own; skipped without a renderer), (2) destroy every widget, last created first (their subscriptions go with them), (3) create the widgets of the new mode as `CreateWidgets` does, (4) log `Run mode: <name>`.
- Two widgets own global state and give it back when destroyed: `ImGuiWidget` shuts ImGui down (waits for the GPU first), `ViewportWidget` clears the safe-area override of `UIEnvironment` and releases its render target.
- **Lost at a switch:** unsaved edits of the editor panels (staged config changes), a mouse button pressed in the viewport and not yet released, the viewport's preview shape (aspect preset and safe-area simulation), the selection of the scene editor. Nothing remembers the last mode between runs; the mode at start is the config's.
- With `--headless` the chord is ignored with one warning (there is no window for an editor).
- Proven by `WidgetManagerTest` (the order of events with fake widgets, no GPU). Not proven in the tests: the real ImGui and Vulkan teardown and rebuild; the owner checks it by hand (switch ten times, quit in Editor mode, no Vulkan validation errors).

## Frame order and time

`Application::RunFrame` does this once per frame (`Run` repeats it until the application stops):

| # | Stage | Where | Does |
|---|-------|-------|------|
| 0 | Clock | `Time::Tick` (real clock) or `Time::Advance(step seconds)` (manual clock) | Measures or sets the frame time; every value below is fixed for the rest of the frame |
| 1 | Input | `EventsProviderManager::ProcessEvents` | Window, mouse and keyboard events become `SDLEvents`, the mouse through `UIInput` first; `InputEvents::ActionEvent` comes from components such as `ClickableComponent` |
| 1b | Commands | `ControlService::Drain` | Requests of the external control API run here, so they see this frame's input (`Control/CLAUDE.md`) |
| 2 | Tasks | `TaskManager::Update` | Delayed tasks that are due and the tasks queued for the main thread |
| 2b | Tweens | `TweenManager::Advance(dt)` | Every running tween moves by the frame time and its setters write the values, so the logic below reads them (`Animation/CLAUDE.md`) |
| 3 | Logic | `SceneManager::UpdateAll` | `SceneFixedUpdateEvent` (zero or more, a constant step), then `SceneUpdateEvent` (once, with the frame time) |
| 4 | Render | `WidgetManager::UpdateAll`, `DrawScreenAll`, `DrawAll` | Editor: UI and `Scene::Draw()` through `ViewportWidget`, then ImGui render. Game: `Scene::Draw()` through `SceneRenderWidget::DrawScreen`. `Scene::Draw()` draws in layer order: the system draws the quad and the sprite itself from the registry and calls `IComponent::OnDraw()` of the other components (`Scene/CLAUDE.md`). Present at the end of the frame |

Logic reads input and writes state; render only reads state. Do not draw from `SceneUpdateEvent` handlers and do not change game state from `OnDraw()`.

After the present and `LogEvent::Flush()` the loop calls `ControlService::EndFrame()`: it runs the frame end hooks and counts down a control call that waits for frames (a `step`, a `screenshot`).

With the manual clock (`--manual-clock`, `Control/CLAUDE.md`) `Application::RunFrame` skips the whole frame unless a `step` or another deferred control call asks for one, and calls `CoreManager::OnIdleCycle` instead. The frame it does run is the same as above, so game logic cannot tell the clocks apart except that `deltaTime` is what the client asked for.

**Time** (`CoreManager::GetTime()`, `Time/Time.h`): `GetDeltaTime()` is the seconds the last frame took, `GetTotalTime()` the game time since the first frame, `GetFrameCount()` counts frames from 1. The frame time is clamped to `Time::kMaxFrameTime` (0.25 s), so a pause in the debugger is one long frame, not a jump. Scale movement and animation by `SceneUpdateEvent::deltaTime`, never by "per frame" constants. Logic that must give the same result at any frame rate (physics-like integration, replays) goes into `SceneFixedUpdateEvent`: its `deltaTime` is always `Time::GetFixedDeltaTime()` (1/60 s by default), and the leftover time carries over to the next frame, so the number of steps per second is constant.

`Time::Advance(seconds)` takes the frame time as a number, so a test drives a component with a fake clock at any frame rate: `CoreManager::GetTime().Advance(1.0 / 32)` (a power of two keeps the sums exact; `Animation/CLAUDE.md`), then `SceneManager::UpdateAll()`. Reset the clock in `TearDown` (`Time::Reset()`, and `SetFixedDeltaTime` back if the test changed it). Example: `tests/BECore/TimeTest.cpp`.

The GoogleTest executable (`tests/`) brings up the same services through `OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>)`. That path skips `TestManager` and disables `DebugBreakHandler`, so a failed assert is reported by gtest instead of trapping the process. See `.claude/rules/tests-module.md`.

## Subsystem guides

Nested `CLAUDE.md` files in `BECore/`: `Animation/` (easing, `Lerp`, `TweenManager`, `Timeline`, `co_await` of a tween), `Config/` (ConfigManager, `XmlNode`, `MakeFromConfig`), `Control/` (external control API core, JSON rules), `FileSystem/` (virtual paths and mount points), `Save/` (save games: slots, the game id and version envelope), `Scene/` (scene graph and components), `Logger/`, `Resource/`, `Text/` (fonts, layout, glyph atlases, `TextComponent`), `UI/` (in-game UI: canvases, adaptive layout, validator). Always read configs through `ConfigManager`, never by creating loaders by hand.

## Assert (CRTDebugHook, StackTrace)

- **CRTDebugHook** (`InstallCRTDebugHooks()`): captures CRT debug reports (heap corruption, invalid params) with a stack trace. Windows Debug builds only; installed first in `InitializeCoreServices()`. Every other build is a no-op; only a Windows Release build prints one stderr line ("Skipped"). Other platforms print nothing, because the browser console shows stderr as an error and the web smoke rejects it.
- **StackTrace** (`CaptureAndPrintStackTrace()`): cross-platform stack capture for assertion failures; uses DbgHelp/PDB on Windows.
- **PlatformDebug.h** (`DebugBreak()`, `ENGINE_DEBUG_BREAK()`): the debug break per platform; on the web (`PLATFORM_WEB`) it is `__builtin_trap()` (the page stops with a wasm trap), the same as on Linux. `StackTrace.cpp` and `OutputSink.cpp` have no web branch of their own: their `#else` fallbacks (no trace, no output) are what the page gets.
- Macros, handlers and `AssertEvent`: `.claude/rules/tests-module.md`.

## Logging before the sinks exist

`LogEvent::Emit` only pushes into a lock-free global ring buffer; sinks get entries when `LogEvent::Flush()` runs on the main thread (once per frame, and after `Fatal`). Logs written during early init therefore reach the sinks once `LoggerManager` has created them. Details in `Logger/CLAUDE.md`.

## File Organization

- One class per file: `ClassName.h` + `ClassName.cpp`.
- GoogleTest tests live in `tests/<Module>/` (a game's in `games/<Name>/tests/`); only runtime `ITest`s live in `BECore/Tests/`.
- Reflected engine classes (`BE_CLASS`, `BE_EVENT`) need their directory in `source_dirs` / `scan_dirs` of `CI/meta_generator/reflection_targets.json`, then re-run `cmake --preset ...`. A game's reflected classes are covered by its own `be_add_game_reflection`.
- Document the public API with Doxygen comments.
