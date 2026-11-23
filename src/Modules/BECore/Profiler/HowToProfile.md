# How to profile

Two kinds of tool answer different questions. **Zones** (this module) say how long the marked stages of a frame or a tool take and, with allocation tracking, how much each of them allocates. **A sampling profiler** needs no marks and sees every function, but not the frame structure. Start with zones to find the stage, then sample inside it.

## Measure a build that is fast

Numbers come from the `macos-profile` preset (RelWithDebInfo, asserts and expects off, no editor, `ENGINE_PROFILER` and `ENGINE_PROFILER_ALLOC` on), never from Debug, whose code is several times slower and differently shaped:

```bash
cmake --preset macos-profile
cmake --build --preset macos-profile --target BagiStress BagiEngineTests
```

`macos-release` keeps the profiler off: the baselines of a performance check are measured without any overhead.

`macos-profile-editor` is the same build with the editor, for the ImGui Profiler panel (Ctrl+Shift+E to the editor, then Ctrl+Shift+P). The editor draws the scene into the viewport's offscreen target under ImGui, so its frame is not the Game frame: measure in `macos-profile` or with Tracy.

## A Chrome trace of a game run

```bash
build/macos-profile/bin/BagiStress --trace=trace.json
```

The file is written when the game ends (relative to the working folder). Open it in https://ui.perfetto.dev (drag the file in) or `chrome://tracing`. Each thread is a track; frame marks are the vertical lines; counters are graphs; a zone's `args` hold its file, line and (with allocation tracking) the allocations it made.

A headless run with the manual clock gives a trace of exactly N frames: `--headless --control --manual-clock --trace=trace.json`, then a `step` of N frames and `quit` through the control API (`tools/control/`).

`--profiler=chrome` without `--trace` records without writing at the end. Flags in a build without the profiler stop the start-up with a message.

## In the editor

Ctrl+Shift+E to the editor, then Ctrl+Shift+P opens the Profiler panel. Zones need a build with `ENGINE_PROFILER` (`macos-profile-editor`); without it the panel says so. The panel records into a ring only while it is open, so it shows frames since it was last opened (a reopened panel starts clean; the time it was closed is in no frame).

- **Frame bars:** one bar per recent frame against a budget line (16.6 ms, 33.3 ms or custom); "Bars" switches to the stage split; a click opens that frame. Counters (draw calls, allocations, ...) can be picked as graphs.
- **Timeline:** a lane per thread with nested zone bars of the opened frame; wheel zooms, drag pans, hover shows name, `file:line`, duration and allocations; Pause freezes the view.
- **Zone table:** calls, total, self, mean, max, p95 and allocations over the frame, the last 60 frames or the ring; sortable, filterable, "Timeline" jumps to the slowest call.
- **Spikes:** frames above a threshold (ms or times the median) are frozen with their zones; open one in the timeline. "Write frozen frames" writes them (or, with none, the selected frame) as a Chrome trace that Perfetto and `tools/perf/trace_summary.py` read.

## A trace of a tool without a window

```cpp
#include <BECore/Profiler/ScopedTrace.h>

{
    BECore::ScopedTrace trace("arena-trace.json");  // records from here
    RunRollouts();                                  // marked with BE_PROFILE_SCOPE inside
}                                                   // written here
```

## Who allocates in a steady frame

In `macos-profile`, with the game running (`--headless --control --manual-clock`), call `zone_allocs` with `reset: true`, `step` some frames, then `zone_allocs` again. `zones` says which zone allocates; `names` says through which container (`zone`, `name`, `allocs`, `bytes`, innermost zone, most bytes first). The default names split by kind (`"EASTL vector"`, `"EASTL basic_string"`); when one zone holds several vectors, name the suspect:

```cpp
_cells.get_allocator().set_name("Board cells");  // a literal: the table keys the name by its address
```

The name has to be a string literal (or live as long as the program). An allocation with an empty `name` came through a plain `operator new`, not an EASTL container; a block of a vendored library that EASTL did not name carries the library's own name (`pugixml`, `ImGui`, `SDL`, `stb`, `dr_libs`). A trace written during the run (`--trace`) carries the same table in `otherData.allocNames`.

`sizes` (`otherData.allocSizes` in a trace) is the histogram of what each zone allocates by size: per zone the non-empty power-of-two buckets, `upTo` the largest size of the bucket in bytes (`0`: the open last bucket, above 16 MiB). Read it when judging a pool: a zone whose allocations all sit in one or two small buckets (many 32-byte nodes) is a candidate for a block pool or an arena of that size; a few large buckets are buffers a pool does not help. Like `names` it counts the innermost zone only.

## The asset load by zone

`build/macos-profile/bin/BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter='AssetLoadAllocBenchTest.DISABLED_*'` (from `build/macos-profile`) prints, after the medians of the benchmark `asset-load`, one report per case (`library-xml`, `library-bin`, `wide-*`, `scene-*`, `particles-*`) of one more load of a file of fresh names, its phases in zones (parse, build; stream open, build cooked). The pass is a run of its own, so the medians carry no zone cost. The zones are literals in `AssetLoadAllocTest.cpp`; the engine has none in its loaders (a zone in a loop that `xml-load-scratch` may change would stay behind).

Three line kinds per zone, each with `case=<c> zone="<name>"`: `AssetLoadZone` (`calls`, `allocs`, `bytes` inclusive of the zones inside, `selfAllocs`), `AssetLoadZoneSizes` (`sizes=<upTo>:<n>,...`, `upTo` the largest size of the bucket, `inf` the open one) and `AssetLoadZoneNames` (the first allocation names of the zone: `pugixml` for the parser's blocks, `(unnamed)` for a plain `operator new`). Sizes and names are self, as in `zone_allocs`. The phase zones of a case hold the load's allocations, so their `bytes` come near the `AssetLoad*` totals of the case, not to the byte: the file of the pass has fresh names, but the pool and the allocator are in another state than in the median run. `(no zone)` is what the case allocates outside a zone (the scratch of the report itself included).

The numbers rank `xml-load-scratch`: what `pugixml` and the global `new` allocate during an XML load, and in which sizes. `AssetLoadParticlesRun` is the particle scene (looping and burst emitters) after its warm-up frames: per frame the allocations and bytes of the update and of the draw, each in a zone, and the report `case=particles-run`; the draw figure includes the test double `RecordingRenderer` when it shows any.

## The room server and its probe

`BagiRoomServer` and `BagiRoomClientProbe` take `--trace=FILE` (profile build only; the file is written at SIGTERM, after the threads are joined). `python3 tools/rooms/load.py --preset macos-profile --clients 16 --messages 200` runs both with traces, one probe process of N clients against a server without the per-address limits, and prints `trace_summary.py` of each: the zones `Room *`, `WS *` and `Client *` by time and, with `ENGINE_PROFILER_ALLOC`, their allocation names and sizes. Those numbers rank `websocket-frame-buffers`.

## Tracy

For a live view (zones, frames, plots, allocations with call stacks, while the game runs):

```bash
cmake --preset macos-profile-tracy
cmake --build --preset macos-profile-tracy --target BagiStress
build/macos-profile-tracy/bin/BagiStress        # Tracy is the default backend of this build
```

Then open the Tracy viewer of the **same version** as the vendored client (v0.15.0; `src/Modules/tracy/README.md` says where to get it) and connect to `127.0.0.1`. Nothing is sent until a viewer connects. `--trace=<file>` in this build records the Chrome trace instead.

In Tracy every `Mutex` and `SharedMutex` is a lockable named after its `BE_LOCK_SITE` (`Task queue`, `Pool busy report`, ...): open the viewer's **Locks** window (or the lock rows of a thread's timeline) to see who held it, who waited and for how long. A lock shows once it has been used while the viewer is connected; Tracy's `lock wait` zones of the Chrome trace are not duplicated there.

## Marking code

```cpp
void SearchPlayer::Decide() {
    BE_PROFILE_FUNCTION();
    for (...) {
        BE_PROFILE_SCOPE("Rollout");
        ...
    }
    BE_PROFILE_COUNTER("Rollouts", count);
}
```

Mark coarse work: a stage, a system, one rollout. A zone that runs millions of times a second distorts what it measures; leave such functions to the sampling profiler.

## A thread that waits for a lock

A `Mutex` or `SharedMutex` of `BECore/Threading/` (every engine lock) that makes a thread wait at least 50 microseconds shows as a zone `lock wait: <the lock's name>` on that thread, inside the zones the thread has open, in the Chrome trace and in the editor's ring. So a stage that is long because it waits shows the lock and for how long; a lock nobody waits for shows nothing. The wait is timed only after a failed `try_lock`, so an uncontended lock reads no clock. `Profiler::SetLockWaitThresholdUs(n)` changes the threshold (a test or an investigation: 0 records every contended wait); with no backend installed nothing is read or recorded.

## What a zone costs

Measured on an Apple M-series machine with `macos-profile` (`BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter='ProfilerCost*'`):

| What | `macos-release` | `macos-profile` |
|------|-----------------|-----------------|
| `steady_clock::now()` | 13 to 20 | 13 |
| `ProfilerClock::Now()` (`mach_absolute_time`) | 4 to 5 | 4 |
| a zone, no backend installed | 0 (compiled out; 1.9 measured directly) | 4.5 (with the allocation snapshot) |
| a zone recorded into the Chrome trace | 25 (the recorder called directly) | 26 |
| `new` + `delete` of 64 bytes, one thread | 33 (the C++ runtime's) | 28 (counted) |

Counting allocations costs nothing measurable on one thread (the replacement calls `malloc` directly, the runtime's `operator new` adds its own layer); many threads allocating at once share the atomic totals and pay more. The libraries that allocate with their own malloc (pugixml, ImGui, SDL, stb, dr_flac) are routed through the same counters in this build and keep malloc in a shipped one (`Alloc/CLAUDE.md`).

## Sampling profilers

Use them on a `macos-profile` or Release build when the zones show a slow stage but not why, or for code without zones.

- **macOS, Instruments Time Profiler:** `xcrun xctrace record --template 'Time Profiler' --launch -- build/macos-profile/bin/BagiStress`, then open the `.trace` in Instruments. Allocations: the `Allocations` template. An `os_signpost` backend that would show the zones inside Instruments is not built (a later entry may add it).
- **Windows, Visual Studio:** Debug > Performance Profiler > CPU Usage on a Release build (or ETW with Windows Performance Recorder / WPA for whole-system views).
