# In-game UI without ImGui

## Context

Today the only UI is ImGui, and it is for tools: the editor panels in `src/Widgets/`. The Game run mode has no ImGui context, so `BagiMatch3` shows no score and has no Reset button (its HUD is an `ImGui::Begin` block in `Match3BoardComponent::DrawHud()`), and Chess has no HUD at all. `TODO.md` already records the gap in four stage-3 entries: "No text rendering in the engine", "Input has no mapping layer", "No in-game UI", "Game UI has no LLM-friendly authoring path yet".

Goal: UI that the engine draws and builds itself, through `IRenderer`, written as data, working in both backends and in both run modes. ImGui stays what it is (editor tools) and is not touched.

**Open decision, kept open on purpose:** how a screen is described — the engine's own declarative XML (TODO option 1, the recommendation there) or RmlUi (HTML/CSS, option 3). The owner wants more data before choosing and wants both to stay possible. So the plan is built around that:

1. Stages 0-2 are the foundation **both** paths need (renderer primitives, text, input).
2. Stage 3 builds a small own-XML MVP and moves the first real screens onto it. It is also the proof of the seam: a UI host is a scene component.
3. Stage 4 is a time-boxed spike that builds the same screens in RmlUi and produces a decision record with measurements.
4. Stage 5 widens whichever path wins. The loser stays possible as an optional module, not a rewrite.

## What the code says (shapes the design)

| Finding | Consequence |
|---|---|
| `IRenderer` draws with `DrawFilledRect` and `DrawTexture` only. No tint, no clip, no texture from pixels. `SDLRendererBackend::DrawFilledRect` sets no blend mode. | Stage 0 adds the missing primitives to the interface first, then both backends (`rendering-module.md`). |
| Textures come only from file loaders; `VulkanTextureLoader` has the upload code inline. | Extract the upload into a helper and add `IRenderer::CreateTexture(pixels)`; the glyph atlas needs it. |
| The push constants of the Vulkan texture pipeline are 40 bytes; the scissor is a dynamic state that `VulkanTexturePipeline::Draw` sets to the full extent. | A tint (`vec4`) and a clip rect fit without a new pipeline. |
| The scene XML describes top-level nodes only; children are runtime-only; a node's layer is not inherited (`Scene/CLAUDE.md`). | A UI tree cannot be scene nodes. The tree lives **inside one component** as a reflected `vector<IntrusivePtr<UIElement>>`, the same mechanism `SceneNode` uses for `_components`. Proving it round-trips is task 3.1. |
| `Scene::Draw()` fits a view with letterbox bars. | UI must anchor to the real window edges, so a UI host sets its own `ViewTransform` around its draw (RAII scope) and maps the mouse itself instead of using `Scene::ScreenToScene*`. |
| `ConvertSDLEvent` forwards only mouse-down, and the only "consumed" rule is ImGui `WantCaptureMouse`. Events carry no consumed flag. | Stage 2 restores the commented-out conversions and adds one input-capture entry point, so a click on a button does not also hit the board under it. |
| Resource loading can run on a pool thread (`LoadAsync`). | A font loader reads the TTF bytes only. Rasterizing and `CreateTexture` happen lazily on the main thread. |
| Both games use `VulkanRendererBackend`; `SDLRendererBackend` has no render targets, but Game mode does not need them. | The RmlUi spike can run on the SDL backend in Game mode (`SDL_RenderGeometry` exists) and estimate the Vulkan cost. To verify first: that the SDL backend runs Match3 in Game mode. |
| The engine-level `assets/` directory is listed in `CLAUDE.md` but does not exist yet. | The default font goes to `assets/fonts/`; check that `FileSystem` layering handles a new engine `assets/`. |
| The Vulkan swapchain is not recreated on resize and there is no HiDPI (separate TODO entry). | Not fixed here. UI layout is computed every frame from `GetOutputWidth/Height()`, and the glyph atlas is keyed by effective pixel size, so both pick the fix up without changes. |

## Design

**Layers, bottom to top** (all in `BECore` unless noted; the module order in `CLAUDE.md` is unchanged):

- **A. Renderer primitives** (`Renderer/`): `CreateTexture`, tinted `DrawTexture`, clip rect stack, alpha blending in the SDL rect path. `DrawGeometry` (indexed triangles with per-vertex color and UV) is added **only** for the RmlUi spike.
- **B. Text** (`Text/`, new): `Font` resource, `FontAtlas`, pure `TextLayout`, `TextComponent`. Glyphs come from `stb_truetype.h` (single header, vendored in a new `src/Modules/stb/`, listed with the other vendored code). No GPU or window is needed to measure text, so layout is testable.
- **C. Input** (`SDL` module + `Input/`): restored mouse motion/up/wheel/key/text events; `UIInput` entry point that returns "consumed".
- **D. UI core** (`UI/`, new): `UIElement` tree, anchor layout (pure code), `UICanvasComponent` (the host), controls, `UIVars` for live values.
- **E. Authoring**: own XML inside `SceneConfig.xml` (default). RmlUi, only if the spike says so, as an optional module `src/Modules/RmlUi` behind `ENGINE_UI_RMLUI` with a second host, `RmlUiDocumentComponent`.

**The seam that keeps both paths open:** any host is a scene component. Both hosts are drawn by `Scene::Draw()`, get input through `UIInput`, emit the existing `InputEvents::ActionEvent` for button presses, and read the same `UIVars`. A game's code sees none of the difference, and one game may use both (RmlUi for menus, own XML for the HUD).

**Own-XML authoring shape** (stable names, no editor-only state, diffable by a model):

```xml
<component type="UICanvasComponent" referenceWidth="1280" referenceHeight="720" scaleMode="MatchHeight">
  <elements>
    <element type="UIPanel" name="Hud" anchor="TopLeft" x="16" y="16" width="220" height="96" color="0,0,0,160">
      <children>
        <element type="UILabel" name="Score" anchor="TopLeft" x="12" y="10" text="Score: {score}" fontSize="28"/>
        <element type="UIButton" name="Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="32" text="Reset" action="Match3Reset"/>
      </children>
    </element>
  </elements>
</component>
```

- Layout: `Anchor` (`CORE_ENUM`: the nine points plus `Stretch` with margins), an offset from the anchor and a size; `UIStack` (vertical or horizontal, spacing, padding) for lists. Layout functions are pure: `ResolveRect(parentRect, spec)`.
- Canvas: `referenceWidth/Height` plus `scaleMode` (`ConstantPixelSize`, `FitReference`, `MatchWidth`, `MatchHeight`). The canvas puts the UI on a high `layer` so it draws over the board.
- Button: normal / hover / pressed / disabled looks; press and release inside the rect emits `ActionEvent(action)`; a press that began on the button stays captured until release.
- `UIVars`: `PoolString` → string with a version counter. Game code does `UIVars.Set("score"_intern, Format("{}", s))`; labels re-lay out when the version changes. Only a variable table, no expression language.

## Not changing

- ImGui: the editor panels, `ImGuiWidget`, its fonts and DPI.
- Swapchain recreation and HiDPI (own TODO entry); the Match3/Chess scaling and stretched-piece entries, except where a HUD needs a view size.
- The input map and gamepad (own TODO entry). UI navigation keys (arrows, Enter, Esc) start as a small default table in a UI config and move to the input map when it exists.
- Audio, tween, rotation and camera in `IRenderer`; those entries are consumed later (button sounds, UI animations).
- Text shaping, RTL, complex scripts; vector shapes and rounded corners (sprites for now).
- Serializing child scene nodes and layer inheritance.

## Stages

Sizes are rough: S under a day, M a few days, L about a week.

### Stage 0 — Renderer primitives (M)

1. `IRenderer`: `CreateTexture(const uint8_t* rgba, uint32_t w, uint32_t h)`; `DrawTexture(..., const Color& tint)` as the virtual, the current overload forwarding with white; `PushClipRect(Rect)` / `PopClipRect()` (output pixels, after the view transform).
2. SDL backend: `SDL_CreateTexture` + `SDL_UpdateTexture` + blend mode; `SDL_SetTextureColorMod/AlphaMod`; `SDL_SetRenderClipRect`; `SDL_SetRenderDrawBlendMode(BLEND)` in `DrawFilledRect`.
3. Vulkan backend: move the upload from `VulkanTextureLoader` into a shared helper used by the loader and `CreateTexture`; tint `vec4` in the push constants and `texture.frag.glsl`; the clip rect as the scissor passed into `VulkanRectPipeline::Draw` / `VulkanTexturePipeline::Draw`.
4. Test support: turn the `FakeRenderer` of `SceneViewTest.cpp` into a recording double in `tests/Common` (closes the "IRenderer test double" item of the test-gaps entry). Tests of the clip stack and the forwarding overload.
5. A debug scene or a throwaway game scene that draws a tinted texture, a translucent rect and a clipped rect, for the manual check on both backends.

### Stage 1 — Text (L)

1. Vendor `stb_truetype.h` into `src/Modules/stb/` (INTERFACE target); update the vendored list in `CLAUDE.md`.
2. `Font` (`IResource`) and `FontLoader` (`IResourceLoader`, `.ttf`/`.otf`), CPU only, per `Resource/CLAUDE.md`.
3. `FontAtlas` keyed by (font, rounded pixel size): glyph ranges from config (Latin, Latin-1 and Cyrillic first, since the project is written in Russian), shelf packer, extra pages instead of resizing a page that may be in flight, fallback glyph for a missing code point. Straight-alpha RGBA, white color, coverage in alpha, tinted at draw.
4. `TextLayout` (pure): UTF-8 decode, measure, word wrap, align, line spacing, overflow (clip or ellipsis) → positioned glyph quads. gtests on a real font from `assets/`.
5. `TextComponent` (text, font, size, color, align), usable outside UI.
6. Default font in `assets/fonts/` with its license file next to it. Needs an OFL/Apache font with Latin and Cyrillic; the choice is made in this stage.

### Stage 2 — Input (M)

1. `SDLEventsProvider::ConvertSDLEvent`: restore mouse motion, button up, wheel, key down/up, text input and window resize/focus (the types are already in `SDLEvents.h`). Mouse events respect ImGui `WantCaptureMouse` as today; keyboard events respect `WantCaptureKeyboard`, which nothing checks today.
2. `UIInput` (service owned by `CoreManager`): canvases of the active scene register with it; the provider calls it first for mouse events and drops the event for the game when it returns "consumed".
3. `ViewportWidget` re-emits motion/up/wheel in viewport-local coordinates like it does for mouse-down, so UI works in the editor viewport.
4. Extend `SDLEventsProviderTest` (headless ImGui context) for the new events and the capture rule.

### Stage 3 — UI core MVP and the first screens (L)

1. Reflection check first: a gtest loads a component with nested `vector<IntrusivePtr<UIElement>>` children from an XML string, saves it back and gets the same text. If that fails, fix it here, before building on it.
2. `UIElement` (`FACTORY_BASE`: name, visible, enabled, anchor, offset, size, children), pure `UILayout` (anchors, stretch, stack), `UICanvasComponent` (layout, scoped `ViewTransform`, draw, input).
3. Controls: `UIPanel` (color, optional sprite with 9-slice border), `UIImage` (sprite, keep-aspect — also the aspect option the Chess sprite entry asks for), `UILabel`, `UIButton`, `UIStack`.
4. `UIVars` and label binding; a lookup by name for show/hide of an element or screen.
5. gtests without a window: layout at 16:9, 4:3, 21:9, 9:16 and tiny/huge sizes; hit-testing and z-order; button states from synthetic events; XML round-trip; draw calls through the recording renderer at two aspect ratios; `UIVars` re-layout.
6. Match3: HUD (score label, Reset button, seed) replaces `DrawHud()`, so the ImGui block and its context check leave `Match3BoardComponent`; the board answers `ActionEvent("Match3Reset")`; a `MainMenu` scene with Play and Quit. This is the "main-menu scene as the first consumer" of the TODO entry.
7. Chess: turn label and a Reset button. The promotion picker waits for `chess-staged-stream.md`.

### Stage 4 — Decision spike: own XML vs RmlUi (L, time-boxed)

Runs after stage 3 so the own path is measured, not guessed. Output: `.claude/plans/ui-authoring-decision.md` with the table below filled with numbers. The owner decides.

- Add `external/RmlUi` (MIT) behind `ENGINE_UI_RMLUI` (OFF by default). `RmlUiRenderInterface` over `IRenderer`, which needs `DrawGeometry` plus the clip stack from stage 0 (`CompileGeometry` / `RenderGeometry` / `ReleaseGeometry`, texture load and generate, scissor are required; clip mask, transforms and layers are optional). `SystemInterface` over `CoreManager::GetTime()` and `LOG_*`. `RmlUiDocumentComponent` forwards `UIInput` events.
- Run on the SDL backend in Game mode (`SDL_RenderGeometry`); estimate the Vulkan geometry pipeline separately.
- Build the same screens in both: the Match3 main menu and HUD, plus a scrolling list with a localized string.

| Question | How it is measured |
|---|---|
| Integration cost | Files and lines added, hours spent, new submodules (RmlUi core; FreeType unless a custom font engine over our `FontAtlas` is written) |
| Build and size | Clean build time delta, release binary size delta |
| Backend fit | `DrawGeometry` in both backends (Vulkan needs a new pipeline and a dynamic vertex buffer), scissor, optional clip mask and layers |
| Platforms | macOS, Windows, Android, web. The RmlUi readme names SDL3 and iOS/Android as platforms but Emscripten only for its GL3 backend; our Vulkan backend has no web path in either case |
| Features past the MVP | Flexbox, transitions, data binding, spatial navigation in RmlUi vs the estimated cost of each in the own path (scroll, slider, text input, focus, tween) |
| Model edit loop | Edit `.rml`/`.rcss` vs XML, hot reload, scene dump, screenshot (screenshot needs the control API plan) |
| Fit with the engine | Own path gets reflection, the scene editor inspector and gtests of layout for free; an RmlUi document is opaque to the scene editor and needs a context with stub interfaces to test |
| Runtime | Frame time and allocations on a 1000-element screen |

### Stage 5 — Widen the chosen path (size depends on the decision)

- **Own XML:** `UIProgressBar`, `UIToggle`, `UISlider`, `UIScrollView` (clip rect), focus and gamepad navigation, modal stack that blocks input behind it, a string table for localization (`{key}`), safe-area insets, UI animations on top of the tween entry, button sounds on top of the audio entry, `UITextInput` last (text input events, IME, caret — the largest control).
- **RmlUi:** the Vulkan geometry pipeline and clip mask, the font engine decision (FreeType or our atlas), hot reload of documents, a gtest harness with stub interfaces.
- **Both kept:** nothing to build; the seam above already allows it.

### Stage 6 — Editor and tooling (ImGui, tools only) (M)

- `ViewportWidget`: preview-resolution presets (16:9, 4:3, 21:9, 9:16) so a layout is checked at several aspect ratios without resizing the window.
- `SceneEditorWidget`: a tree view of UI elements with add/remove, and a custom inspector for anchor presets; the generic visitor already shows reflected fields.
- A debug overlay on the canvas (element outlines and names).
- Control API methods `ui_tree` (elements with resolved rects) and `ui_click(name)`; with the planned screenshot this gives the edit, run, look loop that the authoring TODO entry asks for. Depends on `buzzing-tinkering-cat.md`.

## Docs and bookkeeping

- New `src/Modules/BECore/UI/CLAUDE.md`: how to build a screen (component list, the example above) — the place models read; new `BECore/Text/CLAUDE.md`; the chosen authoring path recorded there and in `Scene/CLAUDE.md`.
- New `.claude/rules/ui-module.md` (`paths:` frontmatter). Update `rendering-module.md` (new primitives), `widgets-module.md` (the "a widget is not game UI" line points to the UI doc), `SDL/CLAUDE.md` (restored events, `UIInput`), `Vulkan/CLAUDE.md` (tint, clip, geometry), `Match3/CLAUDE.md`, `CLAUDE.md` (stb in the vendored list).
- `reflection_targets.json`: add `BECore/UI` and `BECore/Text`; re-run `cmake --preset macos-debug`.
- `TODO.md`: set `**Status:**`, `**Stage:**` and `**Plan:**` on the four UI entries when work starts; delete an entry when it is done; run `todo-sync check`.
- Work stage by stage, one commit per stage, each ending with the build and the tests green.

## Verification

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests
ctest --preset macos-debug
cmake --build --preset macos-debug     # BagiChess, BagiMatch3
```

- Automatic: the gtests listed per stage (renderer recording double, `TextLayout`, `UILayout` at several aspect ratios, hit-testing, button states, XML round-trip, `SDLEventsProvider` events).
- Manual, because no test creates a GPU device and the agent session cannot see the screen: run `BagiMatch3` in Game mode on the Vulkan backend. Check the score changes, the Reset button works, a click on the button does not move a tile, the menu scene starts and quits, Cyrillic text renders. Change the window size in `SDLWindowConfig.xml` between runs to check other aspect ratios (a live resize waits for the swapchain entry). Repeat on the SDL backend for the primitives from stage 0.
- Finish each stage with the `clang-format` skill and then `strip-pch-includes` (dry run first).

## Risks

- Nested polymorphic children in a reflected component may not serialize today (checked first in task 3.1).
- GPU calls only on the main thread: atlas creation must never run inside `LoadAsync`.
- Vulkan textures are destroyed only when the GPU is idle: the atlas adds pages and never frees one in flight.
- Atlas memory grows with distinct pixel sizes: sizes are rounded, and an eviction policy waits until it is measured as a problem.
- The font license and the Cyrillic glyph coverage must be settled before the font is committed.
- The RmlUi spike on the SDL backend can understate the Vulkan cost; the table asks for an explicit Vulkan estimate.
