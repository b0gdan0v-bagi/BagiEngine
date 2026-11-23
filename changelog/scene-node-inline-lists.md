# SceneNode keeps its components and children in heap vectors: 1-2 allocations per created node (the last per-node heap allocation)

- Version: 0.10.0
- Entry: scene-node-inline-lists
- Date: 2026-11-23
- Bump: minor

## Changed
- A `SceneNode` keeps up to two components and one child inside its pooled block, and `QuadRendererComponent` and `ClickableComponent` come from pools: creating and destroying nodes no longer allocates from the heap in the usual case.

## Breaking
- `SceneNode::GetChildren()` returns `const SceneNode::ChildList&` and `GetComponents()` returns `const SceneNode::ComponentList&` (inline `SmallVector`s) instead of `const Vector<...>&`. Adapt: take the result as `const auto&` or name `SceneNode::ChildList` / `SceneNode::ComponentList`; a copy into a `Vector` is built with `Vector<...>(list.begin(), list.end())`.
