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

`LogEvent::Flush()` is called once per frame from `Application::Run()` and right after `Fatal`. A message logged from a worker thread appears at the next flush.

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

If the config yields no sinks, `LoggerManager` falls back to a `ConsoleSink`.

## Best Practices

- Use the macros: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, `LOG_FATAL`.
- The macros take one string, so format first: `LOG_ERROR("Failed to load {}"_format(path))` (or `Format(...)`). `LOG_INFO("x {}", v)` does not compile.
- Pick the level deliberately; `Fatal` is for unrecoverable errors.
- Sinks receive `eastl::string_view`.

## Don't

- Store the `string_view` from a `LogEvent`: it points into the ring buffer. Copy it if needed.
- Emit `LogEvent` directly: use `Logger` / the `LOG_*` macros.
- Create sinks manually: let `LoggerManager` build them from the config. (`InitializeCoreServices()` creates one temporary `OutputSink` before the config exists; that is the only exception.)
