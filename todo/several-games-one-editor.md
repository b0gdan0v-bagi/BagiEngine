---
status: deferred, 2026-11-01. Revisit once the workspace repository from the entry `games-to-own-repos` exists.
---
# Running several games from one editor: choose a model

**Where:** `src/main.cpp` (one executable is one game), `cmake/BagiEngine.cmake` (`be_add_game_executable` bakes `BAGI_GAME_ROOT` in at build time and links the game with `WHOLE_ARCHIVE`), `src/Modules/Games/` (`GameManager::SwitchTo` and `GetAll()` already handle several registered games, no widget exposes them), `src/Modules/BECore/FileSystem/` and `Config/` (the engine and the game layers are fixed at startup).

**What is missing:** a way to start or switch between games without choosing the executable. Needed: a place to build and test every game ([the workspace entry](games-to-own-repos.md)) and, maybe, one window that runs any of them. Options, cheapest first:

1. **Launcher only:** the workspace builds every exe; a VS Code launch configuration, a ctest group or a script starts one. No engine change.
2. **One executable with every game linked,** the game chosen at startup (an argument or a config) or at runtime: `AbstractFactory<IGame>` and `GameManager` already cope; `BAGI_GAME_ROOT` has to become a runtime parameter and the `FileSystem` / `ConfigManager` layers have to be rebuilt on a switch (a restart on switch is the cheap form).
3. **Games as shared libraries** loaded by an editor executable: a hot switch, the most work (ABI across the boundary, `Singleton<T>` and EASTL across modules, registration order, reflection).

**A fix has to cover:**

- the choice of model; recommendation: option 1 with the workspace, option 2 only when a person needs to switch games inside one window;
- for options 2 and 3, what a switch does to the scene, the textures, the input subscriptions and the control API (the `list_games` / `switch_game` methods exist, `Games/CLAUDE.md`);
- the Stage of the roadmap that owns it, if it joins the roadmap.
