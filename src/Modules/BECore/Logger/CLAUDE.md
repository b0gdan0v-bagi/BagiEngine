# Logger Module Guidelines

## Overview

Event-based logging. `Logger` pushes entries into a lock-free ring buffer, `LogEvent::Flush()` drains it on the main thread and dispatches `LogEvent` to the sinks.

## Architecture

| Component | Purpose |
|-----------|---------|
| `Logger` | Static helper (`Debug` / `Info` / `Warning` / `Error` / `Fatal`): `(site, StringView)` raw, `(site, "fmt {}", args...)` compile-time checked and formatted on the stack |
| `LogEvent` | Event with level, message and site; `Emit` writes to the ring buffer, `Flush` dispatches it |
| `LogSite` | `file` / `line` of a log call; `file` is the `__FILE__` literal, kept as a pointer through the ring buffer |
| `LogRingBuffer` | Lock-free MPSC buffer, safe from any thread; entries are dropped when it is full |
| `FlushLogsEvent` | Tells sinks to flush their output |
| `ILogSink` | Interface for output destinations (`FACTORY_BASE`) |
| `LoggerManager` | Creates and sorts sinks from `LoggerConfig`, accessed via `CoreManager::GetLoggerManager()` |

`LogEvent::Flush()` is called once per frame from `Application::RunFrame()` and right after `Fatal` on the main thread. A message logged from a worker thread appears at the next flush.

## Log Levels

`Debug` → `Info` → `Warning` → `Error` → `Fatal`. `Fatal` flushes the buffer and the sinks immediately on the main thread (`Logger::FlushOnMainThread()`: `Flush` has one consumer); `LOG_FATAL` from a worker only pushes, and the frame flush delivers the line. The log line of a failed `ASSERT` / `FATALERROR` (`LogHandler`) carries the site of the assert and is flushed the same way; an `EXPECT` waits for the frame flush. Tests: `tests/BECore/LoggerFatalTest.cpp`.

## Built-in Sinks

| Sink | Output | Options |
|------|--------|---------|
| `ConsoleSink` | stdout/stderr with ANSI colors (only when that stream is a terminal) | `colorEnabled` |
| `FileSink` | Thread-safe file logging through a `TextFileWriter` (a line is formatted into its buffer, no `String` per line; written out when the buffer is full, at `FlushLogsEvent` once per frame, and at destruction; a file that cannot be opened is one `LOG_WARNING`, the sink then writes nothing; a write error stops it quietly) | `filename`, `append` |
| `OutputSink` | Platform debug output (`OutputDebugString` on Windows, stderr on macOS; nothing on the web); a line is built on the stack by `FormatOutputLine` (cut at `kOutputLineSize`, NUL-terminated) | none |
| `LogHistorySink` | Keeps the last lines in a `LogHistory` for the editor's Console panel | `capacity` (default 2048, clamped to 64..65536 when `Initialize()` builds the history) |

## Sink Configuration

File: `config/LoggerConfig.xml`, entries `<sink .../>`:
- `type`: the sink **class name** (`OutputSink`, `ConsoleSink`, `FileSink`, `LogHistorySink`), resolved by `AbstractFactory<ILogSink>`
- `enabled`: enable/disable
- `priority`: processing order (lower runs first; equal priorities keep the order of the file). `EventChannel` calls the last connected listener first and a disconnect moves its last entry into the freed slot, so the order the deserializer left depends on the file. `LoggerManager::Initialize` sorts the sinks (stable), disconnects every sink (`ILogSink::Disconnect()`) and then calls `Initialize()` from the highest number to the lowest; a sink's `Initialize()` must connect on every call (`UnsubscribeAll()` first) and keep one-time setup behind its own flag
- `minLevel`: minimum level to process

A `type` that is not registered in the factory is logged as an error by `MakeFromConfig` (`Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'`) and the entry is dropped. A `sink` without a `type` is dropped without a message. If the config yields no sinks, `LoggerManager` logs a warning and falls back to a `ConsoleSink`. The error and the warning are written while the sinks are still being built, so they sit in the ring buffer and reach the sinks at the first `LogEvent::Flush()`.

`GetSinks()` and `GetSink<T>()` of `LoggerManager` are public (`CoreManager::GetLoggerManager().GetSink<ConsoleSink>()`). Tests: `tests/BECore/FileSinkTest.cpp` (the line a `FileSink` writes), `tests/BECore/LoggerSinkOrderTest.cpp` (the order sinks are served in), `tests/BECore/LoggerConfigTest.cpp` (unknown type reported and dropped, the fallback warning), `tests/BECore/ConfigEditorSaveTest.cpp` (only with `BE_WITH_EDITOR`; the round trip of the Config Editor (`src/Widgets/ConfigEditor/CLAUDE.md`): every sink type it offers is chosen, saved to a temporary file, reloaded and built into a `LoggerManager`). Tests of the history: `tests/BECore/LogSiteTest.cpp` (the site through the macros and the ring), `LogHistoryTest.cpp` (the drop, the counts, the categories), `LogHistorySinkTest.cpp` (the sink from XML, a line from a worker thread). A test that builds sinks points every `FileSink` `filename` into a temporary directory and lets the `LoggerManager` go out of scope before it ends.

## The history

`LogHistory` (`LogHistory.h`, plain C++) is a ring of `capacity` records with the level, the `LogSite`, the time text, a category and a copy of the message; it counts the kept records per level and says when it changed (`GetVersion()`). `LogHistorySink` fills it from `LogEvent` (`FormatLogTimeNow` makes the clock text of a line; `ControlLog` is a second subscriber, with a history of its own for the control API: `.claude/rules/control-qa-api.md`), so it lives on the main thread, where `Flush()` delivers lines, and takes no lock; whoever reads or clears it (the panel) is on that thread too. A full history reuses the oldest slot's string, so a line that fits allocates nothing.

- A line has no category of its own: it is the module folder of the site's file (`LogCategoryOfFile`: after `Modules/`, else after the last `src/`), `General` without one, at most `kMaxLogCategories` kept and the rest `Other`. `sequence` of a record only grows, also across `Clear()`.
- A game that ships its own `LoggerConfig.xml` replaces the engine's whole file, so it has to add the `LogHistorySink` entry itself; without it `GetSink<LogHistorySink>()` is null and the Console panel has nothing to show (it says so).
- The web overlay has no history sink (`tests/BECore/WebConfigTest.cpp` expects its one console sink).

## Best Practices

- Use the macros: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, `LOG_FATAL`; they call `Logger::Info(BE_LOG_SITE, args...)` (site first, so the arguments are variadic). The macros pass the site; a direct `Logger::Info(msg)` or `LogEvent::Emit(level, msg)` has none (`file == nullptr`). `ILogSink::Write(level, message)` does not carry it: a sink that wants it reads it from the `LogEvent`.
- `LOG_ERROR("Failed to load {}", path)`: the format string is checked at compile time and formatted into a stack buffer of one ring cell (`LogEntry::MaxMessageSize - 1`, cut like a plain line), so it allocates on no thread. A wrong placeholder count or a type with no formatter does not compile (an extra argument is ignored by {fmt}); ctest `BagiLogFormatCompileFail`. `LOG_ERROR(text)` with one argument is logged raw, no parsing: a `{` in data is safe, and `LOG_ERROR("x {}")` prints `{}`. The line is formatted before the ring claim, so a slow `Formatter` never holds up the consumer. Never `LOG_ERROR(Format(...))` / `LOG_ERROR("..."_format(...))`: it still compiles but builds a heap `String` per line, and the extended ctest `BagiLogCalls` fails on it (`tools/codemod/codemod.py --family log` rewrites it). Tests: `tests/BECore/LoggerFormatTest.cpp`.
- Pick the level deliberately; `Fatal` is for unrecoverable errors.
- Sinks receive `StringView`.
- The sinks get the time of a line from `LogTime.h` (`GetLocalLogTime()`: the system clock plus the UTC offset that `InitLogClock()` reads once, which `LoggerManager::Initialize` calls before the sinks, because the first `localtime_r` of a process allocates; a change of the zone or a DST switch during a run shows from the next start; `FormatLogTimeNow` of `LogHistory.h` makes the `HH:MM:SS.mmm` text) and write it as `{:02}:{:02}:{:02}` of `tm_hour`, `tm_min`, `tm_sec` (`FileSink` also the date), not `{:%H:%M:%S}` of `<fmt/chrono.h>`: the Clang of Emscripten refuses the `FMT_STRING` that header uses inside, and nothing else needs the header. Do not bring it back.

## Don't

- Store the `string_view` from a `LogEvent`: it points into the ring buffer. Copy it if needed.
- Emit `LogEvent` directly: use `Logger` / the `LOG_*` macros.
- Create sinks manually: let `LoggerManager` build them from the config. (`InitializeCoreServices()` creates one temporary `OutputSink` before the config exists; that is the only exception.)
