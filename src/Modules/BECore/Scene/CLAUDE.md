# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built with `MakeFromConfig<SceneManager>("SceneConfig")` in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`; optional view size `viewWidth` / `viewHeight` (see "View size"). `Update()` emits `SceneEvents::SceneFixedUpdateEvent` once per fixed step due in the frame, then `SceneUpdateEvent` with the frame time (both read `CoreManager::GetTime()`); `Draw()` calls `IComponent::OnDraw()` of every component in draw order (see "Draw order"). `IsActive()` is true for the scene `SceneManager` currently drives. `PrepareForSave()` copies the live hierarchy back into `_nodes` (the scene editor calls it before saving) |
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

Drawing is not an event. `Scene::Draw()` walks the node tree, sorts the nodes by (`SceneNode::GetLayer()`, traversal index) and calls `IComponent::OnDraw()` of each node's components, in component order. So:

- A lower layer is drawn first (further back); the default layer is 0, negative layers are backgrounds.
- On the same layer the order is the scene order: depth-first, a parent before its children, siblings as in the config. A node later in `<nodes>` is drawn on top. The order never depends on event subscription, so a game does not have to arrange its nodes to make a background appear behind the pieces: give the background a lower `layer`.
- A node's own layer counts, children do not inherit it. A child of a layer-1 node is on layer 0 unless it has its own.
- The active scene only: an inactive scene's `Draw()` is not called, so `OnDraw()` needs no `IsSceneActive()` check. A removed component or node is not found by the next walk.
- Override `OnDraw()` in a component and do not subscribe to an event to draw. `OnDraw()` only reads state and issues renderer calls; it must not add or remove nodes or components (the walk holds pointers into the tree).
- `Scene::GetDrawRank(node)` is the position of a node in this order (how many nodes are drawn before it), computed from the tree as it is, with no draw before it. Input handlers that must ask the topmost first use it (`UICanvasComponent` registers with it, `UI/CLAUDE.md`).
- The scene editor shows `Layer` in the node inspector and writes it back to `SceneConfig.xml` when it is not 0.

Tests: `tests/BECore/SceneEventsTest.cpp` (`SceneEventCounter::drawIndex` is the position in the draw pass).

## View size

A scene says how big its picture is in scene units: `<scene type="Scene" name="ChessScene" viewWidth="840" viewHeight="800">`. Everything in the scene (transforms, board origins and cell sizes) is in these units, and the engine fits the view to whatever the render output is:

- `Scene::Draw()` asks the renderer for its output size, builds `ViewTransform::Fit(viewWidth, viewHeight, outputW, outputH)` (one uniform scale, the view centered, bars on the free sides), sets it with `IRenderer::SetViewTransform()` for the draw pass and resets it after. The backends apply it to every rect (`.claude/rules/rendering-module.md`). It is computed every frame, so a window or viewport of any size, resized or not, needs no event.
- Without a view size (both 0, the default) the transform is the identity: one unit is one output pixel, as before. A game that does not need scaling declares nothing. Without a renderer (tests) it is the identity too.
- Mouse positions (down, up, motion and wheel alike) arrive in output pixels (window pixels in Game mode, viewport-local in Editor mode). A handler maps them with `GetScene().ScreenToSceneX(e.button.x)` / `ScreenToSceneY(...)` before it compares them with scene geometry; `ClickableComponent`, `ChessBoardComponent` and `Match3BoardComponent` do. The mapping uses the transform of the last draw (the identity before the first one).
- Pick the view size as the bounding box of the content with the margins you want to keep: the board of Chess is 640x640 at (100, 80), so the view is 840x800: 100 units on the sides, and 80 above and below for the HUD, which lives in those margins as a `SceneView` canvas (`UI/CLAUDE.md`). A view with a different aspect than the window gets bars; the bars show the clear color.
- The scene editor saves `viewWidth` / `viewHeight` when they are set.
- Not handled: the Vulkan swapchain is not recreated when the window is resized (a TODO in `VulkanRendererBackend::AcquireFrame()`), so the output size is the one the swapchain was created with.

Tests: `tests/BECore/ViewTransformTest.cpp` (the math), `tests/BECore/SceneViewTest.cpp` (the fit during a draw with a fake renderer, `ScreenToScene*`, a click in scene units).

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height` | Position/size in scene units (pixels when the scene has no view size); others read it |
| `QuadRendererComponent` | `_color` | Draws a filled rect at the node's transform in `OnDraw()` |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect`, `_fit` | Draws a texture in `OnDraw()`; `_spriteId` resolves through `TextureLibrary`. `_fit` (`SpriteFit`, `Components/SpriteFit.h`) is `Stretch` (default: the texture covers the node's rect, whatever its aspect) or `Contain` (`fit="Contain"`: the aspect of `_srcRect`, or of the whole texture when it is empty, is kept and the sprite is centered in the rect; `FitSpriteRect()`, computed in scene units). `SetTexture()` hands it a texture that is not from the library (one from `IRenderer::CreateTexture()`); `ReloadTexture()` drops it |
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
- A game's own state is a separate check: Chess components also do nothing unless `GameManager::GetActive()` is `ChessGame`, because they read the shared model from there.
- `tests/BECore/SceneEventsTest.cpp` loads two scenes from an XML string and shows how to test a component without a window or a renderer. Use the same setup for a new component.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look the node up again.
- Do not draw from `Update` or change state from `Draw`; the frame order and the use of `deltaTime` are in `BECore/CLAUDE.md`. Movement and animation scale by `SceneUpdateEvent::deltaTime`.
