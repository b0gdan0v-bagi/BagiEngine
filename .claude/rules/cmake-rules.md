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
| stb | `src/Modules/stb/` | `stb_truetype.h` for glyph metrics and rasterization (INTERFACE target `stb`; the implementation is compiled only in `BECore/Text/Font.cpp`; trusted font files only) |
| RmlUi | `src/Modules/RmlUi/` | RmlUi 6.3 core (MIT), the third candidate for authoring UI screens. STATIC target `rmlui_core` (alias `RmlUi::Core`), added only when `ENGINE_UI_RMLUI=ON` and after the `ENGINE_NO_EXCEPTIONS` block (like GoogleTest). Its CMake file replaces the upstream ones; `RMLUI_FONT_ENGINE` is `none` by default (the host installs `RmlUiFontEngine` over the engine's `Font`, so no FreeType is needed) or `freetype` (RmlUi's own engine, needs FreeType installed; the host does not use it). The module `RmlUiHost` (`src/Modules/RmlUiHost/`) is its host |
| Tracy client | `src/Modules/tracy/` | Tracy Profiler v0.15.0 client (BSD-3), only with `ENGINE_PROFILER_TRACY=ON`: `public/TracyClient.cpp` is a source of `BECoreModule` (no target of its own) compiled with the Tracy definitions and without the PCH, like the one file that includes it, `Profiler/Backends/TracyProfilerBackend.cpp` (filtered out of the glob otherwise). Version and viewer: its `README.md` |

### Git Submodules (in `external/`)

| Library | Path | Type |
|---------|------|------|
| SDL3 | `external/SDL/` | Shared library |
| SDL_image | `external/SDL_image/` | Image loading dependency for texture pipeline |
| Vulkan-Headers | `external/Vulkan-Headers/` | Include-only, provides the `Vulkan::Headers` target |
| GoogleTest | `external/googletest/` | Test framework for `BagiEngineTests` (pinned to `v1.17.0`) |

`external/emsdk/` is the one submodule that is not a library: the installer of the Emscripten SDK for the `web-*` presets (pinned to tag `4.0.23`, which is the SDK version). Nothing links against it; the compiler lands in `external/emsdk/upstream/` when `python CI/scripts/setup_emsdk.py` runs (about 2 GB, once per checkout, on demand; `--check` tells whether it is installed). emsdk's own `.gitignore` keeps the installed SDK out of `git status`. Change the pin in the script's `EMSDK_VERSION` and in the submodule tag together.

`external/Vulkan-Loader/` is not a submodule: it is tracked in the repository and holds the prebuilt Windows loader (`vulkan-1.lib`); `VULKAN_SDK`, when set and containing `Lib/vulkan-1.lib`, is used for linking instead.

## Entry Points

- **`cmake/BagiEngine.cmake`** is the engine's entry point: dependencies, options, compiler/platform/output setup, the reflection functions, the engine modules and the public `be_*` functions. A top-level project `include()`s it right after `project()`. It is `include()`d, not `add_subdirectory()`'d, so `add_compile_definitions` / `-fno-exceptions` / output dirs also apply to the games. Every engine path in it goes through `BAGI_ENGINE_DIR` (the engine checkout), never `CMAKE_SOURCE_DIR`, because a game that carries the engine as a submodule is the top-level project.
- **`cmake/BagiPlatformOptions.cmake`**: `include()`d once by `BagiEngine.cmake` (after the C++ standard block, before SDL): the options `ENGINE_WITH_VULKAN`, `ENGINE_WITH_REMOTE_CONTROL`, `ENGINE_STATIC_SDL`, `ENGINE_SINGLE_THREAD`, the variable `ENGINE_TARGET_WEB` and the definitions `BE_WITH_REMOTE_CONTROL`, `BE_SINGLE_THREAD`, `PLATFORM_WEB`; on the web a threaded build (the default) also adds `-pthread` to `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS` before the first `add_subdirectory` (every object of the link needs it, the vendored SDL, SDL_image and EASTL included, or wasm-ld rejects the mix) and the definition `BE_WEB_THREAD_POOL_SIZE`; on the web also the definitions that make the vendored EABase / EASTL work without an edit: `EA_PLATFORM_SAMSUNG_TV` (EABase has no Emscripten branch and this is its one branch that takes the platform from a definition: Linux, POSIX, little endian, ARM32, whose EASTL atomics are mostly the compiler builtins wasm32 has; but `eastl::cpu_pause()` is `asm("yield")` and the barrier of `eastl::atomic` is `asm("dmb ish")`, which wasm cannot assemble: the engine uses `std::atomic` only) and `EASTL_DEBUG_BREAK_OVERRIDE=__builtin_trap` (the ARM debug break is inline assembly). Platform logic of the web build goes here and into `cmake/BagiWeb.cmake`, not into `BagiEngine.cmake`.
- **`cmake/BagiWeb.cmake`**: `include()`d by `BagiEngine.cmake` before the platform options (they read `BE_WEB_THREAD_POOL_SIZE` from it); functions only (plus the Release flags on Emscripten: `-O2 -DNDEBUG`, the level is the cache variable `BE_WEB_OPTIMIZATION`), no dependence on the rest of the setup, so `tools/web/tests/check_bagi_web.cmake` includes it alone under `cmake -P`. `be_web_preload_args` (the `--preload-file` entries of the `config/` and `assets/` folders of the engine, game and web layers, virtual roots `/bagi/engine`, `/bagi/game`, `/bagi/web`), `be_web_link_options` (the `-s` settings, `--shell-file web/shell.html`; `-sASSERTIONS=1` in Debug only, as a `$<CONFIG:Debug>` generator expression; without `SINGLE_THREAD` also `-pthread`, `-sPTHREAD_POOL_SIZE=<BE_WEB_THREAD_POOL_SIZE>` (5: the `TaskManager`'s `kMaxWorkerThreads` plus one, the Web Workers created before `main()`), `-sPTHREAD_POOL_SIZE_STRICT=2` and `-sMAXIMUM_MEMORY=<BE_WEB_MAXIMUM_MEMORY>` (2 GB, which shared memory with growth needs fixed); no `PROXY_TO_PTHREAD`; `TaskSystem/CLAUDE.md`, "Browser"), `be_web_generate_shell` (a copy of `web/shell.html` in the target's binary dir with `@BAGI_WEB_THREADS@` set to `1` or `0`, so the shell of a threaded page can refuse to start without cross-origin isolation; the link options take it as `SHELL_FILE`), `be_web_parse_preload_spec` (a preload entry back into its folder and virtual path), `be_web_configure_target` (suffix `.html`, the options, the preload package, the content files and a stamp with their list (`be_web_content_inputs`) as `LINK_DEPENDS` so an edit, an added or a deleted file under `config/` or `assets/` relinks the `.data` package, and the `BAGI_*_ROOT` definitions as those virtual paths; `be_add_game_executable` calls it on the web instead of baking the checkout paths). It also gives the toolchain's `WHOLE_ARCHIVE` link feature the attributes the desktop ones have (`OVERRIDE=DEFAULT`; the toolchain file defines none, and without them CMake refuses a module that an executable links whole and a game module links plainly). The link settings were checked against `settings.js` of emsdk 4.0.23 and `BagiMatch3` links with them (the defaults that are right already, `GL_ENABLE_GET_PROC_ADDRESS` on and `ASSERTIONS` off from `-O1`, are not repeated); `-O2` is on every compile and link line of `web-release`; the content package and a source path with a space or an `@` (written `@@` in the spec, as emcc's file packager reads it) were built for real. `SDLIMAGE_TIF` and `SDLIMAGE_WEBP` are forced OFF on the web (`BagiEngine.cmake`, in front of the SDL_image `add_subdirectory`: the vendored libtiff and libwebp were 444 KB of `.wasm` and nothing ships those formats); PNG, JPEG, GIF, QOI and XPM stay. The whole build was run in a real browser (headless Chromium and Edge, `tools/web/CLAUDE.md`, "The browser smoke"): the `-sEXIT_RUNTIME=1` / `emscripten_cancel_main_loop()` / `Finish()` path of `src/main.cpp` works (the Quit button ends the runtime, `Module.onExit` is called with no status, the canvas stays blank and the shell says so). Every web preset writes to its own `build/<preset>/` (the page into `bin/`), so the threaded and the single-thread configurations, whose libraries are built with and without `-pthread`, never meet.
- **Root `CMakeLists.txt`** (this repository): `project(BagiEngine)`, `include(cmake/BagiEngine.cmake)`, `add_subdirectory(tests)`, `add_subdirectory(games)` when `games/` exists.
- **`games/<Name>/CMakeLists.txt`**: a game. It also works as the root of its own repository (standalone block at the top: `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)` -> `project()` + `include(engine/cmake/BagiEngine.cmake)`).

### Dependency Loading Order in `cmake/BagiEngine.cmake`

```cmake
# 1. Vendored EA Libraries
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/EABase)

# 2. SDL3 (git submodule), then SDL_image, then Vulkan-Headers
# 3. Other vendored libraries: EnTT, pugixml, imgui, fmt, nlohmann, stb
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
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Rooms)
add_subdirectory(${BAGI_ENGINE_DIR}/src)          # RuntimeModule: src/Application + src/Widgets
# 7. Test support (BagiEngineTestSupport), if ENGINE_BUILD_TESTS
```

The vendored `src/Modules/imgui/CMakeLists.txt` uses `${CMAKE_SOURCE_DIR}/external/SDL/include`; `BagiEngine.cmake` adds the right path with `target_include_directories(imgui ...)` instead of editing vendored code.

## Public API (`cmake/BagiEngine.cmake`)

| Function | Purpose |
|----------|---------|
| `be_add_module(<Name> [LINK libs...])` | STATIC `<Name>Module` from `<dir>/src/**`: PCH reuse + force-include, `IMGUI_VULKAN_AVAILABLE=1`, `REFLECTION_GENERATED=1`, include roots (engine `src`, `src/Modules`, own `src`, build dir). Used by `Games` and the games |
| `be_add_game_reflection(<Target> [SOURCE_DIRS ..] [INCLUDE_DIRS ..])` | Runs the generator for a game module (defaults: `<dir>/src`); the engine's reflected headers are only parsed as reference. See `reflection-module.md` |
| `be_add_game_executable(<Name> [GAME_ROOT dir] [SOURCES ..] [MODULES ..])` | Executable from the engine's `src/main.cpp`: `WHOLE_ARCHIVE` engine runtime and game modules, `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` defines, DLL/dylib copy, rpath, natvis. On the web (`ENGINE_TARGET_WEB`) it calls `be_web_configure_target` instead: an HTML page whose roots are virtual paths in the preloaded package, `BAGI_WEB_ROOT` (the `web/` overlay) added, and no DLL copy |
| `be_add_game_tests(<Name> SOURCES .. [LINK ..] [WHOLE_ARCHIVE ..])` | gtest executable on `BagiEngineTestSupport` + `gtest_discover_tests` (only when `ENGINE_BUILD_TESTS`). `WHOLE_ARCHIVE` links modules whole, for a test that loads the game's own scene file: a component that no test names is otherwise dropped by the linker and the scene silently lacks it |
| `be_add_game_control_tests(<ExeTarget> [SCRIPT file])` | ctest entry `<ExeTarget>ControlSmoke`: runs `tools/control/tests/smoke.py` (or `SCRIPT`) against the game executable, which it starts with `--control`; exit code 77 is a skip (executable not built). Call it from the game's CMakeLists, never from the engine's `tests/`. The script runs the game `--headless` (no window; `--window` for a real one), once per clock (only when `ENGINE_BUILD_TESTS`). The test environment has `BAGI_ENGINE_DIR`, so a game's `SCRIPT` (BagiEngine-Chess has one: `tests/control_smoke.py`) can import `smoke.py` and call its `run_smoke()` with its own scenarios |
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
| `ENGINE_UI_RMLUI` | `OFF` | Builds the vendored RmlUi (`rmlui_core`) and its host `RmlUiHostModule` (`be_add_module` + `be_add_game_reflection`, after `Games`); `be_link_engine` links the host `WHOLE_ARCHIVE` into every executable and test executable, and `tests/RmlUiHost/` joins `BagiEngineTests`. Presets: `macos-rmlui-debug` / `macos-rmlui-release`. |
| `ENGINE_WITH_VULKAN` | `ON` | `OFF`: `VulkanModule` is not added, linked or deployed (its loader DLL is not copied). The window config must then name the SDL renderer: a game whose `ApplicationConfig.xml` says `VulkanRendererBackend` does not start (`SDL3ApplicationFabric::Create` logs `renderer 'VulkanRendererBackend' is not available in this build` and fails); the web overlay `web/config/` sets `SDLRendererBackend`, and `--headless` always uses the SDL renderer. `Vulkan::Headers` stays (imgui needs it). Forced `OFF` on Emscripten |
| `ENGINE_WITH_REMOTE_CONTROL` | `ON` | `OFF`: `RemoteControlModule` is not added and `Application` does not start the socket server (`--control` then logs `Control API: not available in this build` and the start fails); definition `BE_WITH_REMOTE_CONTROL=1\|0`. `tests/CMakeLists.txt` still names `RemoteControlModule`, so a build with this `OFF` needs `-DENGINE_BUILD_TESTS=OFF`. Forced `OFF` on Emscripten |
| `ENGINE_STATIC_SDL` | `OFF` | `ON`: SDL3 and SDL3_image are static libraries; `SDL3-shared` stays as an INTERFACE shim over `SDL3::SDL3-static` (the vendored imgui file and our modules link that name); no SDL DLL is copied. `ON` forces the cache entry `BUILD_SHARED_LIBS` to `OFF` (SDL_image follows it) and `OFF` forces it back to `ON`, so one build folder can switch. Forced `ON` on Emscripten |
| `ENGINE_SINGLE_THREAD` | `OFF` | The definition `BE_SINGLE_THREAD=1\|0` (the task system without worker threads). Not forced on Emscripten: the web default is the threaded build (`-pthread`, a pool of Web Workers, SDL with `SDL_PTHREADS=ON`; the page needs cross-origin isolation); `ON` there is the single-thread page (presets `web-*-single-thread`, `SDL_PTHREADS=OFF`) |
| `ENGINE_PROFILER` | `OFF` | The definition `BE_PROFILER=1\|0`: the `BE_PROFILE_*` macros record (off: they compile to nothing and do not evaluate their arguments). The recorder, the Chrome-trace writer and the frame statistics compile in every build. Forced `OFF` on Emscripten. `src/Modules/BECore/Profiler/CLAUDE.md` |
| `ENGINE_PROFILER_ALLOC` | `OFF` | Needs `ENGINE_PROFILER` (configure stops otherwise). `BE_PROFILER_ALLOC=1\|0`; `ON` replaces every form of the global `operator new` / `delete` in `BECoreModule` to count allocations. Forced `OFF` on Emscripten |
| `ENGINE_PROFILER_TRACY` | `OFF` | Needs `ENGINE_PROFILER`. `BE_PROFILER_TRACY=1\|0`; `ON` builds the vendored Tracy client (`src/Modules/tracy/`) and the Tracy backend, which becomes the default backend. Forced `OFF` on Emscripten |
| `BE_WITH_EDITOR` | `AUTO` | `AUTO` / `ON` / `OFF`; sets `BE_EDITOR_ENABLED` and the definition `BE_WITH_EDITOR=1\|0` on every target; when off, the editor panels and their tests stay out of `RuntimeModule` and the test executables (`src/CMakeLists.txt`, `tests/CMakeLists.txt`). `AUTO` is ON except in a single-config Release; Debug ignores `OFF` with a WARNING. Presets `macos-release-editor` / `windows-msvc-release-editor`. On Emscripten it is forced off, also in Debug (one status line). Libraries of the other setting stay in the preset's `lib/` when `-DBE_WITH_EDITOR` is switched inside one preset: delete every `lib/*.a` then. `cmake/CLAUDE.md`, "The editor option" |

## Module Dependencies Order

BECoreModule must be first (provides PCH):
1. `BECore` — first!
2. `Events`
3. `TaskSystem`
4. `RemoteControl` (`be_add_module`; links `ws2_32` on Windows; has no static initializers, so it is linked plainly: `RuntimeModule` links it for the executables and `BagiEngineTests` takes it through `LINK RemoteControlModule`)
5. `SDL`
6. `Vulkan`, `Games`, `Rooms` (`be_add_module`, links `GamesModule`; pure code, no static initializers, so it is linked plainly: `BagiEngineTests` takes it through `LINK RoomsModule`), `RmlUiHost` (only with `ENGINE_UI_RMLUI`), `Runtime` (`src/`), then the games in `games/`

## Tests (GoogleTest)

- `ENGINE_BUILD_TESTS` (default `ON`) controls `enable_testing()`, `add_subdirectory(external/googletest)`, `BagiEngineTestSupport` and the test executables.
- GoogleTest is added **after** the `ENGINE_NO_EXCEPTIONS` block in `cmake/BagiEngine.cmake` on purpose, so it is compiled with the same `-fno-exceptions` as the test sources. `gtest_force_shared_crt` is `ON` (the engine uses `/MD`); `BUILD_GMOCK` and `INSTALL_GTEST` are `OFF`.
- Every test executable is created with `be_add_game_tests`: `BagiEngineTests` (`tests/CMakeLists.txt`), `Match3Tests` (`games/<Name>/CMakeLists.txt`). The function applies the `REUSE_FROM BECoreModule` PCH, force-include of `BECore/pch.h`, `IMGUI_VULKAN_AVAILABLE=1` + `REFLECTION_GENERATED=1`, `${CMAKE_BINARY_DIR}` on the include path for `<Generated/*.gen.hpp>`, and `WHOLE_ARCHIVE` for `BECoreModule` / `EventsModule` / `TaskSystemModule` so static-initializer registrations survive. `main()`, the engine environment and the assert bridge come from the static library `BagiEngineTestSupport` (`tests/Common/`).
- **Parallel run.** `test-base` in `CMakePresets.json` sets `execution.jobs` 8, so `ctest --preset <p>` runs eight entries at once (`-j1` switches it off; `CTEST_PARALLEL_LEVEL` does not override a preset). Every gtest case is its own ctest entry and process and carries no property: the tests are written to run beside each other (`.claude/rules/tests-module.md`, "Rules"). `be_add_game_control_tests` sets `RESOURCE_LOCK bagi_control_smoke` (two smoke tests never run together; unit tests run beside them), `SKIP_RETURN_CODE 77` and `TIMEOUT 120`. It is not `RUN_SERIAL`, which would stop the whole run for the time of the test. A test of a game that cannot run beside others sets `RUN_SERIAL` or a `RESOURCE_LOCK` in `set_tests_properties` in its own CMakeLists.
- `gtest_discover_tests(<Name> DISCOVERY_MODE PRE_TEST)`: tests are listed when `ctest` starts and run from the executable's binary directory (`build/<preset>/tests`, `build/<preset>/games/<Name>`); without content roots `FileSystem` finds `config/` by walking up from the working directory. The test environment pins the engine's `config/` / `assets/` through `BAGI_ENGINE_ROOT`. Do not pass `WORKING_DIRECTORY` (not even inside `PROPERTIES`): it also moves `cmake_test_discovery_*.json` there, which litters the source tree when it is the repository root. An out-of-tree build directory needs the executable started from inside the repository.
- Build them with `cmake --build --preset <preset> --target BagiEngineTests Match3Tests`: a plain `--build --preset <preset>` builds everything (`all`); the build presets name no targets, so that a game repository can include these presets (a preset name cannot be redefined by the including file).

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

- Hidden bases are composed via `inherits`: generator (`ninja`, `vs2022`), build type (`debug`, `release`), the RmlUi switch (`rmlui`: `ENGINE_UI_RMLUI=ON`, for `macos-rmlui-debug` / `macos-rmlui-release`; no Windows ones until the host is built there), the editor switch (`editor`: `BE_WITH_EDITOR=ON`, for `macos-release-editor` / `windows-msvc-release-editor`), the Tracy switch (`tracy`: `ENGINE_PROFILER_TRACY=ON`, for `macos-profile-tracy`), the profile build (`profile`: `RelWithDebInfo`, asserts and expects off, `BE_WITH_EDITOR=OFF`, `ENGINE_PROFILER` and `ENGINE_PROFILER_ALLOC` on, for `macos-profile`; measured numbers come from it, never from Debug), compiler (`clang`, `msvc`), `no-module-scan` (`CMAKE_CXX_SCAN_FOR_MODULES=OFF`, for `windows-clang-*`: the engine has no C++20 modules, and the scan needs `clang-scan-deps`, which not every LLVM on Windows ships), host `condition` (`host-macos`, `host-windows`). Visible presets are named `<os>-<toolchain>-<type>`.
- Every `binaryDir` is `${sourceDir}/build/${presetName}`. Build presets carry the same host `condition`, so other OSes' presets stay hidden. They name no `targets` (the build is `all`): a game that carries the engine as a submodule includes this file in its own `CMakePresets.json` (`"include": ["engine/CMakePresets.json"]`), and a preset name cannot be defined twice, so a list of the engine's games would break its build.
- `testPresets` mirror the build presets (same names, hidden `test-base` / `test-macos` / `test-windows`) and set `outputOnFailure`. Use `ctest --preset <preset>`.
- Release presets set `ENGINE_ENABLE_ASSERTS=OFF` / `ENGINE_ENABLE_EXPECTS=OFF` explicitly (the logic in `cmake/BagiEngine.cmake` that should do this for `CMAKE_BUILD_TYPE=Release` never fires because `option()` has already defined the cache entries).
- Ninja on Windows uses `architecture`/`toolset` with `strategy: external` so CMake Tools loads the Visual Studio environment. A plain shell does not get it: run `cmake --preset` and `cmake --build --preset` through `CI/scripts/vs_env.py` (vswhere + `vcvarsall.bat x64`, `PATH` extended behind the caller's). Without it Clang stops at configure (`No CMAKE_RC_COMPILER could be found`) and at the archive step (`CMAKE_AR-NOTFOUND`). `windows-clang-*` is the default Windows build (the `build` skill); the MSVC presets are fallbacks.
- Do not put per-machine values (`VULKAN_SDK`, ccache, LLVM paths) in `CMakePresets.json`; use `CMakeUserPresets.json` (git-ignored).
- When adding a preset, add the matching build preset **and test preset** and extend the preset list in `CLAUDE.md` and the `build` skill. The one exception is the web: `web-debug` / `web-release` and `web-debug-single-thread` / `web-release-single-thread` (hidden bases `web` and the mixin `web-single-thread`, which sets `ENGINE_SINGLE_THREAD=ON`; the threaded presets set it `OFF` explicitly, so an old build folder is switched back) have a build preset and no test preset, because ctest cannot run wasm and `ENGINE_BUILD_TESTS` is `OFF` there.
- The web presets use Ninja and the Emscripten toolchain file `${fileDir}/external/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake` (`${fileDir}` is the folder of the `CMakePresets.json` that holds the preset: the engine root, also when a game includes the file). They have no `condition` and read no environment variable (`EMSDK` is not used): they are always listed, and with the SDK not installed `cmake --preset web-release` fails with "could not find toolchain file"; the fix is `python CI/scripts/setup_emsdk.py`. Somebody with an SDK of their own overrides `toolchainFile` in `CMakeUserPresets.json`. They set no compiler (the toolchain does) and no per-machine value. `tools/web/tests/test_cmake_web.py` checks them.
- Do not pre-set a variable to `""` before `find_program()`/`find_path()`: the search is skipped (this broke the `glslc` PATH fallback in `src/Modules/Vulkan/CMakeLists.txt`).

## Output Directory Structure

Outputs go under the binary dir (`CMAKE_BINARY_DIR`), which is `build/<preset>` for every preset:
```
build/<preset>/
├── bin/                 # Game executables, test executables (BagiEngineTests, Match3Tests, ...), shared libraries
└── lib/                 # Static libraries
```

Example: `build/macos-debug/bin/BagiEngineTests`. A build without a preset (`-B dir`) writes `dir/bin` and `dir/lib`. Multi-config generators (Visual Studio) append the configuration, `bin/<Config>/`. `ctest` runs the gtests from the binary dir of their directory (`build/<preset>/tests`), not from `bin/`.
