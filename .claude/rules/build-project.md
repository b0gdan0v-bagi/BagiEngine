---
paths:
  - "**/CMakeLists.txt"
  - "CMakePresets.json"
  - "CMakeUserPresets.json"
  - "**/*.cmake"
  - "CI/**/*"
---

# Build, format and include facts of this repository

Read by the skills `build`, `clang-format` and `strip-pch-includes` (they read it by path: it is not loaded into every session). The three skills are the same text in the engine repository and in a game that carries the engine as the submodule `engine/` (symlinks into `engine/.claude/skills/`); what differs between the repositories is here. A game has a file of the same name with the same headings and fields: this file is its template. The todo areas are in `todo-project.md`, not here.

## Terms

| Term | Meaning |
|------|---------|
| `<engine>` | `.` in the engine repository, `engine` in a game: the folder with `CI/`, `cmake/` and `.venv`. The same term as in `todo-loop` ("Areas and the repository rules"). |
| `<R>` | the repository to build or format. `.` by default (the repository the session runs in). `engine` only for engine work done from a game checkout; then the skills run from `engine/` and read `engine/.claude/rules/build-project.md`, so `<engine>` is `.` there. Every path below is relative to `<R>`. |
| `<product>` | the game executable of `<R>`, from the field `Product`. |
| `<tests>` | the test targets of `<R>`, from the field `Tests`. |
| `<py>` | the Python interpreter that runs `CI/scripts/*.py`, from the field `Python` (below). |
| `<p>` | the preset the skill picked. |

## Build

Fields the `build` skill reads. A game fills every row.

| Field | Meaning | Engine value |
|-------|---------|--------------|
| Product | the executable (target name; the file is `build/<preset>/bin/<name>[.exe]`) | `BagiMatch3` |
| Tests | the gtest targets; the argument `tests` builds exactly these (`--target <tests>`) | `BagiEngineTests Match3Tests` |
| Default build | what `cmake --build --preset <p>` with no `--target` builds | every target: the product and the test executables |
| Source paths | the paths of `git status --porcelain -- <paths>` (a configure trigger) and of the `BE_CLASS` / `BE_EVENT` / `BE_REFLECT_FIELD` search | `src tests games` |
| Extra configure triggers | cases besides the common ones (below) that need `cmake --preset <p>` | a change of `BE_WITH_EDITOR` (`-DBE_WITH_EDITOR=...`, or a switch between a preset with and without the editor): the set of runtime and test sources changes (see "Gotchas") |
| Extra failure hints | cause and fix for a configure, compile or link failure that is specific to this repository | see "Failure hints" |
| Extra gotchas | facts about the output, the tests or the run that hold only here | see "Gotchas" |
| Test run | what `ctest --preset <p>` runs here and what it needs | see "Tests" |
| Local overrides, editor | where per-machine settings and the IDE setup are described | below, "Local overrides" and "VS Code" |

The common configure triggers (the skill states them; a repository adds to them with `Extra configure triggers`): no `build/<p>/CMakeCache.txt`, the argument `configure`, added, deleted, renamed or untracked files under the source paths (sources are globbed without `CONFIGURE_DEPENDS`), a change in `CMakePresets.json`, `CMakeUserPresets.json` or any `*.cmake`, and a changed or new file under the source paths that contains `BE_CLASS`, `BE_EVENT` or `BE_REFLECT_FIELD` (reflection code is generated only at configure time).

### Failure hints

Engine:

- `glslc not found`: install the Vulkan SDK or set `VULKAN_SDK` in `CMakeUserPresets.json` (see "Local overrides").
- A missing submodule (`external/*` not found): `git submodule update --init`. The tests need `external/googletest`: `git submodule update --init external/googletest`.
- Web, `cmake --preset` (the generate step, not `cmake --build`) reports `Impossible to link target 'BagiMatch3' because the link item 'BECoreModule' ... has already occurred with the feature 'WHOLE_ARCHIVE'`: the Emscripten toolchain defines the `WHOLE_ARCHIVE` link feature without attributes; `cmake/BagiWeb.cmake` sets `CMAKE_LINK_LIBRARY_WHOLE_ARCHIVE_ATTRIBUTES` (`OVERRIDE=DEFAULT`) on the web.
- Web, `error: Unknown platform` / `Unknown processor` / `Unknown endianness` from `EABase/config/eaplatform.h`, or `atomic macro not implemented` in EASTL: EABase has no Emscripten branch. `cmake/BagiPlatformOptions.cmake` defines `EA_PLATFORM_SAMSUNG_TV` on the web (its one branch that takes the platform from a definition).
- Web, `call to consteval function 'fmt::basic_format_string<...>::basic_format_string<FMT_COMPILE_STRING, 0>' is not a constant expression`: the Clang of emsdk 4.0.23 (22) evaluates a string literal twice as two objects, and `FMT_STRING()` of the vendored fmt 11.0.2 subtracts pointers into both. The fmt library target is compiled as C++17 on the web (`cmake/BagiEngine.cmake`); engine code must not include `<fmt/chrono.h>` (seen: its `write_fractional_seconds` uses `FMT_STRING`; `fmt/std.h`, `color.h` and `xchar.h` use it in headers too): format a `std::tm` by its fields, as the log sinks do.
- Web, `error: invalid instruction` on `asm("BKPT 10")` in `EASTL/source/assert.cpp`: the EASTL debug break; `EASTL_DEBUG_BREAK_OVERRIDE=__builtin_trap` is defined on the web. The same message on `asm("yield")` or `asm("dmb ish")` means code that uses `eastl::atomic` or `eastl::cpu_pause()`: use `std::atomic` (EASTL's ARM versions are inline assembly, `TaskSystem/CLAUDE.md`, "Browser").
- Web, wasm-ld `--shared-memory is disallowed by <file> because it was not compiled with 'atomics' or 'bulk-memory' features`: an object was compiled without `-pthread` in a threaded build. `cmake/BagiPlatformOptions.cmake` puts it in `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` before the first `add_subdirectory`; a target that overrides those variables, or a build folder configured as single-thread and reused for the threaded one (use the `web-*` preset of the configuration you want: the build folder and the output folder `build/<preset>/bin` are separate per preset), would cause it.
- Web, a threaded start-up that hangs, or `pthread_create` fails with EAGAIN at start-up: the Web Worker pool is smaller than the number of threads that start before `main()` returns to the event loop (`BE_WEB_THREAD_POOL_SIZE` in `cmake/BagiWeb.cmake` must be `TaskManager::kMaxWorkerThreads + 1` or more; `-sPTHREAD_POOL_SIZE_STRICT=2` makes it an error, not a deadlock).
- Web, the smoke fails at (4) with `console error: ...` on a Release page: the engine wrote to stderr (the shell sends stderr to `console.error`; `ConsoleSink` sends only `Error` and above there). A line that is only information must not go to stderr (`CRTDebugHook` did); the Debug page has the one allowed line "Blocking on the main thread is very dangerous" (`--allow-console`).
- Web, the page runs but the loop is on `setTimeout` and not `requestAnimationFrame` (a Debug console warning "rendering without using requestAnimationFrame"): the SDL renderer has no vsync; `SDLRendererBackend` asks for it on the web.
- Web, `cmake --preset web-release` stops with `Could not find toolchain file: .../external/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`: the Emscripten SDK is not installed in this checkout. Run `python CI/scripts/setup_emsdk.py` from the engine root (`python engine/CI/scripts/setup_emsdk.py` in a game), in the background: it downloads about 2 GB. `--check` exits 0 when it is installed. `external/emsdk` empty (submodule not initialised in a worker folder): the script runs `git submodule update --init external/emsdk` itself, in the engine root.

### Gotchas

Engine:

- The engine setup is `cmake/BagiEngine.cmake`. The root `CMakeLists.txt` and the games' own repositories include it. A change there needs a configure, like any `*.cmake`.
- `compile_commands.json` lives in `build/<p>/`. Pass it to `tidy-fix` explicitly when several presets are configured: `--build-dir build/<p>`.
- The option `BE_WITH_EDITOR` (`AUTO` by default: the editor panels are built in Debug and left out of a single-config Release; `ON` / `OFF`; presets `macos-release-editor`, `windows-msvc-release-editor`; `cmake/CLAUDE.md`, "The editor option") is a cache variable, so changing it is a configure (`cmake --preset <p> -DBE_WITH_EDITOR=ON`) and then a rebuild of everything that carries the definition (every engine and game target): sources are chosen at configure time and the definition `BE_WITH_EDITOR` is on every target. A Debug build ignores `OFF` with a configure WARNING. A configure prints `Editor: ENABLED` or `DISABLED`; `grep -c ViewportWidget build/<p>/compile_commands.json` is 0 in a build without the editor. Run `cmake --preset <p>` again without the flag afterwards, or the cache keeps the value.
- **`BE_WITH_EDITOR` inside one preset.** Each preset has its own `build/<preset>/lib`, so Debug and Release never mix. But `ninja` does not notice that a library was built with the other `BE_WITH_EDITOR`: the editor sources are in one and not in the other. After flipping that option in a build folder, delete `build/<preset>/lib/*.a` (on Windows the `lib` folder) before building; a stale `libRuntimeModule.a` / `libBECoreModule.a` alone is not enough. Symptoms of a mix: link errors for editor classes, or crashes and dozens of failures in `BagiEngineTests`.
- **The web build needs the SDK once per checkout.** `external/emsdk` is a submodule at the tag `4.0.23`; `python CI/scripts/setup_emsdk.py` installs and activates that SDK into it (about 2 GB, a few minutes, idempotent: a second run prints that it is installed and does no network access; `--force` installs again). Nothing is written outside the checkout (no `--permanent`, no registry), and `git status` stays clean. The `web-*` presets find it through `${fileDir}` with no `EMSDK` in the environment; they need only `ninja` on PATH (no Visual Studio environment). A worker folder (a git worktree with its own `engine/`) that builds the web installs its own SDK, another 2 GB; one that never builds the web pays nothing. Build the web in one checkout. The default web presets are **threaded** (`-pthread`, a pool of Web Workers, `SDL_PTHREADS=ON`: the page needs cross-origin isolation, `tools/web/CLAUDE.md`); `web-release-single-thread` / `web-debug-single-thread` build the page without threads into `build/web-release-single-thread/bin` (or `web-debug-single-thread`), so the two configurations do not share a `lib/`.
- **The web build, end to end** (run on Windows 11 with emsdk 4.0.23; details and numbers in `tools/web/CLAUDE.md`): `python CI/scripts/setup_emsdk.py` (once per checkout, background), `cmake --preset web-release`, `cmake --build --preset web-release --target BagiMatch3` (background, about 2 minutes from scratch), `python tools/web/package.py build/web-release/bin dist`, `python tools/web/serve.py --root dist` for a person (http://localhost:8080; sends the COOP / COEP headers of a threaded page), and for the check the Playwright venv in `build/web-venv` and `build/web-venv/Scripts/python tools/web/tests/web_smoke.py --dist dist --expect-console "Initialized with \d+ worker threads"` (`bin/python` on macOS and Linux; exit 0 passes, 77 means Playwright or the browser is missing; the recipe and the other runs are in `tools/web/CLAUDE.md`). Build the single-thread page with `web-release-single-thread` into `build/web-release-single-thread/bin` and package it with `--single-thread`. The smoke needs a browser, so it is not a ctest and the loop does not run it.
- The game runs the runtime `ITest`s at startup (`CoreManager::OnApplicationPreInit`) and opens a window, so it is never started to check a build. The gtest executables open no window.

### Tests

Engine: `ctest --preset <p>` runs every gtest in its own process (`BagiEngineTests`, `Match3Tests`) and `BagiMatch3ControlSmoke`, a Python script that starts the game `--headless` and plays it over the control API. The smoke test is skipped (exit code 77) when the executable is not built. A `--target` list without the product (what the argument `tests` builds) builds no game, so add the product to run the smoke test. The Python tools have pytest tests of their own, not part of ctest: `.venv/bin/python -m pytest tools/control/tests tools/perf/tests`.

**Parallel run.** `ctest --preset <p>` runs 8 tests at once (`execution.jobs` 8 in `test-base` of `CMakePresets.json`; the game repositories get it through the included presets). `-j1` runs them one at a time, `-jN` sets another number; `CTEST_PARALLEL_LEVEL` does **not** override a preset. The smoke test takes the `bagi_control_smoke` lock (no other smoke test beside it; unit tests run beside it). Do not start several `ctest`s in one build folder at the same moment right after a build (`gtest_discover_tests` rewrites `<Exe>[1]_tests.cmake` and two writers corrupt it: delete `build/<p>/**/<Exe>*_tests.cmake` and run `ctest -N` once). Rules for a test that runs beside others: `.claude/rules/tests-module.md`.

**Fuzzing** (`tests/Fuzz/CLAUDE.md`). The corpus of every fuzz target is replayed and mutated by `FuzzCorpusTest` in every preset: `ctest --preset <p> -R Fuzz` (`BAGI_FUZZ_ITERATIONS=2000000` makes it longer). Long runs are libFuzzer with ASan + UBSan, which needs Homebrew LLVM (`brew install llvm` once; Apple clang has no libFuzzer runtime). This is the command until the CI entry `no-ci` exists:

```bash
cmake --preset macos-fuzz                                              # never with Homebrew LLVM on PATH: the preset names the compiler
cmake --build --preset macos-fuzz --target BagiFuzzers BagiEngineTests # BagiFuzz<Name> go to build/macos-fuzz/bin
python3 tools/fuzz/run_fuzzers.py --seconds 60                         # every target over a copy of its corpus; exit 1 on a finding, 77 when nothing is built
ctest --preset macos-fuzz -R Fuzz                                      # the corpus test under the sanitizers
.venv/bin/python -m pytest tools/fuzz/tests                            # the run script
```

Findings land in `build/macos-fuzz/fuzz-artifacts/`, never in the repository; the script prints how to minimize one and add it as `fix-<what>`. Running all of `BagiEngineTests` under the sanitizers is not part of this (the entry `sanitizers`).

Timings, `ctest --preset macos-debug` on a warm Debug build, 2026-11-08, Apple M4 Pro (14 cores), **shared with other todo-loop workers** (load average about 5 to 12 before each run, so the numbers move by a few seconds between runs; read them as a factor, not as a benchmark). Wall time of three runs per row, interleaved:

| Set | `-j1` | `-j4` | `-j8` (preset) | `-j14` |
|-----|-------|-------|----------------|--------|
| Engine: `BagiEngineTests` + `Match3Tests` + `BagiMatch3ControlSmoke` (1455 tests) | 86.0 / 86.0 / 80.9 s (min 80.9, median 86.0) | 21.6 / 20.2 / 20.4 s (median 20.4) | 12.2 / 10.3 / 10.3 s (median 10.3) | 8.5 / 7.7 / 7.8 s (median 7.8) |
| Mahjong: `MahjongTests` + `BagiMahjongControlSmoke` (630 tests, a game repository) | 49.7 / 50.1 / 49.7 s (median 49.7) | | 8.1 / 8.4 / 7.7 s (median 8.1) | |

About 6 (Mahjong set) to 8 (engine set) times faster with 8 jobs. 14 jobs (all cores) is a further 25 % on this machine but leaves nothing for other work and does not suit a smaller one, so the preset keeps 8. Stability: `ctest --preset macos-debug --repeat until-fail:10` passed 10 times in a row on the engine set (14550 test runs, 106 s) and on the Mahjong set (6300 runs, 74 s); the serial reference `-j1 --repeat until-fail:3` on the engine set passed 3 times (239 s).

### Local overrides

Engine: `CMakeUserPresets.json` (git-ignored) holds per-machine settings. It may inherit from the shared presets, but it needs its own build preset to appear in CMake Tools:

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "macos-debug-local",
            "inherits": "macos-debug",
            "cacheVariables": {
                "VULKAN_SDK": "/Users/me/VulkanSDK/1.4.363.0/macOS"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "macos-debug-local",
            "inherits": "macos-debug",
            "configurePreset": "macos-debug-local"
        }
    ]
}
```

`VULKAN_SDK` is only needed when `glslc` is not on PATH. ccache needs no preset: the build uses it by itself when it is installed (`ENGINE_CCACHE`, `cmake/BagiCompilerCache.cmake`; `-DENGINE_CCACHE=OFF` switches it off). To share hits between checkouts (the main one and the worker folders), set `ENGINE_CCACHE_BASE_DIR` to a folder that holds all of them; paths in `__FILE__`, diagnostics and debug info then become relative to the build folder.

On Windows the same with the default Windows preset: `"inherits": "windows-clang-debug"` (its own `build/<name>/` comes from `base`), configured and built through `python CI/scripts/vs_env.py` like the shared preset (the `build` skill, step 1).

### VS Code

Engine, for explaining to the user. With the CMake Tools extension (`cmake.useCMakePresets: always` is set in the workspace):

- **CMake: Select Configure Preset** and **CMake: Select Build Preset** pick the preset (also in the status bar).
- **CMake: Configure** / **CMake: Delete Cache and Reconfigure** are the configure step of the `build` skill. **Build** (F7) is the build step.
- F5 runs a debug configuration; its `program` is `${command:cmake.launchTargetPath}` and it builds first via the `CMake: build` task.

## Format

Fields the `clang-format` skill reads (the `strip-pch-includes` skill uses `Python` and `Format scope` too). A game fills every row.

| Field | Meaning | Engine value |
|-------|---------|--------------|
| Python | the interpreter `<py>`. The two scripts use only the standard library, so no venv is needed; `uv run python` also works. A repository may name its venv interpreter instead (a game on Windows: `engine\.venv\Scripts\python`) | `python3` (Windows: `python`) |
| `<engine>` and `<R>` | the values of the two terms for the default run | `.` and `.` (so `--root .`, which is also the scripts' default here) |
| Format scope | the folders `--all` covers (also for `strip-pch-includes`) | `src/`, `config/`, `games/`. The engine's own `tests/` is **not** covered by `--all` (`--changed` formats it) |
| Ignore file | the `.clang-format-ignore` in the root of `<R>`: required, one path or prefix per line, `#` comments, not merged with a default | `external/`, `build/`, `.venv/`, the vendored modules under `src/Modules/` (EABase, EnTT, fmt, imgui, nlohmann, pugixml, RmlUi, tracy, Vulkan), `src/Modules/BECore/pch.h`, `src/Modules/BECore/Assert/StackTrace.cpp`. `BECore` itself is project code and is formatted |
| `--root` | never left out where it matters | a default in the engine repository; **required in a game**, because without it the scripts format the engine |
| Launcher | whether a GUI runs the same scripts | yes, see "Launcher" |

The commands, written with the engine values (run from the repository root):

```bash
python3 CI/scripts/format_sources.py --changed --root .
python3 CI/scripts/format_sources.py --all --root .
```

For engine work done from a game checkout (`<R>` = `engine`), run them from `engine/` with these same values and the rules file of `engine/`.

`clang-format` lookup (the script prints the binary and the version it chose): `PATH`, `LIBCLANG_PATH`, `.venv` of `<R>` and of the engine, then on macOS Homebrew LLVM (keg-only, not on `PATH`) and Xcode. `--changed` skips untracked files (it reads `git diff --name-only HEAD`), so a new file is formatted by a direct call with `--style=file` (the `clang-format` skill shows the line). Homebrew LLVM goes on `PATH` for that one command only, never for `cmake --preset`. The todo-developer agent (step 5) uses the same commands.

## Strip PCH includes

Fields the `strip-pch-includes` skill reads (it shares `Python`, `Format scope` and the ignore file with "Format"). A game fills every row.

| Field | Meaning | Engine value |
|-------|---------|--------------|
| PCH | the header whose includes are dropped from sources; always the engine's, relative to `<engine>` | `src/Modules/BECore/pch.h` (the script's default) |
| Strip example dir | a bounded folder of `<R>` for the dry run and the first real pass | `src/Widgets` (also `src/Modules/BECore`, which the script does not exclude) |
| After a pass | what to build and run to validate the scope | `cmake --build --preset <p>` (the `build` skill), then `ctest --preset <p>`; `BagiEngineTests` for engine code, `Match3Tests` for code under `games/Match3` |

The commands, written with the engine values:

```bash
python3 CI/scripts/strip_pch_includes.py --dry-run --changed --root .
python3 CI/scripts/strip_pch_includes.py --dry-run --all --root . --dir src/Widgets
python3 CI/scripts/strip_pch_includes.py --all --root . --dir src/Widgets
```

The order stays: dry run, a bounded folder, the build and the tests, then a wider scope.

## Launcher

Engine only. A game has no launcher: it fills the heading with "none".

The engine launcher (`!start.cmd` on Windows, `!start.command` on macOS) is a GUI over the same scripts, for a person at the keyboard (an agent runs the commands above). **Quick Actions** has two buttons:

- **Clang-Format**: scope **Only changed files (git)** or **All files (src/, config/)**, then **Run**. The output goes to the launcher log.
- **Strip PCH includes**: **Limit to folder** (**All (src/, config/)** or **Choose folder...**, for example `src/Widgets` or `src/Modules/BECore`), **Scope** (**Only changed files (git)** or **All files**), the checkbox **Dry run** (report only), then **OK**. The output goes to the launcher log.
