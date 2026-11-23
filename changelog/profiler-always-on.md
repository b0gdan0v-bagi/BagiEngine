# Profiler: a stats build with a top-zones overlay and a trace export, also on the web

- Version: 0.14.2
- Entry: profiler-always-on
- Date: 2026-11-23
- Bump: patch

## Added
- The stats build: the option `ENGINE_PROFILER_STATS` (Release otherwise) and the presets `macos-release-stats`, `windows-clang-release-stats`, `web-release-stats` and `web-release-single-thread-stats`. It records zones into the frame ring, also on the web, where it is the only way to switch the profiler on. The desktop stats presets also count allocations. Adapt: nothing; a game builds it with the same presets.
- The profiler overlay shows the top zones by self time from the ring, and saves the last 10 seconds as a Chrome trace (Ctrl+Shift+T or the "Save trace" button): a file on desktop, a download in the browser. `tools/perf/trace_summary.py` reads it. Adapt: nothing.
- `tools/web/web_smoke.py --stats` runs the stats page once. Adapt: nothing.
