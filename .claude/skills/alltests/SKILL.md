---
name: alltests
description: Runs the full test run of this repository (every test, the extended hygiene checks and fuzz mutations included, through the ctest preset <p>-all) on the tip of the local base branch or on a named commit, in a worktree and build folder of its own, in the background; records the result per commit for the deploy gate, files or updates a todo entry alltests-<test> for each failure, and reports in Russian with the entries named. Run ONLY on the owner's explicit command; never run by the loop and never offered by another skill (only the deploy scripts name it when they refuse). Use when the owner says "alltests", "run all tests", "the full test run", "прогони все тесты", "полный прогон".
---

# alltests

`/alltests [commit=<rev>] [dry-run]`. You run the full test run and say what it found; you fix nothing. The script does the work (`<engine>/tools/alltests/CLAUDE.md`: the work folder, the results file, the entries); you pick its arguments, wait for it and read its result. The everyday `ctest --preset <p>` leaves the `extended` tests out; this run is where they run, before a deploy or a release, or when the owner wants to know.

Terms: `<engine>` is `.` in the engine and `engine` in a game. `<p>` is the preset the `build` skill would pick (`.claude/rules/build-project.md` of the repository you run in: `macos-debug` on macOS in the engine). `<area>` is the todo area of this repository (`.claude/rules/todo-project.md`, the row whose `<R>` is `.`). `<base>` is that row's base branch. Ask and report in Russian.

## Steps

1. **Preconditions.** Run from the root of the repository (the game root in a game). Unless `dry-run`: `python3 <engine>/tools/todo/todo.py ping` must list `<area>`, otherwise say that the entries cannot be filed and offer `dry-run` (AskUserQuestion). The run tests a commit of the **local** `<base>` (or `commit=`) and fetches nothing. The first run in a work folder builds everything from scratch (minutes); later runs are incremental.
2. **Run in the background** (`run_in_background`), the output to a log in the scratchpad:
   `python3 <engine>/tools/alltests/alltests.py --repo . --base <base> --preset <p> --area <area> [--commit <rev>] [--dry-run] > <scratchpad>/alltests-<short sha or base>.log 2>&1`.
   Do not start a second run or a build of the same preset beside it (the script's lock refuses a second run of the same repository anyway). Wait for the notice that it ended; do not poll.
3. **Read the result**: only the lines that start with `alltests:` (`grep '^alltests:' <log>`) and, on an error, the last lines of the log. Exit code 0 green, 3 red, 1 an error (nothing recorded: show the message; a missing `<p>-all` test preset or a submodule commit that is not on this machine is the owner's to fix). For a red build, the tail of `logs/build.log` in the work folder gives the first error with `file:line`.
4. **Report** in Russian, short: the commit and the preset, green or red, the number of tests, the failed names, the flaky ones (failed under load, passed alone: not filed, but name them), every entry with what was done to it (`filed`, `updated`, `reopened`; with `dry-run`, what would have been done), the work folder and the logs. Green: the commit passes the deploy gate of `tools/web/build_and_deploy.py` and `tools/rooms/build_and_deploy.py`. Red: the entries wait for the loop or the owner.

## Rules

- No fix, no commit, no `fetch`, `pull` or `push`; the store is written only by the script (`--actor alltests`), never by hand, and a green run closes no entry.
- Not run by `todo-loop`, `todo-merge` or any other skill, and never offered by them: the deploy scripts name it when their gate refuses, and `sync-engine` runs the script itself on `main`.
