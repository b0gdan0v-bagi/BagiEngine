# BECore Module Guidelines

## CoreManager

Central service locator (`GameManager/CoreManager.h`): one static getter per subsystem (`GetFileSystem()`, `GetConfigManager()`, `GetLoggerManager()`, `GetResourceManager()`, `GetTaskManager()`, `GetSceneManager()`, `GetWidgetManager()`, `GetRenderer()`, `GetTextureLibrary()`, ...). A new global service goes here, not into another `Singleton`.

**Lifecycle** (called from `Application`, each takes `PassKey<Application>`):

| Call | Does |
|------|------|
| `OnApplicationPreInit` | `InitializeCoreServices()`, then runs the enabled runtime `ITest`s through `TestManager` |
| `OnApplicationInit` | creates widgets, builds `SceneManager` from `SceneConfig` |
| `OnGameCycle` | events → `TaskManager::Update` → scene update → widgets update → queued events → `DrawScreenAll` → `DrawAll` |
| `OnApplicationDeinit` | `TaskManager::Shutdown` |

**`InitializeCoreServices()` order:** CRT debug hooks → temporary `OutputSink` + `StackTraceHandler` → `FileSystem` → `TaskManager` → `ConfigManager` → `LoggerManager` → `AssertHandlerManager` → `ResourceManager` → `TextureLibrary`.

The GoogleTest executable (`tests/`) brings up the same services through `OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>)`. That path skips `TestManager` and disables `DebugBreakHandler`, so a failed assert is reported by gtest instead of trapping the process. See `.claude/rules/tests-module.md`.

## ConfigManager

**DO**: use `ConfigManager` for all config access:
```cpp
auto rootNode = CoreManager::GetConfigManager().GetConfig("LoggerConfig"_intern);
```

**DON'T**: create config loaders manually or call `LoadFromVirtualPath()` directly.

- Loads every XML under `config/` recursively at startup, in parallel through `TaskManager`; `Initialize()` blocks until all are loaded.
- A config is identified by its filename stem (without `.xml`); `GetConfig` returns an `XmlNode` (invalid node when not found).
- Configs are cached in `UnorderedPoolMap<ConfigEntry>`; writes are serialized by a mutex.

## Assert (CRTDebugHook, StackTrace)

- **CRTDebugHook** (`InstallCRTDebugHooks()`): captures CRT debug reports (heap corruption, invalid params) with a stack trace. Windows Debug builds only; installed first in `InitializeCoreServices()`.
- **StackTrace** (`CaptureAndPrintStackTrace()`): cross-platform stack capture for assertion failures; uses DbgHelp/PDB on Windows.
- Macros, handlers and `AssertEvent`: `.claude/rules/tests-module.md`.

## Logging before the sinks exist

`LogEvent::Emit` only pushes into a lock-free global ring buffer; sinks get entries when `LogEvent::Flush()` runs on the main thread (once per frame, and after `Fatal`). Logs written during early init therefore reach the sinks once `LoggerManager` has created them. Details in `Logger/CLAUDE.md`.

## File Organization

- One class per file: `ClassName.h` + `ClassName.cpp`.
- GoogleTest tests live in `tests/<Module>/`; only runtime `ITest`s live in `BECore/Tests/`.
- Reflected classes (`BE_CLASS`, `BE_EVENT`) need their directory in `source_dirs` / `scan_dirs` of `CI/meta_generator/reflection_targets.json`, then re-run `cmake --preset ...`.
- Document the public API with Doxygen comments.
