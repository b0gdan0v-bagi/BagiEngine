---
paths:
  - "src/Modules/BECore/Scene/SceneControl*"
  - "src/Modules/BECore/Scene/SceneNodePath*"
  - "src/Modules/BECore/Control/ControlProperty*"
  - "tools/control/**/*"
  - "tests/BECore/SceneControl*"
---

# The scene by name over the control API (`scene_tree`, `scene_get`)

Additive to protocol version 1. `RegisterSceneControl` (`BECore/Scene/SceneControl.h`, registered by `CoreManager`) works on the **active scene**, asked on every request, in both run modes. The touch record these methods and games feed: `control-activity.md`.

## Node paths (`Scene/SceneNodePath.h`)

The names from below the scene root down to the node, joined by `/`: `Board/Cell[2]`.

- Inside a name `\/`, `\[` and `\\` stand for the character; any other `\` is a bad path.
- A segment ending in an unescaped `[n]` is the n-th (from 0) child of that name; without it, the first. Brackets that hold no number are part of the name.
- A node with an empty name is an empty segment.
- Names are compared as text: no request interns anything.
- Over `SceneNodePathLimits` (bytes, segments) a path is bad.
- A path names a place, not an object: after a node was added, removed or renamed it may name another node or none.

## Methods

Both are quiet (not in the activity journal).

| Method | Params | Result |
|--------|--------|--------|
| `scene_tree` | `{node?, limit?}` | `{nodes: [{components: [type], layer, name, path}], scene, truncated}` |
| `scene_get` | `{node, component, index?, path?}` | with `path`: `{component, node, path, value}`; without: `{component, fields, node}` |

- `scene_tree` lists depth first, a parent before its children; with `node` that node and what is below it. `limit` is 1 to `kMaxTreeNodes` (default `kDefaultTreeNodes`). `truncated` says nodes are left out: the limit, or a node deeper or longer than a path may be. Every listed `path` reads back to its node.
- `component` is a type name (`TransformComponent`); `index` picks among several of that type on one node, from 0.
- `path` of `scene_get` is a property path as in `get_property` (`color.r`, `viewWidth`) with its rules (`Reflection/CLAUDE.md`, "Reflection methods"); `fields` is every reflected field of the component as saved.
- `node` of an answer is the text of the request.

## Codes

`no_scene`, `node_not_found`, `component_not_found`, `invalid_params` (a missing or mistyped param, a bad node path, `index` or `limit` out of range) and for `path` the codes of the by-name methods (`invalid_path`, `not_found`, `not_a_value`, ...).
