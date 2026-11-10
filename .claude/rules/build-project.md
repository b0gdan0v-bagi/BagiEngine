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
| Product | the executable (target name; the file is `build/{Clang\|MSVC}{macOS\|Windows}/<name>[.exe]`) | `BagiMatch3` |
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

### Gotchas

Engine:

- The engine setup is `cmake/BagiEngine.cmake`. The root `CMakeLists.txt` and the games' own repositories include it. A change there needs a configure, like any `*.cmake`.
- `compile_commands.json` lives in `build/<p>/`. Pass it to `tidy-fix` explicitly when several presets are configured: `--build-dir build/<p>`.
- The option `BE_WITH_EDITOR` (`AUTO` by default: the editor panels are built in Debug and left out of a single-config Release; `ON` / `OFF`; presets `macos-release-editor`, `windows-msvc-release-editor`; `cmake/CLAUDE.md`, "The editor option") is a cache variable, so changing it is a configure (`cmake --preset <p> -DBE_WITH_EDITOR=ON`) and then a rebuild of everything that carries the definition (every engine and game target): sources are chosen at configure time and the definition `BE_WITH_EDITOR` is on every target. A Debug build ignores `OFF` with a configure WARNING. A configure prints `Editor: ENABLED` or `DISABLED`; `grep -c ViewportWidget build/<p>/compile_commands.json` is 0 in a build without the editor. Run `cmake --preset <p>` again without the flag afterwards, or the cache keeps the value.
- **Presets share `build/ClangmacOS/lib` (and the executables).** The output folder does not depend on the preset, so Debug, Release and release-editor overwrite each other's libraries, and `ninja` does not notice that a library was built with the other `BE_WITH_EDITOR`: the editor sources are in one and not in the other. When switching between them, delete every `build/ClangmacOS/lib/*.a` (on Windows every `build/ClangWindows/lib/*.lib`, or `build/MSVCWindows/lib` for the MSVC presets) before building; a stale `libRuntimeModule.a` / `libBECoreModule.a` alone is not enough. Symptoms of a mix: link errors for editor classes, or crashes and dozens of failures in `BagiEngineTests`.
- The game runs the runtime `ITest`s at startup (`CoreManager::OnApplicationPreInit`) and opens a window, so it is never started to check a build. The gtest executables open no window.

### Tests

Engine: `ctest --preset <p>` runs every gtest in its own process (`BagiEngineTests`, `Match3Tests`) and `BagiMatch3ControlSmoke`, a Python script that starts the game `--headless` and plays it over the control API. The smoke test is skipped (exit code 77) when the executable is not built. A `--target` list without the product (what the argument `tests` builds) builds no game, so add the product to run the smoke test. The Python tools have pytest tests of their own, not part of ctest: `.venv/bin/python -m pytest tools/control/tests`.

**Parallel run.** `ctest --preset <p>` runs 8 tests at once (`execution.jobs` 8 in `test-base` of `CMakePresets.json`; the game repositories get it through the included presets). `-j1` runs them one at a time, `-jN` sets another number; `CTEST_PARALLEL_LEVEL` does **not** override a preset. The smoke test takes the `bagi_control_smoke` lock (no other smoke test beside it; unit tests run beside it). Do not start several `ctest`s in one build folder at the same moment right after a build (`gtest_discover_tests` rewrites `<Exe>[1]_tests.cmake` and two writers corrupt it: delete `build/<p>/**/<Exe>*_tests.cmake` and run `ctest -N` once). Rules for a test that runs beside others: `.claude/rules/tests-module.md`.

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
            "name": "macos-debug-ccache",
            "inherits": "macos-debug",
            "cacheVariables": {
                "CMAKE_CXX_COMPILER_LAUNCHER": "ccache",
                "VULKAN_SDK": "/Users/me/VulkanSDK/1.4.363.0/macOS"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "macos-debug-ccache",
            "inherits": "macos-debug",
            "configurePreset": "macos-debug-ccache"
        }
    ]
}
```

`VULKAN_SDK` is only needed when `glslc` is not on PATH.

On Windows the same with the default Windows preset: `"inherits": "windows-clang-debug"`, `CMAKE_CXX_COMPILER_LAUNCHER` and `CMAKE_C_COMPILER_LAUNCHER` `ccache` (its own `build/<name>/` comes from `base`), configured and built through `python CI/scripts/vs_env.py` like the shared preset (the `build` skill, step 1).

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
| Ignore file | the `.clang-format-ignore` in the root of `<R>`: required, one path or prefix per line, `#` comments, not merged with a default | `external/`, `build/`, `.venv/`, the vendored modules under `src/Modules/` (EABase, EnTT, fmt, imgui, nlohmann, pugixml, RmlUi, Vulkan), `src/Modules/BECore/pch.h`, `src/Modules/BECore/Assert/StackTrace.cpp`. `BECore` itself is project code and is formatted |
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
