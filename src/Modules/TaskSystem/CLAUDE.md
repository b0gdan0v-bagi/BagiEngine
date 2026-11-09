# TaskSystem Module Guidelines

## Overview

Coroutine-based asynchronous task execution (C++20 coroutines, no exceptions: results are `std::expected<T, TaskError>`).

## Key Components

| Component | Purpose |
|-----------|---------|
| `Task<T>` | Coroutine return type with `promise_type`; move-only |
| `TaskHandle<T>` | Handle for tracking and cancelling a task; `GetResult()` returns `std::expected`. `Wait()` blocks on a condition variable (no spinning) and also returns when the handle is cancelled; completion is signalled from the promise's `final_suspend` (`Detail::CompletionEvent`). `GetStatus()` is derived: a stored `Running` that is already done reads as `Completed`, and `Cancel()` on a finished task is a no-op that keeps its result |
| `TaskManager` | Singleton: `Run()`, `RunOnMainThread()`, `RunDelayed()` (wall clock), `RunDelayedGameTime()` (game time, default `MainThread`). The scheduled job owns a reference to the handle and `Start()` makes the handle keep itself alive until the coroutine finishes, so dropping the returned handle (fire and forget) is safe; keep it only to `Wait()`, `Cancel()` or read the result. `SetFrameBudget(duration)` / `GetFrameBudget()` (default `kDefaultFrameBudget`, 4 ms; a negative value is stored as zero; only a single-thread build reads it) and `ShouldYield()` (the by-hand form of `YieldIfNeeded()`) belong to the frame budget ("Frame budget" below) |
| `ThreadPool` | Worker threads with work stealing (`TaskQueue::TrySteal`). `ThreadPool(ThreadPool::InlineTag{})` is the inline pool of a single-thread build: no thread, `Submit` runs the job on the calling thread (below) |
| `TaskScheduler` | Priority queues, the cooperative queue of the frame budget ("Frame budget" below), delayed tasks on two clocks (`DelayClock::WallClock`: `steady_clock`; `DelayClock::GameTime`: a clock that only `AdvanceGameTime(seconds)` moves; a non-positive game delay is ready at the next `ProcessDelayedTasks`, a huge one saturates, `AdvanceGameTime` ignores NaN, infinity and a negative step) |
| `CancellationToken` | Cancellation support (`Create()`, `CreateLinked()`, `IsCancelled()`). A linked child sees its parent's cancellation (checked by polling up the chain); cancelling the child does not touch the parent |
| `TaskGroup` | Manages related tasks: `Add()`, `WaitAll()`, `CancelAll()` |
| `EventAwaiter<E>` | Awaits an `EventBase` event |

`TaskPriority`: `Low`, `Normal`, `High`, `Critical`. `ThreadType`: `MainThread`, `Background`, `IO`, `Custom`. `Run()` defaults to `Normal` priority on `Background`.

## Awaitables

| Awaitable | Purpose |
|-----------|---------|
| `SwitchToMainThread()` | Continue on the main thread |
| `SwitchToBackground(priority)` | Continue on a pool thread |
| `DelayMs(ms)` / `DelaySec(s)` | Resume after a delay of WALL-CLOCK time (a `steady_clock` job of the scheduler, so it ignores the frame clamp and the manual clock and does not resume on the main thread by itself). Not for animation: wait on game time with `TweenManager::Wait` (see below) |
| `DelayGameMs(ms)` / `DelayGameSec(s)` / `DelayGame(duration)` | Resume after a delay of GAME time: the frame deltas that `CoreManager::OnGameCycle` feeds to the scheduler (`Time::GetDeltaTime()`: the clamp and the manual clock apply, a pause does not count). Default thread `MainThread`: the task is released in `Update` right before the main-thread queue is drained, so it resumes in the same frame. A zero, negative or NaN delay does not suspend. The seconds are rounded to the nearest nanosecond like the frame step, so a delay equal to the sum of the frame deltas is released on that frame; a decimal delay equal to a float frame delta (e.g. 0.7) may release one frame later |
| `Yield()` | Suspend and be resumed later, always (a real re-queue). Threaded build: back to the pool (a main-thread job: the main-thread queue, resumed in the next frame). Single-thread build: a background job lands in the cooperative queue and `Update` resumes it inside the frame budget; a main-thread job keeps the main-thread queue |
| `YieldIfNeeded()` | Suspend only when the frame budget is spent: `await_ready()` is `!ShouldYield()`. On a threaded build it never suspends (one pointer test and a bool), in a main-thread job it is always ready. The call for a long background loop ("Frame budget" below) |

## Lifecycle

`TaskManager::Initialize`, `AdvanceGameTime`, `Update` and `Shutdown` take `PassKey<CoreManager>`, so only `CoreManager` calls them:

| Method | Called from |
|--------|-------------|
| `Initialize()` | `CoreManager::InitializeCoreServices()` (game PreInit and `OnTestEnvironmentInit`), before `ConfigManager` |
| `AdvanceGameTime(seconds)` | `CoreManager::OnGameCycle` once per frame, right before `Update`, with `Time::GetDeltaTime()` (clamped to `Time::kMaxFrameTime`; the manual clock gives the same value). `OnIdleCycle` runs no frame and advances nothing. A no-op before `Initialize` and after `Shutdown` |
| `Update()` | `CoreManager::OnGameCycle` once per frame; this is where delayed tasks are released and main-thread tasks run |
| `Shutdown()` | `OnApplicationDeinit` and `OnTestEnvironmentDeinit`; stops the scheduler, joins the pool, then abandons the tasks that are left (cancelled, self-reference dropped) |

Tweens (`BECore/Animation/`, `CoreManager::GetTweens()`) are not tasks: they are ticked once per frame from the frame time and are not scheduled here. `co_await` of a tween is not an awaitable of this module either: `AwaitTween(handle)` and `TweenManager::Wait(seconds)` (`BECore/Animation/TweenAwaiter.h`) resume the coroutine from `TweenManager::Advance`, inline on the main thread and on game time (the frame time, so the manual clock and the clamp apply). A coroutine that awaits them must already run on the main thread (`RunOnMainThread`, or `co_await SwitchToMainThread()` first); Which to use: `TweenManager::Wait` for an animation pause on the main thread; `DelayGame*` for a timer on the task system or a `RunDelayedGameTime` job (any thread type, game time); `Delay*` / `RunDelayed` for a wait that must ignore the clamp, pauses and the manual clock (a timeout), not for animation. `Clear()` of the tween manager (shutdown) never resumes a waiter, the frame is abandoned with the other tasks.

## Single-thread build

`ENGINE_SINGLE_THREAD=ON` (always on for Emscripten; `cmake/BagiPlatformOptions.cmake`) defines `BE_SINGLE_THREAD=1`, and `TaskManager::Initialize` then creates an **inline** `ThreadPool` instead of the worker threads: `GetThreadCount()` and `TaskManager::GetWorkerCount()` are 0, `IsMainThread()` stays true, and the log says "single-thread mode". The rest of the system (scheduler, delays, main-thread queue, `TaskGroup`) is unchanged.

- `Submit` / `SubmitToThread` of the inline pool run the job **at once on the calling thread**, so a job is done when `Submit` returns. This is what keeps start-up alive: `ConfigManager::Initialize` runs the config loads with `Run(...)` and calls `TaskGroup::WaitAll()` before any frame, and a resource loader that does `co_await SwitchToBackground()` simply continues inline. A deferred queue (run at the next `Update`) is not an option: that `WaitAll` would block for ever.
- A job submitted while another inline job runs on this stack (a coroutine that hops to the background twice, a job that starts a task) is queued, and the outer `Submit` runs the queue in FIFO order before it returns, so the recursion stays bounded. The pool is single-thread by definition: use it from one thread only. `Shutdown` is trivial and idempotent; later jobs are dropped like in a stopped pool.
- `Wait()` from the main thread works only for tasks that finish by themselves inline. A task parked on a delay (`Delay*`, `RunDelayed*`), on the main-thread queue (`RunOnMainThread`, a `ThreadType::MainThread` schedule, a `Yield()` of a main-thread job) or in the cooperative queue (a `Yield()` / `YieldIfNeeded()` of a background job, "Frame budget" below) is released by `Update`, which cannot run while the main thread waits: it is a DON'T anyway (below), and in a single-thread build nothing else makes progress either. `SwitchToMainThread` does not park: `await_ready` is true on the only thread, so it continues inline.
- Two deadlocks are new here (with workers the same code works): a job running inline that starts a task with `Run(...)` and then calls `Wait()`, `GetResult()` or `WaitAll()` on it blocks for ever (the child is only queued until the outer job returns; wait at top level, as `ConfigManager::Initialize` does, or `co_await` the child); and calling `Run(...)` or a background `Schedule` while holding a mutex that the job also takes locks that mutex twice on one thread (release it first). The inline pool asserts that it is used only from the thread that created it.
- Tests: `tests/BECore/TaskSystemInlineTest.cpp` builds the inline pool directly (it runs in every build; its `TaskManagerSingleThreadTest` cases exist only under `BE_SINGLE_THREAD`). `tests/BECore/TaskSystemBudgetTest.cpp` covers the frame budget: `TaskSystemBudgetTest` (a scheduler of its own over an inline pool and a fake clock, every build), `TaskSystemBudgetThreadedPoolTest` (a worker pool: nothing is budgeted) and `TaskManagerBudgetTest` (the singleton; its drain cases exist only under `BE_SINGLE_THREAD`, the others check the threaded behaviour). Under `BE_SINGLE_THREAD` three cases of `TaskSystemTest.cpp` skip with "needs worker threads (BE_SINGLE_THREAD)": the `TaskManagerLifetimeTest` cases that hold a task at a spin gate on a worker while the test thread opens it (`DroppedHandleKeepsTaskAliveUntilItFinishes`, `...AcrossThreadHop`, `KeptHandleOwnsTaskAfterItFinishes`; inline, the spin would never end). The other cases either build their own `ThreadPool(n)` (real threads, also on the desktop) or pass inline through the single-thread `TaskManager`, or use no pool. How to check on the desktop without Emscripten: root `CLAUDE.md`, "Web-like build on the desktop".

### Frame budget

A coroutine cannot be preempted, so a long background task shares the only thread by being polite: it calls `co_await YieldIfNeeded()` in its loop. What runs where:

| Build | Background task | `YieldIfNeeded()` |
|-------|-----------------|-------------------|
| Threaded | on the pool, as always | never suspends (`ShouldYield()` returns false without reading shared state) |
| Single-thread | the first slice (up to its first suspension) runs inline in `Run()`; the rest in `Update`, at most the budget per frame and at least one slice per frame | suspends once the window of the frame is spent |

- **Only background jobs are budgeted, and only where the coroutine itself suspends.** A task that never calls `YieldIfNeeded()` / `Yield()` still runs to its end in one go. A main-thread job is never budgeted: `ShouldYield()` is false in it and its `Yield()` keeps the main-thread route (next frame).
- **The logic is in `TaskScheduler`** and is decided by `ThreadPool::IsInline()` at run time, not by `#if BE_SINGLE_THREAD`, so it is testable in every build with a scheduler over `ThreadPool(ThreadPool::InlineTag{})`. An inline `DispatchToThreadPool` wraps each job in a scope that sets `_inBackgroundJob` (restored on exit): this is how `Yield` and `ShouldYield` know the caller. Quirk: after `co_await SwitchToMainThread()` a single-thread coroutine continues inline, so it still counts as a background job.
- **The window.** `_sliceDeadline` is `TimePoint::max()` at start-up and after `Initialize` / `Shutdown` (no frame yet, `ShouldYield()` is false, so the `WaitAll` over the config loads in `ConfigManager::Initialize` is safe); `now + budget` while `ProcessBackgroundSlices` runs; `TimePoint::min()` (spent) between drains, so a task started between drains (a scene update, a control command, a delayed task released earlier in `Update`) yields at its first check and continues in the next drain.
- **`Update` order:** delayed tasks, main-thread tasks, then `ProcessBackgroundSlices(GetFrameBudget())`. It pops the parked continuations FIFO, each through the inline pool (so a nested `Run` still queues behind the outer job), and stops when the queue is empty or the window is spent **after at least one slice ran** (a budget of 0 still gives one slice per frame: nothing starves). A slice that yields again is resumed again in the same drain while time remains (round robin). A frame overruns the budget by at most the time between two checks of one task.
- **`Yield()` routing** (`TaskScheduler::ScheduleYielded`): inline pool and background job: the cooperative queue; anything else: the old route, `Schedule(task, Low, threadType)`. `TaskManager::GetPendingTaskCount()` counts the parked continuations. `SetBudgetClock` replaces the clock of the budget (tests only; delayed tasks keep their own).
- **Cancellation** stays cooperative: check the `CancellationToken` before `YieldIfNeeded()`. `TaskHandle::Cancel()` is unchanged, and the drain never drops a parked slice (the coroutine frame must run to its end to free itself); `TaskScheduler::Shutdown` clears the cooperative queue and `AbandonAll` cancels the handles.
- **Limits.** `Wait()` / `WaitAll()` on the main thread for a task that yielded deadlocks, like for any task parked on the main-thread queue (`Update` cannot run while the main thread waits). Before the first frame an explicit `Yield()` parks the task until the frames start, while `YieldIfNeeded()` never parks at start-up. The budget state needs no lock only because an inline pool has one thread; `SetBudgetClock` is not synchronized. The budget is set with `SetFrameBudget`; reading it from `ApplicationConfig.xml` is not done yet (slice 2 of the entry `tasks-frame-budget`).

## Best Practices

### DO
- `co_await SwitchToMainThread()` before touching UI or scene state.
- Pass a `CancellationToken` to long-running tasks and check it.
- In a long background loop `co_await YieldIfNeeded()` after each unit of work, and check the `CancellationToken` before it. Free on a threaded build, it keeps a single-thread build (the browser) drawing:
  ```cpp
  for (const auto& item : items) {
      if (token.IsCancelled()) co_return;
      co_await YieldIfNeeded();
      ProcessItem(item);
  }
  ```
- Use `TaskGroup` for related tasks.
- Use `Critical` only for urgent work.
- Check the `std::expected` from `GetResult()`.

### DON'T
- Schedule a game-time delay (`DelayGame*`, `RunDelayedGameTime`) from a worker thread when the run must be deterministic: it counts from whatever game time the main thread has reached at that moment. Schedule it from the main thread.
- Block the main thread with `handle->Wait()`. The one place that does it is startup (`ConfigManager::Initialize()` via `TaskGroup::WaitAll()`), and only for background tasks.
- `Wait()` on the main thread for a task scheduled with `ThreadType::MainThread`: the main-thread queue is drained in `TaskManager::Update`, so the wait blocks itself forever.
- Create tasks in a tight loop without yielding.
- Run a long background task that never calls `YieldIfNeeded()` / `Yield()`: in a single-thread build it freezes the page until it ends (nothing preempts a coroutine).
- Ignore cancellation tokens.
- Hold locks across `co_await`.
- Call `Task::GetResult()` on a task suspended on the pool: it resumes the coroutine in a loop and races with the pool. Start it with `TaskManager::Run(...)` and wait on the returned handle (`.claude/rules/tests-module.md`).
- `co_await` a `TaskHandle` (it is not awaitable; nothing would resume the waiter). Block with `Wait()` off the main thread, or `co_await` the `Task<T>` itself when one coroutine calls another.
- Count on a task surviving `TaskManager::Shutdown()`: the scheduler drops its queues, so a task parked on `Delay` or `SwitchTo*` is never resumed. Shutdown then abandons every task that has not finished (`Detail::LiveTasks::AbandonAll()`): the handle becomes `Cancelled` (`Wait()` returns, `GetResult()` gives `Cancelled`) and the coroutine frame is freed with the last handle.
- Start tasks after `TaskManager::Shutdown()`: the scheduler drops them, and nothing abandons them any more, so `Wait()` on such a handle blocks forever.

## Thread Safety

- `TaskManager`, `ThreadPool`, `TaskScheduler`, `TaskHandle<T>` and `CancellationToken` are thread-safe. The frame-budget state of `TaskScheduler` (`_inBackgroundJob`, `_sliceDeadline`, the budget clock) is plain data that only the one thread of an inline pool touches (`ShouldYield()` on a worker pool returns before reading it); the cooperative queue has its own mutex.
- `Task<T>` is move-only and not thread-safe.
