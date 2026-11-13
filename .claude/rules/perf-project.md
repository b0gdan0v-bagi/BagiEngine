---
paths:
  - "tools/perf/**/*"
  - "src/Modules/BECore/Profiler/**/*"
---

# The benchmarks of this repository

Read by the skills `perf` and `perf-investigate` and by the sentinel step of `todo-merge` (`.claude/skills/perf/SKILL.md`; the runner and the format of the set: `tools/perf/CLAUDE.md`). A game that carries this repository as the submodule `engine/` has a file of the same name with its own set; the skills are the same text in both.

## Benchmark set

```json
{
  "area": "engine",
  "preset": "macos-release",
  "profilePreset": "macos-profile",
  "targets": ["BagiMatch3", "BagiStress", "BagiEngineTests", "Match3Tests"],
  "repetitions": 5,
  "thresholdPercent": 3,
  "benchmarks": [
    {
      "name": "match3-frames",
      "about": "Match3 without a window: 3000 frames after 120 of warm-up (a frame is a fifth of a millisecond, so fewer would measure the steps)",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiMatch3", "--frames", "3000"],
      "traceCommand": ["{python}", "{engine}/tools/perf/headless_frames.py", "--exe", "{bin}/BagiMatch3", "--frames", "3000", "--trace", "{trace}"],
      "metrics": {
        "fps": {"source": "json", "key": "framesPerSecond", "unit": "frames/s", "better": "higher"},
        "cpu_ms": {"source": "json", "key": "cpuMs", "unit": "ms/frame", "better": "lower"},
        "work_ms": {"source": "json", "key": "workMs", "unit": "ms/frame", "better": "lower"},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-sprites",
      "about": "BagiStress sprites without a window: 5000 spinning sprites, one IRenderer call each, 300 frames after 30 of warm-up. work_ms_p50 is about 0.8 ms (the scene update and the calls); a frame is about 63 ms because the software renderer draws the sprites in its present, so more sprites or frames only make the run longer (10000 sprites: 43 s a run). The number a 2D batcher moves is work_ms",
      "fast": false,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "sprites", "--count", "5000", "--frames", "300", "--warmup", "30"],
      "traceCommand": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "sprites", "--count", "5000", "--frames", "300", "--warmup", "30", "--trace", "{trace}"],
      "metrics": {
        "work_ms_p50": {"source": "json", "key": "workMsP50", "unit": "ms/frame", "better": "lower"},
        "work_ms_p99": {"source": "json", "key": "workMsP99", "unit": "ms/frame", "better": "lower", "gate": false},
        "cpu_ms_p50": {"source": "json", "key": "cpuMsP50", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-entities",
      "about": "BagiStress entities without a window: 30000 nodes with a transform and a tween each, nothing drawn, 3000 frames after 120 of warm-up. work_ms_p50 is about 1.7 ms (p99 2.3), a run about 6 s; 50000 nodes would be 3.3 ms and 12 s",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "entities", "--count", "30000", "--frames", "3000", "--warmup", "120"],
      "traceCommand": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "entities", "--count", "30000", "--frames", "3000", "--warmup", "120", "--trace", "{trace}"],
      "metrics": {
        "work_ms_p50": {"source": "json", "key": "workMsP50", "unit": "ms/frame", "better": "lower"},
        "work_ms_p99": {"source": "json", "key": "workMsP99", "unit": "ms/frame", "better": "lower", "gate": false},
        "cpu_ms_p50": {"source": "json", "key": "cpuMsP50", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-ui",
      "about": "BagiStress ui without a window: a canvas of 3000 text labels in English and Russian, 3000 frames after 120 of warm-up. work_ms_p50 is about 1.7 ms of a 2.5 ms frame (the rest is the present), a run about 8 s; 5000 labels would be 2.1 ms and 10 s",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "ui", "--count", "3000", "--frames", "3000", "--warmup", "120"],
      "traceCommand": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "ui", "--count", "3000", "--frames", "3000", "--warmup", "120", "--trace", "{trace}"],
      "metrics": {
        "work_ms_p50": {"source": "json", "key": "workMsP50", "unit": "ms/frame", "better": "lower"},
        "work_ms_p99": {"source": "json", "key": "workMsP99", "unit": "ms/frame", "better": "lower", "gate": false},
        "cpu_ms_p50": {"source": "json", "key": "cpuMsP50", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-sprites-ramp",
      "about": "BagiStress sprites ramp: the count at which the whole CPU frame fits 16.6 ms (measure cpu: headless the software renderer draws the sprites inside its present, which work_ms leaves out, so the work measure would reach 100000 sprites at over a second a frame). About 1300 on the benchmark machine in 6 to 8 s, though one run in four stops lower on a noisy step. gate false until a baseline shows the noise of the number",
      "fast": false,
      "repetitions": 3,
      "timeoutSeconds": 120,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "sprites", "--ramp", "--measure", "cpu"],
      "metrics": {
        "objects_at_60fps": {"source": "json", "key": "objectsAt60fps", "unit": "objects", "better": "higher", "gate": false}
      }
    },
    {
      "name": "stress-entities-ramp",
      "about": "BagiStress entities ramp: the count at which work_ms fits 16.6 ms, about 150000 to 158000 nodes in 6 to 7 s over three runs (the line says verified true; a fixed run of 150000 nodes after 500 warm-up frames has work_ms_p50 16.7 ms). The tweens restart every frame at these counts. gate false until a baseline shows the noise of the number",
      "fast": false,
      "repetitions": 3,
      "timeoutSeconds": 180,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "entities", "--ramp"],
      "metrics": {
        "objects_at_60fps": {"source": "json", "key": "objectsAt60fps", "unit": "objects", "better": "higher", "gate": false}
      }
    },
    {
      "name": "stress-ui-ramp",
      "about": "BagiStress ui ramp: the count at which work_ms fits 16.6 ms, about 75000 labels in 9 s (the run to run spread is a few percent). gate false until a baseline shows the noise of the number",
      "fast": false,
      "repetitions": 3,
      "timeoutSeconds": 120,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "ui", "--ramp"],
      "metrics": {
        "objects_at_60fps": {"source": "json", "key": "objectsAt60fps", "unit": "objects", "better": "higher", "gate": false}
      }
    },
    {
      "name": "engine-tests",
      "about": "BagiEngineTests in one process (it waits on threads and sockets, so wall time, not CPU)",
      "command": ["{bin}/BagiEngineTests", "--gtest_brief=1"],
      "metrics": {
        "wall_s": {"source": "wall", "unit": "s", "better": "lower"}
      }
    }
  ]
}
```

## Notes

- Headless, the present of the SDL software renderer is most of a frame whatever the scene draws; `work_ms` (the frame without the present) is the number that moves with scene and UI code.
- The `stress-*` benchmarks run `BagiStress` (`tools/stress/CLAUDE.md`) through `tools/perf/stress_frames.py`; the game measures itself (`work_ms` is the CPU time of a frame without the present, nearest-rank percentiles) and the ramp is a number per scene and machine (the scenes do not compare with one another). Their baseline is the owner's word, like every other.
- The sentinel (`todo-merge`): `ask`.
