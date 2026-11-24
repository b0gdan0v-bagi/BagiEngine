# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built by `LoadSceneManagerFromConfig()` (`SceneLoader.h`: `SceneConfig` with its node references expanded, see "Node files") in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`; optional view size `viewWidth` / `viewHeight` and `viewVariants` (see "View size"). `Update()` runs the fixed update list once per step due, then the frame list ("Gotchas"), and emits both events for plain subscribers; `Draw()` runs `SceneDrawSystem` ("Draw order"). `InstantiateNode()` adds a node file at runtime. `PrepareForSave()` copies the live hierarchy back into `_nodes` (`SceneConfigWriter.h`) |
| `SceneNode` | `FACTORY_BASE`, XML element `node`: `_name`, `_layer` (attribute `layer`, default 0), `_components`, children, an `entt::entity`. `_components` / `_children` are `SmallVector`s (`ComponentList` / `ChildList`, the same saved text). `AddComponent`, `RemoveComponent`, `GetComponent<T>()`, `AddChild`. `_children` is reflected, so a node holds nested `<children>` in the file |
| `IComponent` | `FACTORY_BASE` and `SubscriptionHolder` (128 bytes, `Events/CLAUDE.md`); gets its `Scene&` / `SceneNode&` through `GetScene()` / `GetNode()` |
| `SceneNodeFiles` / `SceneLoader` | `SceneNodeFiles`: the expansion of `<node ref>` (`ExpandNodeReference(s)`, `ApplyNodeOverrides`, the node loaders, `LoadSceneDocument`). `SceneLoader`: `LoadSceneManager(configRoot, loader)` / `LoadSceneManagerFromConfig()` build the `SceneManager` from the expanded copy. `SceneNodeInstance`: `DeserializeNode`, `BuildNodeReference`. See "Node files" |

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

`type` is the component class name; field names lose the leading underscore. A `scene` can carry `viewWidth` / `viewHeight` ("View size") and a node `layer="-1"` ("Draw order"). A game's `SceneConfig.xml` replaces the engine's. A node can live in a file (`<node ref="nodes/MyHud"/>`, "Node files").

**Nested nodes:** a node can hold `<children><node name layer>..</node></children>` after its `<components>`, to any depth, in file order, wired like a top-level node (entity, `OnAttached` once). `<node ref>` is a top-level element only: not inside `<children>`.

## Node files

A game can split its one `SceneConfig.xml`: a node kept in a file of its own is referenced by name from a scene.

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
  `name` and `layer` (a new field of `SceneNode` extends `kNodeOverrideAttributes`) replace the node's; any other attribute but `ref` is an error. The one child is `<components>` of `<component type>`, each merged into the file's component of that `type`: an attribute replaces or adds, child elements replace the file's child elements of that name whole (an `<elements>` override replaces the canvas's whole list). **A field is overridden whole in either spelling** (`color="1,2,3,4"` or `<color .../>`); both in one override is an error. A `type` the file lacks, or given twice, is an error. Any problem drops the whole reference, never half applied. A migration that rewrites components must also treat the `<component>` elements of a reference.
- **Where the file is found:** `MakeFileSystemNodeLoader` walks the layers of `config` from the top; the first with `<ref>.xml` or `<ref>.bin` wins whatever the extension, and both in one layer drop the reference with an error. A `.bin` (a stamped `SceneNode`, `Config/CLAUDE.md`, "Binary configs") becomes the same `<node>` element (`DeserializeNode(IDeserializer&)`, `AppendNodeElement`), so overrides and `InstantiateNode` stay one implementation; it has no `contentVersion` (the cook checked it). `MakeDirectoryNodeLoader` reads XML only.
- **Errors never stop a game:** an invalid reference or override, or a missing, unparsable or wrong-version file, is one `LOG_ERROR`; the element is dropped and the rest loads.
- **The expansion is done on a copy:** `LoadSceneManager` deserializes an expanded copy; the cached document of `ConfigManager` keeps its references. A tool that reads the **file** calls `LoadSceneDocument(path, document)`.
- **A cooked `SceneConfig.bin`** is read as it is by `LoadSceneManagerFromBinary` (stamp `SceneManager`, else one `LOG_ERROR` and an empty manager): the cook expanded the `<node ref>` and checked the version, so the load does neither.
- **`sourceRef`:** `SceneNode::GetSourceRef()` names the node file of an instance (empty for an inline node), so the editor can tell it from a typed node; only the loader and `InstantiateNode` set it.
- **Runtime instantiation:** `Scene::InstantiateNode(reference, loader, parent, index)` expands as the XML does (`reference` is a `<node ref name layer>` element with the same overrides), wires the node as `Initialize` does and inserts it into `parent` (default the root, of this scene) at `index` (clamped, `kAppend` last). A bad reference, an uninitialized scene or a foreign parent: one `LOG_ERROR`, an empty pointer.
- **The editor saves an instance as `<node ref>`** with the overrides that differ (`BuildNodeReference`: the file without overrides is the baseline, both sides serialized and compared field by field, the result loaded back and refused unless it equals the live node). A node that gained, lost or reordered a component, **has children** (the override language has none), whose file is gone or changed, or saved without a loader, is written inline with one warning: its data is kept, the link is not. A nested node is always written inline; the writer writes `<children>` recursively. The editor never edits the node file.

## Editing the tree

`Scene::MoveNode(node, newParent, index)` reparents and reorders (null parent = root; `index` counts the children after the node left its place, clamped, `kAppend`); entity and components stay; false and unchanged for the root, a node or parent of another scene, or a parent that is the node or below it. `DuplicateNode(node)` deep-copies a subtree after the original, named `Name (copy)`, `Name (copy 2)`, ...; the top copy keeps `sourceRef`; empty for the root.

## Component lifecycle

1. Deserialization creates the component through `AbstractFactory<IComponent>` and calls `Initialize()` (the reflection hook: runs even when there is no node yet).
2. `Scene::Initialize()` → `SceneNode::WireToScene()` sets `_scene` / `_node` and calls `OnAttached()`. `AddComponent` does the same at runtime.
3. Do node-dependent setup in `OnAttached()`: event subscriptions, resource lookups, sibling components via `GetNode().GetComponent<T>()`. This runs for the components of **every** loaded scene, active or not, so subscribe with `SubscribeInScene` (Gotchas); for the two update events that registers in the scene's update list.

## Draw order

Drawing is not an event. `Scene::Draw()` hands the pass to `SceneDrawSystem`, a loop over the scene's `SceneDrawList` (`SceneDrawList.h`): one entry per drawing component of a visible node, sorted by (`GetLayer()`, scene order, the component's index in its node). It is rebuilt only when `IsDrawListValid()` is false (a node added, removed, moved or put on another layer, a component added or removed, the editor flags changed); a steady frame only loops. The quad and the sprite are drawn by the system from their fields, read at draw time (no hook); every other component through `IComponent::OnDraw()`. `HasOnDraw()` is asked once per rebuild: false leaves a component out (default true, so a game's component that overrides `OnDraw()` needs nothing).

- A lower layer is drawn first (default 0, negative = background). On one layer: scene order, parent before children, later siblings on top. **Give a background a lower `layer`**; the order never depends on subscriptions. A child does not inherit its parent's layer.
- Only the active scene draws, so `OnDraw()` needs no `IsSceneActive()` check. `RemoveChild()` destroys the entities of the removed subtree.
- **Override `OnDraw()`, never subscribe to draw.** `OnDraw()` only reads state and issues renderer calls: it must not add or remove nodes or components, call `SetLayer()` or change the editor flags: the list holds raw pointers and is stale after such a change (Debug asserts).
- **A node's layer changes only through `SceneNode::SetLayer()`**, which makes the list and the ranks stale: a direct write to the reflected `_layer` (reflection, a field visitor) shows in the order only after the next rebuild, which nothing triggers.
- `Scene::GetDrawRank(node)` is a node's position in this order (a `SceneDrawItem` in the registry, written with the list, so O(1) after any pass); `GetInputOrder(component)` = rank times `kMaxComponentsPerNode` plus the component's index, the key of every `UIInput` handler; `GetDrawPass()` counts the `Draw()` calls, so a target takes input only for a node that drew; the clip of a `ClipRectComponent` is pushed around the entries that name its node as clip owner (`SceneDrawList::clips`).
- **Editor flags** (`SceneNode::GetEditorFlags()`: `Hidden`, `Locked`) are a plain member: no file, `Serialize` or `DuplicateNode` sees it, `Scene::ClearEditorFlags()` resets a tree, `SceneManager::ClearEditorFlags()` every loaded scene (the scene editor calls it when it dies). A hidden node and its subtree have no entry but keep item and rank and still update.

**Rotation, flip and blend** (renderer side: `.claude/rules/rendering-module.md`): `TransformComponent::_rotation` (degrees, clockwise) about `_pivotX` / `_pivotY`, **normalized in the node's rect** (0.5 = center), also for a `Contain` sprite; `_flipX` / `_flipY` mirror the UVs. `_blend` (`Alpha`, `Add`, `Multiply`) is scoped to that one draw. Not rotated: the hit rect of a pointer target, `TextComponent`, UI canvases.

## Why EnTT

The object model stays `SceneNode` + `IComponent` (reflection, the XML and the inspector build on it). The `entt::registry` of a `Scene` holds one entity per node and the `SceneDrawItem` (order, rank) written with the draw list ("Draw order"); the components stay the truth.

- It is **not** a pure ECS: the components are not stored in EnTT pools. Component fields have no setter hooks, so a pool copy would be refreshed every frame, which costs the read it was meant to save.
- One `IRenderer` call per quad or sprite, nothing batched (`.claude/rules/rendering-module.md`).

## View size

A scene says how big its picture is in scene units: `<scene type="Scene" name="GameScene" viewWidth="840" viewHeight="800">`. Everything in the scene (transforms, board origins and cell sizes) is in these units, and the engine fits the view to whatever the render output is:

- `Scene::Draw()` asks the renderer for its output size, picks the active view (below), builds `ViewTransform::Fit(activeWidth, activeHeight, outputW, outputH)` (one uniform scale, the view centered, bars on the free sides), sets it with `IRenderer::SetViewTransform()` for the draw pass and resets it after.
- Without a view size (both 0, the default) the transform is the identity: one unit is one output pixel.
- Mouse positions arrive in output pixels; a handler maps them with `GetScene().ScreenToSceneX/Y` (the last draw's transform) before comparing with scene geometry.

**View variants.** A scene may declare other view sizes for other shapes of output, so a game lays itself out for portrait too:

```xml
<scene type="Scene" name="T" viewWidth="1280" viewHeight="720"><viewVariants>
  <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
</viewVariants><nodes>...
```

- `when` is the `UIVariantCondition` language (`UI/CLAUDE.md`), tested against `{0, 0, outputWidth, outputHeight}` in **output pixels**; the first variant that holds and has a positive finite size wins, else the default view. A malformed `when` is one logged error and drops that variant. The pick is the pure `SceneViewSelector::Pick`.
- **Decided in `Draw()`, announced in `Update()`.** The pick uses the output size of that very pass (the editor viewport's target is not the window), before the fit and before any component draws, so `GetActiveView()` (`{name, width, height, index}`; -1 and an empty name for the default) is right already inside `OnDraw()`. No renderer: the default view. `GetViewVersion()` moves exactly when the index changes. `Update()` sends `SceneEvents::SceneViewChangedEvent {scene, view}` once per change (before the fixed updates), never `Draw()`, which must not run handlers that change the scene.
- `GetViewWidth()` / `GetViewHeight()` are the **active** view's (the default's before the first draw).
- Both are saved by `Scene::Serialize` (`viewWidth` / `viewHeight` always, `0` / `0` loads as no view; an empty variant list writes nothing), also through the scene editor. The control API and the editor viewport draw through `Scene::Draw`, so they follow their own output size.
- **A tool's override.** `GetViewVariants()` lists the variants (read only). `SetViewOverride(optional<int32_t>)` replaces the pick: -1 the default view, `i` variant `i`, nullopt or out of range the normal pick. Not saved; the next `Draw()` picks (same version bump and event). A tool sets it right before its own pass and clears it right after (`UI/CLAUDE.md`, "Editor preview").

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height`, `_rotation`, `_pivotX`, `_pivotY` | Position/size in scene units (pixels when the scene has no view size); others read it. `_rotation` (degrees, clockwise) turns the node's quad or sprite about the pivot ("Rotation, flip and blend"); hit tests, text and UI ignore it |
| `QuadRendererComponent` | `_color`, `_blend` | `_blend` is `Alpha` (default), `Add` or `Multiply` for this quad's draw only. `HasOnDraw()` is false: `SceneDrawSystem` draws a filled rect at the node's transform; one quad per node counts |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect`, `_fit`, `_blend`, `_flipX`, `_flipY` | `HasOnDraw()` is false: `SceneDrawSystem` draws it; one sprite per node counts, none without a texture. `_spriteId` resolves through `TextureLibrary`. `_fit` (`SpriteFit`): `Stretch` (default, covers the node's rect) or `Contain` (keeps the aspect of `_srcRect` or the texture, centered; `FitSpriteRect()`). |
| `PointerTargetComponent` | none (base) | The base of a scene object that takes the pointer at its place in the draw order: shape, press / drag / release / enter / exit / move / wheel callbacks (`Input/CLAUDE.md`) |
| `ClickableComponent` | `_actionName`, `_shape`, `_blocksInput` | A `PointerTargetComponent`: a press in the rect (or ellipse) emits `InputEvents::ActionEvent` |
| `ClipRectComponent` | none | The node's transform rect clips the node and its subtree, what they draw and what they hit (`Input/CLAUDE.md`) |
| `TextComponent` | `_text`, `_fontPath`, `_fontSize`, `_color`, `_align`, `_overflow`, `_wrap`, `_lineSpacing`, `_minFontSize` | Draws text in the node's transform box in `OnDraw()` (`Text/CLAUDE.md`) |
| `UICanvasComponent` | `_space`, `_scaleMode`, `_referenceWidth`, `_referenceHeight`, `_match`, `_minScale`, `_maxScale`, `_useSafeArea`, `_modal`, `_visible`, `_inputEnabled`, `_elements`, `_variants` | A canvas of in-game UI: owns an element tree, lays it out for the output, draws it with its own view transform and takes mouse input through `UIInput` (`UI/CLAUDE.md`) |
| `ParticleEmitterComponent` | `_effect`, `_playOnStart`, `_burstAction` | Plays a `ParticleEffect` at the node, one `DrawGeometry` batch (`Particles/CLAUDE.md`) |
| `RenderProbeComponent` | none | Debug: a row of cells that show the renderer primitives, to look at both backends |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |

## Adding a component

1. Class in `Components/` (or a game module's `Components/`) deriving `IComponent` with `BE_CLASS(Name)`; fields get `BE_REFLECT_FIELD`.
2. Include `<Generated/Name.gen.hpp>` in its `.cpp`; the directory must be in `reflection_targets.json`; re-run `cmake --preset ...`.
3. Subscribe in `OnAttached()` through `SubscribeInScene<Event, &Name::Handler>(this)`. Plain `Subscribe` is only for a handler that must run whichever scene is active; one that subscribes in `Initialize()` joins the list when added at runtime (`AddComponent` sets the scene first) but takes the channel path when read from a file. A component that draws overrides `OnDraw()` instead of subscribing (see "Draw order"); `HasOnDraw()` returns false only for one that never draws.
4. Use it from `config/SceneConfig.xml` with `type="Name"`. The inspector shows its reflected fields automatically.

## Gotchas

- **Update events are lists, input events are global.** `SubscribeInScene` of `SceneUpdateEvent` / `SceneFixedUpdateEvent` registers in the scene's list (`SceneUpdateList.h`), which `Scene::Update()` runs when the scene was active at its start: no tree walk, no per-call filter. Last registered first, stable; a component leaves with its node or `RemoveComponent`. A handler may add and remove nodes and components: an added one first runs next pass, a removed one not yet run is skipped. A hand `Emit()` of the two events reaches plain subscribers only. `MouseButtonDownEvent` and `ActionEvent` are global `EventBase` events and `OnAttached()` runs for the components of **every** scene at load: `SubscribeInScene` filters them per call by `IsActive()`, plain `Subscribe` makes an inactive scene react; not inside a handler.
- A component that reads a model owned by its `IGame` does nothing unless `GameManager::GetActive()` is that game. `SceneEventsTest.cpp` loads scenes from an XML string: copy its setup.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look up again.
- Do not draw from `Update` or change state from `Draw`; the frame order is in `BECore/CLAUDE.md`. Movement and animation scale by `SceneUpdateEvent::deltaTime`.
