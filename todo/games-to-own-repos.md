---
status: deferred, 2026-11-01. Planned, nothing moved yet.
---
# Games live in the engine repository: move them out and add a workspace repository

**Where:** `games/Chess`, `games/Match3`, `games/CMakeLists.txt`, `CMakeLists.txt` (`VS_STARTUP_PROJECT`), `CMakePresets.json` (`targets` of `build-base`), `CI/BagiEngine.code-workspace` (launch and task entries), `tools/control/stockfish_bridge.py` and `tools/control/tests/test_stockfish.py` (Chess by nature), `.claude/rules/cmake-rules.md`, `.claude/rules/tests-module.md`, `src/Modules/Games/CLAUDE.md` ("Adding a game"), `WORKSPACE_README.md` and `launcher_config.json` (what the PyQt launcher builds).

**What is missing** (found by reading the code, not by building):

1. Each game's `CMakeLists.txt` has a standalone block (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` → `project()` + `include(engine/cmake/BagiEngine.cmake)`), and the engine side is ready for it (`BAGI_ENGINE_ROOT` comes from the location of `BagiEngine.cmake`, `games/` is guarded by `EXISTS`, `be_add_game_control_tests` passes `BAGI_ENGINE_DIR` to a game's script). Nobody has built a game as the top-level project, so the block is untested.
2. There is no place that builds and tests every game once the games leave. The engine repository alone will build no game.

**A fix has to cover:**

- a rehearsal first, with no change to the repository: copy `games/Chess` next to an engine clone (scratchpad), configure, build `BagiChess` and `ChessTests`, run `ctest` and `BagiChessControlSmoke`; write down every break (paths from `CMAKE_SOURCE_DIR`, presets, `.clang-format` lookup, PCH, `build/Generated`, the Vulkan loader copy) and fix it in the engine before the split;
- Chess first, then Match3: `git filter-repo --subdirectory-filter games/<Name>` on a fresh clone (history kept), the engine as the submodule `engine/`, a `CMakePresets.json` that includes the engine's, `.clang-format` / `.gitignore`, the `.claude/` skills and rules the game needs, and for Chess the Stockfish bridge and its test; configure, build, tests and a run must pass in the new repository. The owner creates the repositories and pushes;
- a workspace repository (`BagiWorkspace`) with the submodules `engine/`, `games/Chess`, `games/Match3` and its own top-level `CMakeLists.txt` and presets: one `cmake --build` builds every game, one `ctest` runs every test. The games' own `engine/` submodules stay uninitialised there (`update = none`), so there is a single engine checkout and an engine change is tested against all games at once. The engine stays free of game names: the workspace is a separate repository, not `games/` submodules of the engine (that would also make a submodule cycle);
- the engine cleanup, after the games build in their repositories: `git rm -r games/`, the `targets` list, the IDE workspace file and the docs above, with `BagiEngineTests` building and passing without any game; "Adding a game" becomes "make a repository like Chess";
- the build commands in `CLAUDE.md` and the `build` skill (the engine repository no longer builds a game; say where), and a note on the `engine/` workflow in each game repository (commit and push inside `engine/`, bump the pointer in a separate commit);
- [the CI entry](no-ci.md) then builds the workspace (`git submodule update --init --recursive`), and [the Mahjong entry](mahjong.md) starts as its own repository.
