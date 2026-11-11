---
name: perf-investigate
description: Investigates where the time and the allocations of one benchmark or scenario go - records a Chrome trace of the engine's profiler in the profile build (or samples with xctrace when the program writes no trace), summarises the top zones by self and total time, the allocations per zone and the frame-time distribution, makes hypotheses and files a todo entry with the numbers. Use when the user asks why something is slow, to profile a benchmark, to investigate a regression found by /perf or the merge sentinel, says "perf-investigate", "почему тормозит", "профилируй", "разберись с регрессией", "сними трейс".
---

# perf-investigate

`/perf-investigate <benchmark | scenario>`. You find where the time goes and say what to change, with numbers; you change no code. The tools: `<engine>/tools/perf/CLAUDE.md` (the runner's `--trace`, `trace_summary.py`), the profiler's contract and its how-to: `<engine>/src/Modules/BECore/Profiler/CLAUDE.md` and `HowToProfile.md` (read both first). Terms as in `.claude/skills/perf/SKILL.md`; read `.claude/rules/perf-project.md` of the repository.

## Steps

1. **What to look at.** A name of the set: that benchmark. Anything else is a scenario: write its command yourself (a game with `--trace=<file>` and the control API through `<engine>/tools/perf/headless_frames.py` and its `--call`, or a tool that takes `--trace=`) and say what you chose. For a regression from `/perf` or the sentinel, note the baseline commit and the commit measured: the diff between them (`git log --oneline A..B`, `git diff --stat A B`) is the first list of suspects.
2. **Trace.** A benchmark with `traceCommand`: `python3 <engine>/tools/perf/perf.py --trace <name> --trace-file <scratchpad>/<name>.json` (builds the set's `profilePreset`, runs once, prints the summary). A scenario: build the profile preset (`build` skill, the preset by name), run it with `--trace=`, then `python3 <engine>/tools/perf/trace_summary.py <file> --top 30`. Keep the JSON (`--json`) for the numbers you quote. The trace opens in https://ui.perfetto.dev for the owner.
3. **No trace possible** (the program has no `--trace=`, or the time is inside one zone and the zones do not split it): sample the Release build with `xcrun xctrace record --template 'Time Profiler' --output <scratchpad>/<name>.trace --launch -- <command>` (macOS; `HowToProfile.md` has the Windows way) and read the heaviest stacks with `xcrun xctrace export --input ... --xpath '/trace-toc/run/data/table[@schema="time-profile"]'`; if that is too coarse, say which zones (`BE_PROFILE_SCOPE`) would split the work and where. Adding zones is a code change for an entry, not part of this skill.
4. **Read it.**
   - **Where the frame goes**: the frame-time distribution (mean, p95, p99, max: spikes and hitches are their own finding), then the zones by self time (the code that burns it) and by total time (the subsystem it belongs to). The waits (present, acquire of the swapchain, idle of the manual clock) are not work: say so and look past them.
   - **Allocations**: the zones by inclusive allocations, per call and per frame (`allocs / calls`); a zone that allocates every frame in steady state is a finding by itself. The per-frame counters (`Allocations`, `Live bytes`) show growth (a leak looks like a live-bytes ramp).
   - **Counters**: draw calls, triangles, texture switches against what the scene should need.
   - Profile numbers carry the profiler's cost (`HowToProfile.md` gives the cost of a zone): compare shares and counts, not absolute times with a Release baseline.
5. **Hypotheses.** Two to four, each with the number that suggests it, the place in the code (`file:line` from the zone's site, which the trace keeps as `args.file` / `args.line` of its `B` event), what to try, and how to verify it (which benchmark and metric should move, by about how much). Order them by the expected gain.
6. **File an entry** (unless the owner says not to): through the `todo-add` skill's steps, in the area of the code (engine or game), with **Where** (the files), **What is wrong** or **What is missing** with the numbers (the summary lines you relied on, the machine, the build, the commit), **A fix has to cover** (the hypotheses as items, each with its check), and the benchmark that shows it. A regression filed by the sentinel already has an entry: add to it (`todo edit <id> --body-file -`) instead.
7. **Report** in the user's language: the top findings with their numbers, the hypotheses, the entry id, the paths of the trace and the summary in the scratchpad.

## Rules

- No code changes and no commits; traces and summaries stay in the scratchpad.
- Numbers you quote come from the files you produced in this run, with the build (preset) and the commit they belong to.
- Profile one thing at a time, on a quiet machine (as `/perf`, step 1).
