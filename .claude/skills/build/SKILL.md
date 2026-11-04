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
| `windows-msvc-debug`, `windows-msvc-release` | Windows | MSVC (`cl`) + Ninja |
| `windows-clang-debug`, `windows-clang-release` | Windows | Clang + Ninja |
| `windows-vs2022-{debug,release}` | Windows | Visual Studio 2022, MSVC |
| `windows-vs2022-clangcl-{debug,release}` | Windows | Visual Studio 2022, ClangCL |

Presets for other hosts are hidden by `condition`, so `cmake --list-presets` shows only what works here.

## Workflow

Run everything from the root of `<R>`.

1. **Pick the preset.**
   - An explicit preset name wins.
   - Otherwise by host (`uname -s`): Darwin -> `macos-<type>`. Windows -> `windows-msvc-<type>` when `cl` is on PATH (Developer prompt), else `windows-vs2022-<type>`: Ninja + MSVC presets only get the Visual Studio environment inside CMake Tools, not from a plain shell.
   - Check it against `cmake --list-presets=build`. If it is missing (for example on Linux, where no presets exist yet), stop and show the list. Do not invent a preset or fall back to `cmake -B build`.

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

- Executables: `build/{Clang|MSVC}{macOS|Windows}/<product>[.exe]` and the test executables (the engine's `cmake/BagiEngine.cmake` sets the output directory from compiler + platform, **not** from the preset). Debug and Release Ninja presets therefore **overwrite each other's executable**; the last build wins. Visual Studio generators add a `Debug/` or `Release/` subfolder.
- Do not use `cmake -B build ...` (the old flat `build/` directory) and do not edit anything under `build/`.
- Release presets turn `ENGINE_ENABLE_ASSERTS` and `ENGINE_ENABLE_EXPECTS` off; Debug keeps them on. The Visual Studio presets are multi-config and leave both on.
- Exceptions are disabled (`ENGINE_NO_EXCEPTIONS`). C++23 is required.
- Do not run the game after a build unless asked. It opens a window. To launch it, use the `run` skill. The gtest executables open no window, but still run them only when asked (what `ctest` runs here: "Tests" in the rules file).
- After building C++ changes, remind the user about the `clang-format` and `strip-pch-includes` skills. Do not run them unasked.
- Everything else that holds only in one repository (extra gotchas, per-machine presets in `CMakeUserPresets.json`, the editor setup) is in the rules file.
