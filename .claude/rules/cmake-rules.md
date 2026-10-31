---
paths:
  - "**/CMakeLists.txt"
  - "**/CmakeLists.txt"
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

### Git Submodules (in `external/`)

| Library | Path | Type |
|---------|------|------|
| SDL3 | `external/SDL/` | Shared library |
| SDL_image | `external/SDL_image/` | Image loading dependency for texture pipeline |
| GoogleTest | `external/googletest/` | Test framework for `BagiEngineTests` (pinned to `v1.17.0`) |

### Dependency Loading Order in Root CMakeLists.txt

```cmake
# 1. Vendored EA Libraries
add_subdirectory(src/Modules/EABase)

# 2. SDL3 (git submodule)
add_subdirectory(external/SDL)

# 2.1 SDL_image (git submodule, after SDL)
add_subdirectory(external/SDL_image)

# 3. Other vendored libraries
add_subdirectory(src/Modules/EnTT)
add_subdirectory(src/Modules/pugixml)
add_subdirectory(src/Modules/imgui)
add_subdirectory(src/Modules/fmt)

# 4. GoogleTest (if ENGINE_BUILD_TESTS) -- after the ENGINE_NO_EXCEPTIONS block, see below

# 5. Engine modules (BECore first!)
add_subdirectory(src/Modules/BECore)
add_subdirectory(src/Modules/Events)
add_subdirectory(src/Modules/TaskSystem)
add_subdirectory(src/Modules/SDL)
add_subdirectory(src/Modules/Vulkan)
add_subdirectory(src/Modules/Games)    # then Chess, Match3

# 6. Tests (if ENGINE_BUILD_TESTS)
add_subdirectory(tests)
```

There is no `Math` CMake module: the math types live in `BECore/Math/` and are part of `BECoreModule`.

## Module Structure

Each engine module has its own `CMakeLists.txt`:
- Collect sources with `file(GLOB_RECURSE ...)`
- Create `STATIC` library
- Include directories: own `src/` + cross-module path
- Link to `BECoreModule` if needed
- Reuse PCH: `target_precompile_headers(MyModule REUSE_FROM BECoreModule)`

## Module Dependencies Order

BECoreModule must be first (provides PCH):
1. `BECore` — first!
2. `Events`
3. `TaskSystem`
4. `SDL`
5. `Vulkan`, `Games`, `Chess`, `Match3`

## Tests (GoogleTest)

- `ENGINE_BUILD_TESTS` (default `ON`) controls `enable_testing()`, `add_subdirectory(external/googletest)` and `add_subdirectory(tests)`.
- GoogleTest is added **after** the `ENGINE_NO_EXCEPTIONS` block in the root `CmakeLists.txt` on purpose, so it is compiled with the same `-fno-exceptions` as the test sources. `gtest_force_shared_crt` is `ON` (the engine uses `/MD`); `BUILD_GMOCK` and `INSTALL_GTEST` are `OFF`.
- `tests/CMakeLists.txt` builds the executable `BagiEngineTests` the same way as `MyGame`: `REUSE_FROM BECoreModule` PCH, force-include of `BECore/pch.h`, `IMGUI_VULKAN_AVAILABLE=1` + `REFLECTION_GENERATED=1`, `${CMAKE_BINARY_DIR}` on the include path for `<Generated/*.gen.hpp>`, and `WHOLE_ARCHIVE` for `BECoreModule` / `EventsModule` / `TaskSystemModule` so static-initializer registrations survive.
- `gtest_discover_tests(BagiEngineTests DISCOVERY_MODE PRE_TEST)`: tests are listed when `ctest` starts and run from `build/<preset>/tests`; `FileSystem` finds `config/` by walking up from the working directory. Do not pass `WORKING_DIRECTORY` (not even inside `PROPERTIES`): it also moves `cmake_test_discovery_*.json` there, which litters the source tree when it is the repository root. An out-of-tree build directory needs the executable started from inside the repository.
- Build it with `cmake --build --preset <preset> --target BagiEngineTests`: `--target` on the command line replaces the preset's `MyGame`.

## Best Practices

| Practice | Visibility |
|----------|------------|
| Headers other modules need | `PUBLIC` |
| Implementation details | `PRIVATE` |
| Static libraries | Always for engine modules |
| PCH reuse | `REUSE_FROM BECoreModule` |

## Platform-Specific

- MSVC: `/FI"BECore/pch.h"` for force include
- MSVC: `/W4` warnings
- GCC/Clang: `-Wall -Wextra`

## Dependency Notes

- Keep SDL_image wired after SDL in root dependency setup to avoid missing SDL linkage during texture-related builds.
- Document new rendering dependencies in CMake when introducing asset/texture features to prevent drift between code and build config.

## Presets

`CMakePresets.json` (project root) is the single source of build configurations; VS Code CMake Tools, the CLI and the `build` skill all use it.

- Hidden bases are composed via `inherits`: generator (`ninja`, `vs2022`), build type (`debug`, `release`), compiler (`clang`, `msvc`), host `condition` (`host-macos`, `host-windows`). Visible presets are named `<os>-<toolchain>-<type>`.
- Every `binaryDir` is `${sourceDir}/build/${presetName}`. Build presets target `MyGame` and carry the same host `condition`, so other OSes' presets stay hidden.
- `testPresets` mirror the build presets (same names, hidden `test-base` / `test-macos` / `test-windows`) and set `outputOnFailure`. Use `ctest --preset <preset>`.
- Release presets set `ENGINE_ENABLE_ASSERTS=OFF` / `ENGINE_ENABLE_EXPECTS=OFF` explicitly (the logic in the root `CmakeLists.txt` that should do this for `CMAKE_BUILD_TYPE=Release` never fires because `option()` has already defined the cache entries).
- Ninja on Windows uses `architecture`/`toolset` with `strategy: external` so CMake Tools loads the Visual Studio environment. A plain shell does not get it; use the `windows-vs2022-*` presets there.
- Do not put per-machine values (`VULKAN_SDK`, ccache, LLVM paths) in `CMakePresets.json`; use `CMakeUserPresets.json` (git-ignored).
- When adding a preset, add the matching build preset **and test preset** and extend the preset list in `CLAUDE.md` and the `build` skill.
- Do not pre-set a variable to `""` before `find_program()`/`find_path()`: the search is skipped (this broke the `glslc` PATH fallback in `src/Modules/Vulkan/CMakeLists.txt`).

## Output Directory Structure

Build outputs are organized by compiler and platform:
```
build/{Compiler}{Platform}/
├── MyGame.exe           # Executable
├── SDL3.dll             # Runtime dependencies
└── lib/                 # Static libraries
```

Example: `build/MSVCWindows/`, `build/ClangmacOS/`
