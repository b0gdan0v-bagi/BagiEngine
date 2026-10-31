# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built with `MakeFromConfig<SceneManager>("SceneConfig")` in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`. `Update()` / `Draw()` emit `SceneEvents::SceneUpdateEvent` / `SceneDrawEvent`. `PrepareForSave()` copies the live hierarchy back into `_nodes` (the scene editor calls it before saving) |
| `SceneNode` | `FACTORY_BASE`, XML element `node`: `_name`, `_components`, children, an `entt::entity`. `AddComponent`, `RemoveComponent`, `GetComponent<T>()`, `AddChild` |
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

`type` is the component class name; field names lose the leading underscore. Scenes in use: `MainScene`, `ChessScene`, `Match3Scene`.

## Component lifecycle

1. Deserialization creates the component through `AbstractFactory<IComponent>` and calls `Initialize()` (the reflection hook: runs even when there is no node yet).
2. `Scene::Initialize()` → `SceneNode::WireToScene()` sets `_scene` / `_node` and calls `OnAttached()`. `AddComponent` does the same at runtime.
3. Do node-dependent setup in `OnAttached()`: event subscriptions, resource lookups, sibling components via `GetNode().GetComponent<T>()`.

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height` | Position/size in screen pixels; others read it |
| `QuadRendererComponent` | `_color` | Draws a filled rect at the node's transform on `SceneDrawEvent` |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect` | Draws a texture; `_spriteId` resolves through `TextureLibrary` |
| `ClickableComponent` | `_actionName` | On mouse-down inside the transform rect, emits `InputEvents::ActionEvent(_actionName)` |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |

## Adding a component

1. Class in `Components/` (or a game module's `Components/`) deriving `IComponent` with `BE_CLASS(Name)`; fields get `BE_REFLECT_FIELD`.
2. Include `<Generated/Name.gen.hpp>` in its `.cpp`; the directory must be in `reflection_targets.json`; re-run `cmake --preset ...`.
3. Subscribe in `OnAttached()` through `Subscribe<Event, &Name::Handler>(this)`.
4. Use it from `config/SceneConfig.xml` with `type="Name"`. The inspector shows its reflected fields automatically.

## Gotchas

- `SceneUpdateEvent` / `SceneDrawEvent` are **global** `EventBase` events, not per scene, and `OnAttached()` runs for the components of **every** scene when the config loads. `SetActiveScene()` only changes which scene emits the events. A component that must act only while its scene or game is active has to check that itself (Chess checks `GameManager::GetActive()`). Verify the behavior of a new component with a second scene loaded.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look the node up again.
- Do not draw from `Update` or change state from `Draw`; the frame order is in `BECore/CLAUDE.md`.
