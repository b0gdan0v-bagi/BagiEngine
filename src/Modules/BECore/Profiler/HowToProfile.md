# How to profile

Two kinds of tool answer different questions. **Zones** (this module) say how long the marked stages of a frame or a tool take and, with allocation tracking, how much each of them allocates. **A sampling profiler** needs no marks and sees every function, but not the frame structure. Start with zones to find the stage, then sample inside it.

## Measure a build that is fast

Numbers come from the `macos-profile` preset (RelWithDebInfo, asserts and expects off, no editor, `ENGINE_PROFILER` and `ENGINE_PROFILER_ALLOC` on), never from Debug, whose code is several times slower and differently shaped:

```bash
cmake --preset macos-profile
cmake --build --preset macos-profile --target BagiMatch3 BagiEngineTests
```

`macos-release` keeps the profiler off: the baselines of a performance check are measured without any overhead.

## A Chrome trace of a game run

```bash
build/macos-profile/bin/BagiMatch3 --trace=trace.json
```

The file is written when the game ends (relative to the working folder). Open it in https://ui.perfetto.dev (drag the file in) or `chrome://tracing`. Each thread is a track; frame marks are the vertical lines; counters are graphs; a zone's `args` hold its file, line and (with allocation tracking) the allocations it made.

A headless run with the manual clock gives a trace of exactly N frames: `--headless --control --manual-clock --trace=trace.json`, then a `step` of N frames and `quit` through the control API (`tools/control/`).

`--profiler=chrome` without `--trace` records without writing at the end. Flags in a build without the profiler stop the start-up with a message.

## A trace of a tool without a window

```cpp
#include <BECore/Profiler/ScopedTrace.h>

{
    BECore::ScopedTrace trace("arena-trace.json");  // records from here
    RunRollouts();                                  // marked with BE_PROFILE_SCOPE inside
}                                                   // written here
```

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

| What | ns per call |
|------|-------------|
| `steady_clock::now()` | 12.8 |
| `ProfilerClock::Now()` (`mach_absolute_time`) | 4.0 |
| a zone, no backend installed | 1.7 |
| a zone recorded into the Chrome trace | 22.7 |

## Sampling profilers

Use them on a `macos-profile` or Release build when the zones show a slow stage but not why, or for code without zones.

- **macOS, Instruments Time Profiler:** `xcrun xctrace record --template 'Time Profiler' --launch -- build/macos-profile/bin/BagiMatch3`, then open the `.trace` in Instruments. Allocations: the `Allocations` template. An `os_signpost` backend that would show the zones inside Instruments is not built (a later entry may add it).
- **Windows, Visual Studio:** Debug > Performance Profiler > CPU Usage on a Release build (or ETW with Windows Performance Recorder / WPA for whole-system views).
