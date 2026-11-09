# Logger Module Guidelines

## Overview

Event-based logging. `Logger` pushes entries into a lock-free ring buffer, `LogEvent::Flush()` drains it on the main thread and dispatches `LogEvent` to the sinks.

## Architecture

| Component | Purpose |
|-----------|---------|
| `Logger` | Static helper (`Debug` / `Info` / `Warning` / `Error` / `Fatal`), takes one `eastl::string_view` |
| `LogEvent` | Event with level and message; `Emit` writes to the ring buffer, `Flush` dispatches it |
| `LogRingBuffer` | Lock-free MPSC buffer, safe from any thread; entries are dropped when it is full |
| `FlushLogsEvent` | Tells sinks to flush their output |
| `ILogSink` | Interface for output destinations (`FACTORY_BASE`) |
| `LoggerManager` | Creates and sorts sinks from `LoggerConfig`, accessed via `CoreManager::GetLoggerManager()` |

`LogEvent::Flush()` is called once per frame from `Application::RunFrame()` and right after `Fatal`. A message logged from a worker thread appears at the next flush.

## Log Levels

`Debug` → `Info` → `Warning` → `Error` → `Fatal`. `Fatal` flushes the buffer and the sinks immediately.

## Built-in Sinks

| Sink | Output | Options |
|------|--------|---------|
| `ConsoleSink` | stdout/stderr with ANSI colors | `colorEnabled` |
| `FileSink` | Thread-safe file logging | `filename`, `append` |
| `OutputSink` | Platform debug output (`OutputDebugString` on Windows, stderr on macOS) | none |

## Sink Configuration

File: `config/LoggerConfig.xml`, entries `<sink .../>`:
- `type`: the sink **class name** (`OutputSink`, `ConsoleSink`, `FileSink`), resolved by `AbstractFactory<ILogSink>`
- `enabled`: enable/disable
- `priority`: processing order (lower runs first)
- `minLevel`: minimum level to process

A `type` that is not registered in the factory is logged as an error by `MakeFromConfig` (`Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'`) and the entry is dropped. A `sink` without a `type` is dropped without a message. If the config yields no sinks, `LoggerManager` logs a warning and falls back to a `ConsoleSink`. The error and the warning are written while the sinks are still being built, so they sit in the ring buffer and reach the sinks at the first `LogEvent::Flush()`.

`GetSinks()` and `GetSink<T>()` of `LoggerManager` are public (`CoreManager::GetLoggerManager().GetSink<ConsoleSink>()`). Tests: `tests/BECore/LoggerConfigTest.cpp` (unknown type reported and dropped, the fallback warning), `tests/BECore/ConfigFieldSchemaTest.cpp` (only with `BE_WITH_EDITOR`; the Config Editor offers exactly the factory names), `tests/BECore/ConfigEditorSaveTest.cpp` (only with `BE_WITH_EDITOR`; the round trip: every offered type is staged, saved to a temporary file, reloaded and built into a `LoggerManager`; a value outside the dropdown is reported on reload). A test that builds sinks points every `FileSink` `filename` into a temporary directory and lets the `LoggerManager` go out of scope before it ends.

## Best Practices

- Use the macros: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, `LOG_FATAL`.
- The macros take one string, so format first: `LOG_ERROR("Failed to load {}"_format(path))` (or `Format(...)`). `LOG_INFO("x {}", v)` does not compile.
- Pick the level deliberately; `Fatal` is for unrecoverable errors.
- Sinks receive `eastl::string_view`.

## Don't

- Store the `string_view` from a `LogEvent`: it points into the ring buffer. Copy it if needed.
- Emit `LogEvent` directly: use `Logger` / the `LOG_*` macros.
- Create sinks manually: let `LoggerManager` build them from the config. (`InitializeCoreServices()` creates one temporary `OutputSink` before the config exists; that is the only exception.)
