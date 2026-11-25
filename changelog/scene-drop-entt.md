# Scene: EnTT is gone; the draw order lives in the draw list and liveness is the walk to the root

- Version: 0.21.0
- Entry: scene-drop-entt
- Date: 2026-11-25
- Bump: minor

## Changed
- `MoveNode` and `DuplicateNode` refuse a node made under a node that `RemoveChild()` took out of the tree, as they refuse the removed node itself; such a node was in no update or draw list before either. Adapt: nothing.

## Breaking
- EnTT is no longer vendored or linked: `src/Modules/EnTT` is removed, `BECoreModule` no longer brings `<entt/...>` headers. Adapt: a game that included EnTT itself vendors it in its own repository.
- `Scene::GetRegistry()` and `SceneNode::GetEntity()` are removed. Adapt: use `Scene::GetDrawRank()` for the draw position and the node pointer as the identity of a node.
