# Scene Module Guidelines

## Overview

Scene graph on top of EnTT: `SceneManager` → `Scene` → `SceneNode` → `IComponent`. Everything is reflected and loaded from `config/SceneConfig.xml`, so the scene editor and the file stay in sync (`.claude/rules/widgets-module.md`).

| Class | Role |
|-------|------|
| `SceneManager` | Built by `LoadSceneManagerFromConfig()` (`SceneLoader.h`: `SceneConfig` with its node references expanded, see "Node files") in `CoreManager::OnApplicationInit`. Holds `_scenes`; the first one is active after `Initialize()`. `SetActiveScene(name)`, `GetActiveScene()`, `GetSceneByName()`; `UpdateAll()` / `DrawAll()` drive the active scene. It is the only writer of `Scene::IsActive()` |
| `Scene` | `FACTORY_BASE`. Owns the `entt::registry`, a runtime root node (`"Root"`) and the serialized `_nodes`; optional view size `viewWidth` / `viewHeight` and `viewVariants` (see "View size"). `Update()` emits `SceneEvents::SceneFixedUpdateEvent` once per fixed step due in the frame, then `SceneUpdateEvent` with the frame time (both read `CoreManager::GetTime()`); `Draw()` runs `SceneDrawSystem` over the registry, which draws the quad and the sprite from their registry data and calls `IComponent::OnDraw()` of every other component, in draw order (see "Draw order"). `IsActive()` is true for the scene `SceneManager` currently drives. `PrepareForSave()` copies the live hierarchy back into `_nodes` (the scene editor save path calls it through `WriteSceneToConfig`, `src/Widgets/SceneEditor/SceneConfigWriter.h`) |
| `SceneNode` | `FACTORY_BASE`, XML element `node`: `_name`, `_layer` (attribute `layer`, default 0), `_components`, children, an `entt::entity`. `AddComponent`, `RemoveComponent`, `GetComponent<T>()`, `AddChild`. The XML describes the top-level nodes and their components only; children exist at runtime (`AddChild`) |
| `IComponent` | `FACTORY_BASE` and `SubscriptionHolder`; gets its `Scene&` / `SceneNode&` through `GetScene()` / `GetNode()` |
| `SceneNodeFiles` / `SceneLoader` | `SceneNodeFiles.{h,cpp}`: the expansion of `<node ref>` into the flat `SceneConfig` document (`IsValidNodeRef`, `ExpandNodeReferences`, `MakeDirectoryNodeLoader`, `MakeFileSystemNodeLoader`, `LoadSceneDocument`), pure pugixml with no engine services. `SceneLoader.{h,cpp}`: `LoadSceneManager(configRoot, loader)` and `LoadSceneManagerFromConfig()` build the `SceneManager` from the expanded copy. See "Node files" |

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

`type` is the component class name; field names lose the leading underscore. A `scene` can carry `viewWidth` / `viewHeight` (see "View size") and a node can carry `layer="-1"` (see "Draw order"). Every `SceneConfig.xml` replaces the one below it in the content layers (see `FileSystem/CLAUDE.md`): the engine's own file holds a debug `MainScene`, and a game's file holds only that game's scenes, so a game executable never loads `MainScene`. A node can also live in a file of its own and be named by `<node ref="nodes/MyHud"/>` (see "Node files").

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

- **Reference:** `<node ref="..."/>` inside `<nodes>`. The value is a path relative to the `config` mount point, without `config/` and `.xml`: segments of `[A-Za-z0-9_-]` separated by `/` (`IsValidNodeRef`: no empty segment, `.` / `..`, leading `/`, `\` or extension). A reference carries no other attribute and no child element (another attribute is an error, not ignored).
- **Node file:** its document element is one `<node name=".." layer="..">` with `<components>`, the same shape as an inline node; it is no reference itself, so no nesting and no cycle. The reference is replaced by a copy of it in place: the order of the references is the scene order, so the draw and input order.
- **Where the file is found:** `FileSystem::ResolvePath("config/<ref>.xml")`, so the content layers apply (a game's `config/nodes/X.xml` replaces the engine's). A tool or test with only the path of a `SceneConfig.xml` resolves next to that file (`MakeDirectoryNodeLoader`).
- **Errors never stop a game:** an invalid reference or a missing, unparsable or wrong file is one `LOG_ERROR` naming the scene and the reference; the element is dropped and the rest loads (as for an unknown component `type`).
- **The expansion is done on a copy.** `LoadSceneManager` expands a copy of the `SceneConfig` document and deserializes that; the cached document of `ConfigManager` keeps its references. Everything that reads the scene after loading (canvases, `ui_tree`, `ui_click`, the scene editor, `GetDrawRank`) sees exactly the tree of an inline scene. A test or tool that reads the **file** calls `LoadSceneDocument(path, document)` and gets the expanded document.
- **`ConfigManager` also loads a node file as a config named by its stem** (`MahjongHud`): the stem must not equal a real config's name, so a game prefixes its node files with its name.
- **The scene editor does not save a scene that has references** (the live tree is flat, saving would inline them): `WriteSceneToConfig` returns false and writes nothing, `SceneEditorWidget::SaveScene` logs a warning and the scene stays dirty. The split files are edited by hand.
- Not built: per-instance overrides on a reference, runtime instantiation of a node file, the editor saving references without flattening.

Tests: `tests/BECore/SceneNodeFilesTest.cpp` (the pure expansion), `SceneSplitTest.cpp` (a split scene is the same scene as the inline one: nodes, canvases, `ui_tree`, validator findings, draw ranks, layering), `SceneEditorSaveTest.cpp` (the refusal).

## Component lifecycle

1. Deserialization creates the component through `AbstractFactory<IComponent>` and calls `Initialize()` (the reflection hook: runs even when there is no node yet).
2. `Scene::Initialize()` → `SceneNode::WireToScene()` sets `_scene` / `_node` and calls `OnAttached()`. `AddComponent` does the same at runtime.
3. Do node-dependent setup in `OnAttached()`: event subscriptions, resource lookups, sibling components via `GetNode().GetComponent<T>()`. This runs for the components of **every** loaded scene, active or not, so subscribe with `SubscribeInScene` (see Gotchas).

## Draw order

Drawing is not an event. `Scene::Draw()` hands the pass to `SceneDrawSystem`: a depth-first walk writes a `SceneDrawItem {layer, order, node}` per node and mirrors its transform and renderer data into the registry (below); the storage is sorted by (`GetLayer()`, `order`) and iterated. For each node its components draw in component order: `QuadRendererComponent` and `SpriteRendererComponent` are drawn by the system from the registry, every other component through `IComponent::OnDraw()`. The view is the only source of the order.

- A lower layer is drawn first (default 0, negative = background). On one layer: scene order, parent before children, later siblings on top. **Give a background a lower `layer`**; the order never depends on subscriptions. A child does not inherit its parent's layer.
- Only the active scene draws, so `OnDraw()` needs no `IsSceneActive()` check. `RemoveChild()` destroys the entities of the removed subtree; do not call it on a node whose `Scene` is gone.
- **Override `OnDraw()`, never subscribe to draw.** `OnDraw()` only reads state and issues renderer calls: it must not add or remove nodes or components, and must not write a `TransformComponent` or a renderer field (the mirror was copied before the pass, so the change shows a frame late and a released texture would dangle).
- `Scene::GetDrawRank(node)` is a node's position in this order without a draw (input handlers that ask the topmost first use it).

**Rotation, flip and blend** (renderer side: `.claude/rules/rendering-module.md`): `TransformComponent::_rotation` (degrees, clockwise) about `_pivotX` / `_pivotY`, **normalized in the node's rect** (0.5 = center), also for a `Contain` sprite; `SpriteRendererComponent::_flipX` / `_flipY` mirror the UVs. The transformed draw is used only when the transform is not the identity. `_blend` (`Alpha`, `Add`, `Multiply`) is scoped to that one draw. Not rotated: `ClickableComponent`'s hit rect, `TextComponent`, UI canvases. Vulkan draws non-`Alpha` as `Alpha` with one warning.

**The mirror** (`SceneRenderData.h`, written only by the system): `TransformData`, `QuadDrawData`, `SpriteDrawData` on the node's entity, copied from the components at the start of every `Draw()`; game code does not read it. At most one quad and one sprite per node count; without a `TransformData` (or a texture) nothing draws.

Tests: `SceneDrawSystemTest` (order, layers, removal, rotation, flips, blend), `SceneRenderDataTest` (the mirror), `SceneEventsTest`, `SpriteRendererComponentTest`.

## Why EnTT

Decided by the owner (entry `ecs-nominal`): the object model stays `SceneNode` + `IComponent`, because reflection, the XML and the node inspector are built on it. The `entt::registry` of a `Scene` holds the data that **systems** work on, sorted: `SceneDrawItem` (the order) and the mirror of the two built-in renderers (`TransformData`, `QuadDrawData`, `SpriteDrawData`, refreshed at the start of every `Draw()`; the components stay the truth). `SceneDrawSystem` issues their `IRenderer` calls.

- It is **not** a pure ECS: the components (`IComponent`) are not stored in EnTT pools; the registry holds one entity per node and the data of the systems only. The other drawing components (`TextComponent`, `UICanvasComponent`, `RenderProbeComponent`, `RmlUiDocumentComponent`, the games') still draw through `OnDraw()` with their own `IRenderer` calls.
- The direction is rendering as a system that hands a **batch** to `IRenderer` (entries `rendering-2d`, `renderer-backends`). Today the system makes one `IRenderer` call per quad or sprite (plain, or transformed for a rotated or flipped node); nothing is batched, so do not describe the batch as done. `IRenderer` has a blend state and two transformed draws for the scene (`.claude/rules/rendering-module.md`).

## View size

A scene says how big its picture is in scene units: `<scene type="Scene" name="GameScene" viewWidth="840" viewHeight="800">`. Everything in the scene (transforms, board origins and cell sizes) is in these units, and the engine fits the view to whatever the render output is:

- `Scene::Draw()` asks the renderer for its output size, picks the active view (below), builds `ViewTransform::Fit(activeWidth, activeHeight, outputW, outputH)` (one uniform scale, the view centered, bars on the free sides), sets it with `IRenderer::SetViewTransform()` for the draw pass and resets it after. The backends apply it to every rect (`.claude/rules/rendering-module.md`). It is computed every frame, so a window or viewport of any size, resized or not, needs no event.
- Without a view size (both 0, the default) the transform is the identity: one unit is one output pixel. Without a renderer (tests) it is the identity too.
- Mouse positions arrive in output pixels (window pixels in Game mode, scaled by the pixel density in `SDLEventsProvider`; viewport-local in Editor mode). A handler maps them with `GetScene().ScreenToSceneX(e.button.x)` / `ScreenToSceneY(...)` before comparing with scene geometry (`ClickableComponent` and `Match3BoardComponent` do), by the transform of the last draw (the identity before the first).
- Pick the view size as the bounding box of the content with the margins you want to keep: a board of 640x640 at (100, 80) gives a view of 840x800, the HUD living in the margins as a `SceneView` canvas (`UI/CLAUDE.md`). A window of another aspect gets bars in the clear color.
- The scene editor takes the scene's attributes (and `viewVariants`) from `Scene::Serialize`: `viewWidth` / `viewHeight` are always saved, `0` / `0` loads as no view.
- The output size follows the window on both backends (`src/Modules/Vulkan/CLAUDE.md`, "Frame lifecycle"), so the scene refills the window after a resize without any event.

**View variants.** A scene may declare other view sizes for other shapes of output, so a game lays itself out for portrait too:

```xml
<scene type="Scene" name="T" viewWidth="1280" viewHeight="720"><viewVariants>
  <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
</viewVariants><nodes>...
```

- `when` is the `UIVariantCondition` language (`UI/CLAUDE.md`), tested against `{0, 0, outputWidth, outputHeight}` in **output pixels**; the first variant that holds and has a positive finite size wins, else the default view. A malformed `when` is one logged error and drops that variant. The pick is the pure `SceneViewSelector::Pick` (`SceneView.h`), callable without a `Scene`.
- **Decided in `Draw()`, announced in `Update()`.** The pick uses the output size of that very pass (the editor viewport's target is not the window), before the fit and before any component draws, so `GetActiveView()` (`{name, width, height, index}`; index -1 and an empty name for the default) is right already inside `OnDraw()`. No renderer: the default view. `GetViewVersion()` moves exactly when the index changes, never back. `Update()` sends `SceneEvents::SceneViewChangedEvent {scene, view}` once when the version moved since the last send (before the fixed updates), never from `Draw()`, which must not run handlers that change the scene.
- `GetViewWidth()` / `GetViewHeight()` are the **active** view's (the default's before the first draw); `ScreenToScene*` use the fit of the last draw.
- Saved by `Scene::Serialize` (an empty list writes nothing), also through the scene editor; the inspector shows nothing for them. The control API (`ui_tree` `view`, screenshots) and the editor viewport draw through `Scene::Draw`, so they follow the view of their own output size.
- **A tool's override.** `GetViewVariants()` lists the variants (read only). `SetViewOverride(optional<int32_t>)` replaces the pick: -1 the default view, `i` variant `i`, nullopt or an index out of range the normal pick. Not saved or reflected; setting it picks nothing, the next `Draw()` does (same version bump and event). A tool sets it right before its own pass and clears it right after (the viewport's view combo, `UI/CLAUDE.md`, "Editor preview"), so Game mode and `Update()` never see it.

Tests: `tests/BECore/ViewTransformTest.cpp` (the math), `SceneViewTest.cpp` (the fit during a draw with a fake renderer, `ScreenToScene*`, a click in scene units, the variants), `SceneViewSelectorTest.cpp` (the pure pick, the file form), `SceneEventsTest.cpp` (the change event).

## Built-in components (`Components/`)

| Component | Fields | Behavior |
|-----------|--------|----------|
| `TransformComponent` | `_x`, `_y`, `_width`, `_height`, `_rotation`, `_pivotX`, `_pivotY` | Position/size in scene units (pixels when the scene has no view size); others read it. `_rotation` (degrees, clockwise, 0) turns the node's quad or sprite about the pivot, normalized in the node's rect (0.5, 0.5: the center); hit tests, text and UI ignore it ("Rotation, flip and blend"). `SceneDrawSystem` copies it into the registry as `TransformData` at the start of every draw |
| `QuadRendererComponent` | `_color`, `_blend` | `_blend` is `Alpha` (default), `Add` or `Multiply` for this quad's draw only. Has no `OnDraw()`: `SceneDrawSystem` draws a filled rect at the node's transform from its registry copy (`QuadDrawData`). The component stays the place of the field and of the editor |
| `SpriteRendererComponent` | `_spriteId`, `_texturePath`, `_srcRect`, `_fit`, `_blend`, `_flipX`, `_flipY` | `_blend` as the quad's; `_flipX` / `_flipY` mirror the picture about its own center. Has no `OnDraw()`: `SceneDrawSystem` draws the texture from its registry copy (`SpriteDrawData`); the component stays the place of the fields, of the texture handle and of the editor. `_spriteId` resolves through `TextureLibrary`. `_fit` (`SpriteFit`, `Components/SpriteFit.h`) is `Stretch` (default: the texture covers the node's rect, whatever its aspect) or `Contain` (`fit="Contain"`: the aspect of `_srcRect`, or of the whole texture when it is empty, is kept and the sprite is centered in the rect; `FitSpriteRect()`, computed in scene units). `SetTexture()` hands it a texture that is not from the library; `ReloadTexture()` drops it |
| `ClickableComponent` | `_actionName` | On mouse-down inside the transform rect, emits `InputEvents::ActionEvent(_actionName)` |
| `ChangeToRandomColorDebugComponent` | `_actionFilter` | Debug: recolors the node's quad on a matching action |
| `TextComponent` | `_text`, `_fontPath`, `_fontSize`, `_color`, `_align`, `_overflow`, `_wrap`, `_lineSpacing`, `_minFontSize` | Draws text in the node's transform box in `OnDraw()` (`Text/CLAUDE.md`) |
| `UICanvasComponent` | `_space`, `_scaleMode`, `_referenceWidth`, `_referenceHeight`, `_match`, `_minScale`, `_maxScale`, `_useSafeArea`, `_modal`, `_visible`, `_inputEnabled`, `_elements`, `_variants` | A canvas of in-game UI: owns an element tree, lays it out for the output, draws it with its own view transform and takes mouse input through `UIInput` (`UI/CLAUDE.md`) |
| `RenderProbeComponent` | none | Debug: from the node's transform origin a row of cells that show the renderer primitives (textures, tint, alpha, clips, `DrawGeometry()`, `DrawTransformedTexture()`, blend modes), to look at both backends |

## Adding a component

1. Class in `Components/` (or a game module's `Components/`) deriving `IComponent` with `BE_CLASS(Name)`; fields get `BE_REFLECT_FIELD`.
2. Include `<Generated/Name.gen.hpp>` in its `.cpp`; the directory must be in `reflection_targets.json`; re-run `cmake --preset ...`.
3. Subscribe in `OnAttached()` through `SubscribeInScene<Event, &Name::Handler>(this)`. Plain `Subscribe` is only for a handler that must run whichever scene is active. A component that draws overrides `OnDraw()` instead of subscribing (see "Draw order").
4. Use it from `config/SceneConfig.xml` with `type="Name"`. The inspector shows its reflected fields automatically.

## Gotchas

- `SceneUpdateEvent` / `SceneFixedUpdateEvent` and the input events (`SDLEvents::MouseButtonDownEvent`, `InputEvents::ActionEvent`) are **global** `EventBase` events, not per scene, and `OnAttached()` runs for the components of **every** scene when the config loads. Subscribe with `IComponent::SubscribeInScene<Event, &Handler>(this)`: the handler is called only while the component's scene is active (`Scene::IsActive()`, set by `SceneManager`). Plain `Subscribe` on such an event makes a component of an inactive scene click and react. The filter sits in the call, the connection stays, so switching scenes never connects or disconnects anything (which is not allowed inside a handler).
- A component that reads a model owned by its `IGame` does nothing unless `GameManager::GetActive()` is that game.
- `SceneEventsTest.cpp` loads scenes from an XML string: the setup to copy for a component test without a window or renderer.
- Do not keep `SceneNode*` / `IComponent*` across scene edits; hold `IntrusivePtr` or look the node up again.
- Do not draw from `Update` or change state from `Draw`; the frame order and the use of `deltaTime` are in `BECore/CLAUDE.md`. Movement and animation scale by `SceneUpdateEvent::deltaTime`.
