# In-game UI without ImGui: adaptive, multi-canvas

## Context

Today the only UI is ImGui, and it is for tools: the editor panels in `src/Widgets/`. The Game run mode has no ImGui context, so `BagiMatch3` shows no score and has no Reset button (its HUD is an `ImGui::Begin` block in `Match3BoardComponent::DrawHud()`), and Chess has no HUD at all. `TODO.md` already records the gap in four stage-3 entries: "No text rendering in the engine", "Input has no mapping layer", "No in-game UI", "Game UI has no LLM-friendly authoring path yet".

Goal: UI that the engine draws and builds itself, through `IRenderer`, written as data, working in both backends and in both run modes. ImGui stays what it is (editor tools) and is not touched.

Three design rules shape everything below.

1. **Adaptivity is in the foundation, not a later feature.** Window size, aspect ratio, orientation, safe area, pixel density and a user UI scale are inputs of layout from the first control. Every screen must pass an automatic layout check on a matrix of sizes before it is considered done.
2. **A scene holds any number of canvases.** A canvas is the unit that owns a coordinate space, a scale and an input order: a HUD, a menu, a popup, labels floating above the board. Each one picks its own space (the whole window, the scene's view, or a place in the world) and its own scaler.
3. **The element model is separate from the file format.** `AbstractFactory<UIElement>` creates elements from a type name and sets their reflected properties; it does not care where the name and the values came from. XML is one source of that data, not the model. A web-style loader maps tags to element types (`div` → `UIPanel`, `button` → `UIButton`, `img` → `UIImage`, `span`/`p` → `UILabel`) and CSS declarations (including `@media`) to the same properties, and builds the same tree through the same factories. The runtime, input, editor view and tests are written once, against the tree.

**Open decision, kept open on purpose:** how a screen is described — the engine's own declarative XML (TODO option 1, the recommendation there), a web-style description (HTML-like markup plus CSS-like styles), or RmlUi (a ready HTML/CSS library, option 3). The owner wants more data before choosing and wants the options to stay possible. Rule 3 is what keeps them possible:

- Stages 0-2 are the foundation **every** option needs (renderer primitives, text, input and window metrics).
- Stage 3 is the canvas and adaptive layout core, in pure code, with no controls yet.
- Stage 4 adds the controls and the XML front-end, and moves the first real screens onto them.
- Stage 5 is a time-boxed spike: a web-style front-end on the same tree, and an RmlUi host, with a decision record.
- Stage 6 widens whichever path wins. The others stay possible as extra loaders or an optional module, not a rewrite.

## Progress

Updated as the work goes; the dashboard counts these lines.

- [x] Stage 0.1 `IRenderer`: `CreateTexture`, tinted `DrawTexture`, clip stack
- [x] Stage 0.2 SDL backend: alpha blending of rects, tint, clip, `CreateTexture`
- [x] Stage 0.3 Vulkan backend: shared upload helper, tint push constant, scissor clip (smoke-run on Apple M4 Pro, no errors; pixels not looked at)
- [x] Stage 0.4 `RecordingRenderer` in `tests/Common`, clip and forwarding tests, SDL pixel tests
- [x] Stage 0.5 `RenderProbeComponent` for the manual check on both backends
- [ ] Stage 0 manual look at the probe on Vulkan (owner)
- [x] Stage 1.1 `stb_truetype.h` v1.26 vendored in `src/Modules/stb/`, vendored lists updated
- [x] Stage 1.2 `Font` and `FontLoader` (CPU only, `.ttf` / `.otf`)
- [x] Stage 1.3 `FontAtlas`, `ShelfPacker`, `FontAtlasLibrary`, `TextSettings` and `config/TextConfig.xml`; new pages instead of rewriting one; the library is cleared by `Application::Cleanup()` before the renderer dies (`IRenderer::WaitIdle()`, `GetId()` were added for it)
- [x] Stage 1.4 `TextLayout` (wrap, align, line spacing, clip / ellipsis / auto-fit) with gtests on the real font
- [x] Stage 1.5 `TextRenderer` and `TextComponent`
- [x] Stage 1.6 Default font: PT Sans (ParaType, OFL 1.1) in `assets/fonts/` with its license; Basic Latin, Latin-1, Cyrillic and punctuation covered (`FontTest`)
- [x] Stage 1 look at the text on the SDL backend (headless screenshot of a text sheet: Cyrillic, sizes 12 to 72, alignment, ellipsis, auto-fit, alpha)
- [ ] Stage 1 manual look at the text on Vulkan (owner; the same scene ran on Vulkan without errors)
- [x] Stage 2.1 `IMainWindow::GetPixelDensity/GetDisplayScale/GetSafeArea` with neutral defaults, answered by `SDLMainWindow`
- [x] Stage 2.2 `ConvertSDLEvent`: mouse up/motion/wheel, keys, text input, window events, `SafeAreaChangedEvent`, `DisplayOrientationChangedEvent`; mouse follows `WantCaptureMouse`, keyboard `WantCaptureKeyboard`
- [x] Stage 2.3 `UIInput` (`CoreManager::GetUIInput()`): ordered handlers, first to consume wins, the game gets what is left; changes inside a handler are deferred
- [x] Stage 2.4 `ViewportWidget` feeds `UIInput` with viewport-local down, up, motion and wheel (a press that began in the image is followed to its release)
- [x] Stage 2.5 tests: `SDLEventsProviderTest` (14), `UIInputTest` (11), `WindowMetricsTest` (5)
- [ ] Stage 2 look at the editor viewport input by hand (owner: click, drag out of the image, wheel; no test can open the ImGui editor)
- [x] Stage 3.1 `UILength`, `UIRectSpec`, `UIStackSpec` and the pure `UILayout` (anchors, stretch, percent, min/max, aspect lock, stack with flex and wrap), `UILayoutTest` (31)
- [x] Stage 3.2 `UICanvasScaler` (`CanvasSpace`, `CanvasScaleMode`, `UICanvasResolved`), `UILayoutContext`, `UISettings` + `config/UIConfig.xml`, `UIEnvironment` (`CoreManager::GetUIEnvironment()`: settings, window metrics as a context, a safe-area override for the editor viewport); `UICanvasScalerTest` (19), `UISettingsTest` (9)
- [x] Stage 3.3 `UIElement` tree (plain, not reflected yet), `UIVariant` (conditions, overrides, `UIVariants::Apply`), `UITreeLayout`; `UIVariantTest` (22)
- [x] Stage 3.4 `UICanvasLayout` (pure pipeline: scale, variants, layout, cache, hit test), `UILayoutValidator`, `tests/Common/UILayoutMatrix.h` (`ValidateOnMatrix`); `UICanvasLayoutTest` (18), `UILayoutValidatorTest` (15)
- [x] Stage 3.5 `UICanvasComponent` (reflected fields, owns a `UICanvasLayout`, scoped view transform, `UIInput` handler, `Scene::GetDrawRank`); `UICanvasComponentTest` (30)
- [x] Stage 3.6 Several canvases in one scene: draw order by node layer, input order refreshed on every draw, `modal`, show / hide by name (`SetVisibleByName`); covered in `UICanvasComponentTest`
- [x] Stage 3.7 Editor preview: `ViewportWidget` toolbar with aspect presets and a safe-area simulation (`UIPreview`, `UIPreviewTest` (9)); the viewport hands its safe area to canvases through `UIEnvironment::SetSafeAreaOverride`
- [ ] Stage 3 look at the viewport presets by hand (owner: pick 16:9 / 4:3 / 21:9 / 9:16, a notch and side insets; the outline must match the layout; no test can open the ImGui editor)
- [x] Stage 4.1 Reflection check: `UIElement` is a `FACTORY_BASE` (XML element `element`), the style is flat attributes, `UILength` / `UIEdges` / `UIColor` are one attribute each, variants are `<variants><variant when="...">`; `UICanvasComponent` has `_elements` and `_variants`; XML and binary round-trip, a scene file lays out like code; an unknown `type` is reported; `UIReflectionTest` (14)
- [x] Stage 4.2 `UIDocument` and `UIDocumentLoader` for `.ui.xml` (`ResourceManager` picks a loader by the longest suffix), `source` on the canvas, `UIScreenContent`; the same tree from inline XML, a file and a property bag built in memory; `UIDocumentTest` (11)
- [x] Stage 4.3 Controls: `UIPanel` (fill, sprite, nine-slice), `UIImage` (fit or stretch), `UILabel` (text, vertical align, overflow, `CheckContent`), `UIButton` (hover / pressed / disabled, click emits `ActionEvent`), `UIStack`; the canvas tracks hover, press and click; `UISprite`, `UINineSlice`, `TextLayoutCache`; `UINineSliceTest` (5), `UIControlsTest` (27)
- [x] Stage 4.4 `UIVars` (`CoreManager::GetUIVars()`), `{name}` binding in `UILabel` (expanded only when the text or the version changed), show / hide of an element by name (`SetElementVisible`, `SetElementVisibleByName`); `UIVarsTest` (10), cases in `UIControlsTest`
- [x] Stage 4.5 gtests of the controls and of a whole screen: hit testing and z-order, button states and clicks from synthetic events, draw calls of each control, a HUD through the size matrix and in two shapes with a variant (`UIScreenTest` (7)); the screens the games ship go through the same matrix with 4.6 and 4.7
- [x] Stage 4.6 Match3: view size 712 x 672, the HUD as a `SceneView` canvas in the margins of the view (score, Reset, Menu, seed) replacing the ImGui block, `Match3Reset` / `Match3Menu` answered by the board, a `MainMenu` scene with Play and Quit (`Match3MenuComponent`) as the first scene; `Match3UiTest` (11) runs the shipped file through the size matrix; a `reset` or an action over the control API enters the game from the menu, so a screenshot shows the board (looked at on the SDL software renderer: menu, board with the HUD, the score after a move)
- [x] Stage 4.7 Chess: the HUD as a `SceneView` canvas in the margins of the view (the turn, the status in words, Reset), the view is 840 x 800 with the board at (100, 80); `StatusText` (pure), `ChessBoardComponent` publishes `turn` and `status` and answers `ChessReset`; `StatusTextTest` (7), `ChessUiTest` (8)
- [x] Stage 5.1 Web-style front-end (B): `UIHtmlParser`, `UICssParser`, `UIWebCompiler` (HTML + CSS to the text of a `.ui.xml`, cascade, inheritance, `@media` to variants, button states, an issue report with counters), `UIWebDocumentLoader` for `.ui.html`; the Match3 menu in HTML + CSS lays out like the shipped XML at five sizes and passes the matrix; `UIWebTest` (41 with 5.2)
- [x] Stage 5.2 Measured A and B on the three shipped screens (menu, Match3 HUD, Chess HUD: the same rects, no issue, size matrix) and on a 1,001-element screen; the Match3 and Chess HUDs in HTML + CSS are covered by `UIWebTest` (41)
- [x] Stage 5.3a RmlUi vendored like pugixml and stb, not as a submodule (a submodule add was refused): `src/Modules/RmlUi` (6.3, core only, unedited, own `CMakeLists.txt` and `README.md`), behind `ENGINE_UI_RMLUI` (OFF by default), built with `-fno-exceptions` and FreeType from Homebrew; `rmlui_core` builds; nothing links it yet
- [ ] Stage 5.3b RmlUi host (C) on the SDL backend: `IRenderer::DrawGeometry`, the render / system / file interfaces, `RmlUiDocumentComponent`, the Match3 screens as `.rml`, the numbers for the C column (steps in `ui-authoring-decision.md`)
- [x] Stage 5.4 Decision record `.claude/plans/ui-authoring-decision.md`: filled for A and B, C marked not measured
- [ ] Stage 5 owner decision: A, A with B, or wait for the C data
- [ ] Stage 6 Widen the chosen path
- [ ] Stage 7 Editor and tooling

## What the code says (shapes the design)

| Finding | Consequence |
|---|---|
| `IRenderer` draws with `DrawFilledRect` and `DrawTexture` only. No tint, no clip, no texture from pixels. `SDLRendererBackend::DrawFilledRect` sets no blend mode. | Stage 0 adds the missing primitives to the interface first, then both backends (`rendering-module.md`). |
| Textures come only from file loaders; `VulkanTextureLoader` has the upload code inline. | Extract the upload into a helper and add `IRenderer::CreateTexture(pixels)`; the glyph atlas needs it. |
| The push constants of the Vulkan texture pipeline are 40 bytes; the scissor is a dynamic state that `VulkanTexturePipeline::Draw` sets to the full extent. | A tint (`vec4`) and a clip rect fit without a new pipeline. |
| The scene XML describes top-level nodes only; children are runtime-only; a node's layer is not inherited (`Scene/CLAUDE.md`). | A UI tree cannot be scene nodes. Each canvas is one node with a `UICanvasComponent` that owns a `vector<IntrusivePtr<UIElement>>`. Elements are created by `AbstractFactory<UIElement>`, the way `SceneNode` creates components, but the factory is the contract, not XML (see "Front-ends"). Inline XML children are reflected like `_components`; proving that round-trips is task 4.1. |
| `Scene::Draw()` fits the scene's view to the output with letterbox bars (`viewWidth` / `viewHeight`), sets a `ViewTransform` for the whole pass and keeps it for `ScreenToScene*`. | A canvas sets its own `ViewTransform` around its draw (RAII scope) and maps the mouse itself. A canvas in the scene's view computes `ViewTransform::Fit(...)` from the scene's view size and the output size (pure, no dependency on "the last draw"), so input works before the first frame too. |
| `IMainWindow` exposes only `GetWidth/GetHeight`. SDL3 in `external/SDL` has `SDL_GetWindowSafeArea`, `SDL_GetWindowPixelDensity`, `SDL_GetWindowDisplayScale`, and the events `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` and `SDL_EVENT_DISPLAY_ORIENTATION` (checked in the headers). | Stage 2 adds `GetPixelDensity()`, `GetDisplayScale()`, `GetSafeArea()` to `IMainWindow` with neutral defaults (1, 1, the whole window). |
| `ConvertSDLEvent` forwards only mouse-down, and the only "consumed" rule is ImGui `WantCaptureMouse`. Events carry no consumed flag. | Stage 2 restores the commented-out conversions and adds one input entry point (`UIInput`), so a click on a button does not also hit the board under it. |
| Resource loading can run on a pool thread (`LoadAsync`). | A font loader reads the TTF bytes only. Rasterizing and `CreateTexture` happen lazily on the main thread. |
| Both games use `VulkanRendererBackend`; `SDLRendererBackend` has no render targets, but Game mode does not need them. | The RmlUi spike can run on the SDL backend in Game mode (`SDL_RenderGeometry` exists) and estimate the Vulkan cost. To verify first: that the SDL backend runs Match3 in Game mode. |
| The engine-level `assets/` directory is listed in `CLAUDE.md` but does not exist yet. | The default font goes to `assets/fonts/`; check that `FileSystem` layering handles a new engine `assets/`. |
| The Vulkan swapchain is not recreated on resize and there is no HiDPI (separate TODO entry). | Not fixed here. Layout is recomputed from the live output size, density and safe area, and the glyph atlas is keyed by effective pixel size, so both pick the fix up without changes. Until then, adaptivity is verified by injecting sizes (tests) and by the editor preview presets, not by dragging the window. |

## Design

**Layers, bottom to top** (all in `BECore` unless noted; the module order in `CLAUDE.md` is unchanged):

- **A. Renderer primitives** (`Renderer/`): `CreateTexture`, tinted `DrawTexture`, clip rect stack, alpha blending in the SDL rect path. `DrawGeometry` (indexed triangles with per-vertex color and UV) is added **only** for the RmlUi spike.
- **B. Text** (`Text/`, new): `Font` resource, `FontAtlas`, pure `TextLayout`, `TextComponent`. Glyphs come from `stb_truetype.h` (single header, vendored in a new `src/Modules/stb/`, listed with the other vendored code). No GPU or window is needed to measure text, so layout is testable.
- **C. Input and window metrics** (`SDL` module, `MainWindow/`, `Input/`): restored mouse motion/up/wheel/key/text and window events; pixel density, display scale and safe area on `IMainWindow`; `UIInput`, the single entry point that routes input to canvases and returns "consumed".
- **D. UI core** (`UI/`, new): canvases and the scaler, the `UIElement` tree built by `AbstractFactory<UIElement>`, adaptive layout (pure code), variants, the layout validator, controls, `UIVars`. Knows nothing about file formats.
- **E. Front-ends** (loaders that fill the tree): XML (inline in `SceneConfig.xml` and as a file) first; a web-style loader (HTML-like markup + CSS-like styles) as the second one, added in the spike. RmlUi, only if the spike says so, as an optional module `src/Modules/RmlUi` behind `ENGINE_UI_RMLUI` with its own host, `RmlUiDocumentComponent`.

### Canvases

A canvas is a `UICanvasComponent` on a node. A scene may have many. Each one resolves, every frame, to a **bounds rect in its own UI units** and a **`ViewTransform` from UI units to output pixels**.

| Field | Meaning |
|---|---|
| `space` (`CORE_ENUM CanvasSpace`) | `Screen`: fills the render output, so the edges are the window edges (HUD, menus). `SceneView`: fills the scene's fitted view (`viewWidth` x `viewHeight`), so it scales and moves with the board (UI that belongs to the playfield). `World`: placed at the node's `TransformComponent` in scene units and follows the node (a label above a tile) |
| `scaleMode` | `ConstantPixelSize`; `ScaleWithScreen` (reference size and `match` 0..1 between width and height, the Unity-style blend); `ConstantPhysicalSize` (display scale) |
| `referenceWidth`, `referenceHeight`, `match`, `minScale`, `maxScale` | Parameters of the modes above |
| `useSafeArea` | Keep the content inside the window's safe area (notches, rounded corners, system bars); the full bounds stay available to backgrounds |
| `modal` | While visible, input does not reach canvases below it or the game |
| `visible`, `enabled` | Show/hide a whole screen by name |

- **Draw order** is the existing one: node `layer`, then scene order. A HUD canvas sits on a high layer, a world canvas on the layer of the thing it labels.
- **Input order** is the reverse of draw order: topmost canvas first, a modal canvas stops the walk.
- **Scaler** is one pure function: `UICanvasScaler::Resolve(params, UILayoutContext, sceneView) → {scale, bounds, safeBounds, transform}`. Final scale = mode scale x user `uiScale` (an accessibility setting from a UI config) clamped to `minScale`..`maxScale`.
- **`UILayoutContext`** is everything layout depends on: output size, pixel density, display scale, safe area, user scale, `UIVars` version. A canvas caches its layout and recomputes only when the context or a dirty flag changes.

### Adaptive layout

- **Relative by default.** Anchors (`CORE_ENUM`: the nine points plus `Stretch` with margins), sizes as `UILength` (`px` or `%` of the parent), `minWidth/maxWidth/minHeight/maxHeight`, `aspectRatio` with `fit`/`fill`, `UIStack` (vertical or horizontal, spacing, padding, per-child flex weight, optional wrap). Layout functions are pure: `ResolveRect(parentRect, spec)`.
- **Text adapts too.** Font size is in UI units and scales with the canvas; a label can `wrap`, `ellipsis` or `autoFit` (shrink down to `minFontSize`). The atlas is rasterized for the effective pixel size (UI size x scale x density).
- **Responsive variants.** A canvas holds a list of `UIVariant { when, overrides }`. `when` is a condition on the canvas bounds in UI units: `orientation` (portrait or landscape), `aspect` range, `width` / `height` ranges. `overrides` is a property bag per named element (visible, anchor, offset, size, stack direction, font size). Variants are applied in order, so a later one wins, like CSS. They are re-evaluated only when the bounds change. In XML it is a `<variants>` block; in the web-style front-end `@media (orientation: portrait) { ... }` maps onto the very same list.
- **Validator.** `UILayoutValidator` (pure) takes a laid-out canvas and reports broken invariants: an element outside the bounds (or the safe bounds when `useSafeArea`), siblings of a stack that overlap, an interactive element smaller than a minimum touch size from the UI config, text that neither fits nor has `ellipsis`/`autoFit`. It runs in the gtests on the matrix below, in the debug overlay and in the control API.
- **Test matrix** (used for every screen shipped in the repo): aspect ratios 16:9, 16:10, 3:2, 4:3, 1:1, 21:9, 32:9, 9:16, 9:19.5, 3:4; output sizes from small (about 480 px on the short side) to 3840x2160; safe-area insets none, a top notch, side insets in landscape; each scale mode; user scale 1.0 and 1.5. The validator must report zero issues.

### Front-ends

A document is a resource: `UIDocument` (`IResource`) loaded by an `IResourceLoader` picked by extension (`.ui.xml`, later `.html` with its `.css`), exactly how textures and XML already load (`Resource/CLAUDE.md`). A loader walks its format and, for each node, asks `AbstractFactory<UIElement>` for the type and passes the property values. Adding a format is adding a loader; elements, canvases, input and tests do not change.

- Property values reach an element through the existing deserialization path (`Initialize(IDeserializer&)` over a property bag), so no new "set field by name" API is needed. For CSS the loader first resolves the cascade (selectors by type, class, id and state such as `:hover`; `@media`) into a bag of declarations per element, then feeds the bag. Verified in task 4.2: a tree of attributes built in memory drives `XmlDeserializer` through `LoadFromXmlNode`, so the bag is a synthetic XML tree and no new `IDeserializer` is needed.
- Layout is a property of a container (`layout="anchor|stack"`, later `flex`), so a CSS `display:flex` maps onto it instead of needing a second layout engine.
- A tree built from a file is owned by the file: the scene editor shows it read-only, and only inline XML children are saved back by `SaveScene()`. Documents can be hot-reloaded in Game mode (pairs with the saves / hot-reload entry).

**The seam that keeps every option open:** any host is a scene component. Hosts are drawn by `Scene::Draw()`, get input through `UIInput`, emit the existing `InputEvents::ActionEvent` for button presses, and read the same `UIVars`. A game's code sees none of the difference, and one game may use several (RmlUi for menus, the element tree for the HUD).

**XML front-end shape** (stable names, no editor-only state, diffable by a model):

```xml
<component type="UICanvasComponent" space="Screen" scaleMode="ScaleWithScreen"
           referenceWidth="1280" referenceHeight="720" match="0.5" useSafeArea="true">
  <elements>
    <element type="UIPanel" name="Hud" anchor="TopLeft" x="16" y="16" width="220" height="96" color="0,0,0,160">
      <children>
        <element type="UILabel" name="Score" anchor="TopLeft" x="12" y="10" text="Score: {score}" fontSize="28"/>
        <element type="UIButton" name="Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="32" text="Reset" action="Match3Reset"/>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait">
      <set element="Hud" anchor="Top" x="0" y="16" width="90%"/>
    </variant>
  </variants>
</component>
```

The same screen from a web-style front-end builds the **same tree** through the same factories (a sketch of the target, not a promise of full HTML/CSS):

```html
<div id="Hud" class="hud">
  <span id="Score">Score: {score}</span>
  <button id="Reset" action="Match3Reset">Reset</button>
</div>
```
```css
.hud     { anchor: top-left; left: 16px; top: 16px; width: 220px; height: 96px; background: rgba(0,0,0,.63); }
#Score   { left: 12px; top: 10px; font-size: 28px; }
#Reset   { anchor: bottom-right; right: 12px; bottom: 10px; width: 96px; height: 32px; }
button:hover { background: #3a6; }
@media (orientation: portrait) { .hud { anchor: top; top: 16px; width: 90%; } }
```

- Button: normal / hover / pressed / disabled looks; press and release inside the rect emits `ActionEvent(action)`; a press that began on the button stays captured until release.
- `UIVars`: `PoolString` → string with a version counter. Game code does `UIVars.Set("score"_intern, Format("{}", s))`; labels re-lay out when the version changes. Only a variable table, no expression language.

## What stage 3 settled (read before stage 4)

- **`UICanvasLayout` is the pure core** (scale, variants, layout, cache, hit test, validator); `UICanvasComponent` is a thin host around one. The matrix helper (`tests/Common/UILayoutMatrix.h`) and later the debug overlay and the control API work on a `UICanvasLayout`.
- **`UIElement` is a plain class, not reflected yet.** `style` is what is authored, `active` what is in effect after variants, `rect` the result. Task 4.1 makes it a `FACTORY_BASE` and checks that nested children, `UILength` strings and `UIVariant` round-trip.
- **Content checks go through `UIContentFit CheckContent(width, height, scale)`** (virtual on `UIElement`): `UILabel` (task 4.3) implements it with `TextLayout`, and the validator's `TextDoesNotFit` rule then has a real producer. Until then it is covered with a test element.
- **Touch size is measured as the user sees it:** `min(w, h) * scale / pixelDensity` in window units against `UISettings::minTouchSize` (32). The first screens must give buttons 48 units at the reference size and set `minScale` (0.7 keeps them above 32 px on a near-square 480 px window); the matrix reports it otherwise.
- **The reflected fields are `visible` / `inputEnabled`**, not `enabled`: a vector of factory-made components writes an `enabled` attribute on every element.
- **Input order:** `Scene::GetDrawRank` (the draw order computed from the tree) registered with `UIInput`, refreshed in `OnDraw()` because a scene attaches its nodes before its tree is complete. A canvas uses the layout of its last draw for input. A drag that began elsewhere (a held button whose press the canvas did not take) is not consumed by a hover.
- `CoreManager` declares `_uiInput` and `_uiEnvironment` before `_sceneManager` so they outlive the scenes (a canvas unregisters in its destructor).

## Not changing

- ImGui: the editor panels, `ImGuiWidget`, its fonts and DPI.
- Swapchain recreation and switching the window to high pixel density (own TODO entry). This plan only reads density and display scale through `IMainWindow`, so that fix changes one value.
- The input map and gamepad (own TODO entry). UI navigation keys (arrows, Enter, Esc) start as a small default table in a UI config and move to the input map when it exists.
- Audio, tween, rotation and camera in `IRenderer`; those entries are consumed later (button sounds, UI animations).
- Text shaping, RTL, complex scripts; vector shapes and rounded corners (sprites for now).
- Serializing child scene nodes and layer inheritance.
- Chess pieces' aspect and the Chess/Match3 scaling entries, except that Match3 gets a view size here (its HUD and board must adapt together).

## Stages

Sizes are rough: S under a day, M a few days, L about a week.

### Stage 0 — Renderer primitives (M)

1. `IRenderer`: `CreateTexture(const uint8_t* rgba, uint32_t w, uint32_t h)`; `DrawTexture(..., const Color& tint)` as the virtual, the current overload forwarding with white; `PushClipRect(Rect)` / `PopClipRect()` (output pixels, after the view transform).
2. SDL backend: `SDL_CreateTexture` + `SDL_UpdateTexture` + blend mode; `SDL_SetTextureColorMod/AlphaMod`; `SDL_SetRenderClipRect`; `SDL_SetRenderDrawBlendMode(BLEND)` in `DrawFilledRect`.
3. Vulkan backend: move the upload from `VulkanTextureLoader` into a shared helper used by the loader and `CreateTexture`; tint `vec4` in the push constants and `texture.frag.glsl`; the clip rect as the scissor passed into `VulkanRectPipeline::Draw` / `VulkanTexturePipeline::Draw`.
4. Test support: turn the `FakeRenderer` of `SceneViewTest.cpp` into a recording double in `tests/Common` (closes the "IRenderer test double" item of the test-gaps entry); it can report any output size, which the adaptive tests rely on. Tests of the clip stack and the forwarding overload.
5. `RenderProbeComponent` (`BECore/Scene/Components/`): a debug component that draws a tinted texture, a translucent rect and a clipped rect, for the manual check on both backends. Put `<component type="RenderProbeComponent"/>` on a node that has a `TransformComponent`.

### Stage 1 — Text (L)

1. Vendor `stb_truetype.h` into `src/Modules/stb/` (INTERFACE target); update the vendored list in `CLAUDE.md`.
2. `Font` (`IResource`) and `FontLoader` (`IResourceLoader`, `.ttf`/`.otf`), CPU only, per `Resource/CLAUDE.md`.
3. `FontAtlas` keyed by (font, rounded effective pixel size): glyph ranges from config (Latin, Latin-1 and Cyrillic first, since the project is written in Russian), shelf packer, extra pages instead of resizing a page that may be in flight, fallback glyph for a missing code point. Straight-alpha RGBA, white color, coverage in alpha, tinted at draw.
4. `TextLayout` (pure): UTF-8 decode, measure, word wrap, align, line spacing, overflow (clip, ellipsis, auto-fit) → positioned glyph quads. gtests on a real font from `assets/`.
5. `TextComponent` (text, font, size, color, align), usable outside UI.
6. Default font in `assets/fonts/` with its license file next to it. Needs an OFL/Apache font with Latin and Cyrillic; the choice is made in this stage.

### Stage 2 — Input and window metrics (M)

1. `IMainWindow`: `GetPixelDensity()`, `GetDisplayScale()`, `GetSafeArea()` with neutral defaults; `SDLMainWindow` answers from `SDL_GetWindowPixelDensity`, `SDL_GetWindowDisplayScale`, `SDL_GetWindowSafeArea`.
2. `SDLEventsProvider::ConvertSDLEvent`: restore mouse motion, button up, wheel, key down/up, text input and window events (resize, focus), and add safe-area-changed and display-orientation-changed (the types are already in `SDLEvents.h`, the last two are new). Mouse events respect ImGui `WantCaptureMouse` as today; keyboard events respect `WantCaptureKeyboard`, which nothing checks today.
3. `UIInput` (service owned by `CoreManager`): canvases of the active scene register with it; the provider calls it first for mouse events and drops the event for the game when it returns "consumed".
4. `ViewportWidget` re-emits motion/up/wheel in viewport-local coordinates like it does for mouse-down, so UI works in the editor viewport.
5. Extend `SDLEventsProviderTest` (headless ImGui context) for the new events and the capture rule.

### Stage 3 — Canvases and adaptive layout, pure core (L)

No controls yet: the point is that the adaptive machinery is proven in code before any screen depends on it.

1. `UILength`, `Anchor`, `UIRectSpec` and the pure `UILayout` (anchors, stretch, percent, min/max, aspect lock, stack with flex weights and wrap). gtests per rule.
2. `UICanvasScaler` and `UILayoutContext`; the three `CanvasSpace` modes and three `scaleMode`s; safe-area handling. gtests: scale and bounds per mode over the size matrix; `SceneView` agrees with `Scene::Draw()`'s fit (`ViewTransform::Fit`); `World` follows the node's `TransformComponent`.
3. `UIVariant` (conditions on the bounds, overrides per element name) with the evaluation rules above. gtests: order, partial overrides, no re-evaluation while the bounds are unchanged.
4. `UILayoutValidator` and the test matrix as a reusable helper (`tests/Common`), so every later screen test is one call.
5. `UICanvasComponent` (the host): reflected fields, owns the `UIElement` tree, caches layout, scoped `ViewTransform` for its draw, registers with `UIInput` (hit-testing in the canvas's own units, topmost canvas first, `modal`). Elements here are plain `UIElement` and a debug rect, which is enough to draw and test.
6. Several canvases in one scene: draw order by node layer, input order reversed, `modal`, show/hide by canvas name; gtests with two overlapping canvases.
7. Editor preview: `ViewportWidget` gets resolution presets (16:9, 4:3, 21:9, 9:16 portrait, free) and a safe-area simulation, so a layout can be looked at in several shapes without resizing the window.

### Stage 4 — Controls, the XML front-end and the first screens (L)

1. Reflection check first: a gtest loads a component with nested `vector<IntrusivePtr<UIElement>>` children from an XML string, saves it back and gets the same text. Includes how `UILength` (`"90%"`) and the `when` conditions are written as strings. If something fails, fix it here, before building on it.
   *Done (4.1).* What the check found: the generator knows a factory field only by `IntrusivePtrAtomic<T>` / `IntrusivePtrNonAtomic<T>` (not the `IntrusivePtr` alias), and named a base without derived classes `uielement` (`ELEMENT=element` fixes the name now); a style as a nested object would have made the file verbose, so `UIElementStyle` is written flat through its own `DataAccessor`; `Ser::Save` drops a value equal to `T{}` under skip-defaults, which would lose `visible="false"` and an override `x="0"`, so those use `DataAccessor::Save` directly; a vector of factory objects accepted an unknown `type` silently (now reported).
2. The format boundary: `UIDocument` (`IResource`) and the XML `IResourceLoader`, so a canvas can take `source="ui/hud.ui.xml"` as well as inline children; a gtest builds the same tree from inline XML, from a file, and from a property bag built in code (the check noted under "Front-ends").

   *Done (4.2).* The property bag check passed without a new `IDeserializer`: a tree of attributes built in memory with pugixml drives `XmlDeserializer::LoadFromXmlNode`, so the web-style front-end resolves its cascade into attributes and reuses the XML path. A document stores the parsed XML and builds fresh elements for each canvas (the layout results live in the elements, so a tree cannot be shared). `ResourceManager` took the extension after the last dot, which sent `hud.ui.xml` to `XmlResourceLoader`; it now tries the longest suffix first.
3. Controls: `UIPanel` (color, optional sprite with 9-slice border), `UIImage` (sprite, `aspectRatio` fit — also the aspect option the Chess sprite entry asks for), `UILabel`, `UIButton`, `UIStack`.

   *Done (4.3).* Found on the way: the generator registered only the direct children of a `FACTORY_BASE` (so `UIButton : UILabel` could not be made by name) and dropped the base itself as soon as it had children; it now follows the chain of parents and `REGISTER_SELF` keeps a concrete base creatable. The stack's `wrap` attribute clashed with the label's text `wrap` (a round trip moved it); it is `wrapChildren` now, and a test sets every property of every control to catch the next clash. `UIImage` keeps proportions by drawing the fitted rect, not through the layout `aspectRatio`, because the proportions belong to the sprite.
4. `UIVars` and label binding; a lookup by name for show/hide of an element or a canvas.
5. gtests without a window: hit-testing and z-order; button states from synthetic events; XML round-trip; draw calls through the recording renderer at two aspect ratios; `UIVars` re-layout; every shipped screen through the validator matrix.
6. Match3: the scene gets a view size (closes that TODO entry). A `Screen` canvas holds the HUD (score label, Reset button, seed) and replaces `DrawHud()`, so the ImGui block and its context check leave `Match3BoardComponent`; a portrait variant moves the HUD above the board; the board answers `ActionEvent("Match3Reset")`. A `MainMenu` scene with Play and Quit is the "main-menu scene as the first consumer" of the TODO entry.

   *Done (4.6), with one change to the plan.* The HUD is a `SceneView` canvas in the margins of the view, not a `Screen` canvas in a corner with a portrait variant: a corner HUD covers the board at 4:3 and at near-square shapes, where there is no free bar, while the margins of the view (80 units above and below the board) are free in every shape and the HUD scales with the board. The cost is that its text and buttons follow the board's scale, so the buttons are 48 view units (32 px at the smallest fit scale of the matrix). The Match3 TODO entry about the view size and the HUD is deleted.

   Also found: the game opens on the menu, so the control smoke test (`reset` with another seed, then compare screenshots) saw the menu both times. A controller that plays has to see the board, so `Match3ControlTarget::enterGame` makes a `reset` and a played action switch to the board's scene. And `CoreManager::GetSceneManager()` is empty in the gtest executable, so `EngineEnvironment::UseSceneManager` hands a test's own manager over (`SetSceneManagerForTest`, guarded by `PassKey`); `be_add_game_tests` got `WHOLE_ARCHIVE` because a test that loads the game's scene file needs the game's components registered.
7. Chess: a `Screen` canvas with a turn label and a Reset button, with the same portrait variant. The promotion picker waits for `chess-staged-stream.md`.

   *Done (4.7), the same change as in 4.6:* a `SceneView` HUD in the margins of the view, not a `Screen` canvas with a portrait variant. The view grew from 760 to 800 units (the board moved from y=60 to y=80) because a 56-unit button, the smallest that is a 32 px touch target at the Chess fit scale (0.57), did not fit the old 60-unit margin. The reason of the end of a game is now on screen, where it had only gone to the log.

### Stage 5 — Decision spike: XML, web-style front-end, RmlUi (L, time-boxed)

Runs after stage 4 so the element tree is measured, not guessed. Three candidates, built for the same screens: (A) the XML front-end from stage 4; (B) a web-style front-end on the same tree; (C) RmlUi as a separate host. Output: `.claude/plans/ui-authoring-decision.md` with the table below filled with numbers. The owner decides, and the answer may be a mix (B is a superset of A in authoring terms; C can sit beside either).

- **B, web-style front-end:** `.html` / `.css` loaders into `UIDocument`. A tag table (`div`, `span`, `p`, `button`, `img`, `ul`/`li`) to element types, a CSS subset (type, class, id and `:hover`/`:active`/`:disabled` selectors; `@media` for orientation, aspect and size; box and anchor properties; color, font and text properties; a `flex` subset mapped onto the stack layout) and a cascade that yields the property bag per element. Measures how much of CSS the pure layout core can take before it becomes a second browser.
- **C, RmlUi:** add `external/RmlUi` (MIT) behind `ENGINE_UI_RMLUI` (OFF by default). `RmlUiRenderInterface` over `IRenderer`, which needs `DrawGeometry` plus the clip stack from stage 0 (`CompileGeometry` / `RenderGeometry` / `ReleaseGeometry`, texture load and generate, scissor are required; clip mask, transforms and layers are optional). `SystemInterface` over `CoreManager::GetTime()` and `LOG_*`. `RmlUiDocumentComponent` forwards `UIInput` events and feeds the canvas bounds, scale and safe area to the RmlUi context.
- Run C on the SDL backend in Game mode (`SDL_RenderGeometry`); estimate the Vulkan geometry pipeline separately. A and B run on both backends.
- Build the same screens in each: the Match3 main menu and HUD, plus a scrolling list with a localized string, and pass them through the same size matrix.

| Question | How it is measured |
|---|---|
| Integration cost | Files and lines added, hours spent, new submodules (RmlUi core; FreeType unless a custom font engine over our `FontAtlas` is written) |
| Adaptivity | Does each candidate pass the size matrix with the same screens; how a portrait variant, the safe area and the user scale are expressed; is the result checkable without a window (A and B: the validator; C: a context with stub interfaces) |
| CSS reach (B) | Which properties and selectors the screens needed, what fell outside the subset, layout bugs found by the matrix |
| Build and size | Clean build time delta, release binary size delta |
| Backend fit | `DrawGeometry` in both backends (Vulkan needs a new pipeline and a dynamic vertex buffer), scissor, optional clip mask and layers |
| Platforms | macOS, Windows, Android, web. The RmlUi readme names SDL3 and iOS/Android as platforms but Emscripten only for its GL3 backend; our Vulkan backend has no web path in either case |
| Features past the MVP | Flexbox, transitions, data binding, spatial navigation in RmlUi vs the estimated cost of each on the element tree (scroll, slider, text input, focus, tween) |
| Model edit loop | Edit `.rml`/`.rcss` vs XML, hot reload, scene dump, screenshot (screenshot needs the control API plan) |
| Fit with the engine | A and B share the element tree, so reflection, the editor tree view, hit-testing and gtests of layout come for free; an RmlUi document has its own DOM, is opaque to the scene editor and needs a context with stub interfaces to test |
| Runtime | Frame time and allocations on a 1000-element screen |

### Stage 6 — Widen the chosen path (size depends on the decision)

- **Element tree (A, and B if kept):** `UIProgressBar`, `UIToggle`, `UISlider`, `UIScrollView` (clip rect), a grid layout, focus and gamepad navigation, a string table for localization (`{key}`), UI animations on top of the tween entry, button sounds on top of the audio entry, `UITextInput` last (text input events, IME, caret — the largest control).
- **RmlUi:** the Vulkan geometry pipeline and clip mask, the font engine decision (FreeType or our atlas), hot reload of documents, a gtest harness with stub interfaces.
- **Several kept:** nothing to build; a new format is a loader, a new library is a host, and the seam above already allows both.

### Stage 7 — Editor and tooling (ImGui, tools only) (M)

- `SceneEditorWidget`: a tree view of canvases and elements (read-only for a tree built from a file, with add/remove for inline XML children), a custom inspector for anchor presets and variants; the generic visitor already shows reflected fields.
- A debug overlay on each canvas (element outlines, names, the safe area, the validator's findings).
- Control API methods `ui_tree` (canvases and elements with resolved rects, validator issues) and `ui_click(name)`; with the planned screenshot this gives the edit, run, look loop that the authoring TODO entry asks for. Depends on `buzzing-tinkering-cat.md`.

## Docs and bookkeeping

- New `src/Modules/BECore/UI/CLAUDE.md`: how to build a screen (canvas spaces, the scaler, the layout rules, variants, the validator, the component list, the example above) — the place models read; new `BECore/Text/CLAUDE.md`; the chosen authoring path recorded there and in `Scene/CLAUDE.md`.
- New `.claude/rules/ui-module.md` (`paths:` frontmatter). Update `rendering-module.md` (new primitives), `widgets-module.md` (the "a widget is not game UI" line points to the UI doc), `SDL/CLAUDE.md` (restored events, window metrics, `UIInput`), `Vulkan/CLAUDE.md` (tint, clip, geometry), `BECore/CLAUDE.md` (run modes: input through `UIInput`), `Match3/CLAUDE.md`, `CLAUDE.md` (stb in the vendored list).
- `reflection_targets.json`: add `BECore/UI` and `BECore/Text`; re-run `cmake --preset macos-debug`.
- `TODO.md`: set `**Status:**`, `**Stage:**` and `**Plan:**` on the four UI entries when work starts; delete an entry when it is done (the Match3 view size entry with stage 4); run `todo-sync check`.
- Work stage by stage, one commit per stage, each ending with the build and the tests green.

## Verification

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests
ctest --preset macos-debug
cmake --build --preset macos-debug     # BagiChess, BagiMatch3
```

- Automatic: the gtests listed per stage (recording renderer, `TextLayout`, `UILayout`, scaler and variants, the validator over the size matrix for every shipped screen, hit-testing with several canvases, button states, XML round-trip, `SDLEventsProvider` events).
- Manual, because no test creates a GPU device and the agent session cannot see the screen: run `BagiMatch3` in Game mode on the Vulkan backend. Check the score changes, the Reset button works, a click on the button does not move a tile, the menu scene starts and quits, Cyrillic text renders. Change the window size in `SDLWindowConfig.xml` between runs (wide, near square, tall) to see the portrait variant and the board fit; in the editor use the viewport presets. A live window resize waits for the swapchain entry. Repeat on the SDL backend for the primitives from stage 0.
- Finish each stage with the `clang-format` skill and then `strip-pch-includes` (dry run first).

## Risks

- Nested polymorphic children in a reflected component may not serialize today (checked first in task 4.1).
- `UILength` (`"90%"`), the `when` conditions and the variant overrides need string forms; `Color` uses child elements, so the way a custom value type reads from one attribute is checked in task 4.1.
- A CSS-like front-end grows without a limit unless the subset is written down: the spike lists what the screens needed and stops there.
- Safe area and display orientation are not exercisable on a desktop: they are covered by injected values in tests and by the editor preview, and stay unchecked on a real phone until an Android build exists (release builds entry).
- A `World` canvas hit-test goes through the scene's fit and the node's transform; if it proves fragile, `World` moves to stage 6 and `Screen` and `SceneView` ship first.
- GPU calls only on the main thread: atlas creation must never run inside `LoadAsync`.
- Vulkan textures are destroyed only when the GPU is idle: the atlas adds pages and never frees one in flight.
- Atlas memory grows with distinct pixel sizes: sizes are rounded, and an eviction policy waits until it is measured as a problem.
- The font license and the Cyrillic glyph coverage must be settled before the font is committed.
- The RmlUi spike on the SDL backend can understate the Vulkan cost; the table asks for an explicit Vulkan estimate.
