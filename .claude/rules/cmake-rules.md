---
paths:
  - "**/CMakeLists.txt"
  - "**/*.cmake"
  - "CMakePresets.json"
---

# CMake Guidelines for BagiEngine

## Project Settings

- CMake 3.31+
- C++23 standard
- Ninja generator (recommended)

## External Dependencies

### Vendored Libraries (in `src/Modules/`)

These are included directly via `add_subdirectory()`:

| Library | Path | Purpose |
|---------|------|---------|
| EABase/EASTL | `src/Modules/EABase/` | EA's STL containers |
| EnTT | `src/Modules/EnTT/` | ECS framework |
| pugixml | `src/Modules/pugixml/` | XML parsing |
| ImGui | `src/Modules/imgui/` | Immediate mode GUI |
| fmt | `src/Modules/fmt/` | Formatting library |
| nlohmann/json | `src/Modules/nlohmann/` | JSON for the external control API; the `nlohmann_json` target defines `JSON_NOEXCEPTION` (use it only through `BECore/Control/Json.h`) |

### Git Submodules (in `external/`)

| Library | Path | Type |
|---------|------|------|
| SDL3 | `external/SDL/` | Shared library |
| SDL_image | `external/SDL_image/` | Image loading dependency for texture pipeline |
| Vulkan-Headers | `external/Vulkan-Headers/` | Include-only, provides the `Vulkan::Headers` target |
| GoogleTest | `external/googletest/` | Test framework for `BagiEngineTests` (pinned to `v1.17.0`) |

`external/Vulkan-Loader/` is not a submodule: it is tracked in the repository and holds the prebuilt Windows loader (`vulkan-1.lib`); `VULKAN_SDK`, when set and containing `Lib/vulkan-1.lib`, is used for linking instead.

## Entry Points

- **`cmake/BagiEngine.cmake`** is the engine's entry point: dependencies, options, compiler/platform/output setup, the reflection functions, the engine modules and the public `be_*` functions. A top-level project `include()`s it right after `project()`. It is `include()`d, not `add_subdirectory()`'d, so `add_compile_definitions` / `-fno-exceptions` / output dirs also apply to the games. Every engine path in it goes through `BAGI_ENGINE_DIR` (the engine checkout), never `CMAKE_SOURCE_DIR`, because a game that carries the engine as a submodule is the top-level project.
- **Root `CMakeLists.txt`** (this repository): `project(BagiEngine)`, `include(cmake/BagiEngine.cmake)`, `add_subdirectory(tests)`, `add_subdirectory(games)` when `games/` exists.
- **`games/<Name>/CMakeLists.txt`**: a game. It also works as the root of its own repository (standalone block at the top: `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)` -> `project()` + `include(engine/cmake/BagiEngine.cmake)`).

### Dependency Loading Order in `cmake/BagiEngine.cmake`

```cmake
# 1. Vendored EA Libraries
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/EABase)

# 2. SDL3 (git submodule), then SDL_image, then Vulkan-Headers
# 3. Other vendored libraries: EnTT, pugixml, imgui, fmt, nlohmann
# 4. GoogleTest (if ENGINE_BUILD_TESTS) -- after the ENGINE_NO_EXCEPTIONS block, see below
# 5. Reflection functions (add_reflection_target, be_add_game_reflection)
# 6. Engine modules (BECore first!)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/BECore)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Events)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/TaskSystem)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/RemoteControl)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/SDL)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Vulkan)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Games)
add_subdirectory(${BAGI_ENGINE_DIR}/src)          # RuntimeModule: src/Application + src/Widgets
# 7. Test support (BagiEngineTestSupport), if ENGINE_BUILD_TESTS
```

The vendored `src/Modules/imgui/CMakeLists.txt` uses `${CMAKE_SOURCE_DIR}/external/SDL/include`; `BagiEngine.cmake` adds the right path with `target_include_directories(imgui ...)` instead of editing vendored code.

## Public API (`cmake/BagiEngine.cmake`)

| Function | Purpose |
|----------|---------|
| `be_add_module(<Name> [LINK libs...])` | STATIC `<Name>Module` from `<dir>/src/**`: PCH reuse + force-include, `IMGUI_VULKAN_AVAILABLE=1`, `REFLECTION_GENERATED=1`, include roots (engine `src`, `src/Modules`, own `src`, build dir). Used by `Games` and the games |
| `be_add_game_reflection(<Target> [SOURCE_DIRS ..] [INCLUDE_DIRS ..])` | Runs the generator for a game module (defaults: `<dir>/src`); the engine's reflected headers are only parsed as reference. See `reflection-module.md` |
| `be_add_game_executable(<Name> [GAME_ROOT dir] [SOURCES ..] [MODULES ..])` | Executable from the engine's `src/main.cpp`: `WHOLE_ARCHIVE` engine runtime and game modules, `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` defines, DLL/dylib copy, rpath, natvis |
| `be_add_game_tests(<Name> SOURCES .. [LINK ..])` | gtest executable on `BagiEngineTestSupport` + `gtest_discover_tests` (only when `ENGINE_BUILD_TESTS`) |
| `be_add_game_control_tests(<ExeTarget> [SCRIPT file])` | ctest entry `<ExeTarget>ControlSmoke`: runs `tools/control/tests/smoke.py` (or `SCRIPT`) against the game executable, which it starts with `--control`; exit code 77 is a skip (executable not built). Call it from the game's CMakeLists, never from the engine's `tests/`. The script runs the game `--headless` (no window; `--window` for a real one), once per clock (only when `ENGINE_BUILD_TESTS`). The test environment has `BAGI_ENGINE_DIR`, so a game's `SCRIPT` (`games/Chess/tests/control_smoke.py`) can import `smoke.py` and call its `run_smoke()` with its own scenarios |
| `be_link_engine(<target> [WITH_RUNTIME])` | `WHOLE_ARCHIVE` link of the engine core (and, with `WITH_RUNTIME`, renderer backends, `Games`, `Runtime`) |
| `be_force_include_pch(<target>)` | `/FI"BECore/pch.h"` (MSVC) or `-include "BECore/pch.h"` |

There is no `Math` CMake module: the math types live in `BECore/Math/` and are part of `BECoreModule`.

## Module Structure

Each engine module has its own `CMakeLists.txt`:
- Collect sources with `file(GLOB_RECURSE ...)`
- Create `STATIC` library (`be_add_module` does this for modules laid out as `<dir>/src/<Name>/`)
- Include directories: own `src/` + cross-module path
- Link to `BECoreModule` if needed
- Reuse PCH: `target_precompile_headers(MyModule REUSE_FROM BECoreModule)`. Targets whose sources rely on the PCH contents (game executables, `RuntimeModule`, game modules, test executables) also force-include it via `be_force_include_pch`
- Reflected classes in an **engine** module: add its source directory to `source_dirs` / `scan_dirs` of `BECoreModule` in `CI/meta_generator/reflection_targets.json`. Only `BECoreModule` calls `add_reflection_target()`. **Game** modules call `be_add_game_reflection()` instead and have no entry in that file
- Registration code (factories) lives in static initializers, so executables link the engine libraries and the game modules with `WHOLE_ARCHIVE` (`be_link_engine`, `be_add_game_executable`)

## Build Options

| Option | Default | Effect |
|--------|---------|--------|
| `ENGINE_NO_EXCEPTIONS` | `ON` | `-fno-exceptions` / `/EHs-c-`, `ENGINE_NO_EXCEPTIONS=1`, `EASTL_EXCEPTIONS_ENABLED=0` |
| `ENGINE_ENABLE_ASSERTS` | `ON` | `ASSERT` / `FATALERROR`; compiled out when `OFF` |
| `ENGINE_ENABLE_EXPECTS` | `ON` | `EXPECT`; compiled out when `OFF` |
| `ENGINE_BUILD_TESTS` | `ON` | GoogleTest, `BagiEngineTestSupport`, `BagiEngineTests` and the game test executables |

## Module Dependencies Order

BECoreModule must be first (provides PCH):
1. `BECore` — first!
2. `Events`
3. `TaskSystem`
4. `RemoteControl` (`be_add_module`; links `ws2_32` on Windows; has no static initializers, so it is linked plainly: `RuntimeModule` links it for the executables and `BagiEngineTests` takes it through `LINK RemoteControlModule`)
5. `SDL`
6. `Vulkan`, `Games`, `Runtime` (`src/`), then the games in `games/`

## Tests (GoogleTest)

- `ENGINE_BUILD_TESTS` (default `ON`) controls `enable_testing()`, `add_subdirectory(external/googletest)`, `BagiEngineTestSupport` and the test executables.
- GoogleTest is added **after** the `ENGINE_NO_EXCEPTIONS` block in `cmake/BagiEngine.cmake` on purpose, so it is compiled with the same `-fno-exceptions` as the test sources. `gtest_force_shared_crt` is `ON` (the engine uses `/MD`); `BUILD_GMOCK` and `INSTALL_GTEST` are `OFF`.
- Every test executable is created with `be_add_game_tests`: `BagiEngineTests` (`tests/CMakeLists.txt`), `ChessTests`, `Match3Tests` (`games/<Name>/CMakeLists.txt`). The function applies the `REUSE_FROM BECoreModule` PCH, force-include of `BECore/pch.h`, `IMGUI_VULKAN_AVAILABLE=1` + `REFLECTION_GENERATED=1`, `${CMAKE_BINARY_DIR}` on the include path for `<Generated/*.gen.hpp>`, and `WHOLE_ARCHIVE` for `BECoreModule` / `EventsModule` / `TaskSystemModule` so static-initializer registrations survive. `main()`, the engine environment and the assert bridge come from the static library `BagiEngineTestSupport` (`tests/Common/`).
- `gtest_discover_tests(<Name> DISCOVERY_MODE PRE_TEST)`: tests are listed when `ctest` starts and run from the executable's binary directory (`build/<preset>/tests`, `build/<preset>/games/<Name>`); without content roots `FileSystem` finds `config/` by walking up from the working directory. The test environment pins the engine's `config/` / `assets/` through `BAGI_ENGINE_ROOT`. Do not pass `WORKING_DIRECTORY` (not even inside `PROPERTIES`): it also moves `cmake_test_discovery_*.json` there, which litters the source tree when it is the repository root. An out-of-tree build directory needs the executable started from inside the repository.
- Build them with `cmake --build --preset <preset> --target BagiEngineTests ChessTests Match3Tests`: `--target` on the command line replaces the preset's game targets.

## Best Practices

| Practice | Visibility |
|----------|------------|
| Headers other modules need | `PUBLIC` |
| Implementation details | `PRIVATE` |
| Static libraries | Always for engine modules |
| PCH reuse | `REUSE_FROM BECoreModule` |

## Platform-Specific

- MSVC: `/FI"BECore/pch.h"` for force include; exceptions off with `/EHs-c-`
- Clang/GCC: `-include "BECore/pch.h"`; exceptions off with `-fno-exceptions`
- The project sets no `/W4` or `-Wall -Wextra` of its own for engine targets; `COMPILER_CLANG` / `COMPILER_GCC` / `COMPILER_MSVC` are defined globally

## Dependency Notes

- Keep SDL_image wired after SDL in root dependency setup to avoid missing SDL linkage during texture-related builds.
- Document new rendering dependencies in CMake when introducing asset/texture features to prevent drift between code and build config.

## Presets

`CMakePresets.json` (project root) is the single source of build configurations; VS Code CMake Tools, the CLI and the `build` skill all use it.

- Hidden bases are composed via `inherits`: generator (`ninja`, `vs2022`), build type (`debug`, `release`), compiler (`clang`, `msvc`), host `condition` (`host-macos`, `host-windows`). Visible presets are named `<os>-<toolchain>-<type>`.
- Every `binaryDir` is `${sourceDir}/build/${presetName}`. Build presets target the game executables (`BagiChess`, `BagiMatch3`) and carry the same host `condition`, so other OSes' presets stay hidden. A new game's executable is added to `targets` of `build-base`.
- `testPresets` mirror the build presets (same names, hidden `test-base` / `test-macos` / `test-windows`) and set `outputOnFailure`. Use `ctest --preset <preset>`.
- Release presets set `ENGINE_ENABLE_ASSERTS=OFF` / `ENGINE_ENABLE_EXPECTS=OFF` explicitly (the logic in `cmake/BagiEngine.cmake` that should do this for `CMAKE_BUILD_TYPE=Release` never fires because `option()` has already defined the cache entries).
- Ninja on Windows uses `architecture`/`toolset` with `strategy: external` so CMake Tools loads the Visual Studio environment. A plain shell does not get it; use the `windows-vs2022-*` presets there.
- Do not put per-machine values (`VULKAN_SDK`, ccache, LLVM paths) in `CMakePresets.json`; use `CMakeUserPresets.json` (git-ignored).
- When adding a preset, add the matching build preset **and test preset** and extend the preset list in `CLAUDE.md` and the `build` skill.
- Do not pre-set a variable to `""` before `find_program()`/`find_path()`: the search is skipped (this broke the `glslc` PATH fallback in `src/Modules/Vulkan/CMakeLists.txt`).

## Output Directory Structure

Build outputs are organized by compiler and platform:
```
build/{Compiler}{Platform}/
├── BagiChess.exe        # Game executables (BagiMatch3.exe, ...), test executables (BagiEngineTests.exe, ChessTests.exe, ...)
├── SDL3.dll             # Runtime dependencies
└── lib/                 # Static libraries
```

Example: `build/MSVCWindows/`, `build/ClangmacOS/`
