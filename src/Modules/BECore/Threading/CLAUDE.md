# Threading Module Guidelines

## Overview

The engine's own lock and thread types, in the PCH (`namespace BECore`): `Mutex`, `SharedMutex`, `ConditionVariable`, `Thread`, the guard aliases `LockGuard` / `UniqueLock` / `SharedLock`, `LockSite` and `BE_LOCK_SITE("name")`. **Every lock and thread of the engine is one of these**, never a raw `std::mutex`, `std::thread` and the like, for two reasons: a wait on a lock is a zone `lock wait: <name>` in the profiler (contention was invisible before), and a thread cannot start without a name in a trace. The profiler's own internals are the only exception (list below).

## The contract

- **A lock has a static name:** `Mutex _mutex{BE_LOCK_SITE("Task queue")};`. There is no default constructor. The name is a string literal (the wait zone is `"lock wait: " name` by concatenation) and says whose lock it is, not what it guards. `BE_LOCK_SITE` is an expression (a lambda returning a function-local `static constexpr LockSite&`), so it works in a default member initialiser and in a static member of a template, and a lock can be constant-initialised (`SingletonAtomic<T>::_mutex`).
- **The std guards work over the types** (Lockable / SharedLockable): `std::lock_guard`, `std::unique_lock` (with `std::try_to_lock` too), `std::shared_lock`, `std::scoped_lock`. The aliases are the explicit spellings with CTAD (`LockGuard lock(_mutex);`). `SharedMutex` taken exclusively (`LockGuard`, `UniqueLock`) is the writer side.
- **`ConditionVariable` is std-shaped** (`wait`, `wait_for`, `wait_until`, `notify_one`, `notify_all`, with the predicate overloads) and takes `std::unique_lock<Mutex>`. It **adopts the native mutex** (`std::unique_lock<std::mutex>(native, adopt_lock)`, `release()` after the wait), so the bookkeeping of the engine lock (lock order stack, Tracy's lock state) stays "held" while the thread waits, and a spurious or timed wake changes nothing in it.
- **`Thread(name, body)` / `Thread(name, sortIndex, body)`** is a `std::thread` that calls `Profiler::SetThreadName(name[, sortIndex])` first thing (a lower sort index is shown higher). The name is copied into the closure, so a run-time built name may die first. Move-only, std-shaped (`joinable`, `join`, `detach`, `get_id`), `Thread::Id`, `Thread::CurrentId()`, `Thread::HardwareConcurrency()`. Like `std::thread`, a joinable `Thread` that is destroyed or assigned over terminates. A default-constructed `Thread` is allowed (a member whose thread starts later). Do not name a thread by calling `BE_PROFILE_THREAD_ORDER` at the top of its body: pass the name and order to `Thread`.
- **The single-thread build** (`BE_SINGLE_THREAD`, the browser's single-thread page) is handled in `Thread` and nowhere else: `Thread::kAvailable` is false, the constructor asserts and starts nothing, `joinable()` is false. The task pool of that build is inline and makes no `Thread`.
- Atomics are not part of this layer (`std::atomic` stays).

## How it works (what the code does not say)

- **Fast path.** `lock()` is `try_lock()` first; only a failed try goes to the out-of-line contended path (`Mutex.cpp`, `SharedMutex.cpp`): read `ProfilerClock`, take the blocking lock, and when the wait was at least `Profiler::GetLockWaitThresholdUs()` (default 50 microseconds; tests set it small) call `backend->RecordZone(site.wait, begin, end)`. An uncontended lock reads no clock and calls nothing out of line. The profiler's side of it: `Profiler/CLAUDE.md`, "Lock waits".
- **Sizes.** `BE_LOCK_SITES` (`LockSite.h`) is on when `BE_PROFILER` or asserts are on. Then a lock keeps a `const LockSite*` (and with `BE_PROFILER` the `LockEvents` context for Tracy). Without it (a shipped build) the site argument is dropped and `static_assert(sizeof(Mutex) == sizeof(std::mutex))` (same for `SharedMutex`): a `Task` carrying a lock pays nothing there. The headers include only std headers, `LockSite.h` and `LockHooks.h` (lean on purpose: `pch.h` includes them before `Singleton.h`); everything that needs the profiler is in `.cpp` files.
- **Tracy.** Only a backend that answers `WantsLockEvents()` true (Tracy) gets an event for every lock operation (lockable announce / before-lock / after-lock / before-unlock / after-try-lock, shared variants). The lock's context is made lazily on its first event while Tracy runs, by compare-exchange (the loser ends its own), ended in the lock's destructor, and every call is a no-op once Tracy has stopped, so a lock may outlive it. Chrome and the ring leave `LockHooks::g_eventBackend` null: one relaxed load per operation, no virtual call.
- **Lock order (Debug).** `LockSite::level` (0 = not ordered, never recorded) and `Threading/LockOrder.*`, only with asserts on (no member and no code otherwise): taking a lock whose level is not above the highest level the thread holds asserts with both names. A successful `try_lock` is recorded but never checked (it cannot deadlock); a release removes its own entry wherever it is; a `ConditionVariable` wait does not touch the stack; shared and exclusive both count, so a thread must not take the same `SharedMutex` of a level twice, not even shared. Per thread, fixed size (`kMaxHeld`), no allocation. **To give a level:** `BE_LOCK_SITE_LEVEL("name", 20)` only where you read the nesting in the code and every path takes the locks in that order; leave 0 otherwise (a wrong level asserts in every Debug run). Today only the Schannel stream has levels (`_lifetime` 10 < `_sendMutex` 20 < `_sspiMutex` 30); the others nest nothing, or not in one order.
- **Why the profiler's locks stay raw.** The recording path (`RecordZone`, `SetThreadName`, the thread buffers, `ProfilerClock`) must not take an engine `Mutex`: a wait inside it would record itself without an end. Using the types there is a loop, not a style choice.

## Allowed raw uses

For the check of raw types (the entry `ci-raw-types-check`): outside `Threading/`, `std::mutex`, `std::shared_mutex`, `std::recursive_mutex`, `std::lock_guard`, `std::unique_lock`, `std::shared_lock`, `std::scoped_lock`, `std::thread`, `std::condition_variable` are allowed only in:

- `Profiler/Profiler.cpp` (`g_backendMutex`, `constinit`), `Profiler/Backends/ChromeTraceBackend.*` (the registry mutex and `std::thread::id`), `Profiler/Ring/FrameRingBackend.*` (its three mutexes and `std::thread::id`), each marked `// raw: the profiler cannot profile itself (Threading/CLAUDE.md)`.
- `std::once_flag` / `std::call_once` in `Utils/EnumUtils.h` (not a lock a thread waits on in the sense of this layer).
- `std::this_thread` (`sleep_for`, `yield`) and `std::thread::id` as an identity are not locks or threads: free to use; the main thread names itself with `BE_PROFILE_THREAD_ORDER("Main", 0)` in `Application`.
- The vendored zones (`todo-project.md`) and `tests/`: a test thread is not engine code, tests keep raw `std::thread` / `std::mutex`.
- The game repositories have locks and threads of their own; they move to these types on their own branches.

The inventory (empty outside these, and `Threading/` itself):

```bash
grep -rnE "std::(mutex|shared_mutex|recursive_mutex|lock_guard|unique_lock|shared_lock|scoped_lock|thread|condition_variable)\b" src --include='*.h' --include='*.cpp'
```

## Tests

`tests/BECore/ThreadingTest.cpp` (the types, the order check, `Thread`), `LockProfileTest.cpp` (the wait zones in the Chrome trace and the ring), `TracyBackendTest.cpp` (lockables, `ENGINE_PROFILER_TRACY`), the benchmark `PoolStringLockCostTest` in `PoolStringTest.cpp` (disabled: run it in `macos-release` with `--gtest_also_run_disabled_tests` when a lock type changes).
