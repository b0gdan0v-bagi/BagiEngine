# BagiEngine

A C++23 game engine on a scene graph of `SceneNode` + components. It runs on SDL3 with an SDL or a Vulkan renderer, on desktop and in the browser (Emscripten).

## What it has

- **A scene graph with components.** A scene is a tree of nodes with components, all reflected and loaded from an XML scene file that the editor writes back (`src/Modules/BECore/Scene/CLAUDE.md`).
- **Its own in-game UI**, adaptive to the window size, the aspect and the safe area, with a validator that checks every screen on a matrix of devices (`src/Modules/BECore/UI/CLAUDE.md`).
- **An editor** on desktop and in the browser (`BE_WITH_EDITOR`, the `web-editor` preset: `cmake/CLAUDE.md`).
- **Reflection and hot reload.** `BE_CLASS` and `BE_REFLECT_FIELD` generate the serialization; a changed config file is read again while the game runs (`src/Modules/BECore/Config/CLAUDE.md`).
- **Tweens and timelines, particles, audio and saves** in the core (`Animation`, `Particles`, `Audio` and `Save` under `src/Modules/BECore/`, each with its own `CLAUDE.md`).
- **Online rooms** with a headless room server over WebSocket (`src/Modules/RoomServer/CLAUDE.md`).
- **A control API** over WebSocket with Python tools, to drive a running game from a script or a model (`tools/control/CLAUDE.md`).
- **Development with coding agents.** The contracts of every module are written in the `CLAUDE.md` files next to the code.

## Principles

- **Zero allocations in a steady frame.** A phone and the browser pay for every allocation in frame time and battery, so a frame in which nothing changed makes nothing new; the profiler counts allocations (`src/Modules/BECore/Profiler/CLAUDE.md`).
- **The engine knows no game.** A game is a repository of its own that carries the engine as the submodule `engine/` (`NEW_GAME.md`).
- **Data apart from code.** Scenes, UI, text and settings are XML under `config/` and `assets/`; a game's file of the same name wins, and a release build reads the cooked `.bin` files (`src/Modules/BECore/Cook/CLAUDE.md`, `src/Modules/BECore/FileSystem/CLAUDE.md`).
- **Content has a version.** A change of what a content file may say comes with a migration step that brings a game's files up to date (`tools/migrate/CLAUDE.md`).
- **Everything a game reflects is reachable from outside.** The access code is generated from `BE_CLASS` / `BE_REFLECT_FIELD`: every reflected field under a root such as `game` is read and written by name and every reflected method is called over the control API (`get_property`, `set_property`, `call_method`, `schema`), so a new field needs no glue; the UI is driven by real clicks (`ui_click`), as a player would (`src/Modules/BECore/Control/CLAUDE.md`, `src/Modules/BECore/Reflection/CLAUDE.md`, `.claude/rules/ui-control-api.md`).
- **Headless and deterministic.** The game and the editor run with no window (`--headless`, a software renderer that still takes screenshots), on a manual clock that moves only on `step`, in any window size, density and safe area (`set_window`). Smoke tests, scenarios with reference pictures and coding agents drive it the same way (`tools/control/CLAUDE.md`, `.claude/rules/control-window-api.md`).
- **Data errors are visible.** An unknown key, a missing required field or a value out of range in a config is a warning in the log (an error with `--strict-config`), never silence; the release cook fails on any such finding, so a release is built from clean files (`src/Modules/BECore/Config/CLAUDE.md`, `src/Modules/BECore/Cook/CLAUDE.md`).

## Build it

You need CMake 3.31 or newer, Ninja, Clang, Git, Python 3 with Jinja2 (the reflection code is generated at configure) and `glslc` from the Vulkan SDK, on the `PATH` or in `VULKAN_SDK`. On Windows also Visual Studio with the C++ tools.

```bash
git clone --recurse-submodules https://github.com/b0gdan0v-bagi/BagiEngine.git
cd BagiEngine
```

After a plain clone run `git submodule update --init --recursive`.

Configure fails without Jinja2. `CI/setup_venv.sh` (Windows: `CI\setup_venv.bat`; needs `uv`) makes `.venv`, which configure uses on its own; or run `python3 -m pip install jinja2`.

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
build/macos-debug/bin/BagiDemo              # the demo scene
build/macos-debug/bin/BagiDemo --editor     # the editor on it
```

Windows, from the repository root:

```bat
python CI/scripts/vs_env.py cmake --preset windows-clang-debug
python CI/scripts/vs_env.py cmake --build --preset windows-clang-debug
build\windows-clang-debug\bin\BagiDemo.exe
```

Tests: `ctest --preset macos-debug` (or `windows-clang-debug`). Every preset: `cmake --list-presets`; the presets, the editor option and the browser builds are described in `cmake/CLAUDE.md`.

## More

- The demo in the browser: https://engine.bagiz.dev/demo/
- A guide for people, one task per page: [docs/guide/README.md](docs/guide/README.md).
- A game of your own: [NEW_GAME.md](NEW_GAME.md).
- The architecture and the contracts of every module: [CLAUDE.md](CLAUDE.md).
- Changes: [CHANGELOG.md](CHANGELOG.md).
- Licence: Beerware ([LICENSE](LICENSE)); the libraries it carries are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
