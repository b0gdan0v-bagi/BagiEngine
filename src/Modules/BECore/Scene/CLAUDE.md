# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built by `LoadSceneManagerFromConfig()` (`SceneLoader.h`: `SceneConfig` with its node references expanded, see "Node files") in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`; optional view size `viewWidth` / `viewHeight` and `viewVariants` (see "View size"). `Update()` emits `SceneFixedUpdateEvent` once per fixed step due, then `SceneUpdateEvent` with the frame time (`CoreManager::GetTime()`); `Draw()` runs `SceneDrawSystem` ("Draw order"). `IsActive()`: the scene `SceneManager` drives. `InstantiateNode()` adds a node file at runtime ("Node files"). `PrepareForSave()` copies the live hierarchy back into `_nodes` (the editor's save path: `WriteSceneToConfig`, `src/Widgets/SceneEditor/SceneConfigWriter.h`) |
| `SceneNode` | `FACTORY_BASE`, XML element `node`: `_name`, `_layer` (attribute `layer`, default 0), `_components`, children, an `entt::entity`. `AddComponent`, `RemoveComponent`, `GetComponent<T>()`, `AddChild`. `_children` is reflected, so a node holds nested `<children>` in the file ("XML shape") |
| `IComponent` | `FACTORY_BASE` and `SubscriptionHolder`; gets its `Scene&` / `SceneNode&` through `GetScene()` / `GetNode()` |
| `SceneNodeFiles` / `SceneLoader` | `SceneNodeFiles.{h,cpp}`: the expansion of `<node ref>` (`IsValidNodeRef`, `ExpandNodeReference(s)`, `ApplyNodeOverrides`, the node loaders, `LoadSceneDocument`). `SceneLoader.{h,cpp}`: `LoadSceneManager(configRoot, loader)` / `LoadSceneManagerFromConfig()` build the `SceneManager` from the expanded copy. `SceneNodeInstance.{h,cpp}`: `DeserializeNode`, `BuildNodeReference`. See "Node files" |

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

`type` is the component class name; field names lose the leading underscore. A `scene` can carry `viewWidth` / `viewHeight` ("View size") and a node `layer="-1"` ("Draw order"). A game's `SceneConfig.xml` replaces the engine's. A node can also live in a file of its own (`<node ref="nodes/MyHud"/>`, "Node files").

**Nested nodes:** a node can hold `<children><node name layer>..</node></children>` after its `<components>`, to any depth; the tree is the file order (parent before children, siblings in order) and loads wired like a top-level node (entity, `OnAttached` once). The field is optional, so a file without it keeps its meaning (no migration step). `<node ref>` is a top-level element only: not inside `<children>`.

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
- **Where the file is found:** `FileSystem::ResolvePath("config/<ref>.xml")`, so a game's `config/nodes/X.xml` replaces the engine's; a tool with only a `SceneConfig.xml` path resolves next to it (`MakeDirectoryNodeLoader`).
- **Errors never stop a game:** an invalid reference or override, or a missing, unparsable or wrong-version file, is one `LOG_ERROR` naming the scene and the reference; the element is dropped and the rest loads. The copy loses `contentVersion` (`Config/CLAUDE.md`).
- **The expansion is done on a copy:** `LoadSceneManager` deserializes an expanded copy; the cached document of `ConfigManager` keeps its references. Whatever reads the loaded scene sees an inline scene. A tool that reads the **file** calls `LoadSceneDocument(path, document)`.
- **`ConfigManager` also loads a node file as a config named by its stem**: it must not equal a real config's name, so a game prefixes its node files.
- **`sourceRef`:** `ExpandNodeReference` (one reference: load, overrides, the copy; `ExpandScene` is built on it) can mark the copy `sourceRef="<ref>"`. `LoadSceneManager` does, so `SceneNode::GetSourceRef()` names the node file of an instance (empty for an inline node) and the editor can tell it from a node the user typed. Only the loader and `InstantiateNode` set it; a hand-written one is not supported.
- **Runtime instantiation:** `Scene::InstantiateNode(reference, loader, parent, index)` expands as the XML does (`reference` is a `<node ref name layer>` element with the same overrides, which a game writes as text and loads with `XmlDocument`), deserializes (`DeserializeNode`), wires the node as `Initialize` does (entity, `OnAttached`) and inserts it into `parent` (default the root, of this scene) at `index` (clamped, `kAppend` last); the tree gives the order. The `ref` string and `XmlNode` overloads use the layered `FileSystem`. A bad reference, an uninitialized scene or a foreign parent: one `LOG_ERROR`, an empty pointer.
- **The editor saves an instance as `<node ref>`** with the overrides that differ (`BuildNodeReference`: the file without overrides is the baseline, both sides serialized and compared field by field, the result loaded back and refused unless it equals the live node). A node that gained, lost or reordered a component, **has children** (the override language has none), whose file is gone or changed, or saved without a loader, is written inline with one warning: its data is kept, the link is not. A nested node is always written inline (a warning when it was an instance); the writer writes `<children>` recursively. The editor never edits the node file.
- Not built: keyed merge of UI elements in an override, adding or removing a component or children in an instance, `<scene ref>`.

Tests: `SceneNodeFilesTest`, `SceneSplitTest`, `SceneInstantiateTest`, `SceneNodeInstanceTest`, `SceneEditorSaveTest`, `SceneNestedNodesTest`.

## Editing the tree

`Scene::MoveNode(node, newParent, index)` reparents and reorders (null parent = root; `index` counts the children after the node left its place, clamped, `kAppend`); it keeps entity and components (no second `OnAttached`); false, nothing changed, for the root, a node or parent of another scene, or a parent that is the node or below it. `DuplicateNode(node)` deep-copies a subtree after the original (`AppendNodeElement`, `DeserializeNode`, wired like `InstantiateNode`), named `Name (copy)`, `Name (copy 2)`, ... (`MakeUniqueSiblingName`); the top copy keeps `sourceRef`; empty for the root.

## Component lifecycle

1. Deserialization creates the component through `AbstractFactory<IComponent>` and calls `Initialize()` (the reflection hook: runs even when there is no node yet).
2. `Scene::Initialize()` → `SceneNode::WireToScene()` sets `_scene` / `_node` and calls `OnAttached()`. `AddComponent` does the same at runtime.
3. Do node-dependent setup in `OnAttached()`: event subscriptions, resource lookups, sibling components via `GetNode().GetComponent<T>()`. This runs for the components of **every** loaded scene, active or not, so subscribe with `SubscribeInScene` (see Gotchas).

## Draw order

Drawing is not an event. `Scene::Draw()` hands the pass to `SceneDrawSystem`: a depth-first walk writes a `SceneDrawItem {layer, order, node}` per node and mirrors its transform and renderer data into the registry (below); the storage is sorted by (`GetLayer()`, `order`) and iterated. A node's components draw in component order: the quad and the sprite by the system from the registry, every other component through `IComponent::OnDraw()`.

- A lower layer is drawn first (default 0, negative = background). On one layer: scene order, parent before children, later siblings on top. **Give a background a lower `layer`**; the order never depends on subscriptions. A child does not inherit its parent's layer.
- Only the active scene draws, so `OnDraw()` needs no `IsSceneActive()` check. `RemoveChild()` destroys the entities of the removed subtree.
- **Override `OnDraw()`, never subscribe to draw.** `OnDraw()` only reads state and issues renderer calls: it must not add or remove nodes or components, and must not write a `TransformComponent` or a renderer field (the mirror was copied before the pass: the change shows a frame late and a released texture would dangle).
- `Scene::GetDrawRank(node)` is a node's position in this order without a draw (input handlers use it to ask the topmost first).
- **Editor flags** (`SceneNode::GetEditorFlags()`: `Hidden`, `Locked`) are a plain member: no file, `Serialize` or `DuplicateNode` sees it, `Scene::ClearEditorFlags()` resets the tree. A hidden node and its subtree draw nothing (quad, sprite, `OnDraw()`) but keep item and rank and still update.

**Rotation, flip and blend** (renderer side: `.claude/rules/rendering-module.md`): `TransformComponent::_rotation` (degrees, clockwise) about `_pivotX` / `_pivotY`, **normalized in the node's rect** (0.5 = center), also for a `Contain` sprite; `_flipX` / `_flipY` mirror the UVs. `_blend` (`Alpha`, `Add`, `Multiply`) is scoped to that one draw (Vulkan: non-`Alpha` draws as `Alpha`, one warning). Not rotated: `ClickableComponent`'s hit rect, `TextComponent`, UI canvases.

**The mirror** (`SceneRenderData.h`, written only by the system): `TransformData`, `QuadDrawData`, `SpriteDrawData` on the node's entity, copied from the components at the start of every `Draw()`; game code does not read it. At most one quad and one sprite per node count; without a `TransformData` (or a texture) nothing draws.

Tests: `SceneDrawSystemTest`, `SceneRenderDataTest` (the mirror).

## Why EnTT

The object model stays `SceneNode` + `IComponent` (reflection, the XML and the inspector build on it). The `entt::registry` of a `Scene` holds the data **systems** work on, sorted: `SceneDrawItem` (the order) and the mirror of the two built-in renderers ("Draw order"; the components stay the truth), whose `IRenderer` calls `SceneDrawSystem` issues.

- It is **not** a pure ECS: the components are not stored in EnTT pools; the registry holds one entity per node and the data of the systems only. The other drawing components still draw through `OnDraw()`.
- The direction is a system that hands a **batch** to `IRenderer` (entries `rendering-2d`, `renderer-backends`). Today: one call per quad or sprite, nothing batched (`.claude/rules/rendering-module.md`).

## View size

A scene says how big its picture is in scene units: `<scene type="Scene" name="GameScene" viewWidth="840" viewHeight="800">`. Everything in the scene (transforms, board origins and cell sizes) is in these units, and the engine fits the view to whatever the render output is:

- `Scene::Draw()` asks the renderer for its output size, picks the active view (below), builds `ViewTransform::Fit(activeWidth, activeHeight, outputW, outputH)` (one uniform scale, the view centered, bars on the free sides), sets it with `IRenderer::SetViewTransform()` for the draw pass and resets it after. Computed every frame, so a resize needs no event.
- Without a view size (both 0, the default) the transform is the identity: one unit is one output pixel.
- Mouse positions arrive in output pixels (window pixels in Game mode, viewport-local in the editor). A handler maps them with `GetScene().ScreenToSceneX(e.button.x)` / `ScreenToSceneY(...)` (the transform of the last draw, the identity before the first) before comparing with scene geometry, as `ClickableComponent` does.
- Pick the view size as the bounding box of the content plus the margins to keep (a board of 640x640 at (100, 80) gives 840x800); the HUD lives in the margins as a `SceneView` canvas (`UI/CLAUDE.md`).

**View variants.** A scene may declare other view sizes for other shapes of output, so a game lays itself out for portrait too:

```xml
<scene type="Scene" name="T" viewWidth="1280" viewHeight="720"><viewVariants>
  <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
</viewVariants><nodes>...
```

- `when` is the `UIVariantCondition` language (`UI/CLAUDE.md`), tested against `{0, 0, outputWidth, outputHeight}` in **output pixels**; the first variant that holds and has a positive finite size wins, else the default view. A malformed `when` is one logged error and drops that variant. The pick is the pure `SceneViewSelector::Pick`.
- **Decided in `Draw()`, announced in `Update()`.** The pick uses the output size of that very pass (the editor viewport's target is not the window), before the fit and before any component draws, so `GetActiveView()` (`{name, width, height, index}`; -1 and an empty name for the default) is right already inside `OnDraw()`. No renderer: the default view. `GetViewVersion()` moves exactly when the index changes. `Update()` sends `SceneEvents::SceneViewChangedEvent {scene, view}` once per change (before the fixed updates), never `Draw()`, which must not run handlers that change the scene.
- `GetViewWidth()` / `GetViewHeight()` are the **active** view's (the default's before the first draw).
- Both are saved by `Scene::Serialize` (`viewWidth` / `viewHeight` always, `0` / `0` loads as no view; an empty variant list writes nothing), also through the scene editor. The control API and the editor viewport draw through `Scene::Draw`, so they follow the view of their own output size.
- **A tool's override.** `GetViewVariants()` lists the variants (read only). `SetViewOverride(optional<int32_t>)` replaces the pick: -1 the default view, `i` variant `i`, nullopt or out of range the normal pick. Not saved; the next `Draw()` picks (same version bump and event). A tool sets it right before its own pass and clears it right after (`UI/CLAUDE.md`, "Editor preview").

Tests: `ViewTransformTest`, `SceneViewTest`, `SceneViewSelectorTest`.

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height`, `_rotation`, `_pivotX`, `_pivotY` | Position/size in scene units (pixels when the scene has no view size); others read it. `_rotation` (degrees, clockwise) turns the node's quad or sprite about the pivot ("Rotation, flip and blend"); hit tests, text and UI ignore it. `SceneDrawSystem` mirrors it as `TransformData` |
| `QuadRendererComponent` | `_color`, `_blend` | `_blend` is `Alpha` (default), `Add` or `Multiply` for this quad's draw only. Has no `OnDraw()`: `SceneDrawSystem` draws a filled rect at the node's transform from its registry copy (`QuadDrawData`) |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect`, `_fit`, `_blend`, `_flipX`, `_flipY` | Has no `OnDraw()`: `SceneDrawSystem` draws it from its registry copy (`SpriteDrawData`); the component keeps the fields and the texture handle. `_spriteId` resolves through `TextureLibrary`. `_fit` (`SpriteFit`) is `Stretch` (default: the texture covers the node's rect) or `Contain` (the aspect of `_srcRect`, or of the whole texture when it is empty, is kept and the sprite is centered; `FitSpriteRect()`). `SetTexture()` hands it a texture not from the library; `ReloadTexture()` drops it |
| `ClickableComponent` | `_actionName` | On mouse-down inside the transform rect, emits `InputEvents::ActionEvent` |
| `TextComponent` | `_text`, `_fontPath`, `_fontSize`, `_color`, `_align`, `_overflow`, `_wrap`, `_lineSpacing`, `_minFontSize` | Draws text in the node's transform box in `OnDraw()` (`Text/CLAUDE.md`) |
| `UICanvasComponent` | `_space`, `_scaleMode`, `_referenceWidth`, `_referenceHeight`, `_match`, `_minScale`, `_maxScale`, `_useSafeArea`, `_modal`, `_visible`, `_inputEnabled`, `_elements`, `_variants` | A canvas of in-game UI: owns an element tree, lays it out for the output, draws it with its own view transform and takes mouse input through `UIInput` (`UI/CLAUDE.md`) |
| `RenderProbeComponent` | none | Debug: a row of cells that show the renderer primitives, to look at both backends |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |

## Adding a component

1. Class in `Components/` (or a game module's `Components/`) deriving `IComponent` with `BE_CLASS(Name)`; fields get `BE_REFLECT_FIELD`.
2. Include `<Generated/Name.gen.hpp>` in its `.cpp`; the directory must be in `reflection_targets.json`; re-run `cmake --preset ...`.
3. Subscribe in `OnAttached()` through `SubscribeInScene<Event, &Name::Handler>(this)`. Plain `Subscribe` is only for a handler that must run whichever scene is active. A component that draws overrides `OnDraw()` instead of subscribing (see "Draw order").
4. Use it from `config/SceneConfig.xml` with `type="Name"`. The inspector shows its reflected fields automatically.

## Gotchas

- The update and input events (`SceneUpdateEvent`, `SDLEvents::MouseButtonDownEvent`, `InputEvents::ActionEvent`) are **global** `EventBase` events and `OnAttached()` runs for the components of **every** scene at load. Subscribe with `SubscribeInScene<Event, &Handler>(this)`: the handler runs only while the component's scene is `IsActive()`; plain `Subscribe` makes a component of an inactive scene react. The connection stays, so switching scenes connects nothing (not allowed inside a handler).
- A component that reads a model owned by its `IGame` does nothing unless `GameManager::GetActive()` is that game. `SceneEventsTest.cpp` loads scenes from an XML string: the setup to copy for a component test.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look up again.
- Do not draw from `Update` or change state from `Draw`; the frame order is in `BECore/CLAUDE.md`. Movement and animation scale by `SceneUpdateEvent::deltaTime`.
