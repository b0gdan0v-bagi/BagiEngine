# Assert and logger: handlers and sinks run against their configured priority; assert and log paths are not safe off the main thread

- Version: 0.2.0
- Entry: assert-logger-handler-order
- Date: 2026-11-16
- Bump: minor

## Added
- `IAssertHandler::Disconnect()` and `ILogSink::Disconnect()`: drop every connection of a handler or sink.
- `Logger::FlushOnMainThread()`: delivers pending lines to the sinks when called on the main thread.
- `Logger/LogTime.h`: `GetLocalLogTime()`, the one local-time helper of the sinks.

## Changed
- Assert handlers and log sinks now run in their configured priority order (lower first, equal ones in config order), as the configs and docs always said; before, the order was reversed, so `DebugBreakHandler` trapped before `LogHandler` logged.
- The log line of an assert carries its file and line and is flushed before a trap or crash.
- `AssertEvent` is safe to emit from any thread (one lock; handlers run one at a time on the failing thread); DbgHelp on Windows is serialised.
- `Logger::Fatal` / `LOG_FATAL` flush immediately only on the main thread; from a worker the line goes out at the next frame flush.
- `FileSink::Write` prints exactly the view it is given.

## Breaking
- `IAssertHandler::Initialize()` and `ILogSink::Initialize()` are now called again by the managers, after a disconnect, to fix the delivery order: an override that connects only once (behind an `_initialized` flag) ends up disconnected. Adapt: connect on every call of `Initialize()` and keep only one-time setup behind a flag of your own.
- A game whose own sinks or handlers relied on the old reversed order now sees them in config order. Adapt: check the `priority` values of the game's `LoggerConfig.xml` / `AssertHandlersConfig.xml` (lower runs first).
