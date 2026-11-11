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

## Allocations (`ENGINE_PROFILER_ALLOC`)

- `Alloc/GlobalOperatorNew.cpp` replaces every form of the global `operator new` / `delete` (plain, array, nothrow, sized, aligned) and is compiled only with the option; without it the C++ runtime's operators run and every count is zero. `BECoreModule` is linked `WHOLE_ARCHIVE`, so the replacement reaches every executable of the engine and of a game.
- **Scope:** what goes through `operator new`: our code, EASTL (`Utils/EASTLOperators.cpp` forwards to it), `std`. `malloc` from vendored C code (SDL, stb, ImGui's allocator, Tracy) is not counted.
- **No header in front of a block** (`Alloc/AllocPlatform.h`): `malloc` / `posix_memalign` / `free` with `malloc_size` (macOS) or `malloc_usable_size`, and on Windows `_aligned_malloc` for every block, so whichever delete frees a block is right. That keeps EASTL's aligned `new[]` freed by a plain `delete[]` harmless.
- `AllocTracker` (static, `constinit`): totals (count, bytes asked, frees, live usable bytes and their peak, the largest allocation, no-alloc violations) as relaxed atomics; per thread a monotonic count and bytes in a `constinit thread_local`, read by `ProfileZone` at both ends, so a zone's allocations are inclusive of inner zones and never another thread's. Per-site totals go into a fixed lock-free table (`kZoneSlots`), with or without a backend, so a window-less tool reads allocations per zone without a trace. A re-entrancy guard keeps a backend's `OnAlloc` that allocates (Tracy) from being counted twice. **Nothing on the allocation path allocates, locks or logs.**
- `BE_PROFILE_NO_ALLOC(name)`: allocations of the thread inside the scope are counted as violations and reported by one `LOG_WARNING` when the scope ends (name, file, line); never an abort.

## Frame statistics (always on)

`FrameStatsCollector` (`CoreManager::GetFrameStats()`, a normal main-thread service, unlike the rest of this folder) keeps the last `kFrameHistory` frames: `frameMs` (end to end), `cpuMs`, the stages (`FrameStage`: `Update` and inside it `SceneUpdate`, `Render` and inside it `SceneDraw`, `Present`), the renderer's `RenderCounters`, the allocations of the frame (zero without `ENGINE_PROFILER_ALLOC`) and up to `kMaxFrameCounters` named counters (`SetCounter`; ImGui's draws come this way). `Application::RunFrame` opens the frame after the clock and closes it after the present with `TakeRenderCounters()`; `CoreManager::OnGameCycle` and `Scene::Draw` time their stages with `StageScope`. A manual-clock idle pass is no frame. The end of a frame also sends `Draw calls`, `Allocations` and `Live bytes` to the profiler as counters (graphs in the Chrome trace). Tests give the collector a fake clock (`SetClock`).

**Control methods** (`ProfilerControl.cpp`, registered by `CoreManager::InitializeCoreServices`): `frame_stats`, `profiler_dump`, `zone_allocs` (the table in `ProfilerControl.h`, `Control/CLAUDE.md`), MCP tools of the same names (`tools/control/bagi_mcp.py`). The Match3 smoke test reads `frame_stats` after its steps.

## The views

- **Ctrl+Shift+P** (`ProfilerToggle.h`, `IsProfilerToggle`: the modifiers of Ctrl+Shift+E on `SDLK_P`) is read from the raw `SDLEventWrapper` by the widgets themselves, so it works over an ImGui text field. Ctrl+P is the editor's command palette; Ctrl+Shift+P is reserved in the hotkey map and listed as the fixed command `view.profiler`.
- **Game mode:** `ProfilerOverlayWidget` (`src/Widgets/`, last in `config/GameWidgetsConfig.xml`): a translucent rect, the lines of `FormatOverlayLines` (`FrameStats/FrameStatsText.h`, pure) in the default font of `TextConfig.xml` (as a label without a font), one bar per recent frame against the frame budget. Output pixels (the scene's view transform is put aside), its draws under `ScopedRenderCountersPause`. It is compiled in every build so a config that lists it never fails, but in a plain Release (`kProfilerViewsBuilt`: no editor, no profiler) it ignores the chord and draws nothing. A game with its own `GameWidgetsConfig.xml` adds the entry itself.
- **Editor:** `ProfilerWidget` (`src/Widgets/Profiler/`, editor only): the frame time plot, the same lines, the heap totals, the allocations per zone (with a reset) and the Chrome trace (record, restart, write a file).

## Where the engine's zones are

`Application::RunFrame` (`Frame` around one frame, `Idle` for a manual-clock pass, `Present`, the frame mark at the end), the stages of `CoreManager::OnGameCycle` (`Game cycle` around `Input`, `Tasks`, `Tweens`, `Scene update`, `Widgets update`, `Queued events`, `Draw screen`, `Draw widgets`), `Scene::Update`, `Scene::Draw`, `SceneDrawSystem::Run`, `LogEvent::Flush`, the tasks of the thread pool (`Task`) and of the main thread (`Main thread task`). Named threads: `Main`, `Worker <n>`, `Control accept`, `Control reader`, `Control writer`.

## Tests

`tests/BECore/ProfilerTest.cpp`: the flags, the recorder (nesting, escaping, a dump while four threads record, the drop cap, `MarkTraceStart`), the macros (`#if BE_PROFILER`: every path balances; otherwise: nothing recorded, arguments not evaluated), `ScopedTrace`, and the disabled `ProfilerCostTest` that `HowToProfile.md` quotes. `tests/BECore/AllocTrackerTest.cpp` (`#if BE_PROFILER_ALLOC`, explicit `::operator new` calls so the optimizer keeps them): a zone and its parent, live bytes after a free, every form, an `alignas(64)` EASTL vector, another thread, the no-alloc report, the `E` args of the trace; without the option nothing is counted. `tests/BECore/ProfilerOverlayTest.cpp`: the chord, the overlay lines, the overlay on `RecordingRenderer`. `tests/BECore/FrameStatsTest.cpp`: the draw counters on `RecordingRenderer`, a known scene's draws, the collector on a fake clock, the control methods.
