# CLAUDE.md

BagiEngine: a C++23 game engine on a scene graph of `SceneNode` + `IComponent`, with an EnTT registry for system data (`src/Modules/BECore/Scene/CLAUDE.md`, "Why EnTT"). Code, comments, docs and commits are in English; explanations to the user in Russian when the user writes in Russian.

## Build and test

Presets of `CMakePresets.json` build into `build/<preset>/` (`bin`, `lib`); the `build` skill reads `.claude/rules/build-project.md`; presets and `BE_WITH_EDITOR`: `cmake/CLAUDE.md`.

```bash
cmake --preset macos-debug           # configure, regenerates reflection code
cmake --build --preset macos-debug   # BagiStress and the test executables
ctest --preset macos-debug           # each gtest executable in shards, 8 at a time, without label extended
ctest --preset macos-debug-all       # every test (build-project.md, "Tests")
```

- Exceptions are disabled. Re-run `cmake --preset` after changing `BE_CLASS` / `BE_REFLECT_FIELD` or adding files (sources are globbed).
- Tests: `BagiEngineTests` (`tests/`), `StressTests`, `BagiStressControlSmoke`, `BagiStressScenario.*` (77 without `BagiStress`). **They run 8 at a time**: temp names from `tests/Common/TempPath.h`, port 0, polls with a time limit, never read `engine.log` (`.claude/rules/tests-module.md`). Runtime `ITest`s run at game start-up (`config/TestsConfig.xml`).
- Untrusted input (save and content readers, WebSocket, rooms, control API) has fuzz targets: `FuzzCorpusTest` in every `ctest`, libFuzzer in `macos-fuzz` (`tests/Fuzz/CLAUDE.md`). Python tools: `tools/CLAUDE.md`.
- **Licence:** Beerware; every vendored library or font has a row in `THIRD_PARTY_NOTICES.md` (ctest `BagiThirdPartyNotices`), added in the same commit.

## Architecture

**Engine and games:** the engine knows no concrete game. A game is its own repository carrying this one as `engine/` (`NEW_GAME.md`), uses only `cmake/BagiEngine.cmake` (`be_add_*`) and registers an `IGame` (`src/Modules/Games/CLAUDE.md`). One executable is one game; `src/main.cpp` is compiled into each.

**Module order** (bottom → top; every module links `BECoreModule`, which provides the PCH):
```
BECore → Events → TaskSystem → RemoteControl → SDL / Vulkan → Games → Rooms → RoomClient → RoomServer → Runtime (src/Application, src/Widgets) → game → executable
```

**Stack:** CMake 3.31+ / Ninja, SDL3, ImGui, EnTT, EASTL, pugixml, stb, dr_flac, fmt, GoogleTest.

```
src/Application/  the application loop (RuntimeModule)
src/Widgets/      editor panels and frame widgets, no game UI (.claude/rules/widgets-module.md)
src/Modules/      engine modules; RoomServer only with ENGINE_WITH_REMOTE_CONTROL, RmlUiHost only with ENGINE_UI_RMLUI
                  vendored, never edited, left out of searches: EABase, EnTT, fmt, imgui, pugixml, RmlUi, stb, dr_libs, tracy
config/, assets/  engine content; a game's file of the same name wins (FileSystem/CLAUDE.md)
tools/            Python and helper tools (tools/CLAUDE.md)
```

**BECore subsystems** (each has a `CLAUDE.md`): `GameManager/CoreManager` (service locator), `Scene/`, `UI/`, `Text/`, `Animation/`, `Particles/`, `Control/`, `Save/`, `Config/`, `Cook/` (XML content to `.bin`), `File/`, `FileSystem/`, `Resource/`, `Renderer/`, `Audio/`, `Reflection/`, `Logger/`, `Profiler/`, `Memory/` (arenas, pools).

**Serialization:** split `ISerializer` / `IDeserializer`, never archive-style. Reflection code is generated from `BE_CLASS` / `BE_REFLECT_FIELD` (`.claude/rules/reflection-module.md`).

## Code style

`.clang-format` rules (`clang-format` skill). `PascalCase` types, methods, namespaces, enum values; `camelCase` locals; `_camelCase` private members; `T` / `TName` template parameters; `"Key"_intern` for `PoolString` literals.

**DO:**
- `BECore` type names (`String`, `StringView`, `Vector`, `HashMap`, `Optional`, `UniquePtr`, ...); `eastl::` / `std::` only in `BECore/Types/` and reflection traits (`Types/CLAUDE.md`). `PoolString` for identifiers and keys: a code constant is `"text"_intern` (named in a structure per file, `PoolString/CLAUDE.md`), `Intern(text)` only for names from data; `Intern("literal")` does not compile; ctest `BagiPoolStrings` holds the rest.
- `IntrusivePtr<T>` (no raw `new` / `delete`), `Singleton<T>` for global services, `PassKey<T>` for access control.
- `CORE_ENUM` instead of `enum class`, `EnumUtils<E>` to convert.
- `ASSERT()` / `EXPECT()` / `FATALERROR()` compile out in Release: no side effects inside. `std::expected<T, E>` for recoverable errors.
- `BECore/Threading/` primitives, each lock with `BE_LOCK_SITE("name")` (`Threading/CLAUDE.md`).
- Callbacks: `MoveOnlyFunction` stored, `FunctionRef` as a parameter, `FixedFunction<Bytes>` where it must never allocate (`Utils/CLAUDE.md`); no `std::function`.
- `Format("... {}", args)` / `"... {}"_format(args)` (`Format/CLAUDE.md`); log with `LOG_X("... {}", args)`, never `LOG_X(Format(...))`.
- `override`, `#pragma once`, namespace closing comments, inline member initialisers.
- **Comments** say *why* or state a contract, invariant or trap; never restate code, tell history, repeat a constant's value or retell a test; cite a `CLAUDE.md` section only if that heading exists. `CI/scripts/check_comment_only.py` proves a comment-only diff.
- New code goes in the matching module, after looking at similar code; widely used headers go into `BECore/pch.h`; a new module: `.claude/rules/cmake-rules.md`.
- `#include "..."` only for a source including its own header; every other include is `<...>` from an include root. A header compiles alone. Ctests `BagiIncludeStyle`, `BagiHeadersStandalone`.
- **Zero allocations in a steady frame:** rebuild only when a key moved, keep lists as members. Check in `macos-profile` (`Profiler/CLAUDE.md`).

**DON'T:** `std::string` / `std::string_view` / `fmt::` where the engine types fit (a real boundary says `//-NoStd: <why>`; ctest `BagiTypesCheck`); names like an alias (`Span`, `Map`); `enum class`; C-style casts; `+` string concatenation; unnamed magic numbers; globals; circular module dependencies; abstractions the task does not need.

## After making changes

- `clang-format` changed files; after include changes `strip-pch-includes` (`.cpp` only).
- A change of what a content file may say adds a step to `tools/migrate/steps/` and bumps `kContentVersion`; a game's content is never edited by hand (`tools/migrate/CLAUDE.md`).
- A change of the public C++ API keeps the old name one merge as `[[deprecated("use X")]]` when cheap, and the entry says how a game follows; `todo-merge` writes the changelog fragment, a developer never does (`tools/changelog/CLAUDE.md`).
- Plans live in the todo store, never in git (`tools/todo/todo.py plan put NAME.md`). Status `review` is set only by `todo-loop`; `/todo-merge` merges, closes and deletes the branch.

## Docs, rules and skills

- **Docs budget:** this file loads into every session: under 12k characters; a nested `CLAUDE.md` under 20k. Put a fact in the nested file of its folder; write contracts and reasons, not what a test checks, constant values, measurements or history. `python3 CI/scripts/check_doc_budget.py --root .`
- `.claude/rules/`: path-scoped guidance; `build-project.md`, `todo-project.md`, `perf-project.md`, `qa-project.md` are read by the skills.
- `.claude/skills/` and `.claude/agents/` are shared with games by symlink; the per-repository part is in those four rules files. `sync-engine` is the owner's command only and never pushes.
