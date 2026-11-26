# tools

Each tool has its own `CLAUDE.md`; Python tools have pytest suites outside ctest (`.venv/bin/python -m pytest tools/<t>/tests`).

| Folder | What |
|--------|------|
| `todo` | CLI loader of the todo store (the BagiTodo service); nothing of the store is in git |
| `tokstats` | token cost of a todo entry per role, with its review rounds and plan accuracy |
| `control` | control API client, smoke test, scenario player, MCP server |
| `rooms` | BagiRoomServer smoke test, BagiRoomClientProbe, `load.py` (traced load run), `docker_check.py`, `Dockerfile` |
| `perf` | benchmarks, trace summary (skills `perf`, `perf-investigate`) |
| `qa` | the QA player and monkey (skills `qa`, `qa-loop`) |
| `stress` | `BagiStress`, the 2D stress app: benchmarks and a control smoke |
| `shots` | before / after pictures of scenes, PNG diff |
| `sync` | helper of the `sync-engine` skill |
| `alltests` | the full test run on demand (skill `alltests`, the deploy gate, `sync-engine`); a failure files `alltests-<test>` |
| `changelog` | engine version and changelog fragments |
| `migrate` | rewrites a game's content files to the engine's format |
| `schema` | XSDs from reflection |
| `codemod` | moves `eastl::` / `std::` names to the `BECore` aliases; `log`: `LOG_X(Format(...))` to `LOG_X("...", args)` |
| `dev` | `dev.py`: a game's products from its `dev.json` |
| `artgen` | AI art and sounds: ComfyUI, presets (skill `artgen`) |
| `ccache` | shared cache hits |
| `export` | a package of a game that runs by itself, desktop or web |
| `web` | the browser build |
| `fuzz` | `run_fuzzers.py`, the long libFuzzer runs (`tests/Fuzz/CLAUDE.md`) |
| `natvis` | debugger views of EASTL |
| `vscode` | the `bagi-dev` panel (the Todo Workers panel comes from the BagiTodo service: `todo/CLAUDE.md`) |
