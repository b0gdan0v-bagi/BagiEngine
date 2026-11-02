---
stage: 5
id: no-prefabs-all-of-a-game-s-scenes-live-in-one-sceneconfig
---
# No prefabs: all of a game's scenes live in one `SceneConfig.xml`

**Where:** `games/*/config/SceneConfig.xml`, `Scene/CLAUDE.md` ("Every `SceneConfig.xml` replaces the one below it"), `Scene/SceneManager.cpp`.

**What is missing** (found by reading the docs and configs): a reusable node template and a scene in its own file. A game's `SceneConfig.xml` replaces the engine's whole file, and a node such as a Match3 gem or a Chess piece is spelled out every time it appears.

**A fix has to cover:**

- a prefab: a node (with children and components) in its own XML file, instantiated by name with per-instance overrides, and the same instantiation at runtime (`Scene::CreateNode` equivalent);
- scenes loaded from separate files listed in `SceneConfig.xml`, keeping the layering rule (a game's file wins);
- the scene editor saving prefab instances without flattening them.
