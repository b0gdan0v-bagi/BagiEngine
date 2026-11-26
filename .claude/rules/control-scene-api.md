---
paths:
  - "src/Modules/BECore/Scene/SceneControl*"
  - "src/Modules/BECore/Scene/SceneNodeControl*"
  - "src/Modules/BECore/Scene/SceneNodePath*"
  - "src/Modules/BECore/Control/ControlProperty*"
  - "tools/control/**/*"
  - "tests/BECore/SceneControl*"
  - "tests/BECore/SceneNodeControl*"
---

# The scene by name over the control API (`scene_tree`, `scene_get`, `scene_set`, `scene_set_node`)

Additive to protocol version 1. `RegisterSceneControl` (`BECore/Scene/SceneControl.h`, registered by `CoreManager`) works on the **active scene**, asked on every request, in both run modes. `scene_set_node` has a registration of its own over the same scene (`RegisterSceneNodeControl`, `SceneNodeControl.h`).

## Node paths (`Scene/SceneNodePath.h`)

The names from below the scene root down to the node, joined by `/`: `Board/Cell[2]`.

- Inside a name `\/`, `\[` and `\\` stand for the character; any other `\` is a bad path.
- A segment ending in an unescaped `[n]` is the n-th (from 0) child of that name; without it, the first. Brackets that hold no number are part of the name.
- A node with an empty name is an empty segment.
- Names are compared as text: no request interns anything.
- Over `SceneNodePathLimits` (bytes, segments) a path is bad.
- A path names a place, not an object: after a node was added, removed or renamed it may name another node or none.

## Methods

| Method | Params | Result |
|--------|--------|--------|
| `scene_tree` | `{node?, limit?}` | `{nodes: [{components: [type], layer, name, path}], scene, truncated}` |
| `scene_get` | `{node, component, index?, path?}` | with `path`: `{component, node, path, value}`; without: `{component, fields, node}` |
| `scene_set` | `{node, component, index?, path, value}` | `{component, node, path, value}`: what the field holds after the write |
| `scene_set_node` | `{node, layer}` | `{layer, node}` |

- `scene_tree` lists depth first, a parent before its children; with `node` that node and what is below it. `limit` is 1 to `kMaxTreeNodes` (default `kDefaultTreeNodes`). `truncated` says nodes are left out: the limit, or a node deeper or longer than a path may be. Every listed `path` reads back to its node.
- `component` is a type name (`TransformComponent`); `index` picks among several of that type on one node, from 0.
- `path` of `scene_get` and `scene_set` is a property path as in `get_property` (`color.r`, `viewWidth`) with its rules (`Reflection/CLAUDE.md`, "Reflection methods"); `fields` is every reflected field of the component as saved.
- `node` of an answer is the text of the request.

## Writing (`scene_set`)

- The value rules and refusals are those of `set_property` (`type_mismatch`, `out_of_range` with a field's `range`, `read_only`, `hidden`); a refused write changes nothing and sends no event. It writes one leaf and adds or removes no node, component or list element; the layer of a node is `scene_set_node`'s.
- A name field (`PoolString`) takes only a text the pool already holds (a known sprite, action or path, or `""`), else `interning`: a client must not grow the pool.
- The editor's Locked and Hidden flags do not stop a write: they are the human editor's own state, and the method also works in the Game mode.
- A successful write calls `IComponent::OnPropertyWritten(<top-level field>)` and sends `SceneEvents::SceneNodeEditedEvent`. For it the Scene Editor marks the active scene unsaved, unless the node or one above it is generated (Save leaves it out).
- A component rebuilds what it keeps from a field there: the sprite renderer its texture (`spriteId`, `texturePath`; a `texturePath` written while `spriteId` is set is replaced by the sprite's, which the answer shows), the audio source its clip (`clip`), a canvas its layout (`elements`). A component of a game overrides it for what it caches.
- **Only at the next load** (after a save): a canvas's `source` and `variants`, and anything a component reads in `Initialize()` or `OnAttached()` alone.

## Writing what a node holds (`scene_set_node`)

- `layer` is the node's draw layer, an `int32`. It goes through `SceneNode::SetLayer()`, which a write of the reflected field would bypass, leaving the draw order stale.
- The event, the unsaved mark and the editor's flags are as for `scene_set`.
- A node's name has no method: it is a `PoolString`, so a new name would grow the pool with a client's text.

## Codes

`no_scene`, `node_not_found`, `component_not_found`, `invalid_params` (a missing or mistyped param, a bad node path, `index`, `limit` or `layer` out of range) and for `path` and `value` the codes of the by-name methods (`invalid_path`, `not_found`, `not_a_value`, `interning`, ...). `scene_set_node` has `no_scene`, `node_not_found` and `invalid_params` alone.
