# Profiler — zones, frame marks, counters, allocations

How to use it (flags, Perfetto, sampling profilers, measured costs): `HowToProfile.md`. The build options `ENGINE_PROFILER`, `ENGINE_PROFILER_ALLOC`, `ENGINE_PROFILER_TRACY` and the preset `macos-profile`: `cmake/CLAUDE.md`.

## The contract

- **Code is marked only with the macros of `Profile.h`** (`BE_PROFILE_SCOPE`, `_FUNCTION`, `_FRAME`, `_COUNTER`, `_THREAD`, `_NO_ALLOC`; included by the PCH). No backend name appears outside `Backends/`. With `BE_PROFILER=0` a macro is `static_cast<void>(0)`: its arguments are not evaluated, so they must have no side effect the program needs.
- **What compiles in every build:** the recorder (`ChromeTraceBackend`), `Profiler`, `ProfileZone`, `ScopedTrace`, the flags. Only the macros compile out, so the everyday Debug tests cover the logic and a test can drive a recorder directly.
- **A zone name is a string literal** (the macro keeps a `static constexpr ProfileSite` whose address is the zone's identity); a thread name may be built at run time (`BE_PROFILE_THREAD` copies it). Counters are named by literals too.
- **Zones are coarse**: a frame stage, a system, a task, one rollout of a search. A zone costs a clock read and a store into the thread's buffer when recording and an atomic load when no backend is installed (`HowToProfile.md`); a function called millions of times a second gets no zone, the sampling profiler sees it.

## Why not a CoreManager service

`Profiler` and the allocation counters are static classes over `constinit` globals, not a `Singleton` or a `CoreManager` getter (the one place the engine's "no globals" rule is broken on purpose): zones run on every thread, the global `operator new` runs before `main` and after `CoreManager` is gone, and window-less tools (the bot arena of a game, a benchmark) have no `CoreManager` at all. Nothing in them is destroyed: a backend that was ever installed stays alive, because a zone ends on the backend it began on.

## Pieces

| Piece | Does |
|-------|------|
| `ProfileSite` | `{name, function, file, line, color}`, laid out like Tracy's source location (checked only in the Tracy backend) |
| `IProfilerBackend` | `Start` / `Stop`, `BeginZone` → token, `EndZone(token, site, allocs)`, `FrameMark`, `Counter`, `SetThreadName`, `OnAlloc` / `OnFree`. Called from any thread |
| `Profiler` | the installed backend (`SetBackend`, an atomic pointer), thread ids (1, 2, ... in the order threads first ask) and names (kept here, so a backend installed later still names the threads), `Start(settings)` / `Finish()` of an application run, the process's `ChromeTraceBackend` |
| `ProfilerClock` | `mach_absolute_time` on macOS (measurably cheaper than `steady_clock`), `steady_clock` elsewhere; ticks converted only when written |
| `ChromeTraceBackend` | per-thread chunked buffers, no lock on the recording path; writes Chrome trace JSON (below) |
| `ProfilerSettings` | `ResolveProfilerSettings(args)`: `--trace=<file>`, `--profiler=none\|chrome\|tracy` (table in the header) |
| `ScopedTrace` | RAII trace of one scope for tools: installs the recorder, marks the start, writes the file and restores the old backend at the end |

**The application run:** `Application::Initialize` resolves the flags first (an error stops the start-up, as a bad control flag does) and calls `Profiler::Start`; `Application::Cleanup` calls `Profiler::Finish` after `OnApplicationDeinit`, when the worker threads have been joined, so every zone is complete in the file.

## The Chrome trace

- Each thread records into a buffer of its own (chunks of `std::malloc`, never `new`, so recording does not enter the operator new it may count). The number of finished events is published with a release store; a writer reads the published part with an acquire load. A buffer is registered once under a mutex and freed only with its recorder (the process's one is never freed), so a thread may end at any time.
- Writing (`WriteJson`, `WriteFile`) is safe at any moment from the main thread: zones still open are closed at their thread's last event, so the file always nests. `MarkTraceStart` starts a session: what each buffer held before is left out (by index: the clock ticks coarser than events follow each other) and its time is 0.
- A full thread buffer (`kMaxEventsPerThread`) or too many threads drop events; the count is `otherData.dropped` of the file.
- Events: `B` / `E` per zone (`args`: `file`, `line`; with `ENGINE_PROFILER_ALLOC` the `E` carries `allocs` and `bytes`, inclusive of inner zones), `C` counters, `i` frame marks (global scope), `M` thread names. Time in microseconds.

## Where the engine's zones are

`Application::RunFrame` (`Frame` around one frame, `Idle` for a manual-clock pass, the frame mark at the end), the stages of `CoreManager::OnGameCycle` (`Input`, `Tasks`, `Tweens`, `Scene update`, `Widgets update`, `Queued events`, `Draw screen`, `Draw and present`), `Scene::Update`, `Scene::Draw`, `SceneDrawSystem::Run`, `LogEvent::Flush`, the tasks of the thread pool (`Task`) and of the main thread (`Main thread task`). Named threads: `Main`, `Worker <n>`, `Control accept`, `Control reader`, `Control writer`.

## Tests

`tests/BECore/ProfilerTest.cpp`: the flags, the recorder (nesting, escaping, a dump while four threads record, the drop cap, `MarkTraceStart`), the macros (`#if BE_PROFILER`: every path balances; otherwise: nothing recorded, arguments not evaluated), `ScopedTrace`, and the disabled `ProfilerCostTest` that `HowToProfile.md` quotes.
