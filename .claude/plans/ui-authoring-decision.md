# How an in-game UI screen is authored: the data for the decision

Stage 5 of `.claude/plans/magical-leaping-blossom.md`. This file holds what was measured; the owner's decision is the last section. Status: **all three candidates are built and measured** (A and B on the element tree, C as a separate host). "Reading the data" reads the numbers; it does not choose.

The three candidates, for the same screens:

- **A. The engine's XML** (`.ui.xml`, or inline in `SceneConfig.xml`): stage 4. Elements are `<element type="UIButton" ...>`, the style is flat attributes, responsive variants are `<variants>`.
- **B. A web-style front-end** (`.ui.html` + CSS): stage 5.1. A compiler writes the text of an A document, so it builds the same tree through the same factories (`UI/CLAUDE.md`, "Writing a screen in HTML and CSS").
- **C. RmlUi** as a separate host with its own DOM and layout (`.rml` + `.rcss`): stage 5.3. The vendored library (`src/Modules/RmlUi`) and its host (`src/Modules/RmlUiHost`, `CLAUDE.md` there), both behind `-DENGINE_UI_RMLUI=ON`, off by default.

Numbers are from a Debug and a Release build on macOS (Apple M4 Pro, 12 cores, Apple Clang), so they are good for comparing the three, not as absolute figures. The tests that produce them are in the repository (`tests/RmlUiHost/`, and `UIWebTest` for B).

## What was measured

| Question | A: XML | B: HTML + CSS | C: RmlUi |
|---|---|---|---|
| **Integration cost** | Stage 4 (reflection of the tree, `UIDocument`, controls): not counted here | +2,850 lines in 11 files (`UIHtmlParser`, `UICssParser`, `UIWebCompiler`, `UIWebText`, `UIWebReport`, `UIWebDocumentLoader`), +900 lines of test (41 cases), one `friend` line in `UIDocument.h`; no dependency | **+1,033 lines in 12 files** (`RmlUiHost`: render, system and file interfaces, runtime, `UIVars` adapter, the component), +1,413 lines of test (28 cases), `IRenderer::DrawGeometry` (about 120 lines in `IRenderer`, `SDLRendererBackend` and `RecordingRenderer`, 9 cases), the vendored library (**3.2 MB**, 444 files, unedited); **FreeType** from the system for its font engine (`brew install freetype`; a real adoption vendors it or writes a `FontEngineInterface` over `FontAtlas`). One working session each for B and C |
| **Same screens** | The three shipped screens (Match3 menu, Match3 HUD, Chess HUD) | All three in HTML + CSS with 0 issues; every element on the same rect as the shipped XML (`UIWebTest`) | All three in RML + RCSS: **every element on the same rect as the shipped XML in all 138 cases of the size matrix** (10 aspect ratios, 480 to 2160 px short side, no insets / notch / side insets; `RmlUiScreensTest`), nothing outside the content, buttons over the touch size, no text overflowing its box. A deliberately broken screen is caught by the same checks. Looked at on the SDL software renderer (menu, both HUDs, and the menu inside a running `BagiMatch3`) |
| **Adaptivity** | Anchors, percents, limits, stack, variants, safe area, validator on the matrix | The same machinery: `@media` becomes a `UIVariantCondition`; the matrix and the validator work unchanged | The same scaler decides the rect and the scale; the context is that rect in pixels and `dp` is a UI unit, so a screen in `dp` and `%` follows it. Flexbox, `max-width` and `%` did what the XML stack does. **No validator**: the checks above are written on RmlUi's boxes (`GetAbsoluteOffset`, `GetBox`, `GetScrollWidth` against `GetClientWidth` for text that does not fit), they exist for the test's screens only |
| **Source size** (the 1,001-element screen below) | 120,061 bytes | 76,338 bytes (compiles to the XML) | 76,547 bytes |
| **Backend fit** | Rect + texture + tint + clip: both backends | Same | Needs `DrawGeometry`: done for SDL (`SDL_RenderGeometryRaw`), **not for Vulkan** (a pipeline and a dynamic vertex buffer; the default logs once and draws nothing), so RmlUi documents show on the SDL backend only. CSS `transform`, filters, shaders, gradients, `box-shadow` and the rounded clip mask are not implemented by the render interface: RmlUi draws nothing for them, silently |
| **Platforms** | Whatever the engine runs on | Same | The core is portable C++; FreeType is the added dependency. SDL backend only for now. Android and web not tried |
| **Model edit loop** | Text file, stable names, validator, report | Text in a notation models know well; the compile reports every property, selector, tag and value it cannot apply with the line | Text in the same notation (RmlUi's dialect: `dp` units, `data-model`, `{{vars.x}}`); RmlUi reports what it cannot parse through our logger (`RmlUi: ...`). There is no report of what the host ignores: an unsupported effect is silent. Screenshots through the control API work as for the others |
| **Fit with the engine** | Native: reflection, scene editor tree view (stage 7), hit testing, gtests of layout | Native, since it is A underneath | A scene component beside the canvases: draw order, `UIInput` order, `modal`, `ActionEvent`, `UIVars` all line up (`{{vars.score}}` through an adapter, not `{score}`). The document is opaque to the scene editor, `UILayoutValidator` and `ui_tree`; edits in the editor do not reach it |

### One screen of 1,001 elements (250 cards x 4: a plate, two labels, a button), all in the frame

`UIAuthoringBenchmarkTest`, one process, the three formats on the same generated screen. This replaces the first figures of this record, which came from a script that was not kept and a screen that was partly outside the window (RmlUi does not draw what is outside; the element tree does).

| | Debug A | Debug B | Debug C | Release A | Release B | Release C |
|---|---|---|---|---|---|---|
| source bytes | 120,061 | 76,338 | 76,547 | same | same | same |
| load: parse and build (ms) | 4.6 | 29.4 | 19.5 | 2.1 | 5.6 | 6.0 |
| layout, first (ms) | 0.13 | 0.10 | 4.4 | 0.04 | 0.03 | 0.64 |
| layout, window resized (ms) | 0.10 | 0.10 | 3.7 | 0.02 | 0.02 | 0.53 |
| layout, nothing changed (ms) | 0.00 | 0.00 | 0.04 | 0.00 | 0.00 | 0.02 |
| draw, first: glyphs are made (ms) | 10.5 | (A's tree) | 8.0 | 3.1 | (A's tree) | 1.05 |
| draw, warm (ms) | 1.28 | (A's tree) | 2.4 | 0.09 | (A's tree) | 0.40 |
| draw calls per frame | 4,780 | (A's tree) | 1,250 | 4,780 | (A's tree) | 1,250 |
| heap of the screen (KB) | 625 | (A's tree) | 4,726 | 625 | (A's tree) | 4,726 |

B is A's tree once compiled, so its layout and draw are A's. Draw calls: the engine draws a glyph at a time, RmlUi one geometry per text run (21,120 vertices a frame). RmlUi's layout costs 16 to 37 times the element tree's here (the first layout and a resize, Debug and Release alike) and is still under a millisecond in Release; it lays the whole document out again when the window changes.

### Build and size

| | Without RmlUi | With RmlUi | Delta |
|---|---|---|---|
| `BagiMatch3`, Release | 5,187,864 bytes | 7,162,360 bytes | **+1,974,496 bytes (+1.97 MB, +38%)** |
| `BagiMatch3`, Release, `strip -x` | 4,813,808 bytes | 6,535,984 bytes | +1,722,176 bytes (+1.72 MB, +36%) |
| clean build of `rmlui_core` (+ the host in Release) | | Debug: 7.5 s wall, 79 s CPU; Release: 15.6 s wall, 172 s CPU | on 12 cores |

B: five sources, about 1.3 s to compile together, 5.9 MB of Debug objects. The default build (`ENGINE_UI_RMLUI=OFF`) is unchanged: no RmlUi symbol in the executables, no extra target.

### CSS reach (B)

What the three screens and the tests needed from CSS (all of it supported): `display: none | flex`, `flex-direction`, `gap`, `padding`, `flex-wrap`, `align-items`, `flex`; `left / top / right / bottom / inset` and the non-CSS `anchor`; width, height, min and max in px and %, `aspect-ratio`, `object-fit`; `background`, `background-color`, `background-image`, `border-image-slice`; `color`, `font-size`, `font-family`, `text-align`, `vertical-align`, `text-overflow`, `white-space`, `line-height`; `pointer-events`; `:hover / :active / :disabled` on a button; `@media` on orientation, size and aspect; the cascade with specificity, `!important`, inline `style=""` and inheritance.

What is **outside** the subset (reported when it appears, never silent) and what it costs:

| Outside | Why it is outside | Cost to add |
|---|---|---|
| `justify-content`, `margin` between siblings, `grid` | `UIStack` has no main-axis alignment, no margins in a stack, no grid | Small (an option on the stack) for `justify-content`; a second layout for `grid` |
| `em`, `rem`, `vw`, `vh`, `calc()`, `var()` | The layout takes px and % | `rem` and `vw / vh` are small; `calc()` and `var()` are an expression language |
| `opacity`, `border`, `border-radius`, `box-shadow`, `transform`, `transition`, `animation`, `z-index`, `overflow`, `font-weight` | The renderer has no group alpha, rounded shapes, transforms, tween or font weights; draw order is the tree order | Each is a renderer or UI feature of its own (the engine's tween, rotation and audio entries) |
| a `@media` block that changes a background, a margin or a padding | A variant overrides visibility, anchor, offset, size, stack direction and font size only | Widen `UIElementOverride` (a small, mechanical change) |
| Attribute selectors, `+`, `~`, `:first-child`, `:not()`, pseudo-elements, `@import`, `@font-face`, `@keyframes` | Left out of the parser | Selectors are small; `@keyframes` needs animation |
| Text next to block elements, `<p>` closed by the next sibling | Not a browser's HTML parser | Not planned |
| The cascade of shorthands and longhands | They are separate names (the later declaration wins, not CSS's expansion) | Small, a table of what a shorthand sets |

Findings while writing B:

1. **A wrapper `div` must not take clicks.** A full-screen `div` that only groups elements would swallow every click of the game under it. The compile makes a group take clicks only with a visible background or `pointer-events: auto`.
2. **A `div` with loose text is a label,** and an element with only inline children and no text of its own is a container.
3. **Specificity bites a model the way it bites a web developer:** `#Reset { background }` beats `button:hover { background }`.
4. **Centering has no CSS spelling that the engine can take**, so the front-end adds one non-CSS property, `anchor`, and two engine-only ones for text auto-fit.
5. **A button keeps its own text alignment**, as a browser's default style sheet does.
6. The compile is **2,850 lines**, more than the roughly 1,000 first estimated: the property table, the cascade, the variants diff and the state rules are each a real piece.

### What building C showed

1. **RmlUi is used as designed, with few seams.** The three interfaces (render, system, file) are 8 + 2 + 6 methods; the optional ones (transform, clip mask, layers, filters, shaders) are left at their defaults. `Context` per document, `dp` ratio from the scaler, `ProcessMouse*` from `UIInput`, `click` listener to `ActionEvent`: no surprise in the library.
2. **Premultiplied alpha.** RmlUi's vertex colors and generated textures are premultiplied; `IRenderer` blends straight alpha. The host converts once, when the geometry is compiled or the texture generated. Looked at: no dark halos on text. This is a cost that every RmlUi backend pays and a bug waiting in the first one that forgets it.
3. **The same click-through decision, the other way round.** An RmlUi element takes the click wherever it has a box, `body` included (`IsMouseInteracting()` is true over anything but the context root), so the full-screen wrapper problem of finding 1 shows up as "put `pointer-events: none` on `body`, `auto` on buttons". It is inherited.
4. **`body` has no size** in RmlUi (`body { width: 100%; height: 100% }` is the first line of every screen), and `dp`, not `px`, is the unit that follows the scaler.
5. **A click can destroy the component.** RmlUi dispatches `click` from inside `ProcessMouseButtonUp`; a handler of `ActionEvent` that switches scenes would free the context it is running in. The host queues the action and emits it after the call (as `UICanvasComponent` clicks last).
6. **RmlUi culls elements outside the window; the element tree does not.** The first version of the benchmark compared that, not the formats.
7. **Silent gaps.** What the render interface does not implement draws nothing without a message. A check of "does this screen use only what the host supports" does not exist (B's report is what the other path offers).
8. **The font engine brings FreeType** (resolved in stage 6.2: `RmlUiFontEngine` over the engine's `Font`, no FreeType; this was true of the build measured here). Without a system FreeType the library does not build (`RMLUI_FONT_ENGINE=none` plus a `FontEngineInterface` over `FontAtlas` is the alternative, six methods, not tried). One face is loaded (`TextConfig.xml`'s default font); weights and fallback faces are not set up.
9. **Text binding is RmlUi's own** (`data-model` and `{{ }}`): the host exposes `UIVars` as the struct `vars` through a small custom `VariableDefinition`, so game code is unchanged and a screen says `{{vars.score}}` where the others say `{score}`.
10. **Textures:** `<img src>` takes a file path, not a `TextureLibrary` sprite name; a sprite from an atlas would need a decorator or a host-side lookup. The three screens use none.
11. **Tool support:** nothing in the scene editor, the validator or `ui_tree` reaches into a document; its tests need a context with a renderer double (`RecordingRenderer` records geometry for that, `RmlUiHostTest`).

## Reading the data

- **A already covers every screen the repository ships.** B covers them too, with no gap found, and adds a notation that models and designers know, at a cost of about 2,850 lines in one place and nothing in the runtime. **C covers them as well**, in the same notation family, with every rect on the XML's, and brings features that A and B lack (flexbox in full, transitions and animations, data binding, `overflow` with scrolling, spatial navigation, `:focus`, `@media` on more features, hot reload of a document through the library).
- **What C costs, measured:** +1.97 MB of executable (+38%), +4.7 MB of heap per 1,000 elements (7.5 times the element tree), a layout 16 to 37 times slower (still under a millisecond in Release for 1,001 elements, relaid out on every window change), a system dependency (FreeType), a render interface to finish for Vulkan (not started) and to keep complete for features (nothing is reported when one is missing), and a second DOM that the engine's tooling cannot see (no validator, no editor view, no `ui_tree`). Its draw is cheaper in calls (1,250 against 4,780) and its text is cheaper to make the first time in Release (1.05 ms against 3.1).
- **What C buys:** the controls of stage 6 that A and B would build one by one (`UIScrollView`, slider, toggle, progress bar, focus and gamepad navigation, animation, `UITextInput` being the largest) exist in RmlUi, with their CSS.
- **The decision is between building those controls (the plan estimates each) and living with a second DOM.** Nothing measured says one is wrong: the measurements say what each costs.
- If the owner chooses now: the data supports **A as the base with B as an optional second loader** (nothing but a loader depends on B, and a screen written in HTML compiles to the XML it stands for), and **RmlUi kept as the optional module it is** (off by default, nothing in the engine depends on it), to be taken up again when Mahjong, the consumer of stage 6 in the roadmap, needs a control that is more than a week of work on the element tree, or when the Vulkan backend gets `DrawGeometry`. The alternative the data also supports is to **make C the path for screens with scrolling, text input or animation** and keep A and B for HUDs and menus, since the seam (a scene component, `UIInput`, `ActionEvent`, `UIVars`) already allows both in one game.

## Decision

**2026-11-02, the owner: A with B, and RmlUi for the screens that need scrolling, text input or animation.** HUDs and menus stay on the element tree, written as `.ui.xml` or as `.ui.html` + CSS (B is a second loader onto the same tree). A screen with a scrolling list, a text field or an animation is an `.rml` document in `RmlUiDocumentComponent`. The seam (a scene component, `UIInput`, `ActionEvent`, `UIVars`) lets one game use both.

What this asks of the code, in `.claude/plans/magical-leaping-blossom.md`, stage 6: the Vulkan `DrawGeometry` (RmlUi showed on the SDL backend only when this was written; done in stage 6.1), the font engine (FreeType from the system today), hot reload of documents, and on the element tree only the small controls that a screen asks for. `UIScrollView`, `UITextInput`, UI animation, grid layout and focus navigation are **not** built on the element tree: they are RmlUi's.
