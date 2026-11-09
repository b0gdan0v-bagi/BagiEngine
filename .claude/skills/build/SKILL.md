---
name: build
description: Builds the game of this repository through CMakePresets.json (the same presets VS Code CMake Tools uses). Picks the preset for this OS, runs configure only when needed, builds, and reports errors with file:line. Use when the user asks to build, compile, rebuild, configure, switch Debug/Release, or says the project does not build.
---

# Build

Builds the game executable and the gtest executables with `CMakePresets.json` from the repository root. Each preset builds into its own `build/<preset>/` directory.

## Rules file first

Read `.claude/rules/build-project.md` of the repository you run in before anything else. This skill is the same text in the engine repository and in a game that carries the engine as the submodule `engine/`; the rules file says what differs. Terms (defined there): `<product>` (the game executable), `<tests>` (the test targets), `<R>` (the repository to build: `.` by default, `engine` only for engine work done from a game checkout, then run everything from `engine/` with the rules file of `engine/`), `<engine>` (`.` or `engine`: the folder with `CI/` and `cmake/`). Paths below are relative to `<R>`.

## Arguments (all optional)

| Argument | Effect |
|----------|--------|
| `debug` / `release` | Build type. Default: `debug`. |
| `clean` | Add `--clean-first` to the build. |
| `configure` | Force a configure step. |
| `tests` | Build only `<tests>` (`--target <tests>`) instead of everything. Run the tests afterwards only when asked: `ctest --preset <p>`. |
| `<preset name>` | Use this preset as-is; skip auto-selection. |

## Presets

The presets are the engine's. A game includes them through its own `CMakePresets.json`, so the table holds in both.

| Preset (configure + build + test) | Host | Toolchain |
|----------------------------|------|-----------|
| `macos-debug`, `macos-release` | macOS | Clang + Ninja |
| `macos-rmlui-debug`, `macos-rmlui-release` | macOS | Clang + Ninja, with `ENGINE_UI_RMLUI=ON` (RmlUi host and its tests, which are part of the engine's own test executable). The executables land in the same `build/ClangmacOS/` as the plain presets' |
| `windows-clang-debug`, `windows-clang-release` | Windows | Clang + Ninja: **the default on Windows** |
| `windows-msvc-debug`, `windows-msvc-release` | Windows | MSVC (`cl`) + Ninja: only by explicit name |
| `macos-release-editor`, `windows-msvc-release-editor` | macOS / Windows | The same toolchains as the plain Release presets, with `BE_WITH_EDITOR=ON` (the editor panels and their tests in a Release build; a plain Release build leaves them out) |
| `windows-vs2022-{debug,release}` | Windows | Visual Studio 2022, MSVC: only by explicit name |
| `windows-vs2022-clangcl-{debug,release}` | Windows | Visual Studio 2022, ClangCL |
| `web-debug`, `web-release` (threaded, the default), `web-debug-single-thread`, `web-release-single-thread` | any | Emscripten + Ninja, the browser build (`BagiMatch3.html`; the single-thread presets write to `build/ClangWebSingleThread`, the others to `build/ClangWeb`): only by explicit name, build only (no test preset, no editor). No environment variable: the SDK is the submodule `external/emsdk`, installed once per checkout by `python <engine>/CI/scripts/setup_emsdk.py` (`--check`: is it installed; about 2 GB, run it in the background). No Visual Studio environment is needed on Windows, only `ninja` on PATH: `cmake --preset web-release`, then `cmake --build --preset web-release --target <product>` |

Presets for other hosts are hidden by `condition`, so `cmake --list-presets` shows only what works here.

## Workflow

Run everything from the root of `<R>`.

1. **Pick the preset.**
   - An explicit preset name wins.
   - Otherwise by host (`uname -s`): Darwin -> `macos-<type>`. Windows (`MINGW*`, `MSYS*`, `CYGWIN*`, or PowerShell) -> `windows-clang-<type>`. The MSVC presets (`windows-msvc-*`, `windows-vs2022-*`) are fallbacks, taken only by explicit name: MSVC's debug information (`/Zi`, one `mspdbsrv` for every build on the machine, a PCH shared by all modules) breaks parallel builds now and then with `LNK1103` / `LNK4020 ... pdb is corrupted` / `C1090`, and a second run passes; clang has no `cl` PDB and no `mspdbsrv`.
   - Check it against `cmake --list-presets=build`. If it is missing (for example on Linux, where no presets exist yet), stop and show the list. Do not invent a preset or fall back to `cmake -B build`.
   - **Windows Ninja presets (`windows-clang-*`, `windows-msvc-*`) need the x64 Visual Studio environment** for configure **and** build (the resource compiler `rc` and its SDK includes, the archiver `lib`, for MSVC `cl` and `link`). They do not load it (`vs-env` is the `external` strategy; CMake Tools loads it, a plain shell does not). Run every `cmake --preset` and `cmake --build --preset` of these presets through `<py> <engine>/CI/scripts/vs_env.py` (it finds Visual Studio with vswhere, runs `vcvarsall.bat x64`, extends `PATH` behind the caller's and runs the command; inside a developer prompt it just runs it): `python engine/CI/scripts/vs_env.py cmake --build --preset windows-clang-debug` in a game, `python CI/scripts/vs_env.py ...` in the engine. Without it configure stops with `No CMAKE_RC_COMPILER could be found` and the build with `CMAKE_AR-NOTFOUND`. `ctest` needs no environment. The `windows-vs2022-*` presets need none either.

2. **Decide whether to configure** (`cmake --preset <p>`). Configure if any of these holds, or when unsure:
   - `build/<p>/CMakeCache.txt` does not exist, or the `configure` argument was given.
   - `git status --porcelain -- <source paths>` shows added, deleted, renamed or untracked (`??`) files. Sources are collected with `GLOB_RECURSE` and no `CONFIGURE_DEPENDS`. The source paths are a field of the rules file.
   - `CMakePresets.json`, `CMakeUserPresets.json` or any `*.cmake` changed. Edits to `CMakeLists.txt` are picked up by Ninja itself.
   - A changed or new file under the source paths contains `BE_CLASS`, `BE_EVENT` or `BE_REFLECT_FIELD` (`git diff --name-only HEAD -- <source paths>`, plus untracked files, then `grep -l`). **Reflection code is generated only at configure time** (`add_reflection_target` / `be_add_game_reflection` -> `execute_process`), into `build/<p>/Generated/`. A plain build leaves stale `*.gen.hpp`.
   - Any case the rules file lists under "Extra configure triggers".

3. **Build:** `cmake --build --preset <p>` (add `--clean-first` for `clean`; `--target <tests>` for `tests`; `--verbose` to see compiler commands). Ninja already uses all cores. The field "Default build" of the rules file says what the preset builds without `--target`.
   - A first build in a new `build/<p>/` is full (SDL, SDL_image, ImGui, ...) and takes minutes. Run it in the background with output redirected to a log file in the scratchpad and wait for the completion notification. Do not poll, and do not set a timeout that would cut the build off.

4. **On failure,** read the log and report the first real errors:
   `grep -E "error:|FAILED:|CMake Error|ld: " <log> | head -20`.
   - Say which phase failed: configure (`Meta-generator failed`, `glslc not found`, missing submodule or Python package), compile, or link. The rules file has the "Extra failure hints" of the repository; give the matching fix.
   - Show each error as `path/to/File.cpp:line` with a line or two of context.
   - Fix code only when the user asked to "build and fix". Then rebuild, at most 3 iterations, and report what is left. Warnings: report only a count unless asked.

5. **Report.** Claim success only when the exit code is 0. State the preset, whether it was configure + build or build only, and the path of `<product>`. Say plainly if a step was skipped or failed.

## Output location and gotchas

- Executables: `build/{Clang|MSVC}{macOS|Windows}/<product>[.exe]` and the test executables (the engine's `cmake/BagiEngine.cmake` sets the output directory from compiler + platform, **not** from the preset): `build/ClangWindows/` for the default Windows presets, `build/MSVCWindows/` for the MSVC ones. Debug and Release Ninja presets therefore **overwrite each other's executable**; the last build wins. The libraries in `build/{Clang|MSVC}{macOS|Windows}/lib` clash the same way, and `ninja` does not see that one was built with another `BE_WITH_EDITOR`: when switching between Debug, Release and the `-editor` presets, delete every `lib/*.a` (on Windows the `lib` folder) before building (the rules file, "Gotchas", where it applies). Visual Studio generators add a `Debug/` or `Release/` subfolder.
- Do not use `cmake -B build ...` (the old flat `build/` directory) and do not edit anything under `build/`.
- Release presets turn `ENGINE_ENABLE_ASSERTS` and `ENGINE_ENABLE_EXPECTS` off; Debug keeps them on. The Visual Studio presets are multi-config and leave both on.
- Exceptions are disabled (`ENGINE_NO_EXCEPTIONS`). C++23 is required.
- Do not run the game after a build unless asked. It opens a window. To launch it, use the `run` skill. The gtest executables open no window, but still run them only when asked (what `ctest` runs here: "Tests" in the rules file).
- After building C++ changes, remind the user about the `clang-format` and `strip-pch-includes` skills. Do not run them unasked.
- Everything else that holds only in one repository (extra gotchas, per-machine presets in `CMakeUserPresets.json`, the editor setup) is in the rules file.
