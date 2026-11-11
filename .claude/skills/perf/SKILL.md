---
name: perf
description: Runs the benchmark set of this repository (.claude/rules/perf-project.md) in a Release build N times, compares each benchmark with the baseline of this machine in the todo store and says regression, improvement or noise per benchmark; updates the baseline only on the owner's word. Use when the user asks to benchmark, measure performance, check whether a change or a merge made something slower, compare with the baseline, record or update a baseline, says "perf", "замерь производительность", "не стало ли медленнее", "прогони бенчмарки", "обнови baseline".
---

# perf

`/perf [fast] [<benchmark>,...] [runs=N] [no-build] [update-baseline]`. You are a performance engineer: you measure, compare and report numbers with their uncertainty, and you never call noise a regression or a regression noise. The runner does the measuring and the statistics (`<engine>/tools/perf/CLAUDE.md`); you choose what to run, guard the conditions, read the result and say what it means.

Terms: `<engine>` is `.` in the engine and `engine` in a game; the benchmark set, its notes and the sentinel's setting are in `.claude/rules/perf-project.md` of the repository you run in. Read that file first. `todo` is `python3 <engine>/tools/todo/todo.py --actor perf`.

## Steps

1. **Preconditions.** Run from the root of the repository (the game root in a game; the engine's set only from `engine/` itself). `python3 <engine>/tools/perf/perf.py --list` must list the set. Check the load of the machine: `uptime` (the load average) and `ps -Ao pcpu,comm -r | head -8`. A build, a test run or a worker loop (`python3 <engine>/tools/todo/worker.py list`) running beside makes the numbers worthless: name it and ask whether to wait, rather than measuring through it. Battery power on a laptop (`pmset -g batt`) throttles: say so.
2. **Run.** `python3 <engine>/tools/perf/perf.py [--fast] [--only a,b] [--repetitions N] [--no-build] --json <scratchpad>/perf-<date>.json` in the background when it takes more than a minute (the set's `about` lines and `--list` give a feel; the build is incremental). Do not run two at once and do not build anything while it runs. Exit 1 is an error: show it and stop.
3. **Read the result.** For each benchmark: the verdict and, for every gated metric, the mean ± the 95 % half-width, the baseline's mean and the change in percent. Then judge it:
   - **regression** (exit 3): repeat that benchmark once with `--only <name> --repetitions 10 --no-build` before believing it. If it holds, it is a regression: say how big, since which baseline commit (`baselineCommit` of the report), and offer `/perf-investigate <name>`.
   - **improvement**: say it; if the owner wants it kept as the new reference, that is an update of the baseline (step 4).
   - **noise**: say "no change measured", with the interval: a half-width of 10 % cannot see a 5 % change, and then say that more runs (`runs=`) would.
   - **new**: there is no baseline for it on this machine; offer to record one.
   - A metric whose half-width is more than about a fifth of its mean is unstable: name it; more frames or seeds in its command (the rules file) would help, which is the owner's change.
4. **Update the baseline only on the owner's word.** `update-baseline` in the arguments, or a yes to a question you asked after the report, is the word; a regression is never written as the new baseline without that word naming it. Then `perf.py --update-baseline --no-build` (it measures again: a baseline is never written from numbers of another run). It refuses a tree with uncommitted changes; `--allow-dirty` only when the owner says so. The plan is `perf-baseline-<area>-<machine>.md` in the store (`todo plan url <name>` for the page). If the store refuses the kind `baseline`, the service has not been deployed since `schema/005_baseline_plans.sql`: say that the deploy is the owner's step.
5. **Report** in the user's language, short: the machine (the slug from the first line), the preset, the commit, a table of benchmark / verdict / the main metric with ± and change, what you concluded and what you propose (investigate, more runs, update the baseline). The JSON report stays in the scratchpad; give its path.

## Rules

- Measure only Release builds (the set's `preset`); the profile build is for `/perf-investigate`, its numbers carry the profiler's cost and are never compared with a baseline.
- One machine, one baseline: never compare with the baseline of another machine, never copy numbers between machines.
- Never edit the baseline plan by hand and never write it without the owner's word. Never commit a report or a trace.
- The benchmark set is the owner's: propose a change to `.claude/rules/perf-project.md`, make it only when asked.
