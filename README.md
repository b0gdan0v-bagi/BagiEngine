# BagiEngine

A C++23 game engine on a scene graph of `SceneNode` + components. It runs on SDL3 with an SDL or a Vulkan renderer, on desktop and in the browser (Emscripten).

## What it has

- **A scene graph with components.** A scene is a tree of nodes with components, all reflected and loaded from an XML scene file that the editor writes back (`src/Modules/BECore/Scene/CLAUDE.md`).
- **Its own in-game UI**, adaptive to the window size, the aspect and the safe area, with a validator that checks every screen on a matrix of devices (`src/Modules/BECore/UI/CLAUDE.md`).
- **An editor** on desktop and in the browser (`BE_WITH_EDITOR`, the `web-editor` preset: `cmake/CLAUDE.md`).
- **Reflection and hot reload.** `BE_CLASS` and `BE_REFLECT_FIELD` generate the serialization; a changed config file is read again while the game runs (`src/Modules/BECore/Config/CLAUDE.md`).
- **Tweens and timelines, particles, audio and saves** in the core (`src/Modules/BECore/{Animation,Particles,Audio,Save}/`).
- **Online rooms** with a headless room server over WebSocket (`src/Modules/RoomServer/CLAUDE.md`).
- **A control API** over WebSocket with Python tools, to drive a running game from a script or a model (`tools/control/CLAUDE.md`).
- **Development with coding agents.** The contracts of every module are written in the `CLAUDE.md` files next to the code.

## Principles

- **Zero allocations in a steady frame.** A phone and the browser pay for every allocation in frame time and battery, so a frame in which nothing changed makes nothing new; the profiler counts allocations (`src/Modules/BECore/Profiler/CLAUDE.md`).
- **The engine knows no game.** A game is a repository of its own that carries the engine as the submodule `engine/` (`NEW_GAME.md`).
- **Data apart from code.** Scenes, UI, text and settings are XML under `config/` and `assets/`; a game's file of the same name wins, and a release build reads the cooked `.bin` files (`src/Modules/BECore/Cook/CLAUDE.md`, `src/Modules/BECore/FileSystem/CLAUDE.md`).
- **Content has a version.** A change of what a content file may say comes with a migration step that brings a game's files up to date (`tools/migrate/CLAUDE.md`).

## Build it

You need CMake 3.31 or newer, Ninja, Clang, Git, Python 3 (the reflection code is generated at configure) and `glslc` from the Vulkan SDK, on the `PATH` or in `VULKAN_SDK`. On Windows also Visual Studio with the C++ tools.

```bash
git clone --recurse-submodules https://github.com/b0gdan0v-bagi/BagiEngine.git
cd BagiEngine
```

After a plain clone run `git submodule update --init --recursive`.

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
- A step-by-step guide is being written under `docs/guide/`.
- A game of your own: `NEW_GAME.md`.
- The architecture and the contracts of every module: `CLAUDE.md`.
- Changes: `CHANGELOG.md`.
- Licence: Beerware (`LICENSE`); the libraries it carries are in `THIRD_PARTY_NOTICES.md`.
