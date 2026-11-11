# tools/perf

The benchmark runner of the skills `perf` and `perf-investigate` (`.claude/skills/perf/`, `.claude/skills/perf-investigate/`) and of the merge sentinel of `todo-merge`. Standard library only, Python 3.10+. The engine knows nothing of it; what a repository measures is its benchmark set in `.claude/rules/perf-project.md`.

| File | Purpose |
|------|---------|
| `perf.py` | the runner: builds, runs the set N times, compares with the baseline, `--update-baseline`, `--trace NAME` |
| `perf_stats.py` | mean, t interval, Welch comparison, the verdicts |
| `perf_baseline.py` | the machine fingerprint, the baseline plan (read, merge, append), the todo store through `tools/todo/todo.py` |
| `headless_frames.py` | a benchmark command: a game `--headless --manual-clock`, N frames stepped over the control API, one JSON line of `frame_stats` means |
| `trace_summary.py` | a Chrome trace of the profiler: zones by self / total time and allocations, the frame-time distribution, the counters |
| `tests/` | pytest of all of it (a whole run over a fake benchmark; no build, no store) |

```bash
python3 <engine>/tools/perf/perf.py --list                   # from the root of the repository whose set is meant
python3 <engine>/tools/perf/perf.py [--fast] [--only a,b] [--repetitions N] [--no-build] [--json FILE]
python3 <engine>/tools/perf/perf.py --update-baseline        # the owner's word only (the skill asks)
python3 <engine>/tools/perf/perf.py --trace NAME [--trace-file F]   # the profile preset, one run with a trace (build/perf/), its summary
python3 <engine>/tools/perf/trace_summary.py trace.json [--top N] [--json]
.venv/bin/python -m pytest tools/perf/tests                  # in the engine
```

Exit code of `perf.py`: 0 (no regression), 3 (a regression), 1 (a failed build or run, a bad set, the store unreachable).

## The benchmark set

A fenced `json` block under `## Benchmark set` of `.claude/rules/perf-project.md` (the runner finds the first one after that heading):

| Key | Meaning |
|-----|---------|
| `area` | the todo area: names the baseline plan `perf-baseline-<area>-<machine>.md` |
| `preset`, `targets` | what is built and measured (a Release preset: no profiler, no asserts) |
| `profilePreset` | the build of `--trace` (default `macos-profile`) |
| `repetitions`, `thresholdPercent` | runs per benchmark (default 5) and the smallest change that counts (default 3) |
| `benchmarks[]` | `name`, `about`, `fast` (in the sentinel's subset), `command`, `traceCommand` (optional: the same work writing a trace into `{trace}`), `repetitions`, `warmup` (runs thrown away first, default 1), `timeoutSeconds` (600), `metrics` |
| `metrics.<name>` | `source`: `regex` (`pattern`, group 1 of the first match, multiline), `json` (`key` of the last stdout line that starts with `{`) or `wall` (the wall seconds of the command); `unit`; `better` `lower` / `higher`; `gate` (default true; false: shown, never a verdict); `thresholdPercent` |

Placeholders in a command: `{bin}` (`<root>/build/<preset>/bin`), `{root}`, `{engine}` (the engine folder), `{python}` (the interpreter of the runner), `{trace}`. Anything else in braces stays as written (JSON parameters are safe).

A benchmark prints its numbers; it never reads the baseline. A metric of the time of a decision, a frame or a test run is fine; a metric that a seed changes (the draw calls of a random deal) is `gate: false`.

## Statistics and verdicts

Each metric: the mean of the repetitions and the half-width of its 95 % interval by Student's t (five runs are too few for the 1.96 of the normal approximation). Against the baseline: Welch's interval of the difference of the means. **Regression** or **improvement** needs both: the interval excludes 0, and the change is at least `thresholdPercent` of the baseline's mean (a machine drifts a percent or two between days; five runs of one day do not see that drift). Otherwise **noise**; a metric without a baseline is **new**. A benchmark takes its worst gated verdict.

## The baseline

A plan of kind `baseline` in the todo store, one per area and machine, no entry. The machine is named by a fingerprint (model, CPU, cores, memory, OS and its major version); the slug of the plan name keeps model, cores and OS (`mac16-7-14c-macos26`). Each `--update-baseline` appends a section (the date, the commits, the preset) with a fenced JSON record; the last record is the baseline, the ones above are its history. A record holds every benchmark of the area, each with the commit it was measured on and its samples, so updating a few keeps the others. The commit is `<repo>[-dirty]` plus `+engine-<commit>[-dirty]` in a game; `--update-baseline` refuses a dirty tree unless `--allow-dirty`. `--baseline-file F` uses a local file instead of the store (tests, offline).

## Caveats

- Headless frames are mostly the software renderer's present (`SDLRenderer::PresentFrame`): `workMs` (`cpuMs` minus the `Present` stage) is the part that follows the scene, and it is small and noisy in an idle scene.
- The runner measures one machine against itself. Other load on the machine (a build of a worker) makes noise or false regressions: the skill checks the load first.
- Allocation counts need `ENGINE_PROFILER_ALLOC` (the profile preset); Release reports 0, so allocations are read through `--trace` (the `E` events' `allocs`) and `zone_allocs`, not gated in the Release set.
