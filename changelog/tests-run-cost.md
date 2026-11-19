# Tests: a full ctest costs about 1700 s of CPU and runs at every step; process start, hygiene checks, timing, crash and copied-helper debt

- Version: 0.6.0
- Entry: tests-run-cost
- Date: 2026-11-19
- Bump: minor

## Added
- `be_add_game_tests` registers a gtest executable as `ENGINE_TEST_SHARDS` (default 16) ctest entries `<Exe>.Shard<k>` instead of one entry per case; a full engine ctest takes about 14 s and 49 s of CPU instead of about 200 s and 1450 s. `-DENGINE_TESTS_PER_CASE=ON` gives back one entry per case. Adapt: nothing to change; a game's cases now run many to a process, so a case must leave no state for the next one (`.claude/rules/tests-module.md`, "Shards"), and one case is run with `bin/<Exe> --gtest_filter=Suite.Case` from `build/<p>`.
- ctest labels `unit`, `smoke`, `scenario` (set by the `be_add_*` helpers) and `extended` (hygiene checks, fuzz mutations, room server smoke and hostile, TLS endpoint), and the test presets `<p>-all`, `<p>-fast`, `<p>-smoke`, `<p>-scenarios` beside every test preset. Adapt: a game's own test preset gets the four siblings (`.claude/rules/cmake-rules.md`).
- `tools/alltests/alltests.py` and the skill `alltests`: the full run (`<p>-all`) of a commit of the base branch in a worktree of its own, a result per commit, and a todo entry `alltests-<test>` per failure. Adapt: a game links the skill into its `.claude/skills/` like the others and runs `alltests.py --repo <its root>`.
- `CI/scripts/baseline_cache.py`: the loop's test baseline cached per commit and preset for every worktree.
- `GetRoomCloseCode(reason)` in `RoomServer/RoomWebSocketServer.h`: the WebSocket close code of a hub close reason.

## Changed
- The default test preset leaves out the label `extended`; the hygiene checks, the fuzz mutations and the room server end-to-end tests run only in `<p>-all` (the skill `alltests`, the deploy gate, `sync-engine`). `FuzzCorpusTest` replays the corpus only, unless `BAGI_FUZZ_ITERATIONS` is set.
- `tools/web/build_and_deploy.py` and `tools/rooms/build_and_deploy.py` refuse `--deploy` unless the commit has a green alltests result and the working tree is clean; `--skip-alltests` deploys anyway. Adapt: run `alltests` on the commit before a deploy.
- A command-line flag that appears twice keeps its first value everywhere: `ResolveControlSettings` (`--control-port`, `--window`, ...) reads through `CommandLine` like the rest. Adapt: a launcher that appended a flag to override an earlier one puts it first instead.
- The todo loop's developer, reviewer and `todo-merge` run the default preset, take the baseline from the cache, and give a large entry by its part.

## Breaking
- `SubscriptionHolder` can no longer be moved: a moved holder kept connections bound to the moved-from address and called into a destroyed object. Adapt: a game type that derives from it and is moved (into a container or a member) is kept in place instead (held by `UniquePtr`, or built where it lives).
