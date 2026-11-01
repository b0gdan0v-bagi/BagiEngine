# Chess: pieces are not drawn — make draw order explicit (layer + sorted pass)

## Context

In `BagiChess` the board shows but the 32 pieces do not. I have not run the game (plan mode, and `engine.log` from the last runs is 0 bytes), so the cause comes from reading the code. The verification section confirms or refutes it.

**Cause: draw order depends on the event dispatcher.** Every drawing component subscribes to the global `SceneDrawEvent`, which goes through `entt::dispatcher`. In the vendored EnTT 3.13.2 `sigh::publish` calls listeners from the last connected to the first (`src/Modules/EnTT/entt/signal/sigh.hpp:170`, "Order isn't guaranteed"). `SceneNode::WireToScene` connects components in `<nodes>` order, and in `games/Chess/config/SceneConfig.xml` the `Board` node is first. So `ChessBoardComponent::OnDraw` runs last and paints 64 opaque `DrawFilledRect` squares over the sprites (`ChessBoardComponent.cpp:56-68`). The Board has been first since the MVP commit `1c8c99a1`.

The user asked for a general fix: sorting, so that no game depends on handler order. `TODO.md` ("2D rendering is minimal", item 1: "no z-order or layers: draw order is the order of event handlers") and the entry on rendering as a per-component event handler ("nowhere to sort or batch draws") already describe the gap. This plan closes the ordering part of it and nothing more (no camera, rotation, blending or batching).

Ruled out by reading: the texture path and `TextureLibrary.xml` (the atlas `games/Chess/assets/chess_test.png` is 700x381 RGBA, its virtual path resolves through the game layer), the Vulkan `DrawTexture` path and shaders, `ChessPieceComponent::OnUpdate` (late registration covers `GameManager` starting after the scene loads) and `SubscribeInScene`.

## Design

`Scene::Draw()` stops emitting a global event. It walks its own node tree, sorts, and calls `OnDraw()` on the components in that order:

- **Order key = (`layer` of the node, traversal index).** Traversal is depth-first, a parent before its children, siblings in `<nodes>` order, so equal layers draw in document order (later on top, as in any scene editor). Keys are unique, so `eastl::sort` is deterministic (no stable sort, no allocator buffer).
- **`layer`** is a reflected `int32_t` on `SceneNode`, default 0, XML attribute `layer` on `<node>`. It is per node, not per component, so a node without a `TransformComponent` (the Chess `Board`) can have one, and the field exists once instead of in every renderer. A node's own layer is used; children do not inherit it (kept simple, documented).
- **`IComponent::OnDraw()`**: a new virtual with an empty default, next to `OnAttached()`. Components override it and no longer subscribe. This removes the lifetime problem a registry would add: a removed component or node simply is not found by the next walk, and only the active scene's `Draw()` runs, so no `SubscribeInScene` filtering is needed for drawing.
- **`SceneDrawEvent` is removed.** Nothing else listens to it (checked: four components and one test probe). Keeping it would leave a second, unordered way to draw.
- The sort runs per frame over a reused member vector in `Scene` (`{layer, index, SceneNode*}`), so there is no allocation after the first frame. `OnDraw` must not add or remove nodes (the existing rule: do not change state from the draw phase).

## Changes

1. **Engine, `src/Modules/BECore/Scene/`**
   - `IComponent.h`: `virtual void OnDraw() {}` with a doc comment (called once per frame in draw order while the scene is active; draw only; do not subscribe to events to draw). Update the `SubscribeInScene` comment, which names `SceneDrawEvent` as its example.
   - `SceneNode.h/.cpp`: `BE_REFLECT_FIELD int32_t _layer = 0;`, `GetLayer()` / `SetLayer()`.
   - `Scene.h/.cpp`: `Draw()` builds the list, sorts by (layer, index) and calls each node's components' `OnDraw()` in component order.
   - `src/Modules/Events/src/Events/SceneEvents.h`: remove `SceneDrawEvent`.
2. **Components move from `SubscribeInScene<SceneDrawEvent>` to `void OnDraw() override`** (drop the `SceneEvents` forward declaration and the include if it becomes unused):
   - `QuadRendererComponent` (its `OnAttached` becomes empty, remove it).
   - `SpriteRendererComponent`: `OnAttached` only loads the texture; `ReloadTexture` loses the `UnsubscribeAll()` and re-subscribe; fix the header comment "re-subscribe to draw events".
   - `games/Chess/.../ChessBoardComponent` and `games/Match3/.../Match3BoardComponent` (they keep their `MouseButtonDownEvent` subscription).
3. **Scene editor, `src/Widgets/SceneEditor/SceneEditorWidget.cpp`**: `BuildNodeXml` writes the `layer` attribute when it is not 0 (the editor saves nodes by hand, not through reflection, so the field would be lost on save otherwise); `RenderInspector` gets a `Layer` `ImGui::InputInt` that calls `SetLayer` and sets `_isDirty`.
4. **Chess config, `games/Chess/config/SceneConfig.xml`**: `Board` gets `layer="-1"` with a comment (background). The result no longer depends on node order. Nothing else in the file changes.
5. **Tests, `tests/BECore/SceneEventsTest.{h,cpp}`** (the fixture loads scenes from an XML string and needs no renderer):
   - `SceneEventCounter` switches to `OnDraw() override` and records `drawIndex` from a static counter that the fixture resets, so a test can compare the position of each node in the draw pass.
   - Cases: equal layers draw in document order; a lower layer draws first whatever its position in `<nodes>` (the Chess shape: background node first and last in XML give the same result); depth-first order (parent, its children, then the next sibling); the existing active-scene case still holds (inactive scene is not drawn); a component removed at runtime is not drawn; `layer` is read from the XML attribute.
6. **Docs**
   - `src/Modules/BECore/Scene/CLAUDE.md`: Scene row (`Draw()` description), XML shape (`layer`), component lifecycle (`OnDraw`), the component table row for `QuadRendererComponent`, a "Draw order" note (key, depth-first, no inheritance, MainScene note below), and the first Gotcha that names `SceneDrawEvent`.
   - `src/Modules/BECore/CLAUDE.md`: frame order row 4 and the "do not change game state from `SceneDrawEvent`" sentence.
   - `src/Modules/Events/CLAUDE.md` and `.claude/rules/reflection-events.md` (line 66 uses `SceneDrawEvent` as an example event): drop the event and pick another example.
   - `games/Chess/CLAUDE.md`: one line that the board is the background layer.
   - `TODO.md`: item 1 of "2D rendering is minimal" (layers: done, node `layer` plus document order; left: layer inheritance, a camera) and the draw-handler entry about nowhere to sort or batch (the draw pass is now that place).

## Behavior changes to expect

- The engine debug `MainScene` quads (`config/SceneConfig.xml`) now overlap in document order; before, it was the reverse by accident.
- Highlights of the selected square and legal moves (drawn in the board's `OnDraw`) end up under the pieces.

## Not changing (noting only)

The atlas cell is 116.67x190.5 and is drawn into an 80x80 square, so the pieces will look squeezed horizontally. Fixing it needs a decision on aspect ratio (destination rect or letterboxed sprite). I will leave it unless asked.

## Verification

1. `cmake --preset macos-debug` (reflection regenerates for the new field and the removed event), then `cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests BagiChess BagiMatch3`.
2. `build/ClangmacOS/BagiEngineTests --gtest_filter='SceneEvents*'`, then `ctest --preset macos-debug` (all 177+ tests; the new cases must pass).
3. Run `build/ClangmacOS/BagiChess`: board with 32 pieces, a click on a pawn highlights its square under the piece. If the pieces are still missing, the diagnosis is incomplete: read `engine.log` after the run for `SpriteRendererComponent` and `VulkanTextureLoader` errors and check `DrawTexture` inside the swapchain pass. Setting `mode="Editor"` in `games/Chess/config/ApplicationConfig.xml` renders the scene through an offscreen target and helps to compare.
4. Run `build/ClangmacOS/BagiMatch3`: unchanged picture. In Editor mode, set a node layer in the inspector, save, and check that `layer` is in the written `SceneConfig.xml`.
5. Finish with the `clang-format` skill on the changed C++ files and `strip-pch-includes` for the files whose includes changed (a subfolder at a time, dry run first).

## Status (done)

- Changes 1-6 implemented; `clang-format` applied (Homebrew LLVM on `PATH`), `strip-pch-includes` dry run found nothing to remove.
- `BagiEngineTests`, `ChessTests`, `Match3Tests`: 284/284 pass under `ctest --preset macos-debug`, including 6 new draw-order cases in `SceneEventsTest`.
- `BagiChess` starts in Game mode with a clean log (texture `assets/chess_test.png` loaded, no sprite or assert errors), which rules out the texture-loading path. Not verified: the picture itself (no screenshot access from the agent session), so the user has to look at the window.
