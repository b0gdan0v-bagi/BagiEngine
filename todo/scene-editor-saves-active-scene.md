---
id: the-scene-editor-saves-only-the-active-scene
---
# The scene editor saves only the active scene

**Where:** `src/Widgets/SceneEditor/SceneEditorWidget.cpp` (`SaveScene()`).

**What is wrong** (found by reading the code, not by saving in the editor): `SaveScene()` removes the whole `<scenes>` element of `SceneConfig` and writes back one `<scene>`, the active one. A config with two scenes (the engine's own file has a debug `MainScene`; a game with a menu would have more) loses every other scene on the first save. The same code writes the scene attributes by hand (`type`, `name`, `viewWidth`, `viewHeight`), so a new reflected field of `Scene` is dropped on save unless it is added there.

**A fix has to cover:**

- replace only the `<scene>` element with the active scene's name and keep the others as they are;
- write the scene attributes through the reflection (`Scene::Serialize`) instead of by hand;
- a round-trip test of the save path (load, edit, save, load) with two scenes.
