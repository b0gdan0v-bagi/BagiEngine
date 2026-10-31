---
paths:
  - "src/Modules/BECore/**/*"
---

# BECore Module Guidelines

## CoreManager

Central hub for core subsystems. Access via static methods:
- `GetFileSystem()`, `GetConfigManager()`, `GetLoggerManager()`
- `GetAssertHandlerManager()`, `GetTestManager()`, `GetResourceManager()`

**Initialization order** (`InitializeCoreServices()`, called by `OnApplicationPreInit`):
1. `FileSystem` → 2. `TaskManager` → 3. `ConfigManager` → 4. `LoggerManager` → 5. `AssertHandlerManager` → 6. `ResourceManager` → 7. `TextureLibrary`

`OnApplicationPreInit` then runs the runtime `ITest`s through `TestManager`. The GoogleTest executable (`tests/`) brings up the same services through `OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>)`, which skips `TestManager` and disables `DebugBreakHandler` so a failed assert is reported by gtest instead of trapping the process.

## ConfigManager

**DO**: Use `ConfigManager` for all config access:
```cpp
auto rootNode = CoreManager::GetConfigManager().GetConfig("LoggerConfig"_intern);
```

**DON'T**: Create config loaders manually or call `LoadFromVirtualPath()` directly.

**Features:**
- Loads all configs from `config/` recursively at startup (parallel via TaskManager)
- Identifies by filename stem (without `.xml`); returns `XmlNode` (backed by `XmlDocument`)
- Thread-safe access; configs cached in `UnorderedPoolMap<IntrusivePtrAtomic<XmlDocument>>`

## Assert (CRTDebugHook, StackTrace)

- **CRTDebugHook** (`InstallCRTDebugHooks()`): Captures CRT debug reports (heap corruption, invalid params) and stack traces. Windows Debug builds only; call early in init.
- **StackTrace** (`CaptureAndPrintStackTrace()`): Cross-platform stack capture for assertion failures; uses DbgHelp/PDB on Windows.

## Pre-Init Log Buffer

Logs before `LoggerManager::Initialize()` are buffered and replayed when sinks are ready. Ensures no logs lost during early init (TaskManager, ConfigManager).

## PoolString Usage

Use for: config keys, identifiers, map keys  
Don't use for: temporary strings, user-facing text

## File Organization

- One class per file: `ClassName.h` + `ClassName.cpp`
- Tests: GoogleTest tests in `tests/<Module>/` (see `tests-module.md`); only runtime `ITest`s live in `BECore/Tests/`

## Checklist

- [ ] Add to `pch.h` if widely used
- [ ] Update `CMakeLists.txt`
- [ ] Add Doxygen comments for public API
