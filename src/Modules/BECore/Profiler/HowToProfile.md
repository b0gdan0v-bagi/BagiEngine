# How to profile

Two kinds of tool answer different questions. **Zones** (this module) say how long the marked stages of a frame or a tool take and, with allocation tracking, how much each of them allocates. **A sampling profiler** needs no marks and sees every function, but not the frame structure. Start with zones to find the stage, then sample inside it.

## Measure a build that is fast

Numbers come from the `macos-profile` preset (RelWithDebInfo, asserts and expects off, no editor, `ENGINE_PROFILER` and `ENGINE_PROFILER_ALLOC` on), never from Debug, whose code is several times slower and differently shaped:

```bash
cmake --preset macos-profile
cmake --build --preset macos-profile --target BagiMatch3 BagiEngineTests
```

`macos-release` keeps the profiler off: the baselines of a performance check are measured without any overhead.

`macos-profile-editor` is the same build with the editor, for the ImGui Profiler panel (Ctrl+Shift+E to the editor, then Ctrl+Shift+P). The editor draws the scene into the viewport's offscreen target under ImGui, so its frame is not the Game frame: measure in `macos-profile` or with Tracy.

## A Chrome trace of a game run

```bash
build/macos-profile/bin/BagiMatch3 --trace=trace.json
```

The file is written when the game ends (relative to the working folder). Open it in https://ui.perfetto.dev (drag the file in) or `chrome://tracing`. Each thread is a track; frame marks are the vertical lines; counters are graphs; a zone's `args` hold its file, line and (with allocation tracking) the allocations it made.

A headless run with the manual clock gives a trace of exactly N frames: `--headless --control --manual-clock --trace=trace.json`, then a `step` of N frames and `quit` through the control API (`tools/control/`).

`--profiler=chrome` without `--trace` records without writing at the end. Flags in a build without the profiler stop the start-up with a message.

## In the editor

Ctrl+Shift+E to the editor, then Ctrl+Shift+P opens the Profiler panel. Zones need a build with `ENGINE_PROFILER` (`macos-profile-editor`); without it the panel says so. The panel records into a ring only while it is open, so it shows frames since it was opened.

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

## Tracy

For a live view (zones, frames, plots, allocations with call stacks, while the game runs):

```bash
cmake --preset macos-profile-tracy
cmake --build --preset macos-profile-tracy --target BagiMatch3
build/macos-profile-tracy/bin/BagiMatch3        # Tracy is the default backend of this build
```

Then open the Tracy viewer of the **same version** as the vendored client (v0.15.0; `src/Modules/tracy/README.md` says where to get it) and connect to `127.0.0.1`. Nothing is sent until a viewer connects. `--trace=<file>` in this build records the Chrome trace instead.

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

## What a zone costs

Measured on an Apple M-series machine with `macos-profile` (`BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter='ProfilerCost*'`):

| What | `macos-release` | `macos-profile` |
|------|-----------------|-----------------|
| `steady_clock::now()` | 13 to 20 | 13 |
| `ProfilerClock::Now()` (`mach_absolute_time`) | 4 to 5 | 4 |
| a zone, no backend installed | 0 (compiled out; 1.9 measured directly) | 4.5 (with the allocation snapshot) |
| a zone recorded into the Chrome trace | 25 (the recorder called directly) | 26 |
| `new` + `delete` of 64 bytes, one thread | 33 (the C++ runtime's) | 28 (counted) |

Counting allocations costs nothing measurable on one thread (the replacement calls `malloc` directly, the runtime's `operator new` adds its own layer); many threads allocating at once share the atomic totals and pay more.

## Sampling profilers

Use them on a `macos-profile` or Release build when the zones show a slow stage but not why, or for code without zones.

- **macOS, Instruments Time Profiler:** `xcrun xctrace record --template 'Time Profiler' --launch -- build/macos-profile/bin/BagiMatch3`, then open the `.trace` in Instruments. Allocations: the `Allocations` template. An `os_signpost` backend that would show the zones inside Instruments is not built (a later entry may add it).
- **Windows, Visual Studio:** Debug > Performance Profiler > CPU Usage on a Release build (or ETW with Windows Performance Recorder / WPA for whole-system views).
