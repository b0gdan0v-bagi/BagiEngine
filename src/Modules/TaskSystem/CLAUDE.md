# TaskSystem Module Guidelines

## Overview

Coroutine-based asynchronous tasks (C++20 coroutines, no exceptions: results are `std::expected<T, TaskError>`).

## Key Components

| Component | Purpose |
|-----------|---------|
| `Task<T>` | coroutine return type; move-only, not thread-safe |
| `TaskHandle<T>` | track and cancel; `GetResult()` returns `std::expected`. `Wait()` blocks on a condition variable and also returns on cancel; completion is signalled from `final_suspend`. `GetStatus()` reads a done task as `Completed`; `Cancel()` on a finished task is a no-op that keeps its result (both decided under the completion's mutex). A copyable result can be read again; a move-only one is handed out once |
| `TaskManager` | singleton: `Run()` (default `Normal` on `Background`), `RunOnMainThread()`, `RunDelayed()` (wall clock), `RunDelayedGameTime()` (game time, default `MainThread`). The scheduled job holds the handle and the handle keeps itself alive until the coroutine ends, so **dropping the returned handle is safe** (fire and forget). `SetFrameBudget()` / `ShouldYield()`: "Frame budget" |
| `ThreadPool` | workers with work stealing; an idle worker waits on the pool, so a job of any queue wakes it; `ThreadPool(InlineTag{})` is the inline pool of a single-thread build |
| `TaskScheduler` | priority queues, the cooperative queue, delayed tasks on two clocks (`WallClock`: `steady_clock`; `GameTime`: moved only by `AdvanceGameTime`, which ignores NaN, infinity and negative steps) |
| `TaskLabel` | `BE_TASK_LABEL("name")`, last argument of `Run*` / `SwitchTo*`: names the profiler zone of the slice (`Profiler/CLAUDE.md`) |
| `CancellationToken` | `Create()`, `CreateLinked()`: a child sees the parent's cancellation, cancelling the child leaves the parent |
| `TaskGroup` | `Add()`, `WaitAll()`, `CancelAll()` |
| `EventAwaiter<E>` | awaits an `EventBase` event once; `WaitForEventWithTimeout<E>(time)` gives an empty `Optional` when the wall-clock time runs out first (main thread only: `TaskManager::Update` releases the timeout) |

## Awaitables

| Awaitable | Purpose |
|-----------|---------|
| `SwitchToMainThread()` / `SwitchToBackground(priority)` | continue on the main thread / a pool thread |
| `DelayMs` / `DelaySec` | WALL-CLOCK delay: ignores clamp, pause and manual clock; does not return to the main thread. Not for animation |
| `DelayGameMs` / `DelayGameSec` / `DelayGame` | GAME-time delay (the frame deltas `CoreManager::OnGameCycle` feeds: clamp and manual clock apply, a pause does not count). Default `MainThread`: released in `Update` right before the main-thread queue drains, so it resumes in that frame. Zero, negative or NaN does not suspend. A delay equal to the sum of the frame deltas releases on that frame, but a decimal delay equal to a float frame delta (0.7) may release one frame later |
| `Yield()` | always a real re-queue. Threaded: back to the pool (a main-thread job: next frame). Single-thread: a background job goes to the cooperative queue, a main-thread job keeps the main-thread queue |
| `YieldIfNeeded()` | suspends only when the frame budget is spent; never on a threaded build or in a main-thread job. The call for a long background loop |

## Lifecycle

`Initialize`, `AdvanceGameTime`, `Update`, `Shutdown` take `PassKey<CoreManager>`:

| Method | Called from |
|--------|-------------|
| `Initialize()` | `CoreManager::InitializeCoreServices()` (game PreInit, `OnTestEnvironmentInit`), before `ConfigManager` |
| `AdvanceGameTime(seconds)` | `OnGameCycle`, once per game frame right before `Update`, with `Time::GetDeltaTime()`. Neither `OnIdleCycle` nor a held frame of a paused game advances it, so a task finished on a worker reaches the game at the next game frame. No-op outside `Initialize`..`Shutdown` |
| `Update()` | `OnGameCycle`, once per game frame: releases delayed tasks, runs main-thread tasks, then (single-thread) the background slices within the budget |
| `Shutdown()` | `OnApplicationDeinit`, `OnTestEnvironmentDeinit`: stops the scheduler, joins the pool, abandons the remaining tasks |

**Tweens are not tasks** (`BECore/Animation/`). `AwaitTween(handle)` and `TweenManager::Wait(seconds)` (`TweenAwaiter.h`) resume from `TweenManager::Advance`, inline on the main thread on game time; the coroutine must already be on the main thread. Choose: `TweenManager::Wait` for an animation pause; `DelayGame*` / `RunDelayedGameTime` for a game-time timer on any thread type; `Delay*` / `RunDelayed` only for a wait that must ignore clamp, pause and manual clock (a timeout). The tween manager's `Clear()` never resumes a waiter.

## Single-thread build

`ENGINE_SINGLE_THREAD=ON` (presets `web-*-single-thread`; Emscripten does not force it, "Browser") defines `BE_SINGLE_THREAD=1`, and `Initialize` creates an **inline** `ThreadPool`: no worker, `GetWorkerCount()` 0, `IsMainThread()` true. Scheduler, delays, main-thread queue and `TaskGroup` are unchanged.

- The inline pool's `Submit` runs the job **at once on the calling thread**. This keeps start-up alive: `ConfigManager::Initialize` runs the config loads with `Run(...)` and `WaitAll()`s before any frame, and a `co_await SwitchToBackground()` just continues inline. A deferred queue would make that `WaitAll` block for ever.
- A job submitted while an inline job runs is queued and run FIFO before the outer `Submit` returns (bounded recursion). The pool asserts it is used only from its creating thread; `Shutdown` is idempotent and later jobs are dropped.
- `Wait()` on the main thread works only for tasks that finish inline. A task parked on a delay, the main-thread queue or the cooperative queue is released by `Update`, which cannot run while the main thread waits. `SwitchToMainThread` never parks here.
- **Two deadlocks specific to this build:** an inline job that starts a child with `Run(...)` and then `Wait()` / `GetResult()` / `WaitAll()` on it (the child runs only after the outer job returns: wait at top level or `co_await` the child); and `Run(...)` / a background `Schedule` while holding a mutex the job also takes (locked twice on one thread).
- Test guards depend on `BE_SINGLE_THREAD` only, so the web-like desktop build (`cmake/CLAUDE.md`) runs them without Emscripten.

### Frame budget

A coroutine cannot be preempted, so a long background task shares the only thread by calling `co_await YieldIfNeeded()` in its loop.

- **Threaded build:** `YieldIfNeeded()` never suspends (`ShouldYield()` reads only the stop flag and the pool kind).
- **Single-thread build:** a background task's first slice (to its first suspension) runs inline in `Run()`; the rest run in `Update`, within the budget, **at least one slice per frame** even at budget 0 (nothing starves), FIFO and round robin while time remains.
- **Only background jobs, only where the coroutine suspends.** A task that never yields runs to its end in one go. A main-thread job is never budgeted.
- The logic lives in `TaskScheduler`, chosen by `ThreadPool::IsInline()` at run time, not by `#if`, so it is testable in every build. The window is open before the first frame (the start-up `WaitAll` is safe) and spent between drains (a task started then yields at its first check): `TaskScheduler.h`.
- `Yield()` routing: `TaskScheduler::ScheduleYielded`. `GetPendingTaskCount()` counts parked continuations. `SetBudgetClock` is for tests and not synchronized.
- **Cancellation** stays cooperative: check the token before `YieldIfNeeded()`. A drain never drops a parked slice (the frame must run to its end to free itself); `Shutdown` clears the queue and abandons the handles.
- **Limits:** `Wait()` / `WaitAll()` on the main thread for a task that yielded deadlocks. Before the first frame an explicit `Yield()` parks until frames start; `YieldIfNeeded()` never parks at start-up. The budget state needs no lock only because an inline pool has one thread.
- Set with `SetFrameBudget` or the `taskFrameBudgetMs` attribute of `ApplicationConfig.xml` (fractional ms; missing or negative keeps the default).

### Browser

On Emscripten the default is the **threaded** build: `-pthread` everywhere, workers as on the desktop, and the page needs cross-origin isolation (`SharedArrayBuffer`). The single-thread build is the option the frame budget exists for.

- **The pool is sized in two places that must agree.** `TaskManager::kMaxWorkerThreads` caps the workers; `BE_WEB_THREAD_POOL_SIZE` (`cmake/BagiWeb.cmake`) is `-sPTHREAD_POOL_SIZE`, the Web Workers loaded **before** `main()`. The main thread blocks at start-up (`ConfigManager::Initialize` -> `WaitAll`), and a worker that only the main thread's event loop could create would never start, so the pool is the workers plus one. A `static_assert` in `TaskManager.cpp` and `tools/web/tests/check_bagi_web.cmake` keep them together. No `PROXY_TO_PTHREAD`: `main()` and the SDL loop stay on the main thread, which owns the canvas (link flags: `cmake/BagiWeb.cmake`).
- **Blocking on the main thread** busy-spins (browsers forbid `Atomics.wait` there). The start-up `WaitAll` is short; a long main-thread `Wait()` stays a DON'T. A **Debug** build (`-sASSERTIONS`) prints "Blocking on the main thread is very dangerous" once to `console.error`: harmless, so its browser smoke needs `--allow-console "Blocking on the main thread is very dangerous"`; a Release build that prints it fails.
- **EASTL atomics are ARM assembly** (`eastl::cpu_pause()`, the barrier of `eastl::atomic`) and do not assemble for wasm: use `std::atomic` only.

## Best Practices

### DO
- `co_await SwitchToMainThread()` before touching UI or scene state.
- Pass a `CancellationToken` to long tasks and check it.
- In a long background loop, check the token, then `co_await YieldIfNeeded()` after each unit of work: free on a threaded build, it keeps the single-thread page drawing.
- `TaskGroup` for related tasks; `Critical` only for urgent work; check the `std::expected` of `GetResult()`.

### DON'T
- Schedule a game-time delay from a worker when the run must be deterministic: it counts from whatever game time the main thread has reached. Schedule it from the main thread.
- Block the main thread with `Wait()`. Only start-up does (`ConfigManager::Initialize`, `WaitAll` over background tasks).
- `Wait()` on the main thread for a `ThreadType::MainThread` task: that queue drains in `Update`, so it blocks for ever.
- Create tasks in a tight loop without yielding, or run a long background task that never yields (freezes the single-thread page).
- Ignore cancellation tokens; hold locks across `co_await`.
- `Task::GetResult()` on a task suspended on the pool: it resumes the coroutine in a loop and races the pool. Start it with `TaskManager::Run(...)` and wait on the handle (`.claude/rules/tests-module.md`).
- `co_await` a `TaskHandle`: it is not awaitable. Block with `Wait()` off the main thread, or `co_await` the `Task<T>`.
- Count on a task surviving `Shutdown()`: queues are dropped, so a parked task is never resumed; every unfinished task is abandoned (`Detail::LiveTasks::AbandonAll()`: the handle becomes `Cancelled`, the frame is freed with the last handle).
- Start tasks after `Shutdown()`: dropped and never abandoned, so `Wait()` blocks for ever.

## Tasks are move-only

- A job is a `MoveOnlyFunction<void()>` (`TaskQueue::TaskFunc`), moved scheduler -> queue -> worker, never copied; clearing a queue destroys each job once. A worker keeps the job it ran until it takes another.
- Priority queues are `Detail::MovableHeap` (`eastl::priority_queue` copies on push); equal keys are not FIFO.
- The inline pool wraps each background job (`SubmitInlineBackgroundJob`): one heap allocation per background job in a single-thread build.

## Thread Safety

`TaskManager`, `ThreadPool`, `TaskScheduler`, `TaskHandle<T>` and `CancellationToken` are thread-safe, except the frame-budget state of `TaskScheduler`, which only the one thread of an inline pool touches (details: `TaskScheduler.h`).
