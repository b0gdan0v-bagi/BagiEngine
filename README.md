# BagiEngine

A C++23 game engine on a scene graph of `SceneNode` + `IComponent`, with an EnTT registry for the data of the systems. It runs on SDL3 with an SDL or Vulkan renderer, on desktop and in the browser (Emscripten), and drives its in-game UI with its own canvases (adaptive layout, a validator of touch sizes and text fit). Games live in `games/<Name>/` (the engine's own is Match3) or in a repository of their own that carries the engine as the submodule `engine/` (`NEW_GAME.md`).

## Principles

- **Zero allocations in a steady frame.** A frame in which nothing changed allocates nothing: the scene, the UI canvases, the text and the game's own components make a thing again only when its key moved and keep their lists between frames. A phone and the browser pay for every allocation in frame time and battery, and an allocation per frame stays invisible until a profile shows it. The `macos-profile` preset counts every `operator new` (`ENGINE_PROFILER_ALLOC`): per frame (`frame_stats`), per profiler zone (`zone_allocs`) and in tests (`AllocTracker`); a game holds its scene to zero with a test of its own.
- **External clients through the control API** (`tools/control`): the same actions a player has, over WebSocket, for bots, smoke tests and MCP.

## Build and test

```bash
cmake --preset macos-debug          # configure (also regenerates reflection code); list: cmake --list-presets
cmake --build --preset macos-debug  # BagiMatch3 and the test executables
ctest --preset macos-debug
```

Presets, the editor option and the browser builds: `cmake/CLAUDE.md`. The architecture, the code style and the contracts of every module are in `CLAUDE.md` and the nested `CLAUDE.md` files under `src/`. The launcher for the workspace: `WORKSPACE_README.md`.
