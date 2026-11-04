# Build, format and include facts of this repository

Read by the skills `build`, `clang-format` and `strip-pch-includes`. The three skills are the same text in the engine repository and in a game that carries the engine as the submodule `engine/` (symlinks into `engine/.claude/skills/`); what differs between the repositories is here. A game has a file of the same name with the same headings and fields: this file is its template. The todo areas are in `todo-project.md`, not here.

## Terms

| Term | Meaning |
|------|---------|
| `<engine>` | `.` in the engine repository, `engine` in a game: the folder with `CI/`, `cmake/` and `.venv`. The same term as in `todo-loop` ("Areas and the repository rules"). |
| `<R>` | the repository to build or format. `.` by default (the repository the session runs in). `engine` only for engine work done from a game checkout; then the skills run from `engine/` and read `engine/.claude/rules/build-project.md`, so `<engine>` is `.` there. Every path below is relative to `<R>`. |
| `<product>` | the game executable of `<R>`, from the field `Product`. |
| `<tests>` | the test targets of `<R>`, from the field `Tests`. |
| `<p>` | the preset the skill picked. |

## Build

Fields the `build` skill reads. A game fills every row.

| Field | Meaning | Engine value |
|-------|---------|--------------|
| Product | the executable (target name; the file is `build/{Clang\|MSVC}{macOS\|Windows}/<name>[.exe]`) | `BagiMatch3` |
| Tests | the gtest targets; the argument `tests` builds exactly these (`--target <tests>`) | `BagiEngineTests Match3Tests` |
| Default build | what `cmake --build --preset <p>` with no `--target` builds | every target: the product and the test executables |
| Source paths | the paths of `git status --porcelain -- <paths>` (a configure trigger) and of the `BE_CLASS` / `BE_EVENT` / `BE_REFLECT_FIELD` search | `src tests games` |
| Extra configure triggers | cases besides the common ones (below) that need `cmake --preset <p>` | none |
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
- The game runs the runtime `ITest`s at startup (`CoreManager::OnApplicationPreInit`) and opens a window, so it is never started to check a build. The gtest executables open no window.

### Tests

Engine: `ctest --preset <p>` runs every gtest in its own process (`BagiEngineTests`, `Match3Tests`) and `BagiMatch3ControlSmoke`, a Python script that starts the game `--headless` and plays it over the control API. The smoke test is skipped (exit code 77) when the executable is not built. A `--target` list without the product (what the argument `tests` builds) builds no game, so add the product to run the smoke test. The Python tools have pytest tests of their own, not part of ctest: `.venv/bin/python -m pytest tools/control/tests`.

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

### VS Code

Engine, for explaining to the user. With the CMake Tools extension (`cmake.useCMakePresets: always` is set in the workspace):

- **CMake: Select Configure Preset** and **CMake: Select Build Preset** pick the preset (also in the status bar).
- **CMake: Configure** / **CMake: Delete Cache and Reconfigure** are the configure step of the `build` skill. **Build** (F7) is the build step.
- F5 runs a debug configuration; its `program` is `${command:cmake.launchTargetPath}` and it builds first via the `CMake: build` task.

## Format

Filled by the next change (the `clang-format` skill).

## Strip PCH includes

Filled by the next change (the `strip-pch-includes` skill).

## Launcher

Filled by the next change.
