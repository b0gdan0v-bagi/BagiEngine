# CLAUDE.md

BagiEngine: a C++23 game engine on a scene graph of `SceneNode` + `IComponent`, with an EnTT registry for the data of the systems (`src/Modules/BECore/Scene/CLAUDE.md`, "Why EnTT"). Code, comments, docs and commit messages are in English; explanations to the user are in Russian when the user writes in Russian.

## Build and test

Presets of `CMakePresets.json`, each into `build/<preset>/`; the `build` skill automates it and reads `.claude/rules/build-project.md`. The presets, the editor option `BE_WITH_EDITOR` and the web-like desktop build: `cmake/CLAUDE.md`.

```bash
cmake --preset macos-debug                 # configure (also regenerates reflection code)
cmake --build --preset macos-debug         # BagiMatch3, BagiStress and the test executables
ctest --preset macos-debug                 # every test in its own process, 8 at a time
```

- C++23, exceptions disabled (`ENGINE_NO_EXCEPTIONS=ON`). Re-run `cmake --preset` after changing `BE_CLASS` / `BE_REFLECT_FIELD` or adding files (sources are globbed).
- **Output goes to the build folder of the preset**: executables in `build/<preset>/bin`, static libraries in `build/<preset>/lib`, so Debug and Release sit side by side (Visual Studio presets add `bin/<Config>/`).
- Tests: `BagiEngineTests` (`tests/`), `Match3Tests` (`games/Match3/tests/`), `StressTests` (`tools/stress/tests/`), `BagiMatch3ControlSmoke`, `BagiStressControlSmoke` and `BagiMatch3Scenario.*` (the recorded sessions of `games/Match3/tests/scenarios/`; both skipped with 77 when the game is not built). **They run 8 at a time**, so every test must be safe beside others: temp names from `tests/Common/TempPath.h`, port 0, polls with a time limit, never read `engine.log`. Rules: `.claude/rules/tests-module.md`. Runtime `ITest`s run at game start-up (`config/TestsConfig.xml`).
- Untrusted input (the readers of saves and content, the WebSocket transport, the room protocol and hub, the control API) has fuzz targets: `FuzzCorpusTest` replays and mutates their corpus in every `ctest`, libFuzzer with ASan + UBSan runs them long in the preset `macos-fuzz` (`tests/Fuzz/CLAUDE.md`).
- Python tools have pytest suites outside ctest: `.venv/bin/python -m pytest tools/control/tests`, `... tools/perf/tests`, `... tools/shots/tests`, `... tools/migrate/tests`, `... tools/codemod/tests`, `... tools/dev/tests`, `... CI/meta_generator/tests`, `... CI/scripts/tests`.
- **Licence:** the engine is Beerware (`LICENSE`); every vendored library, external folder and font has a row in `THIRD_PARTY_NOTICES.md`, held to the tree by ctest `BagiThirdPartyNotices`: a new one adds its row in the same commit.

## Architecture

**Engine and games:** the engine knows nothing about concrete games. A game lives in `games/<Name>/` or in a repository of its own that carries this one as `engine/` (`NEW_GAME.md`), reaches the engine only through `cmake/BagiEngine.cmake` (`be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests`) and registers an `IGame` (`src/Modules/Games/CLAUDE.md`). One executable is one game; `src/main.cpp` is compiled into each.

**Module order** (bottom → top; every module links `BECoreModule`, which provides the PCH):
```
BECore → Events → TaskSystem → RemoteControl → SDL / Vulkan → Games → Rooms → RoomClient → RoomServer → Runtime (src/Application, src/Widgets) → games/<Name> → executable
```

**Stack:** CMake 3.31+ / Ninja, SDL3 + SDL_image, ImGui, EnTT, EASTL, pugixml, nlohmann/json (control API only), stb_truetype, fmt, GoogleTest.

```
cmake/            engine setup, be_* functions, build options (cmake/CLAUDE.md)
src/Application/  the application loop (RuntimeModule)
src/Widgets/      editor panels and frame widgets — no game UI (.claude/rules/widgets-module.md)
src/Modules/      BECore, Events, TaskSystem, RemoteControl, SDL, Vulkan, Games, Rooms (online rooms by code, pure code), RoomClient (the client of the rooms: `RoomClient` over a native or a browser transport; built everywhere, the web build included), RoomServer (the room server over WebSocket, only with ENGINE_WITH_REMOTE_CONTROL; executable BagiRoomServer, Docker image `tools/rooms/Dockerfile`, README "Hosting the room server"), RmlUiHost (only with ENGINE_UI_RMLUI)
                  vendored, never edited, left out of broad searches: EABase, EnTT, fmt, imgui, nlohmann, pugixml, RmlUi, stb, tracy
config/, assets/  engine content; a game's files of the same name replace the engine's (FileSystem/CLAUDE.md)
games/Match3/     the engine's own game
tests/            BagiEngineTests + Common (BagiEngineTestSupport)
tools/            todo (the todo store), control (control API client, smoke test, MCP server), rooms (smoke test of BagiRoomServer, which also runs the client probe BagiRoomClientProbe; `docker_check.py`, the manual check of the Docker image), perf (benchmarks, trace summary), shots (before / after pictures of a game's scenes and their text diff, for the todo reviewer), stress (`BagiStress`, the engine's own 2D stress app: the target of the stress benchmarks and of a control smoke; `tools/stress/CLAUDE.md`), migrate (rewrites the content files of a game to the engine's format), codemod (moves `eastl::` / `std::` names to the `BECore` aliases), web, dev (`dev.py`: build and run a game's products from its `dev.json`, flags as `-dev` presets), vscode/todo-workers and vscode/bagi-dev (the panels)
```

**BECore subsystems** (each has a `CLAUDE.md`): `GameManager/CoreManager` (the service locator), `Scene/`, `UI/` (canvases, scaler, layout, validator), `Text/`, `Animation/` (tweens, timelines), `Particles/` (2D particle effects), `Control/` (the control API core; transport in `RemoteControl`), `Save/`, `Config/`, `FileSystem/`, `Resource/`, `Renderer/` (`IRenderer`, SDL or Vulkan backend by `renderer` in `ApplicationConfig.xml`), `Reflection/`, `Logger/`, `Profiler/` (the `BE_PROFILE_*` macros; `ENGINE_PROFILER`, preset `macos-profile`).

**Serialization:** split `ISerializer` / `IDeserializer` (never archive-style); `XmlSerializer` and `BinarySerializer`. Reflection code is generated by `CI/meta_generator/meta_generator.py` from `BE_CLASS` / `BE_REFLECT_FIELD` (`.claude/rules/reflection-module.md`).

## Code style

`.clang-format` is the source of truth; run the `clang-format` skill. Naming: `PascalCase` types, methods, namespaces, enum values; `camelCase` locals and parameters; `_camelCase` private members; `T` / `TName` template parameters; `"Key"_intern` for `PoolString` literals.

**DO:**
- The `BECore` type names (`String`, `StringView`, `Vector`, `SmallVector`, `HashMap`, `Optional`, `UniquePtr`, ...); `eastl::` and `std::` types directly only inside `BECore/Types/` and the reflection traits (`Types/CLAUDE.md`). `PoolString` for identifiers and keys.
- `IntrusivePtr<T>` (never raw `new` / `delete`); `Singleton<T>` for global services; `PassKey<T>` for access control.
- `CORE_ENUM(Name, Type, Values...)` instead of `enum class`; convert with `EnumUtils<E>`.
- `ASSERT()` / `EXPECT()` / `FATALERROR()`; they compile out in Release, so no side effects inside.
- `std::expected<T, E>` for recoverable errors.
- `Mutex`, `SharedMutex`, `ConditionVariable`, `Thread` of `BECore/Threading/` instead of raw `std::mutex` / `std::thread`; a lock has a `BE_LOCK_SITE("name")` (`Threading/CLAUDE.md`).
- `Format("... {}", args)` or `"... {}"_format(args)`; `LOG_*` take one string.
- `override`, `#pragma once`, namespace closing comments, inline member initialisers.
- Look at similar code first and put new code in the matching module. Widely used headers go into `BECore/pch.h`. A new module needs its own `CMakeLists.txt` and an `add_subdirectory` (`.claude/rules/cmake-rules.md`).
- **Zero allocations in a steady frame:** a frame in which nothing changed allocates nothing, on every path a frame runs (scene update and draw, UI canvases and layout, text, a game's components). Make a thing again only when its key moved (a version, the window, the strings) and keep lists as members so they keep their capacity. A phone and the browser pay for every allocation in frame time, and one per frame stays hidden until a profile shows it. Check it in `macos-profile` (`ENGINE_PROFILER_ALLOC`): `AllocTracker::GetThreadCounters()` around a frame in a test, `frame_stats` / `zone_allocs` in a running game (`Profiler/CLAUDE.md`); a game holds its scene to it with a test of its own.

**DON'T:** `std::string` / `std::string_view` where `PoolString`, `String` or `StringView` fit (a real boundary says `// std: <why>`; `ToString` / `Format` make strings, `ToStd` / `FromStd` cross); a function or a variable named like an alias (`Span`, `Map`); `enum class`; C-style casts; string concatenation with `+`; magic numbers without a named constant; globals instead of `Singleton<T>`; circular module dependencies; abstractions the task does not need.

## After making changes

- Run `clang-format` on changed files; after include changes run `strip-pch-includes`.
- A change that alters what a content file (`SceneConfig.xml`, a node file) may say (renames or removes a reflected class or field it can name, moves a field, turns a control into components) **adds a step** to `tools/migrate/steps/`, bumps `kContentVersion` and runs the tool on the engine's content; a game's content files are never edited by hand (`tools/migrate/CLAUDE.md`, `src/Modules/BECore/Config/CLAUDE.md`, "Content version").
- Plans are never committed: they live in the todo store (`python3 tools/todo/todo.py plan put NAME.md`). Status `review` is set only by `todo-loop`; `/todo-merge` merges, closes and deletes the branch.

## Docs, rules and skills

- **Docs budget:** this file loads into every session and every agent: keep it under 12k characters; a nested `CLAUDE.md` loads whenever a file of its folder is read: keep it under 20k. Put a fact in the nested file of the folder it is about. Write contracts and reasons; do not restate what a test checks, a constant's value, measurements or decision history (those belong to the entry's plan in the todo store). `python3 CI/scripts/check_doc_budget.py --root .` checks it (in a game: `engine/CI/scripts/... --root .`).
- `.claude/rules/` holds path-scoped guidance (CMake, reflection, reflection events, rendering, widgets, tests, UI: layout, controls, control API, HTML and CSS, box shapes). `build-project.md`, `todo-project.md` and `perf-project.md` (the benchmark set) are read by the skills by path.
- `.claude/skills/` (`build`, `clang-format`, `strip-pch-includes`, `tidy-fix`, `context-sanitizer` (audits and slims the docs that load by themselves), `todo-*`, `perf` and `perf-investigate` (benchmarks against a per-machine baseline in the todo store, traces: `tools/perf/CLAUDE.md`), `cpp-senior-programmer`) and `.claude/agents/` (the four `todo-loop` agents) are one text shared with games by symlink; what differs per repository is in the three rules files above.
- The todo list is the **todo store** (`tools/todo/CLAUDE.md`): a service on Railway, mirrored into Vikunja by itself; nothing of it is in git.
