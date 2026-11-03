# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Use the presets from `CMakePresets.json` (same ones VS Code CMake Tools shows). Each preset builds into `build/<preset>/`; the `build` skill automates this.

```bash
cmake --list-presets=build                 # presets available on this OS
cmake --preset macos-debug                 # configure (also regenerates reflection code)
cmake --build --preset macos-debug         # build the game executables (BagiChess, BagiMatch3)
```

| OS | Presets |
|----|---------|
| macOS | `macos-debug`, `macos-release` (Clang + Ninja); `macos-rmlui-debug`, `macos-rmlui-release`: the same with `ENGINE_UI_RMLUI=ON` (the RmlUi host and its tests; no system library is needed, the engine's own fonts draw the text) |
| Windows | `windows-msvc-{debug,release}`, `windows-clang-{debug,release}` (Ninja), `windows-vs2022-{debug,release}`, `windows-vs2022-clangcl-{debug,release}` (Visual Studio 2022) |

Per-machine settings go in `CMakeUserPresets.json` (git-ignored). Without presets (legacy flat `build/`):

```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

C++23 is required. Exceptions are disabled (`ENGINE_NO_EXCEPTIONS=ON`). Output lands in `build/{CompilerName}{PlatformName}/` regardless of preset, so Debug and Release Ninja presets overwrite each other's executables. Reflection code is generated at configure time: re-run `cmake --preset ...` after changing `BE_CLASS` / `BE_REFLECT_FIELD`.

## Running Tests

Unit and module tests are GoogleTest tests, built as separate executables (not part of the games): `BagiEngineTests` for the engine (`tests/`), `ChessTests` and `Match3Tests` next to their game (`games/<Name>/tests/`). The GoogleTest submodule needs `git submodule update --init external/googletest`; `-DENGINE_BUILD_TESTS=OFF` skips the whole thing.

```bash
cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests   # --target overrides the preset's game targets
ctest --preset macos-debug                                       # every test in its own process
build/ClangmacOS/BagiEngineTests --gtest_filter='Reflection*'    # run directly; cwd must be inside the repository
```

Runtime tests that need the live engine are `ITest` classes (`src/Modules/BECore/Tests/`). `CoreManager::OnApplicationPreInit()` runs the ones enabled in `config/TestsConfig.xml` at game startup.

`ctest` also runs `BagiMatch3ControlSmoke` and `BagiChessControlSmoke`: Python scripts that start the game `--headless` and play it over the control API. They are skipped (exit code 77) when the executable is not built, and `--target` above builds no game, so build `BagiMatch3 BagiChess` as well to run them. The Python tools have pytest tests of their own, not part of ctest: `.venv/bin/python -m pytest tools/control/tests` (`tools/control/CLAUDE.md`).

## Architecture Overview

BagiEngine is a C++23 game engine with modular ECS architecture (EnTT). The main entry is `src/main.cpp` → `Application::Initialize()/Run()`; each game executable compiles it.

**Engine and games:** the engine knows nothing about concrete games. A game lives in `games/<Name>/` (module, executable, `config/`, `assets/`, `tests/`), reaches the engine only through `cmake/BagiEngine.cmake` (`be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests`) and registers an `IGame` in `AbstractFactory<IGame>`. One executable is one game. `games/` is built to move out into separate repositories that carry this repository as a submodule (`engine/`); no engine code, CMake or config outside `games/` refers to a concrete game. See `src/Modules/Games/CLAUDE.md`.

**Stack:** CMake 3.31+ with Ninja, SDL3 + SDL_image, ImGui, EnTT (ECS), EASTL, pugixml (XML), nlohmann/json (control API only), stb_truetype (text), fmt, magic_enum (reflection), GoogleTest (tests).

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
games/<Name>/           # a game: CMakeLists.txt, src/, config/, assets/, tests/, CLAUDE.md (Chess, Match3)
tests/                  # GoogleTest executable BagiEngineTests (BECore) + Common (BagiEngineTestSupport)
tools/                  # developer tools: natvis, todo_sync (`todo/` dashboard and Vikunja sync; Python, pytest), control (Python: the control API client and smoke test, the MCP server, the Stockfish bridge; `tools/control/CLAUDE.md`)
```

**Content is layered.** `FileSystem` and `ConfigManager` merge the engine's `config/` and `assets/` with the game's; the game is on top, so a game file of the same name replaces the engine's (a game's `SceneConfig.xml` replaces the engine's whole file). Roots come from `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` (defined by `be_add_game_executable`); without them the project root is searched upwards from the working directory. Details: `src/Modules/BECore/FileSystem/CLAUDE.md`.

**Vendored code** (`EABase/`, `EnTT/`, `fmt/`, `imgui/`, `nlohmann/`, `pugixml/`, `RmlUi/`, `stb/`): do not edit it, and leave it out of broad searches. `RmlUi/` is built only with `-DENGINE_UI_RMLUI=ON`, together with its host module `RmlUiHost` (`src/Modules/RmlUiHost/CLAUDE.md`, `.claude/plans/ui-authoring-decision.md`).

**Key subsystems in `src/Modules/BECore/`:**

| Subsystem | Purpose |
|-----------|---------|
| `GameManager/CoreManager` | Central service locator — owns FileSystem, ConfigManager, Logger, SceneManager, ResourceManager, etc. |
| `Control/` | External control API core: a thread-safe command queue run on the main thread at frame stage 1b (`ControlService`, JSON through nlohmann with exceptions off) and the start-up flags (`ControlSettings`). The WebSocket transport is the `RemoteControl` module; start a game with `--control` to use it (`--manual-clock`, `--headless`, `step` and `screenshot`: `Control/CLAUDE.md`) |
| `Scene/` | Scene graph (SceneNode → IComponent), serialization |
| `Reflection/` | IPropertyVisitor, ISerializer/IDeserializer, BE_CLASS codegen markers |
| `Resource/` | TextureLibrary, resource pooling |
| `Text/` | Fonts (`stb_truetype`), pure text layout, glyph atlases in page textures, `TextComponent` (`Text/CLAUDE.md`) |
| `UI/` | In-game UI: canvases (`UICanvasComponent`), the scaler, adaptive layout, responsive variants, the layout validator (`UI/CLAUDE.md`) |
| `Renderer/` | `IRenderer` / `ITexture`; backend (`SDLRendererBackend` or `VulkanRendererBackend`) is picked by `renderer` in `config/ApplicationConfig.xml` |

`Events` (CRTP pub/sub: `EventBase<T>`, `SubscriptionHolder` for RAII) and `TaskSystem` (async coroutines: `TaskManager`, `Task<T>`) are sibling modules, not part of BECore.

Widgets live in `src/Widgets/`: the engine's editor panels and the frame widgets that run the render frame (`ClearScreenWidget`, `SceneRenderWidget`, `ImGuiWidget`). None of them is game UI. `SceneEditorWidget` handles orchestration/UI flow; `ComponentInspectors` handles per-field editing via `IPropertyVisitor`. Scene editor behavior is driven by `SceneConfig.xml` (the active game's, see layering); keep scene/component configuration consistent with it when adding or changing editor behavior.

Reflection code is generated by `CI/meta_generator/meta_generator.py` (Python, regex-based). `BE_CLASS` and `BE_REFLECT_FIELD` macros are the generation triggers.

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
- The status `review` of a `todo/` entry is set only by the `todo-loop` skill, in the branch of the task; `main` is not touched, and after the merge `/todo-loop close` deletes the entry file.
- For complex tasks, keep a plan and update it as work progresses. Plan mode writes it to the local store `~/.claude/plans/`; plans are not committed. Link the plan from the `todo/` entry with `plan: NAME.md`, run `todo-sync push` so the tracker task carries its checklist, and delete the plan (and the `plan:` line) when the work is done. Include testing and documentation tasks. For plans with substantial C++ changes, end with a formatting step (skill `clang-format`) and an includes step (skill `strip-pch-includes`; use a subfolder and dry-run first if doing it in parts).

## Rules and Skills

- Module guidance sits next to the code in nested `CLAUDE.md` files: `src/Modules/BECore/` (plus `Config/`, `Control/`, `FileSystem/`, `Logger/`, `Resource/`, `Scene/`, `Text/`, `UI/`), `Events/`, `TaskSystem/`, `RemoteControl/`, `SDL/`, `Vulkan/`, `Games/`, `RmlUiHost/`, and `games/Chess/`, `games/Match3/`, and `tools/control/`. Claude Code loads one when a file under that directory is read.
- `.claude/rules/` holds guidance that spans several directories or matches by file name (`paths:` frontmatter): CMake, Reflection, Reflection events (`*Event.h`), Rendering, Widgets, Tests. Read the relevant file before changing that area.
- `.claude/skills/` holds `build`, `clang-format`, `strip-pch-includes`, `tidy-fix`, `todo-sync` (`todo/` dashboard and Vikunja sync, `tools/todo_sync/CLAUDE.md`), `todo-loop` (works the `todo/` entries one by one, each in its own local branch `todo/<id>`, through the agents of `.claude/agents/`; `/todo-loop close` after the merge deletes the entry and runs `pull` and `push`), `todo-add` (records an idea as a new entry) and `cpp-senior-programmer`.
- `.claude/agents/` holds the four agents of `todo-loop`: `todo-classifier` (haiku), `todo-architect`, `todo-developer` and `todo-reviewer` (sonnet). `TODO.md` in the root only points at `todo/`.
- The todo list is the folder `todo/`: one markdown file per entry, with optional front matter `status:`, `stage:` and `plan:` (format in `todo/README.md`). Keep them current when work on an entry starts or stops, and delete the file when the entry is finished. Run `todo-sync check` after editing. The tracker (Vikunja) is synced both ways: `pull` brings back a task closed or created there, then `push` exports (`tools/todo_sync/CLAUDE.md`).
