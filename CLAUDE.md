# CLAUDE.md

BagiEngine: a C++23 game engine on a scene graph of `SceneNode` + `IComponent`, with an EnTT registry for the data of the systems (`src/Modules/BECore/Scene/CLAUDE.md`, "Why EnTT"). Code, comments, docs and commit messages are in English; explanations to the user are in Russian when the user writes in Russian.

## Build and test

Presets of `CMakePresets.json`, each into `build/<preset>/`; the `build` skill reads `.claude/rules/build-project.md`. The presets, the editor option `BE_WITH_EDITOR` and the web-like desktop build: `cmake/CLAUDE.md`.

```bash
cmake --preset macos-debug                 # configure (also regenerates reflection code)
cmake --build --preset macos-debug         # BagiStress and the test executables
ctest --preset macos-debug                 # every test in its own process, 8 at a time
```

- C++23, exceptions disabled (`ENGINE_NO_EXCEPTIONS=ON`). Re-run `cmake --preset` after changing `BE_CLASS` / `BE_REFLECT_FIELD` or adding files (sources are globbed).
- **Output goes to the build folder of the preset**: executables in `build/<preset>/bin`, static libraries in `build/<preset>/lib`, so Debug and Release sit side by side.
- Tests: `BagiEngineTests` (`tests/`), `StressTests` (`tools/stress/tests/`), `BagiStressControlSmoke` and `BagiStressScenario.*` (recorded sessions, `tools/stress/tests/scenarios/`; skipped with 77 without `BagiStress`). A game's own tests live in its repository. **They run 8 at a time**, so every test must be safe beside others: temp names from `tests/Common/TempPath.h`, port 0, polls with a time limit, never read `engine.log`. Rules: `.claude/rules/tests-module.md`. Runtime `ITest`s run at game start-up (`config/TestsConfig.xml`).
- Untrusted input (readers of saves and content, WebSocket transport, room protocol and hub, control API) has fuzz targets: `FuzzCorpusTest` replays and mutates their corpus in every `ctest`; libFuzzer with ASan + UBSan runs long in `macos-fuzz` (`tests/Fuzz/CLAUDE.md`).
- Python tools have pytest suites outside ctest: `.venv/bin/python -m pytest tools/control/tests`, `... tools/perf/tests`, `... tools/shots/tests`, `... tools/qa/tests`, `... tools/sync/tests`, `... tools/migrate/tests`, `... tools/changelog/tests`, `... tools/codemod/tests`, `... tools/dev/tests`, `... CI/meta_generator/tests`, `... CI/scripts/tests`.
- **Licence:** the engine is Beerware (`LICENSE`); every vendored library, external folder and font has a row in `THIRD_PARTY_NOTICES.md`, held to the tree by ctest `BagiThirdPartyNotices`: a new one adds its row in the same commit.

## Architecture

**Engine and games:** the engine knows nothing about concrete games. A game lives in a repository of its own that carries this one as `engine/` (`NEW_GAME.md`), reaches the engine only through `cmake/BagiEngine.cmake` (`be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests`) and registers an `IGame` (`src/Modules/Games/CLAUDE.md`). One executable is one game; `src/main.cpp` is compiled into each.

**Module order** (bottom → top; every module links `BECoreModule`, which provides the PCH):
```
BECore → Events → TaskSystem → RemoteControl → SDL / Vulkan → Games → Rooms → RoomClient → RoomServer → Runtime (src/Application, src/Widgets) → game (own repository) → executable
```

**Stack:** CMake 3.31+ / Ninja, SDL3 + SDL_image, ImGui, EnTT, EASTL, pugixml, nlohmann/json (control API only), stb_truetype, fmt, GoogleTest.

```
cmake/            engine setup, be_* functions, build options (cmake/CLAUDE.md)
src/Application/  the application loop (RuntimeModule)
src/Widgets/      editor panels and frame widgets — no game UI (.claude/rules/widgets-module.md)
src/Modules/      BECore, Events, TaskSystem, RemoteControl, SDL, Vulkan, Games, Rooms (online rooms by code, pure code), RoomClient (over a native or a browser transport; built everywhere), RoomServer (WebSocket room server, only with ENGINE_WITH_REMOTE_CONTROL; BagiRoomServer, `tools/rooms/Dockerfile`), RmlUiHost (only with ENGINE_UI_RMLUI)
                  vendored, never edited, left out of broad searches: EABase, EnTT, fmt, imgui, nlohmann, pugixml, RmlUi, stb, tracy
config/, assets/  engine content; a game's files of the same name replace the engine's (FileSystem/CLAUDE.md)
tests/            BagiEngineTests + Common (BagiEngineTestSupport)
tools/            todo (the todo store), control (control API client, smoke test, MCP server), rooms (BagiRoomServer smoke test, BagiRoomClientProbe, `docker_check.py`), perf (benchmarks, trace summary), sync (helper of `sync-engine`), changelog (version, fragments), shots (before / after pictures of scenes), qa (`/qa`), stress (`BagiStress`, the 2D stress app: benchmarks and a control smoke), migrate (rewrites a game's content files to the engine's format), codemod (moves `eastl::` / `std::` names to the `BECore` aliases), web, dev (`dev.py`: a game's products from its `dev.json`), ccache (shared hits), vscode/todo-workers and vscode/bagi-dev (the panels)
```

**BECore subsystems** (each has a `CLAUDE.md`): `GameManager/CoreManager` (the service locator), `Scene/`, `UI/` (canvases, scaler, layout, validator), `Text/`, `Animation/` (tweens, timelines), `Particles/` (2D particle effects), `Control/` (the control API core; transport in `RemoteControl`), `Save/`, `Config/`, `FileSystem/`, `Resource/`, `Renderer/` (`IRenderer`, SDL or Vulkan backend by `renderer` in `ApplicationConfig.xml`), `Audio/`, `Reflection/`, `Logger/`, `Profiler/` (the `BE_PROFILE_*` macros; `ENGINE_PROFILER`, preset `macos-profile`).

**Serialization:** split `ISerializer` / `IDeserializer` (never archive-style); `XmlSerializer` and `BinarySerializer`. Reflection code is generated by `CI/meta_generator/meta_generator.py` from `BE_CLASS` / `BE_REFLECT_FIELD` (`.claude/rules/reflection-module.md`).

## Code style

`.clang-format` is the source of truth (`clang-format` skill). Naming: `PascalCase` types, methods, namespaces, enum values; `camelCase` locals and parameters; `_camelCase` private members; `T` / `TName` template parameters; `"Key"_intern` for `PoolString` literals.

**DO:**
- The `BECore` type names (`String`, `StringView`, `Vector`, `SmallVector`, `HashMap`, `HashSet`, `Pair`, `Variant`, `Optional`, `UniquePtr`, ...); `eastl::` and `std::` types directly only inside `BECore/Types/` and the reflection traits (`Types/CLAUDE.md`). `PoolString` for identifiers and keys.
- `IntrusivePtr<T>` (never raw `new` / `delete`); `Singleton<T>` for global services; `PassKey<T>` for access control.
- `CORE_ENUM(Name, Type, Values...)` instead of `enum class`; convert with `EnumUtils<E>`.
- `ASSERT()` / `EXPECT()` / `FATALERROR()`; they compile out in Release, so no side effects inside.
- `std::expected<T, E>` for recoverable errors.
- `Mutex`, `SharedMutex`, `ConditionVariable`, `Thread` of `BECore/Threading/` instead of raw `std::mutex` / `std::thread`; a lock has a `BE_LOCK_SITE("name")` (`Threading/CLAUDE.md`).
- Callbacks: `MoveOnlyFunction` for a stored one, `FunctionRef` for a parameter called only during the call, `FixedFunction<Bytes>` where it must never allocate (`Utils/CLAUDE.md`); no `std::function`, no `SharedPtr` made only to copy a capture.
- Format through `BECore::Format` / `FormatArg` / `Formatter<T>` (`Format/CLAUDE.md`): `Format("... {}", args)` or `"... {}"_format(args)`.
- `override`, `#pragma once`, namespace closing comments, inline member initialisers.
- **Comments** say *why*, or state a contract (units, ownership, thread, lifetime), an invariant or a trap. Never restate the code, tell history ("was", "measured", "no longer"), repeat a named constant's value or retell a test; cite a `CLAUDE.md` section only if that heading exists. Doxygen of the public API stays at one or two sentences. `//-NoStd:`, `NOLINT`, `clang-format off/on` stay; `CI/scripts/check_comment_only.py` proves a comment-only diff.
- Look at similar code first; new code goes in the matching module. Widely used headers go into `BECore/pch.h`. A new module needs its own `CMakeLists.txt` and an `add_subdirectory` (`.claude/rules/cmake-rules.md`).
- `#include "..."` only for a `.cpp` / `.mm` / `.c` including its own header (`Foo.cpp` -> `"Foo.h"`); every other include is `<...>` from an include root (`<BECore/...>`, `<Games/...>`, a test helper as `<BECore/Rooms/TestClient.h>`); `CI/scripts/check_includes.py --root .` (ctest `BagiIncludeStyle`) holds it. A header includes what it uses and compiles alone (the PCH is for `.cpp`): `check_headers.py`, ctest `BagiHeadersStandalone`.
- **Zero allocations in a steady frame:** a frame in which nothing changed allocates nothing, on every path a frame runs. Make a thing again only when its key moved (a version, the window, the strings) and keep lists as members so they keep their capacity. Check it in `macos-profile` (`ENGINE_PROFILER_ALLOC`; `Profiler/CLAUDE.md`): `AllocTracker::GetThreadCounters()` in a test, `frame_stats` / `zone_allocs` in a game.

**DON'T:** `std::string` / `std::string_view` where `PoolString`, `String` or `StringView` fit (a real boundary says `//-NoStd: <why>`; `ToString` / `Format` make strings, `ToStd` / `FromStd` cross; `CI/scripts/check_types.py`, ctest `BagiTypesCheck`, holds the type rules); a function or a variable named like an alias (`Span`, `Map`); `enum class`; C-style casts; string concatenation with `+`; magic numbers without a named constant; globals instead of `Singleton<T>`; `fmt::` outside `BECore/Format/` and the type layer (a real boundary says `//-NoStd: <why>`); circular module dependencies; abstractions the task does not need.

## After making changes

- Run `clang-format` on changed files; after include changes run `strip-pch-includes` (`.cpp` files only).
- A change that alters what a content file may say (renames or removes a reflected class or field it can name, moves a field, turns a control into components) **adds a step** to `tools/migrate/steps/`, bumps `kContentVersion` and runs the tool on the engine's content; a game's content files are never edited by hand (`tools/migrate/CLAUDE.md`, `src/Modules/BECore/Config/CLAUDE.md`, "Content version").
- A change of the public C++ API a game calls (a rename, a signature, changed behaviour) keeps the old name one merge as `[[deprecated("use X")]]` when that is cheap; the entry says in words what a game does to follow, and `todo-merge` lifts it into the `Breaking` / `Adapt:` of the changelog fragment (`tools/changelog/CLAUDE.md`). A developer never writes the fragment.
- Plans are never committed: they live in the todo store (`tools/todo/todo.py plan put NAME.md`). Status `review` is set only by `todo-loop`; `/todo-merge` merges, closes and deletes the branch.

## Docs, rules and skills

- **Docs budget:** this file loads into every session and every agent: keep it under 12k characters; a nested `CLAUDE.md` loads whenever a file of its folder is read: keep it under 20k. Put a fact in the nested file of the folder it is about. Write contracts and reasons; do not restate what a test checks, a constant's value, measurements or decision history (those belong to the entry's plan in the todo store). `python3 CI/scripts/check_doc_budget.py --root .` checks it.
- `.claude/rules/` holds path-scoped guidance (CMake, reflection, rendering, widgets, tests, UI, control API, ...); `build-project.md`, `todo-project.md`, `perf-project.md` and `qa-project.md` are read by the skills by path.
- `.claude/skills/` (`build`, `clang-format`, `strip-pch-includes`, `tidy-fix`, `context-sanitizer`, `todo-*`, `sync-engine` (the owner's command only; never pushes), `perf` and `perf-investigate` (`tools/perf/CLAUDE.md`), `qa`, `cpp-senior-programmer`) and `.claude/agents/` (the four `todo-loop` agents) are one text shared with games by symlink; the per-repository part is in the four rules files above.
- The todo list is the **todo store** (the BagiTodo service); `tools/todo/` is only its CLI loader (`tools/todo/CLAUDE.md`). Nothing of it is in git.
