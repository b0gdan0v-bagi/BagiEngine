# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built with `MakeFromConfig<SceneManager>("SceneConfig")` in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`; optional view size `viewWidth` / `viewHeight` (see "View size"). `Update()` emits `SceneEvents::SceneFixedUpdateEvent` once per fixed step due in the frame, then `SceneUpdateEvent` with the frame time (both read `CoreManager::GetTime()`); `Draw()` runs `SceneDrawSystem` over the registry, which draws the quad and the sprite from their registry data and calls `IComponent::OnDraw()` of every other component, in draw order (see "Draw order"). `IsActive()` is true for the scene `SceneManager` currently drives. `PrepareForSave()` copies the live hierarchy back into `_nodes` (the scene editor save path calls it through `WriteSceneToConfig`, `src/Widgets/SceneEditor/SceneConfigWriter.h`) |
| `SceneNode` | `FACTORY_BASE`, XML element `node`: `_name`, `_layer` (attribute `layer`, default 0), `_components`, children, an `entt::entity`. `AddComponent`, `RemoveComponent`, `GetComponent<T>()`, `AddChild`. The XML describes the top-level nodes and their components only; children exist at runtime (`AddChild`) |
| `IComponent` | `FACTORY_BASE` and `SubscriptionHolder`; gets its `Scene&` / `SceneNode&` through `GetScene()` / `GetNode()` |

## XML shape

```xml
<SceneManager><scenes>
  <scene type="Scene" name="MainScene"><nodes>
    <node name="YellowQuad"><components>
      <component type="TransformComponent" x="167.3" y="107.7" width="42.5" height="71.7"/>
      <component type="QuadRendererComponent"><color r="255" g="255" b="0" a="255"/></component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>
```

`type` is the component class name; field names lose the leading underscore. A `scene` can carry `viewWidth` / `viewHeight` (see "View size") and a node can carry `layer="-1"` (see "Draw order"). Every `SceneConfig.xml` replaces the one below it in the content layers (see `FileSystem/CLAUDE.md`): the engine's own file holds a debug `MainScene`, and a game's file holds only that game's scenes, so a game executable never loads `MainScene`.

## Component lifecycle

1. Deserialization creates the component through `AbstractFactory<IComponent>` and calls `Initialize()` (the reflection hook: runs even when there is no node yet).
2. `Scene::Initialize()` → `SceneNode::WireToScene()` sets `_scene` / `_node` and calls `OnAttached()`. `AddComponent` does the same at runtime.
3. Do node-dependent setup in `OnAttached()`: event subscriptions, resource lookups, sibling components via `GetNode().GetComponent<T>()`. This runs for the components of **every** loaded scene, active or not, so subscribe with `SubscribeInScene` (see Gotchas).

## Draw order

Drawing is not an event. `Scene::Draw()` hands the pass to `SceneDrawSystem` (`SceneDrawSystem.{h,cpp}`). At the start of every `Draw()` the system walks the node tree depth-first and writes a `SceneDrawItem {layer, order, node}` into the registry for each node (`emplace_or_replace`, so a changed layer is picked up; `order` is the traversal index) and, in the same walk, mirrors the node's transform and renderer data into the registry (below). It sorts the `SceneDrawItem` storage with `registry.sort<SceneDrawItem>` by (`SceneNode::GetLayer()`, `order`), then iterates `registry.view<SceneDrawItem>()` and, for each node, goes through its components in component order: the node's `QuadRendererComponent` and `SpriteRendererComponent` are drawn by the system from the registry (`DrawFilledRect`; `FitSpriteRect()` and `DrawTexture`), every other component through `IComponent::OnDraw()`. So a node with `[Text, Quad]` still draws the text first. The view is the only source of the order; no second list of nodes is kept. So:

- A lower layer is drawn first (further back); the default layer is 0, negative layers are backgrounds.
- On the same layer the order is the scene order: depth-first, a parent before its children, siblings as in the config. A node later in `<nodes>` is drawn on top. The order never depends on event subscription, so a game does not have to arrange its nodes to make a background appear behind the pieces: give the background a lower `layer`.
- A node's own layer counts, children do not inherit it. A child of a layer-1 node is on layer 0 unless it has its own.
- The active scene only: an inactive scene's `Draw()` is not called, so `OnDraw()` needs no `IsSceneActive()` check. A removed component or node is not found by the next walk. `SceneNode::RemoveChild()` also destroys the entity of the removed node and of its descendants (`Scene::DestroyNodeEntities`, behind a `PassKey<SceneNode>`) and `GetEntity()` of a removed node is `entt::null`, so the registry holds no `SceneDrawItem` that points into a removed subtree. `RemoveChild()` reaches the registry through the node's `_scene`; a node that is still held by an `IntrusivePtr` after its `Scene` is gone has a dangling `_scene`, so do not call `RemoveChild()` on it.
- Override `OnDraw()` in a component and do not subscribe to an event to draw. `OnDraw()` only reads state and issues renderer calls; it must not add or remove nodes or components (the walk holds pointers into the tree) and must not write a `TransformComponent` or a renderer field (`QuadRendererComponent::_color`, `SetTexture()` / `ReloadTexture()`): the mirror was copied before the first `OnDraw()` of the pass, so the change would show a frame late and a released texture would dangle in `SpriteDrawData`. `QuadRendererComponent` and `SpriteRendererComponent` have no `OnDraw()`.
- `Scene::GetDrawRank(node)` is the position of a node in this order (how many nodes are drawn before it), computed from the tree as it is, with no draw before it. Input handlers that must ask the topmost first use it (`UICanvasComponent` registers with it, `UI/CLAUDE.md`).
- The scene editor shows `Layer` in the node inspector and writes it back to `SceneConfig.xml` when it is not 0.

**The mirror** (`SceneRenderData.h`, written by the system and by nothing else): `TransformData {x, y, width, height}`, `QuadDrawData {color, owner}` and `SpriteDrawData {texture, srcRect, fit, owner}` are components of the node's entity. `TransformComponent`, `QuadRendererComponent` and `SpriteRendererComponent` stay the source of truth (reflection, the XML, the scene editor and game code write their fields; `ClickableComponent`, `TextComponent`, `UICanvasComponent` and the games read `TransformComponent`). At the start of every `Draw()` the walk copies the fields of each node's component when it has one (`emplace_or_replace`, no allocation once the node has it) and removes the registry component when it does not, so a changed field, an added or removed component and a texture set by `SetTexture()` / `ReloadTexture()` show in the next draw with no event or setter hook. A node has at most one quad and one sprite: the first of each counts, and a second one (only possible from a hand-written XML) draws nothing. A quad or a sprite without a `TransformData` draws nothing, and a sprite without a texture draws nothing. `SpriteDrawData::texture` is a raw pointer taken from the component's handle in the same `Draw()`, so nothing may change the component between the sync and the draw (see below). The structs are valid after a `Draw()`; game code does not read them.

Tests: `tests/BECore/SceneEventsTest.cpp` (`SceneEventCounter::drawIndex` is the position in the draw pass), `tests/BECore/SceneDrawSystemTest.cpp` (the `SceneDrawItem` of every node, the order of the view, `SetLayer` between draws, entities of a removed subtree, a node added between draws, the rects a `RecordingRenderer` sees in layer order, the component order inside a node, a quad or a sprite without a transform, a quad and a sprite ordered together by layer), `tests/BECore/SceneRenderDataTest.cpp` (the mirror: the values, a field changed between draws, a removed component, the texture of a sprite, `owner`, a removed subtree), `tests/BECore/SpriteRendererComponentTest.cpp` (the rects a sprite draws).

## Why EnTT

Decided by the owner on 2026-11-07 (entry `ecs-nominal`): the object model stays `SceneNode` + `IComponent`, because reflection, the XML and the node inspector are built on it. The `entt::registry` of a `Scene` holds the data that **systems** work on, and a system works on a sorted view of it. Today that is `SceneDrawItem` (the order) and the transform and the data of the two built-in renderers: `TransformData`, `QuadDrawData` and `SpriteDrawData`, refreshed from the components at the start of every `Draw()` (the components are the truth, the registry copy is a mirror). `SceneDrawSystem` issues the `IRenderer` calls of the quad and the sprite from them.

- It is **not** a pure ECS: the components (`IComponent`) are not stored in EnTT pools, and the registry holds one entity per node and the data of the systems only. The components keep their fields, and the editor and the XML are built on them. The other drawing components (`TextComponent`, `UICanvasComponent`, `RenderProbeComponent`, `RmlUiDocumentComponent`, the games' components) still draw through `OnDraw()` and issue their own `IRenderer` calls.
- The direction is rendering as a system that hands a **batch** to `IRenderer` and the backends (slice 3 of `ecs-nominal`; the entries `rendering-2d` and `renderer-backends`). Today the system still makes one `IRenderer` call per quad or sprite, in the same order as before; nothing is batched and the `IRenderer` interface is unchanged. Do not describe the batch as done.

## View size

A scene says how big its picture is in scene units: `<scene type="Scene" name="GameScene" viewWidth="840" viewHeight="800">`. Everything in the scene (transforms, board origins and cell sizes) is in these units, and the engine fits the view to whatever the render output is:

- `Scene::Draw()` asks the renderer for its output size, builds `ViewTransform::Fit(viewWidth, viewHeight, outputW, outputH)` (one uniform scale, the view centered, bars on the free sides), sets it with `IRenderer::SetViewTransform()` for the draw pass and resets it after. The backends apply it to every rect (`.claude/rules/rendering-module.md`). It is computed every frame, so a window or viewport of any size, resized or not, needs no event.
- Without a view size (both 0, the default) the transform is the identity: one unit is one output pixel, as before. A game that does not need scaling declares nothing. Without a renderer (tests) it is the identity too.
- Mouse positions (down, up, motion and wheel alike) arrive in output pixels (window pixels in Game mode, viewport-local in Editor mode). A handler maps them with `GetScene().ScreenToSceneX(e.button.x)` / `ScreenToSceneY(...)` before it compares them with scene geometry; `ClickableComponent` and `Match3BoardComponent` do. The mapping uses the transform of the last draw (the identity before the first one).
- Pick the view size as the bounding box of the content with the margins you want to keep: a board of 640x640 at (100, 80) gives a view of 840x800: 100 units on the sides, and 80 above and below for the HUD, which lives in those margins as a `SceneView` canvas (`UI/CLAUDE.md`). A view with a different aspect than the window gets bars; the bars show the clear color.
- The scene editor takes the scene's attributes from `Scene::Serialize`, so `viewWidth` / `viewHeight` are always saved: a scene without a view carries `0` / `0`, which loads as no view, the same as missing attributes.
- Not handled: the Vulkan swapchain is not recreated when the window is resized (a TODO in `VulkanRendererBackend::AcquireFrame()`), so the output size is the one the swapchain was created with.

Tests: `tests/BECore/ViewTransformTest.cpp` (the math), `tests/BECore/SceneViewTest.cpp` (the fit during a draw with a fake renderer, `ScreenToScene*`, a click in scene units).

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height` | Position/size in scene units (pixels when the scene has no view size); others read it. `SceneDrawSystem` copies it into the registry as `TransformData` at the start of every draw |
| `QuadRendererComponent` | `_color` | Has no `OnDraw()`: `SceneDrawSystem` draws a filled rect at the node's transform from its registry copy (`QuadDrawData`). The component stays the place of the field and of the editor |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect`, `_fit` | Has no `OnDraw()`: `SceneDrawSystem` draws the texture from its registry copy (`SpriteDrawData`); the component stays the place of the fields, of the texture handle and of the editor. `_spriteId` resolves through `TextureLibrary`. `_fit` (`SpriteFit`, `Components/SpriteFit.h`) is `Stretch` (default: the texture covers the node's rect, whatever its aspect) or `Contain` (`fit="Contain"`: the aspect of `_srcRect`, or of the whole texture when it is empty, is kept and the sprite is centered in the rect; `FitSpriteRect()`, computed in scene units). `SetTexture()` hands it a texture that is not from the library (one from `IRenderer::CreateTexture()`); `ReloadTexture()` drops it |
| `ClickableComponent` | `_actionName` | On mouse-down inside the transform rect, emits `InputEvents::ActionEvent(_actionName)` |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |
| `TextComponent` | `_text`, `_fontPath`, `_fontSize`, `_color`, `_align`, `_overflow`, `_wrap`, `_lineSpacing`, `_minFontSize` | Draws text in the node's transform box in `OnDraw()` (`Text/CLAUDE.md`) |
| `UICanvasComponent` | `_space`, `_scaleMode`, `_referenceWidth`, `_referenceHeight`, `_match`, `_minScale`, `_maxScale`, `_useSafeArea`, `_modal`, `_visible`, `_inputEnabled`, `_elements`, `_variants` | A canvas of in-game UI: owns an element tree, lays it out for the output, draws it with its own view transform and takes mouse input through `UIInput` (`UI/CLAUDE.md`) |
| `RenderProbeComponent` | none | Debug: from the node's transform origin draws a row of cells that show the renderer primitives (texture from pixels, tint, half alpha, translucent rects, nested clips, and two `DrawGeometry()` quads: a color per corner, and a mirrored texture drawn through the translation), to look at both backends |

## Adding a component

1. Class in `Components/` (or a game module's `Components/`) deriving `IComponent` with `BE_CLASS(Name)`; fields get `BE_REFLECT_FIELD`.
2. Include `<Generated/Name.gen.hpp>` in its `.cpp`; the directory must be in `reflection_targets.json`; re-run `cmake --preset ...`.
3. Subscribe in `OnAttached()` through `SubscribeInScene<Event, &Name::Handler>(this)`. Plain `Subscribe` is only for a handler that must run whichever scene is active. A component that draws overrides `OnDraw()` instead of subscribing (see "Draw order").
4. Use it from `config/SceneConfig.xml` with `type="Name"`. The inspector shows its reflected fields automatically.

## Gotchas

- `SceneUpdateEvent` / `SceneFixedUpdateEvent` and the input events (`SDLEvents::MouseButtonDownEvent`, `InputEvents::ActionEvent`) are **global** `EventBase` events, not per scene, and `OnAttached()` runs for the components of **every** scene when the config loads. Subscribe with `IComponent::SubscribeInScene<Event, &Handler>(this)`: the handler is called only while the component's scene is active (`Scene::IsActive()`, set by `SceneManager`). Plain `Subscribe` on such an event makes a component of an inactive scene click and react. The filter sits in the call, the connection stays, so switching scenes never connects or disconnects anything (which is not allowed inside a handler).
- A game's own state is a separate check: a component that reads a model owned by its `IGame` does nothing unless `GameManager::GetActive()` is that game, because it reads the shared model from there.
- `tests/BECore/SceneEventsTest.cpp` loads two scenes from an XML string and shows how to test a component without a window or a renderer. Use the same setup for a new component.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look the node up again.
- Do not draw from `Update` or change state from `Draw`; the frame order and the use of `deltaTime` are in `BECore/CLAUDE.md`. Movement and animation scale by `SceneUpdateEvent::deltaTime`.
