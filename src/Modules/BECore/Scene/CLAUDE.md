# Scene Module Guidelines

## Overview

Scene graph: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file agree (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built by `LoadSceneManagerFromConfig()` (`SceneLoader.h`: `SceneConfig` with its node references expanded, see "Node files") in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns a runtime root node (`"Root"`) and the serialized `_nodes`; optional view size `viewWidth` / `viewHeight` and `viewVariants` (see "View size"). `Update()`: "Gotchas"; `Draw()` runs `SceneDrawSystem` ("Draw order"). `PrepareForSave()` copies the live hierarchy back into `_nodes`, leaving generated top-level nodes out (`SceneConfigWriter.h`) |
| `SceneConfigWriter` | A live scene into its `SceneConfig` file (`SaveSceneToConfig`): the editor's Save and `scene_save` |
| `SceneNode` | `FACTORY_BASE`, XML element `node`: `_name`, `_layer` (attribute `layer`, default 0), `_components`, children. `_children` is reflected, so a node holds nested `<children>`. **`IsGenerated()`** (a plain member, never in a file): true for a node made at run time (`AddChild`, `CreateNode`, `InstantiateNode`), false for one read from a file or made by `DuplicateNode`; the code that made it makes it again at the next load, so Save and `DuplicateNode` skip it with its subtree and the editor's "Add node" clears it (`SetGenerated(false)`) |
| `ScenePick` | Picks by a point or a box, selection outlines: the viewport and `scene_pick` |
| `IComponent` | `FACTORY_BASE` and `SubscriptionHolder` (`Events/CLAUDE.md`); `GetNode()`, and `GetScene()`, which is the node's scene ("Component lifecycle") |
| `SceneNodeFiles` / `SceneLoader` | `SceneNodeFiles`: the expansion of `<node ref>` and the node loaders. `SceneLoader`: `LoadSceneManager(configRoot, loader)` builds the `SceneManager` from the expanded copy. `SceneNodeInstance`: one node to and from XML |

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

`type` is the component class name; field names lose the leading underscore. A `<node type>` makes a `SceneNode` subclass through its factory (unknown: an error and a plain node). A sleeping component has `enabled="false"` ("Component lifecycle"). A `scene` can carry `viewWidth` / `viewHeight` ("View size") and a node `layer="-1"` ("Draw order"). A game's `SceneConfig.xml` replaces the engine's. A node can live in a file (`<node ref="nodes/MyHud"/>`, "Node files").

**Nested nodes:** a node can hold `<children><node name layer>..</node></children>` after its `<components>`, to any depth, in file order, wired like a top-level node (`OnAttached` once). `<node ref>` is a top-level element only: not inside `<children>`.

## Node files

A node kept in a file of its own is referenced by name from a scene.

```xml
<!-- config/SceneConfig.xml -->
<scene type="Scene" name="MahjongScene"><nodes>
  <node name="Table"> ... </node>
  <node ref="nodes/MahjongHud"/>
</nodes></scene>

<!-- config/nodes/MahjongHud.xml -->
<node name="Hud" layer="10"><components>
  <component type="UICanvasComponent" .../>
</components></node>
```

- **Reference:** `<node ref="..."/>` inside `<nodes>`: a path relative to the `config` mount point, without `config/` and `.xml`: segments of `[A-Za-z0-9_-]` separated by `/` (`IsValidNodeRef`).
- **Node file:** its document element is one `<node name=".." layer="..">` with `<components>`, like an inline node; it is no reference, so no nesting and no cycle. The reference is replaced by a copy of it in place: the order of the references is the scene, draw and input order.
- **Overrides of an instance** ride on the reference and are merged into the file's node before the copy (`ApplyNodeOverrides`):
  ```xml
  <node ref="nodes/MahjongSeatPlate" name="SeatEast" layer="3"><components>
    <component type="TransformComponent" x="40"/></components></node>
  ```
  `name` and `layer` (`kNodeOverrideAttributes`) replace the node's; any other attribute but `ref` is an error. The one child is `<components>` of `<component type>`, each merged into the file's component of that type: an attribute replaces or adds, child elements replace the file's of that name whole. **A field is overridden whole in either spelling** (`color="1,2,3,4"` or `<color .../>`); both in one override, or a `type` the file lacks or a repeated one, is an error. Any problem drops the whole reference; a migration that rewrites components must treat a reference's `<component>` elements too.
- **Where the file is found:** `MakeFileSystemNodeLoader` walks the layers of `config` from the top; the first with `<ref>.xml` or `<ref>.bin` wins whatever the extension, and both in one layer drop the reference with an error. A `.bin` (a stamped `SceneNode`, `Config/CLAUDE.md`, "Binary configs") becomes the same `<node>` element, so overrides and `InstantiateNode` stay one implementation; it has no `contentVersion` and no node `type` (the cook refuses a subclass). `MakeDirectoryNodeLoader` reads XML only.
- **Errors never stop a game:** an invalid reference or override, or a missing, unparsable or wrong-version file, is one `LOG_ERROR`; the element is dropped and the rest loads.
- **The expansion is done on a copy:** `LoadSceneManager` deserializes an expanded copy; the cached document of `ConfigManager` keeps its references. A tool that reads the **file** calls `LoadSceneDocument`.
- **A cooked `SceneConfig.bin`** is read as it is by `LoadSceneManagerFromBinary` (stamp `SceneManager`, else one `LOG_ERROR` and an empty manager): the cook expanded the `<node ref>` and checked the version.
- **`sourceRef`:** `SceneNode::GetSourceRef()` names the node file of an instance (empty for an inline node); only the loader and `InstantiateNode` set it.
- **Runtime instantiation:** `Scene::InstantiateNode(reference, loader, parent, index)` expands as the XML does (`reference` is a `<node ref>` element with the same overrides), wires the node as `Initialize` does and inserts it into `parent` (default the root) at `index` (clamped, `kAppend` last). A bad reference, an uninitialized scene or a foreign parent: one `LOG_ERROR`, an empty pointer.
- **The editor saves an instance as `<node ref>`** with the overrides that differ (`BuildNodeReference`: the file without overrides is the baseline, both compared field by field, the result loaded back and refused unless it equals the live node). A node that gained, lost or reordered a component, differs in its type or in a field the overrides lack, **has authored children** (generated ones do not count), whose file is gone or changed, or saved without a loader, is written inline with one warning: its data is kept, the link is not. A nested node is always written inline, `<children>` recursively. An authored node under a generated parent is not written and is noted.
- **A saved node** (`AppendNodeElement`: Save, `DuplicateNode`) takes its own fields from the virtual `SceneNode::Serialize`, so a new reflected field needs nothing in the writer. **Only what differs from the defaults is written** (a node or component field equal to a fresh object's, `name` excepted; a scene attribute equal to a default `Scene`'s); the file is not byte for byte (pugixml respaces it).

## Editing the tree

`Scene::MoveNode(node, newParent, index)` reparents and reorders (null parent = root; `index` counts the children after the node left its place, clamped, `kAppend`); the components stay; false and unchanged for the root, a node or parent of another scene, or a parent that is the node or below it. `DuplicateNode(node)` deep-copies a subtree after the original, named `Name (copy)`, `Name (copy 2)`, ...; the top copy keeps `sourceRef`; empty for the root.

## Component lifecycle

1. Deserialization creates the component through `AbstractFactory<IComponent>` and calls `Initialize()` (the reflection hook: runs even when there is no node yet).
2. `Scene::Initialize()` → `SceneNode::WireToScene()` sets the node and calls `OnAttached()`. `AddComponent` does the same at runtime.
3. Do node-dependent setup in `OnAttached()`: event subscriptions, resource lookups, sibling components via `GetNode().GetComponent<T>()`. This runs for the components of **every** scene, so subscribe with `SubscribeInScene` (Gotchas).

**Detached** (before step 2, after `RemoveComponent` or a replacement by `AddComponent`, once the node died): `TryGetNode()` / `TryGetScene()` are null, `GetNode()` / `GetScene()` assert and `SubscribeInScene` joins no update list. What can run then (a destructor, a held component's public method or `UIInput` handler, a handler after it removed itself) asks the `Try` pair. A dying scene leaves the nodes of its tree, but a node `RemoveChild()` took out still names it: do not hold one longer than the scene.

**Sleeping** (`IComponent::IsEnabled()` false: the saved, hidden field `enabled`). At run time `SetEnabled()` writes it, also for `scene_set`, and calls `OnEnabledChanged()` on a change; a load calls no hook.
- Off: both update lists, `SubscribeInScene` handlers, the draw pass (`OnDraw()`, the quad or sprite, the clip of a `ClipRectComponent`), mouse input. Read at each call: no list is rebuilt.
- On: `Initialize()`, `OnAttached()`, `OnPropertyWritten()`, plain `Subscribe` handlers (one that cares asks `IsEnabled()`), calls of code.
- `TransformComponent` has `CanBeDisabled()` false: others read its fields.

## Draw order

Drawing is not an event. `Scene::Draw()` hands the pass to `SceneDrawSystem`, a loop over the scene's `SceneDrawList` (`SceneDrawList.h`): one entry per drawing component of a visible node, sorted by (`GetLayer()`, scene order, the component's index in its node). It is rebuilt only when `IsDrawListValid()` is false (an edit of the tree, of a node's components or of the editor flags); a steady frame only loops. The system draws the quad and the sprite from their fields, read at draw time; every other component draws through `IComponent::OnDraw()`. `HasOnDraw()` is asked once per rebuild: false leaves a component out (default true). The pass ends with `IRenderer::Flush()`; a custom `OnDraw()` needs no flush.

- **Culling:** a quad or sprite whose node rect cannot reach the render output (bars included) is skipped, its clip push too: no call, no counted primitive. A turned node's bound is loose; `OnDraw()` is never culled; a sizeless output culls nothing.
- A lower layer is drawn first (default 0, negative = background). On one layer: scene order, parent before children, later siblings on top. **Give a background a lower `layer`.** A child does not inherit its parent's layer.
- Only the active scene draws, so `OnDraw()` needs no `IsSceneActive()` check.
- **Override `OnDraw()`, never subscribe to draw.** `OnDraw()` only reads state and issues renderer calls: it must not add or remove nodes or components, call `SetLayer()` or change the editor flags: the list holds raw pointers and is stale after such a change (Debug asserts).
- **A node's layer changes only through `SceneNode::SetLayer()`**, which makes the list and the ranks stale: a direct write to the reflected `_layer` (reflection, a field visitor) shows in the order only after the next rebuild, which nothing triggers.
- `Scene::GetDrawRank(node)` is a node's position in this order (O(1) after any pass); `GetInputOrder(component)` = rank times `kMaxComponentsPerNode` plus the component's index, the key of every `UIInput` handler; `GetDrawPass()` counts the `Draw()` calls, so a target takes input only for a node that drew; a `ClipRectComponent`'s clip is pushed around the entries that name its node as clip owner.
- **Editor flags** (`SceneNode::GetEditorFlags()`: `Hidden`, `Locked`) are a plain member: no file, `Serialize` or `DuplicateNode` sees it, `Scene::ClearEditorFlags()` resets a tree, `SceneManager::ClearEditorFlags()` every loaded scene (the scene editor, when it dies). A hidden node and its subtree have no entry but keep item and rank and still update.

**Rotation, flip and blend** (renderer side: `.claude/rules/rendering-module.md`): `TransformComponent::_rotation` (degrees, clockwise) about `_pivotX` / `_pivotY`, **normalized in the node's rect** (0.5 = center), also for a `Contain` sprite; `_flipX` / `_flipY` mirror the UVs. `_blend` (`Alpha`, `Add`, `Multiply`) is scoped to that one draw. Not rotated: the hit rect of a pointer target, `TextComponent`, UI canvases.

## Object model

The components are the truth: reflection, the XML and the inspector build on `SceneNode` + `IComponent`. Each subsystem keeps a dense list derived from them and rebuilt only when its key moved (`SceneUpdateList`, `SceneDrawList`, the particle pools).

- It is **not** a pure ECS: component fields have no setter hooks, so a stored copy would be refreshed every frame. Only a write by name (`scene_set`) calls `IComponent::OnPropertyWritten(field)`.
- The system still makes one `IRenderer` call per quad or sprite; the renderer batches them (`.claude/rules/rendering-module.md`).

## View size

A scene says how big its picture is in scene units: `<scene type="Scene" name="GameScene" viewWidth="840" viewHeight="800">`. Everything in the scene (transforms, board origins and cell sizes) is in these units, and the engine fits the view to whatever the render output is:

- `Scene::Draw()` picks the active view (below) for the renderer's output size and sets `ViewTransform::Fit(...)` (one uniform scale, the view centered, bars on the free sides) with `IRenderer::SetViewTransform()` for the draw pass only.
- Without a view size (both 0, the default) the transform is the identity: one unit is one output pixel.
- Mouse positions arrive in output pixels; a handler maps them with `GetScene().ScreenToSceneX/Y` (the last draw's transform) before comparing with scene geometry.

**View variants.** A scene may declare other view sizes for other shapes of output, so a game lays itself out for portrait too:

```xml
<scene type="Scene" name="T" viewWidth="1280" viewHeight="720"><viewVariants>
  <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
</viewVariants><nodes>...
```

- `when` is the `UIVariantCondition` language (`UI/CLAUDE.md`), tested against `{0, 0, outputWidth, outputHeight}` in **output pixels**; the first variant that holds and has a positive finite size wins, else the default view. A malformed `when` is one logged error and drops that variant.
- **Decided in `Draw()`, announced in `Update()`.** The pick uses the output size of that very pass (the viewport's target is not the window), before any component draws, so `GetActiveView()` (`{name, width, height, index}`; -1 for the default) is right inside `OnDraw()`. No renderer: default view. `GetViewVersion()` moves exactly when the index changes. `Update()` sends `SceneEvents::SceneViewChangedEvent {scene, view}` once per change, before the fixed updates (`Draw()` never: it must not run handlers that change the scene).
- `GetViewWidth()` / `GetViewHeight()` are the **active** view's (the default's before the first draw).
- Both are saved by `Scene::Serialize`. The control API and the editor viewport draw through `Scene::Draw`, so they follow their own output size.
- **A tool's override.** `GetViewVariants()` lists the variants. `SetViewOverride(optional<int32_t>)` replaces the pick: -1 the default view, `i` variant `i`, nullopt or out of range the normal pick. Not saved; the next `Draw()` picks. A tool sets it right before its own pass and clears it right after (`UI/CLAUDE.md`, "Editor preview").

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height`, `_rotation`, `_pivotX`, `_pivotY` | Position/size in scene units (pixels when the scene has no view size); others read it. Rotation: "Rotation, flip and blend" |
| `QuadRendererComponent` | `_color`, `_blend` | `HasOnDraw()` is false: `SceneDrawSystem` draws a filled rect at the node's transform; one quad per node counts |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect`, `_fit`, `_blend`, `_flipX`, `_flipY` | `HasOnDraw()` is false: `SceneDrawSystem` draws it; one sprite per node counts, none without a texture. `_fit` (`SpriteFit`): `Stretch` (default, covers the node's rect) or `Contain` (keeps the aspect of `_srcRect` or the texture, centered; `FitSpriteRect()`). |
| `PointerTargetComponent` | none (base) | The base of a scene object that takes the pointer at its place in the draw order: shape and pointer callbacks (`Input/CLAUDE.md`) |
| `ClickableComponent` | `_actionName`, `_shape`, `_blocksInput` | A `PointerTargetComponent`: a press in the rect (or ellipse) emits `InputEvents::ActionEvent` |
| `ClipRectComponent` | none | The node's transform rect clips the node and its subtree, what they draw and what they hit (`Input/CLAUDE.md`) |
| `TextComponent` | `_text`, `_fontPath`, `_fontSize`, `_color`, `_align`, ... | Draws text in the node's transform box in `OnDraw()` (`Text/CLAUDE.md`) |
| `UICanvasComponent` | `_space`, `_scaleMode`, `_modal`, `_visible`, `_inputEnabled`, `_elements`, ... | A canvas of in-game UI: owns an element tree, lays it out for the output, draws it with its own view transform and takes mouse input through `UIInput` (`UI/CLAUDE.md`) |
| `ParticleEmitterComponent` | `_effect`, `_playOnStart`, `_burstAction` | Plays a `ParticleEffect` at the node, one `DrawGeometry` batch (`Particles/CLAUDE.md`) |
| `RenderProbeComponent` | none | Debug: cells of the renderer primitives, to look at both backends |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |

## Adding a component

1. Class in `Components/` (or a game module's `Components/`) deriving `IComponent` with `BE_CLASS(Name)`; fields get `BE_REFLECT_FIELD`.
2. Include `<Generated/Name.gen.hpp>` in its `.cpp`; the directory must be in `reflection_targets.json`; re-run `cmake --preset ...`.
3. Subscribe in `OnAttached()` through `SubscribeInScene<Event, &Name::Handler>(this)`. Plain `Subscribe` is only for a handler that must run whichever scene is active; `SubscribeInScene` in `Initialize()` joins the list when added at runtime but takes the channel path when read from a file. Drawing is no subscription ("Draw order").
4. Use it from `config/SceneConfig.xml` with `type="Name"`.

## Gotchas

- **Update events are lists, input events are global.** `SubscribeInScene` of `SceneUpdateEvent` / `SceneFixedUpdateEvent` registers in the scene's list (`SceneUpdateList.h`), which `Scene::Update()` runs when the scene was active at its start: no tree walk, no per-call filter. Last registered first, stable; a component leaves with its node or `RemoveComponent`, and one added to a removed node never joins. A handler may add and remove nodes and components: an added one first runs next pass, a removed one is skipped. `MouseButtonDownEvent` and `ActionEvent` are global `EventBase` events: `SubscribeInScene` filters them per call by `IsActive()`, plain `Subscribe` makes an inactive scene react.
- A component that reads a model owned by its `IGame` does nothing unless `GameManager::GetActive()` is that game. `SceneEventsTest.cpp` loads scenes from an XML string: copy its setup.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look up again.
