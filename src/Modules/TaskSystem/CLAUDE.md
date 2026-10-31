# TaskSystem Module Guidelines

## Overview

Coroutine-based asynchronous task execution (C++20 coroutines, no exceptions: results are `std::expected<T, TaskError>`).

## Key Components

| Component | Purpose |
|-----------|---------|
| `Task<T>` | Coroutine return type with `promise_type`; move-only |
| `TaskHandle<T>` | Handle for tracking and cancelling a task; `GetResult()` returns `std::expected`. `Wait()` blocks on a condition variable (no spinning) and also returns when the handle is cancelled; completion is signalled from the promise's `final_suspend` (`Detail::CompletionEvent`). `GetStatus()` is derived: a stored `Running` that is already done reads as `Completed`, and `Cancel()` on a finished task is a no-op that keeps its result |
| `TaskManager` | Singleton: `Run()`, `RunOnMainThread()`, `RunDelayed()`. The scheduled job owns a reference to the handle and `Start()` makes the handle keep itself alive until the coroutine finishes, so dropping the returned handle (fire and forget) is safe; keep it only to `Wait()`, `Cancel()` or read the result |
| `ThreadPool` | Worker threads with work stealing (`TaskQueue::TrySteal`) |
| `TaskScheduler` | Priority queues, delayed tasks |
| `CancellationToken` | Cancellation support (`Create()`, `CreateLinked()`, `IsCancelled()`). A linked child sees its parent's cancellation (checked by polling up the chain); cancelling the child does not touch the parent |
| `TaskGroup` | Manages related tasks: `Add()`, `WaitAll()`, `CancelAll()` |
| `EventAwaiter<E>` | Awaits an `EventBase` event |

`TaskPriority`: `Low`, `Normal`, `High`, `Critical`. `ThreadType`: `MainThread`, `Background`, `IO`, `Custom`. `Run()` defaults to `Normal` priority on `Background`.

## Awaitables

| Awaitable | Purpose |
|-----------|---------|
| `SwitchToMainThread()` | Continue on the main thread |
| `SwitchToBackground(priority)` | Continue on a pool thread |
| `DelayMs(ms)` / `DelaySec(s)` | Resume after a delay |
| `Yield()` | Yield to the scheduler |

## Lifecycle

`TaskManager::Initialize`, `Update` and `Shutdown` take `PassKey<CoreManager>`, so only `CoreManager` calls them:

| Method | Called from |
|--------|-------------|
| `Initialize()` | `CoreManager::InitializeCoreServices()` (game PreInit and `OnTestEnvironmentInit`), before `ConfigManager` |
| `Update()` | `CoreManager::OnGameCycle` once per frame; this is where main-thread tasks run |
| `Shutdown()` | `OnApplicationDeinit` and `OnTestEnvironmentDeinit` |

## Best Practices

### DO
- `co_await SwitchToMainThread()` before touching UI or scene state.
- Pass a `CancellationToken` to long-running tasks and check it.
- Use `TaskGroup` for related tasks.
- Use `Critical` only for urgent work.
- Check the `std::expected` from `GetResult()`.

### DON'T
- Block the main thread with `handle->Wait()`. The one place that does it is startup (`ConfigManager::Initialize()` via `TaskGroup::WaitAll()`), and only for background tasks.
- `Wait()` on the main thread for a task scheduled with `ThreadType::MainThread`: the main-thread queue is drained in `TaskManager::Update`, so the wait blocks itself forever.
- Create tasks in a tight loop without yielding.
- Ignore cancellation tokens.
- Hold locks across `co_await`.
- Call `Task::GetResult()` on a task suspended on the pool: it resumes the coroutine in a loop and races with the pool. Start it with `TaskManager::Run(...)` and wait on the returned handle (`.claude/rules/tests-module.md`).
- `co_await` a `TaskHandle` (it is not awaitable; nothing would resume the waiter). Block with `Wait()` off the main thread, or `co_await` the `Task<T>` itself when one coroutine calls another.
- Count on a task surviving `TaskManager::Shutdown()`: the scheduler drops its queues, so a task parked on `Delay` or `SwitchTo*` is never resumed, and its self-reference (plus the coroutine frame) is leaked rather than freed. `Wait()` on such a handle blocks forever.

## Thread Safety

- `TaskManager`, `ThreadPool`, `TaskScheduler`, `TaskHandle<T>` and `CancellationToken` are thread-safe.
- `Task<T>` is move-only and not thread-safe.
