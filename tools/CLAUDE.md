# tools

Each tool has its own `CLAUDE.md`; Python tools have pytest suites outside ctest (`.venv/bin/python -m pytest tools/<t>/tests`).

| Folder | What |
|--------|------|
| `todo` | CLI loader of the todo store (the BagiTodo service); nothing of the store is in git |
| `control` | control API client, smoke test, scenario player, MCP server |
| `rooms` | BagiRoomServer smoke test, BagiRoomClientProbe, `docker_check.py`, `Dockerfile` |
| `perf` | benchmarks, trace summary (skills `perf`, `perf-investigate`) |
| `qa` | the QA player and monkey (skills `qa`, `qa-loop`) |
| `stress` | `BagiStress`, the 2D stress app: benchmarks and a control smoke |
| `shots` | before / after pictures of scenes, PNG diff |
| `sync` | helper of the `sync-engine` skill |
| `changelog` | engine version and changelog fragments |
| `migrate` | rewrites a game's content files to the engine's format |
| `schema` | XSDs from reflection |
| `codemod` | moves `eastl::` / `std::` names to the `BECore` aliases |
| `dev` | `dev.py`: a game's products from its `dev.json` |
| `artgen` | AI art and sounds: ComfyUI, presets (skill `artgen`) |
| `ccache` | shared cache hits |
| `web` | the browser build |
| `fuzz` | `run_fuzzers.py`, the long libFuzzer runs (`tests/Fuzz/CLAUDE.md`) |
| `natvis` | debugger views of EASTL |
| `vscode` | the `todo-workers` and `bagi-dev` panels |
