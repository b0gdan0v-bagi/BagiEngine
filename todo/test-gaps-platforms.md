---
stage: 5
id: test-gaps-and-platforms
---
# Test gaps and platforms

**Where:** `tests/BECore/` (engine tests), `games/*/tests/`, `CMakePresets.json` (macOS and Windows presets only), `cmake/BagiEngine.cmake` (it has a Linux branch).

**What is missing** (found by reading the repository):

1. `Widgets` and most components have no tests (`Renderer` has the primitives and the view fit, with the `RecordingRenderer` double in `tests/Common`). `Scene` / `SceneManager` are covered only for which scene reacts to events (`SceneEventsTest`). The tests cover reflection, events, file system, resources, TaskSystem, format, enums and the pure game rules.
2. There are no Linux presets although the CMake has a Linux branch.

**A fix has to cover:**

- Linux presets (`linux-debug`, `linux-release`) and a CI job once [the CI entry](no-ci.md) is done.
