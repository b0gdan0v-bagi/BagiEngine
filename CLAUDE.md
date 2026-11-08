# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Use the presets from `CMakePresets.json` (same ones VS Code CMake Tools shows). Each preset builds into `build/<preset>/`; the `build` skill automates this, and the facts of this repository that it reads (targets, configure triggers, failure hints) are in `.claude/rules/build-project.md`.

```bash
cmake --list-presets=build                 # presets available on this OS
cmake --preset macos-debug                 # configure (also regenerates reflection code)
cmake --build --preset macos-debug         # build everything: the game executable (BagiMatch3) and the test executables
```

| OS | Presets |
|----|---------|
| macOS | `macos-debug`, `macos-release` (Clang + Ninja); `macos-release-editor`: Release with the editor (below); `macos-rmlui-debug`, `macos-rmlui-release`: the same with `ENGINE_UI_RMLUI=ON` (the RmlUi host and its tests; no system library is needed, the engine's own fonts draw the text) |
| Windows | `windows-msvc-{debug,release}`, `windows-msvc-release-editor`, `windows-clang-{debug,release}` (Ninja), `windows-vs2022-{debug,release}`, `windows-vs2022-clangcl-{debug,release}` (Visual Studio 2022) |

**By default the editor is in Debug builds and not in a single-config Release; Debug always has it.** The option `BE_WITH_EDITOR` (`cmake/BagiEngine.cmake`, section "Editor Option") decides whether the editor panels (`src/Widgets/{ConfigEditor,EditorLayout,SceneEditor,TextureEditor,Viewport}`) and their tests are built:

| `BE_WITH_EDITOR` | Result |
|------------------|--------|
| `AUTO` (default) | ON, except in a single-config build of type `Release`, `RelWithDebInfo` or `MinSizeRel`, where it is OFF. So `macos-debug` has the editor and `macos-release` does not. A multi-config generator (Visual Studio, Ninja Multi-Config) picks the configuration at build time but the sources are picked at configure time, so `AUTO` is ON there; pass `-DBE_WITH_EDITOR=OFF` for a build without the editor |
| `ON` | the editor is built, also in Release: `cmake --preset macos-release -DBE_WITH_EDITOR=ON`, or the presets `macos-release-editor` and `windows-msvc-release-editor` |
| `OFF` | no editor. In a Debug build this is not honoured: configure prints a `WARNING` and builds the editor anyway (Debug builds always include the editor). Any other value is an error |

Configure prints the result (`Editor: ENABLED (BE_WITH_EDITOR=AUTO)` or `DISABLED`). The option adds the compile definition `BE_WITH_EDITOR=1|0` to every engine and game target (`kEditorBuilt` in `BECore/Application/RunMode.h` is its `constexpr` form); code that needs an editor header is guarded with `#if BE_WITH_EDITOR`. ImGui and `ImGuiWidget` are in every build (a game may use `ImGuiWidget` for its HUD). Changing the option is a configure and rebuilds the runtime and the tests.

A game that asks for `mode="Editor"` in `ApplicationConfig.xml` in a build without the editor starts in **Game** mode with one log warning (not an error, so `--headless` and the engine's own `mode="Editor"` default still start). In a build with the editor, **Ctrl+Shift+E** switches Game and Editor while the program runs (applied at the end of a frame; staged editor changes are lost; not with `--headless`): `src/Modules/BECore/CLAUDE.md`, "Run mode".

Per-machine settings go in `CMakeUserPresets.json` (git-ignored). Without presets (legacy flat `build/`):

```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

C++23 is required. Exceptions are disabled (`ENGINE_NO_EXCEPTIONS=ON`). Output lands in `build/{CompilerName}{PlatformName}/` regardless of preset, so Debug, Release and release-editor Ninja presets overwrite each other's executables and libraries: after switching between them delete every `build/ClangmacOS/lib/*.a` (the folder name is the compiler and platform), or the old objects of the other configuration are linked in and the tests crash. Reflection code is generated at configure time: re-run `cmake --preset ...` after changing `BE_CLASS` / `BE_REFLECT_FIELD`.

## Running Tests

Unit and module tests are GoogleTest tests, built as separate executables (not part of the games): `BagiEngineTests` for the engine (`tests/`), `Match3Tests` next to its game (`games/<Name>/tests/`). The GoogleTest submodule needs `git submodule update --init external/googletest`; `-DENGINE_BUILD_TESTS=OFF` skips the whole thing.

```bash
cmake --build --preset macos-debug --target BagiEngineTests Match3Tests   # only the test executables
ctest --preset macos-debug                                       # every test in its own process, 8 at a time
ctest --preset macos-debug -j1                                   # one at a time (the old run)
build/ClangmacOS/BagiEngineTests --gtest_filter='Reflection*'    # run directly; cwd must be inside the repository
```

**The tests run in parallel.** `test-base` in `CMakePresets.json` sets `execution.jobs` to 8, so `ctest --preset <p>` runs eight tests at once, in every game repository too (they include these presets). `-j1` on the command line switches it off, `-jN` changes the number; the environment variable `CTEST_PARALLEL_LEVEL` does **not** override a preset. Because of that every gtest case must be safe in a process beside others: temp folders and files get their names from `tests/Common/TempPath.h` (`TestTempDirectory(prefix)`, `TestTempPath(stem)`: the test name and a per-process tag, so even two `ctest` runs at once, such as the parallel todo-loop workers, do not meet), servers bind port 0, a wait is a poll with a time limit and not a fixed sleep, and nothing reads the `engine.log` of the working directory (every process that brings up the core services truncates it). The only entry with a lock is the control smoke test (`RESOURCE_LOCK bagi_control_smoke`: it runs beside the unit tests, not beside another smoke test); the rules for a new test are in `.claude/rules/tests-module.md`, the timings in `.claude/rules/build-project.md`, "Tests".

Runtime tests that need the live engine are `ITest` classes (`src/Modules/BECore/Tests/`). `CoreManager::OnApplicationPreInit()` runs the ones enabled in `config/TestsConfig.xml` at game startup.

`ctest` also runs `BagiMatch3ControlSmoke`: a Python script that starts the game `--headless` and plays it over the control API. It is skipped (exit code 77) when the executable is not built, and a `--target` list without the games (as above) builds none, so add `BagiMatch3` to run it. It picks a free port itself and retries once on another when the game reports that the port was taken in between (`tools/control/CLAUDE.md`). The Python tools have pytest tests of their own, not part of ctest: `.venv/bin/python -m pytest tools/control/tests` (`tools/control/CLAUDE.md`), and so has the reflection generator: `.venv/bin/python -m pytest CI/meta_generator/tests` (`.claude/rules/reflection-module.md`, "Codegen"; `pytest` comes from `requirements.txt`, which `CI/setup_venv.*` installs).

## Architecture Overview

BagiEngine is a C++23 game engine built on a scene graph of `SceneNode` + `IComponent`; an EnTT registry holds the data of the systems (today the draw pass), see `src/Modules/BECore/Scene/CLAUDE.md`, "Why EnTT". The main entry is `src/main.cpp` → `Application::Initialize()/Run()`; each game executable compiles it.

**Engine and games:** the engine knows nothing about concrete games. A game lives in `games/<Name>/` (module, executable, `config/`, `assets/`, `tests/`), reaches the engine only through `cmake/BagiEngine.cmake` (`be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests`) and registers an `IGame` in `AbstractFactory<IGame>`. One executable is one game. `games/` is built to move out into separate repositories that carry this repository as a submodule (`engine/`); no engine code, CMake or config outside `games/` refers to a concrete game. See `src/Modules/Games/CLAUDE.md`; a game in a repository of its own (git identity, submodule, shared skills, todo area) is set up by `NEW_GAME.md`.

**Stack:** CMake 3.31+ with Ninja, SDL3 + SDL_image, ImGui, EnTT (entity registry), EASTL, pugixml (XML), nlohmann/json (control API only), stb_truetype (text), fmt, magic_enum (reflection), GoogleTest (tests).

**Module order (bottom → top; every module links `BECoreModule`, which provides the PCH):**
```
BECore → Events → TaskSystem → RemoteControl → SDL / Vulkan → Games → Runtime (src/Application, src/Widgets) → games/<Name> → game executable (src/main.cpp)
```

**Layout:**
```
cmake/BagiEngine.cmake  # engine setup + the be_* functions games build on
src/
├── main.cpp            # Entry point, compiled into every game executable
├── Application/        # Application loop        (RuntimeModule)
├── Widgets/            # Editor panels and frame widgets (RuntimeModule)
└── Modules/
    ├── BECore/         # Core engine (math types live in BECore/Math/)
    ├── Events/         # Event system
    ├── TaskSystem/     # Async tasks (coroutines)
    ├── RemoteControl/  # WebSocket server of the control API (localhost, off by default)
    ├── SDL/            # SDL3 integration
    ├── Vulkan/         # Vulkan renderer backend
    ├── Games/          # IGame / GameManager: the engine-game contract
    ├── RmlUiHost/      # host of the vendored RmlUi (only with -DENGINE_UI_RMLUI=ON)
    └── EABase/, EnTT/, fmt/, imgui/, nlohmann/, pugixml/, RmlUi/, stb/   # vendored
config/, assets/        # engine content (Application, Logger, Assert, SDLWindow, Widgets, GameWidgets, Tests, Text, UI, default SceneConfig; assets/fonts/ has the default font)
games/<Name>/           # a game: CMakeLists.txt, src/, config/, assets/, tests/, CLAUDE.md (Match3; Chess is the repository BagiEngine-Chess, which carries this one as its submodule `engine/`)
tests/                  # GoogleTest executable BagiEngineTests (BECore) + Common (BagiEngineTestSupport)
tools/                  # developer tools: natvis, todo (the todo store: a service on Railway and its CLI `todo.py`; `tools/todo/CLAUDE.md`), control (Python: the control API client and smoke test, the MCP server; `tools/control/CLAUDE.md`), vscode/todo-workers (a VS Code panel of the parallel todo-loop workers; its `README.md`)
```

**Content is layered.** `FileSystem` and `ConfigManager` merge the engine's `config/` and `assets/` with the game's; the game is on top, so a game file of the same name replaces the engine's (a game's `SceneConfig.xml` replaces the engine's whole file). Roots come from `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` (defined by `be_add_game_executable`); without them the project root is searched upwards from the working directory. Details: `src/Modules/BECore/FileSystem/CLAUDE.md`.

**Vendored code** (`EABase/`, `EnTT/`, `fmt/`, `imgui/`, `nlohmann/`, `pugixml/`, `RmlUi/`, `stb/`): do not edit it, and leave it out of broad searches. `RmlUi/` is built only with `-DENGINE_UI_RMLUI=ON`, together with its host module `RmlUiHost` (`src/Modules/RmlUiHost/CLAUDE.md`; the decision is in the entry `ui-authoring` of the todo store).

**Key subsystems in `src/Modules/BECore/`:**

| Subsystem | Purpose |
|-----------|---------|
| `GameManager/CoreManager` | Central service locator — owns FileSystem, ConfigManager, Logger, SceneManager, ResourceManager, etc. |
| `Control/` | External control API core: a thread-safe command queue run on the main thread at frame stage 1b (`ControlService`, JSON through nlohmann with exceptions off) and the start-up flags (`ControlSettings`). The WebSocket transport is the `RemoteControl` module; start a game with `--control` to use it (`--manual-clock`, `--headless`, `step` and `screenshot`: `Control/CLAUDE.md`) |
| `Animation/` | Easing curves, `Lerp` (float, `Vec2`, `Color`), `TweenManager` (`CoreManager::GetTweens()`, ticked once per frame: change a value over time through a setter callback), `Timeline` (clips in sequence and parallel, exact time between them, `TweenManager::Play`) and `co_await` of a tween on game time (`AwaitTween`, `Wait`) (`Animation/CLAUDE.md`) |
| `Scene/` | Scene graph (SceneNode → IComponent), serialization |
| `Reflection/` | IPropertyVisitor, ISerializer/IDeserializer, BE_CLASS codegen markers |
| `Resource/` | TextureLibrary, resource pooling |
| `Text/` | Fonts (`stb_truetype`), pure text layout, glyph atlases in page textures, `TextComponent` (`Text/CLAUDE.md`) |
| `UI/` | In-game UI: canvases (`UICanvasComponent`), the scaler, adaptive layout, responsive variants, the layout validator (`UI/CLAUDE.md`) |
| `Renderer/` | `IRenderer` / `ITexture`; backend (`SDLRendererBackend` or `VulkanRendererBackend`) is picked by `renderer` in `config/ApplicationConfig.xml` |

`Events` (CRTP pub/sub: `EventBase<T>`, `SubscriptionHolder` for RAII) and `TaskSystem` (async coroutines: `TaskManager`, `Task<T>`) are sibling modules, not part of BECore.

Widgets live in `src/Widgets/`: the engine's editor panels and the frame widgets that run the render frame (`ClearScreenWidget`, `SceneRenderWidget`, `ImGuiWidget`). None of them is game UI. `SceneEditorWidget` handles orchestration/UI flow; `ComponentInspectors` handles per-field editing via `IPropertyVisitor`. Scene editor behavior is driven by `SceneConfig.xml` (the active game's, see layering); keep scene/component configuration consistent with it when adding or changing editor behavior.

Reflection code is generated by `CI/meta_generator/meta_generator.py` (Python, regex-based). `BE_CLASS` and `BE_REFLECT_FIELD` macros are the generation triggers. A marker the parser cannot use is reported at configure time as `path:line: warning: reflection: ...` (`.claude/rules/reflection-module.md`).

## Code Style

Code, comments, docs and commit messages are in English. Explanations to the user are in Russian when the user writes in Russian.

**Naming:**

| Element | Style | Example |
|---------|-------|---------|
| Class, method, namespace, enum value | `PascalCase` | `FileSystem`, `Initialize()`, `EventType::KeyDown` |
| Local, parameter | `camelCase` | `virtualPath` |
| Private member | `_camelCase` | `_isRunning` |
| Template parameter | `T`, `TName` | `template<typename T>` |
| `PoolString` literal | `_intern` suffix | `"Key"_intern` |

**Formatting:** `.clang-format` is the source of truth (4 spaces, `Type* ptr` / `Type& ref`, sorted include groups). Run the `clang-format` skill instead of formatting by hand.

**DO:**
- Use EASTL containers over `std::` (esp. `eastl::vector`, `eastl::hash_map`, `eastl::string`)
- Use `PoolString` for identifier/key strings; use `"Key"_intern` for string literals
- Use `IntrusivePtr<T>` (never raw `new`/`delete`)
- Use `CORE_ENUM(Name, Type, Values...)` instead of `enum class`; convert with `EnumUtils<E>` (`ToString`, `FromString`, `Cast` → `std::optional`, `ToPoolString`)
- Use `ASSERT()` (not `std::assert`), `EXPECT()` for soft checks, `FATALERROR()` for critical failures. They compile out in Release presets, so never put side effects inside them
- Use `std::expected<T, E>` for recoverable errors (no exceptions)
- Build strings with `Format("... {}", args...)` or `"... {}"_format(args...)`. `LOG_*` take a single string, so write `LOG_INFO("x {}"_format(v))`
- Add `override` for virtual methods, `#pragma once`, namespace closing comments
- Initialize members inline: `bool _flag = false;`
- Look at similar code first and put new code in the matching module; add widely used headers to `BECore/pch.h`. Modules glob their sources, so a new file only needs `cmake --preset ...` re-run; a new module needs its own `CMakeLists.txt` and an `add_subdirectory` in the root (`.claude/rules/cmake-rules.md`)

**DON'T:**
- Use `std::string` where `PoolString` fits
- Use `enum class` — use `CORE_ENUM`
- Use C-style casts
- Concatenate with `+` (`eastl::string("a") + b`, `str + "x"`) — use `Format()`
- Use magic numbers without a named constant, or globals instead of `Singleton<T>`
- Create circular dependencies between modules
- Suggest abstractions the task does not need

**Key patterns:**

| Pattern | Usage |
|---------|-------|
| `Singleton<T>` | Global services (`BECore/Utils/Singleton.h`) |
| `PassKey<T>` | Access control |
| `RefCounted` + `IntrusivePtr` | Reference counting |
| `EventBase<T>` | Events (CRTP, `Events/EventBase.h`) |
| `IWidget` | UI/rendering |
| `CoreManager::GetConfigManager().GetConfig("Name"_intern)` | Config access, returns `XmlNode` |
| `ISerializer` / `IDeserializer` | Serialization (`XmlSerializer`, `BinarySerializer`) |
| `LOG_INFO` / `LOG_ERROR` | Logging (`BECore/Logger/Logger.h`) |

## Serialization / Reflection

Use split interfaces: `ISerializer` (write) and `IDeserializer` (read). Do not introduce archive-style APIs. Implementations: `XmlSerializer`/`XmlDeserializer` (config, readable saves) and `BinarySerializer`/`BinaryDeserializer` (compact, name-keyed; format in `.claude/rules/reflection-module.md`). Codegen via `CI/meta_generator/` is the source of truth.

## After Making Changes

- Apply `clang-format` to changed files before pushing.
- If includes were added or modified, run `strip-pch-includes` to avoid duplicating PCH-provided headers.
- The status `review` of a todo entry is set only by the `todo-loop` skill, in the todo store, for the branch of the task; nothing about it is committed and the base branch (`main`, or `games/chess` for the engine inside a game) is not touched. The run ends with that branch checked out, so the change can be tried live; after the merge `/todo-loop close` closes the entry in the store (`/todo-merge` does the merge, the close and the branch deletion in one go, after a report and the owner's yes; in a game it also commits the moved pointer of `engine/`). Which store area, repository and base branch each todo list uses: `.claude/rules/todo-project.md`.
- For complex tasks, keep a plan and update it as work progresses. Plan mode writes it to `~/.claude/plans/`; **plans are never committed**: they live in the todo store (`python3 tools/todo/todo.py plan put NAME.md --kind owner`, then `todo.py edit <id> --plan NAME.md`), and Vikunja shows the checklist of the plan by itself. Remove the link (`--plan none`) and the plan when the work is done. Include testing and documentation tasks. For plans with substantial C++ changes, end with a formatting step (skill `clang-format`) and an includes step (skill `strip-pch-includes`; use a subfolder and dry-run first if doing it in parts).

## Rules and Skills

- Module guidance sits next to the code in nested `CLAUDE.md` files: `src/Modules/BECore/` (plus `Animation/`, `Config/`, `Control/`, `FileSystem/`, `Logger/`, `Resource/`, `Scene/`, `Text/`, `UI/`), `Events/`, `TaskSystem/`, `RemoteControl/`, `SDL/`, `Vulkan/`, `Games/`, `RmlUiHost/`, and `games/Match3/` and `tools/control/`. Claude Code loads one when a file under that directory is read.
- `.claude/rules/` holds guidance that spans several directories or matches by file name (`paths:` frontmatter): CMake, Reflection, Reflection events (`*Event.h`), Rendering, Widgets, Tests. Read the relevant file before changing that area.
  Two files have no `paths:` and are read by skills instead: `todo-project.md` (the todo areas, build and test targets of the loop, zones nobody edits; read by the `todo-*` skills and agents) and `build-project.md` (the product and test targets, configure triggers, format scope, ignore file, strip-pch example and the launcher; read by `build`, `clang-format` and `strip-pch-includes`).
- `.claude/skills/` is shared: `build`, `clang-format`, `strip-pch-includes` and the `todo-*` skills are one text that a game carrying the engine as `engine/` reaches by symlink, so they say nothing that holds for one repository only. What differs between the repositories is in `.claude/rules/build-project.md` (build, format, includes) and `.claude/rules/todo-project.md` (todo areas); each skill reads its file first. The folder holds `build`, `clang-format`, `strip-pch-includes`, `tidy-fix`, `todo-db` (the todo store: list, show, history, the dashboard, the state of the Vikunja mirror; `tools/todo/CLAUDE.md`), `todo-loop` (works the entries of the store one by one, each in its own local branch `todo/<id>`, and stays on the last finished one, through the agents of `.claude/agents/`; several loops run at once in worker folders, git worktrees made by `tools/todo/worker.py`; `/todo-loop close` after the merge closes the entry in the store), `todo-loop-parallel` (starts `/todo-loop` in every worker folder as background sessions, shows and stops them), `todo-merge` (reports on a `review` branch, asks, then merges it into its base branch, closes the entry and deletes the branch), `todo-add` (records an idea as a new entry in the store) and `cpp-senior-programmer`.
- `.claude/agents/` holds the four agents of `todo-loop`: `todo-classifier` (haiku), `todo-architect` and `todo-developer` (sonnet) and `todo-reviewer` (opus). `TODO.md` in the root only points at the todo store.
- The todo list is the **todo store** (`tools/todo/CLAUDE.md`): a service on Railway over Postgres holds the entries, their status, the stages, the plans and the reviews, and `python3 tools/todo/todo.py` reads and writes them (`TODO_API_URL` and `TODO_API_TOKEN` in the git-ignored `.env`). Nothing of it is in git: no entry files, no status commits, no plans. Vikunja is the person's view of it: the service writes every change there and takes back what a person changes there, so there is no push and no pull. A finished entry is closed, not deleted.
