---
name: build
description: Builds BagiEngine through CMakePresets.json (the same presets VS Code CMake Tools uses). Picks the preset for this OS, runs configure only when needed, builds, and reports errors with file:line. Use when the user asks to build, compile, rebuild, configure, switch Debug/Release, or says the project does not build.
---

# Build (BagiEngine)

Builds the `MyGame` target (or the `BagiEngineTests` gtest executable, see `tests`) with `CMakePresets.json` from the project root. Each preset builds into its own `build/<preset>/` directory.

## Arguments (all optional)

| Argument | Effect |
|----------|--------|
| `debug` / `release` | Build type. Default: `debug`. |
| `clean` | Add `--clean-first` to the build. |
| `configure` | Force a configure step. |
| `tests` | Build the `BagiEngineTests` target (`--target BagiEngineTests`, which replaces the preset's `MyGame`) instead of the game. Run the tests afterwards only when asked: `ctest --preset <p>`. |
| `<preset name>` | Use this preset as-is; skip auto-selection. |

## Presets

| Preset (configure + build + test) | Host | Toolchain |
|----------------------------|------|-----------|
| `macos-debug`, `macos-release` | macOS | Clang + Ninja |
| `windows-msvc-debug`, `windows-msvc-release` | Windows | MSVC (`cl`) + Ninja |
| `windows-clang-debug`, `windows-clang-release` | Windows | Clang + Ninja |
| `windows-vs2022-{debug,release}` | Windows | Visual Studio 2022, MSVC |
| `windows-vs2022-clangcl-{debug,release}` | Windows | Visual Studio 2022, ClangCL |

Presets for other hosts are hidden by `condition`, so `cmake --list-presets` shows only what works here.

## Workflow

Run everything from the project root.

1. **Pick the preset.**
   - An explicit preset name wins.
   - Otherwise by host (`uname -s`): Darwin -> `macos-<type>`. Windows -> `windows-msvc-<type>` when `cl` is on PATH (Developer prompt), else `windows-vs2022-<type>`: Ninja + MSVC presets only get the Visual Studio environment inside CMake Tools, not from a plain shell.
   - Check it against `cmake --list-presets=build`. If it is missing (for example on Linux, where no presets exist yet), stop and show the list. Do not invent a preset or fall back to `cmake -B build`.

2. **Decide whether to configure** (`cmake --preset <p>`). Configure if any of these holds, or when unsure:
   - `build/<p>/CMakeCache.txt` does not exist, or the `configure` argument was given.
   - `git status --porcelain -- src tests` shows added, deleted, renamed or untracked (`??`) files. Sources are collected with `GLOB_RECURSE` and no `CONFIGURE_DEPENDS`.
   - `CMakePresets.json`, `CMakeUserPresets.json` or any `*.cmake` changed. Edits to `CMakeLists.txt` are picked up by Ninja itself.
   - A changed or new file under `src/` or `tests/` contains `BE_CLASS`, `BE_EVENT` or `BE_REFLECT_FIELD` (`git diff --name-only HEAD -- src tests`, plus untracked files, then `grep -l`). **Reflection code is generated only at configure time** (`add_reflection_target` -> `execute_process`), into `build/<p>/Generated/`. A plain build leaves stale `*.gen.hpp`.

3. **Build:** `cmake --build --preset <p>` (add `--clean-first` for `clean`; `--verbose` to see compiler commands). Ninja already uses all cores.
   - A first build in a new `build/<p>/` is full (SDL, SDL_image, ImGui, ...) and takes minutes. Run it in the background with output redirected to a log file in the scratchpad and wait for the completion notification. Do not poll, and do not set a timeout that would cut the build off.

4. **On failure,** read the log and report the first real errors:
   `grep -E "error:|FAILED:|CMake Error|ld: " <log> | head -20`.
   - Say which phase failed: configure (`Meta-generator failed`, `glslc not found`, missing submodule), compile, or link.
   - Show each error as `path/to/File.cpp:line` with a line or two of context.
   - Fix code only when the user asked to "build and fix". Then rebuild, at most 3 iterations, and report what is left. Warnings: report only a count unless asked.

5. **Report.** Claim success only when the exit code is 0. State the preset, whether it was configure + build or build only, and the executable path. Say plainly if a step was skipped or failed.

## Output location and gotchas

- Executable: `build/{Clang|MSVC}{macOS|Windows}/MyGame[.exe]` (`CmakeLists.txt` sets the output directory from compiler + platform, **not** from the preset). Debug and Release Ninja presets therefore **overwrite each other's executable**; the last build wins. Visual Studio generators add a `Debug/` or `Release/` subfolder.
- `compile_commands.json` lives in `build/<p>/`. Pass it to `tidy-fix` explicitly when several presets are configured: `--build-dir build/<p>`.
- Do not use `cmake -B build ...` (the old flat `build/` directory) and do not edit anything under `build/`.
- Release presets turn `ENGINE_ENABLE_ASSERTS` and `ENGINE_ENABLE_EXPECTS` off; Debug keeps them on. The Visual Studio presets are multi-config and leave both on.
- Exceptions are disabled (`ENGINE_NO_EXCEPTIONS`). C++23 is required.
- The root file is spelled `CmakeLists.txt` (lowercase `m`). It works on macOS and Windows only because their file systems ignore case.
- Do not run the game after a build unless asked. It opens a window and runs the runtime `ITest`s at startup (`CoreManager::OnApplicationPreInit`). To launch it, use the `run` skill. `BagiEngineTests` opens no window, but still run it only when asked.
- After building C++ changes, remind the user about the `clang-format` and `strip-pch-includes` skills (CLAUDE.md). Do not run them unasked.

## Local overrides

`CMakeUserPresets.json` (git-ignored) holds per-machine settings. It may inherit from the shared presets, but it needs its own build preset to appear in CMake Tools:

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

## VS Code (for explaining to the user)

With the CMake Tools extension (`cmake.useCMakePresets: always` is set in the workspace):

- **CMake: Select Configure Preset** and **CMake: Select Build Preset** pick the preset (also in the status bar).
- **CMake: Configure** / **CMake: Delete Cache and Reconfigure** correspond to workflow step 2. **Build** (F7) is step 3.
- F5 runs a debug configuration; its `program` is `${command:cmake.launchTargetPath}` and it builds first via the `CMake: build` task.
