# Scene: drawing and the update events run over dense per-scene lists, not a walk of the node tree every frame

- Version: 0.20.0
- Entry: scene-dense-update-draw
- Date: 2026-11-24
- Bump: minor

## Added
- `IComponent::HasOnDraw()` (default true): a component that never draws returns false and stays out of the scene's draw list; the engine's non-drawing built-ins do. Adapt: nothing; a game component without `OnDraw()` may override it to return false.
- `Scene::GetDrawListBuilds()`, `IsDrawListValid()` and `GetUpdaterCount(kind)` report the scene's draw list and update lists. Adapt: nothing.

## Changed
- `SceneDrawSystem` keeps a dense draw list per scene (`SceneDrawList.h`), one entry per drawing component, rebuilt only when a node is created, removed, moved, duplicated or instantiated, a layer changes through `SetLayer()`, a component is added or removed, or editor flags change; a steady frame only draws it. Quads and sprites read their components at draw time. A layer written to the reflected field without `SetLayer()` shows in the order only after the next rebuild. Adapt: change a node's layer only through `SetLayer()`.
- `SubscribeInScene` of `SceneUpdateEvent` and `SceneFixedUpdateEvent` registers the component in its scene's update lists (`SceneUpdateList.h`); `Scene::Update()` runs them (fixed list once per fixed step due, then the frame list, last registered first, stable when one leaves) and then emits the events for plain subscribers. A component leaves on `RemoveComponent` or `RemoveChild`; a component added to a removed node never joins; an inactive scene calls none. Adapt: nothing for components that use `SubscribeInScene` through `Scene::Update()` / `SceneManager::UpdateAll()`.
- In Debug, adding or removing nodes or components and changing editor flags inside `OnDraw()` asserts. Adapt: do such edits in an update handler.

## Breaking
- `BECore/Scene/SceneRenderData.h` (`TransformData`, `QuadDrawData`, `SpriteDrawData`, the per-frame mirror in the scene registry) is removed. Adapt: read `TransformComponent`, `QuadRendererComponent` and `SpriteRendererComponent` instead.
- A hand `SceneUpdateEvent::Emit()` / `SceneFixedUpdateEvent::Emit()` no longer reaches components that subscribed with `SubscribeInScene`; plain `Subscribe` still gets it. Adapt: a test or tool that advanced time by emitting the event drives `CoreManager::GetTime().Advance(seconds)` and `SceneManager::UpdateAll()` (or `Scene::Update()`) instead.
