# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built with `MakeFromConfig<SceneManager>("SceneConfig")` in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`. `Update()` emits `SceneEvents::SceneFixedUpdateEvent` once per fixed step due in the frame, then `SceneUpdateEvent` with the frame time (both read `CoreManager::GetTime()`); `Draw()` calls `IComponent::OnDraw()` of every component in draw order (see "Draw order"). `IsActive()` is true for the scene `SceneManager` currently drives. `PrepareForSave()` copies the live hierarchy back into `_nodes` (the scene editor calls it before saving) |
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

`type` is the component class name; field names lose the leading underscore. A node can carry `layer="-1"` (see "Draw order"). Every `SceneConfig.xml` replaces the one below it in the content layers (see `FileSystem/CLAUDE.md`): the engine's own file holds a debug `MainScene`, and a game's file holds only that game's scenes, so a game executable never loads `MainScene`.

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
- The scene editor shows `Layer` in the node inspector and writes it back to `SceneConfig.xml` when it is not 0.

Tests: `tests/BECore/SceneEventsTest.cpp` (`SceneEventCounter::drawIndex` is the position in the draw pass).

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height` | Position/size in screen pixels; others read it |
| `QuadRendererComponent` | `_color` | Draws a filled rect at the node's transform in `OnDraw()` |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect` | Draws a texture in `OnDraw()`; `_spriteId` resolves through `TextureLibrary` |
| `ClickableComponent` | `_actionName` | On mouse-down inside the transform rect, emits `InputEvents::ActionEvent(_actionName)` |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |

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
