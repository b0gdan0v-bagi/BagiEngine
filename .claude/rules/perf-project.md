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
  "targets": ["BagiMatch3", "BagiEngineTests", "Match3Tests"],
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
- The sentinel (`todo-merge`): `ask`.
