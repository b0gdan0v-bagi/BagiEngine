# How an in-game UI screen is authored: the data for the decision

Stage 5 of `.claude/plans/magical-leaping-blossom.md`. The owner decides; this file holds what was measured. Status: **A and B are built and measured; C (RmlUi) is vendored and compiles, but no host is built**, see the last section for what is needed.

The three candidates, for the same screens:

- **A. The engine's XML** (`.ui.xml`, or inline in `SceneConfig.xml`): stage 4. Elements are `<element type="UIButton" ...>`, the style is flat attributes, responsive variants are `<variants>`.
- **B. A web-style front-end** (`.ui.html` + CSS): stage 5.1. A compiler writes the text of an A document, so it builds the same tree through the same factories (`UI/CLAUDE.md`, "Writing a screen in HTML and CSS").
- **C. RmlUi** as a separate host with its own DOM and layout. Vendored (`src/Modules/RmlUi`), no host yet.

## What was measured

Numbers are from a Debug build on macOS (Apple M4 Pro, Clang), so they are only good for comparing A with B. `BECoreModule` is the only module touched; no new dependency.

| Question | A: XML | B: HTML + CSS | C: RmlUi |
|---|---|---|---|
| **Integration cost** | Stage 4 (reflection of the tree, `UIDocument`, controls): not counted here | **+2,850 lines** in 11 files (`UIHtmlParser`, `UICssParser`, `UIWebCompiler`, `UIWebText`, `UIWebReport`, `UIWebDocumentLoader`), +900 lines of test (41 cases), one `friend` line in `UIDocument.h`; about one working session; no submodule | not measured |
| **Same screens** | The three shipped screens (Match3 menu, Match3 HUD, Chess HUD) | All three written in HTML + CSS with **0 issues**; every element gets the same rect as the shipped XML at 4 to 5 window shapes (`UIWebTest.TheMenuInHtmlAndCssLaysOutLikeTheShippedXml`, `TheMatch3HudInHtml...`, `TheChessHudInHtml...`); the menu and both HUDs pass `ValidateOnMatrix` with the longest words | not measured |
| **Adaptivity** | Anchors, percents, limits, stack, variants, safe area, validator on the matrix | Same machinery: `@media (orientation / min-width / max-width / min-aspect-ratio ...)` becomes a `UIVariantCondition` (widths are canvas units, not browser pixels); the matrix and the validator work unchanged | not measured |
| **Source size** (250 cards × 4 elements = 1,001 elements) | 99,537 bytes of XML | 30,070 bytes of HTML + CSS (a third), which compile to 130,065 bytes of XML | not measured |
| **Build cost of a 1,001-element screen** | parse + build 5.0 ms | compile 18.9 ms + parse + build 4.6 ms (a document is compiled once, when it is loaded) | not measured |
| **Runtime** (same tree) | layout 0.23 ms cold, 0 cached, 0.21 ms after `Invalidate()`; draw 0.57 ms, 2,890 draw calls | identical: it is the same tree | not measured |
| **Build and size** | - | 5 sources: about 1.3 s to compile together, 5.9 MB of Debug objects (the unit of the Debug build; a Release figure was not taken) | clean build time and binary size delta: not measured |
| **Backend fit** | Rect + texture + tint + clip: both backends, today | Same | needs `DrawGeometry` (indexed triangles with vertex color and UV) in `IRenderer`: SDL has `SDL_RenderGeometry`; Vulkan needs a new pipeline and a dynamic vertex buffer. Not built |
| **Model edit loop** | Text file, stable names, validator, report | Text file in a notation models know well; the compile **reports every property, selector, tag and value it cannot apply** with the line (`UIWebReport`), so a wrong guess is not silent; no screenshot yet (control API: `ui_tree`, `ui_click` are stage 7) | not measured |
| **Fit with the engine** | Native: reflection, scene editor tree view (stage 7), hit testing, gtests of layout | Native, since it is A underneath. The editor shows the compiled tree, and edits made there do not go back into the HTML | An RmlUi document has its own DOM: opaque to the scene editor, tested only through a context with stub interfaces |

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

Findings while writing it:

1. **A wrapper `div` must not take clicks.** A full-screen `div` that only groups elements would swallow every click of the game under it (a `UIPanel` takes input). The compile makes a group take clicks only with a visible background or `pointer-events: auto`. This is a place where "CSS semantics" and "game UI semantics" differ, and each such place needs a rule.
2. **A `div` with loose text is a label,** and an element with only inline children and no text of its own is a container. The first version merged `<div><span id="a">1</span><span id="b">2</span></div>` into one label; the test of a nested selector found it.
3. **Specificity bites a model the way it bites a web developer:** `#Reset { background }` beats `button:hover { background }`. The compile follows CSS, which is right and not obvious.
4. **Centering has no CSS spelling that the engine can take** (`left: 50%` is not an offset in units, `justify-content` has no counterpart), so the front-end adds one non-CSS property, `anchor`, as the plan's sketch had. Two engine-only properties (`-be-text-fit`, `-be-min-font-size`) cover text auto-fit.
5. **A button keeps its own text alignment** (the compile does not let `text-align` reach it), as a browser's default style sheet does.
6. The compile is **2,850 lines**, more than the roughly 1,000 the first estimate had in mind: the property table, the cascade, the variants diff and the state rules are each a real piece. That is the size of a "subset" that three screens did not outgrow, and not the size of the whole of CSS.

## What is not measured, and why

**C (RmlUi) is vendored and compiles, but no host is built, so the C column above stays empty.** The plan added it as a submodule; that command was refused when this stage ran, and the owner chose to vendor it the way pugixml and stb are (`src/Modules/RmlUi/`, tag `6.3`, MIT). What that step showed:

- Only the core is copied: `Include/RmlUi/{Core,Config,Core.h}` and `Source/Core`, unedited: 444 files, **3.2 MB** (the whole upstream repository is 24.8 MB with samples, tests and backends). The upstream CMake files are replaced by one of ours; the Debugger (fonts under the SIL OFL), the Lua / SVG / Lottie plugins and the backends are left out.
- It sits behind `ENGINE_UI_RMLUI` (OFF by default) and is added after the `ENGINE_NO_EXCEPTIONS` block like GoogleTest, so it compiles with the engine's `-fno-exceptions`. The only change needed for that is the define `ITLIB_FLAT_MAP_NO_THROW` (its flat map throws otherwise); `robin_hood.h` detects the missing exceptions itself. The RTTI is RmlUi's own (`rmlui_dynamic_cast`), not the language's.
- `rmlui_core` builds clean on macOS Debug (Apple Clang, FreeType from Homebrew). Its objects are 94 MB and the archive 97 MB **with debug info**, so that is not a binary-size figure: the delta of a linked game is not measured yet.
- Nothing in the engine links it, so the default build is unchanged.

What the work still needs, in order:

1. FreeType for RmlUi's default font engine (Homebrew has it on this machine for the spike; a real adoption would vendor FreeType too, or write a font engine over our `FontAtlas`: six methods of `FontEngineInterface`).
2. `IRenderer::DrawGeometry` with an `SDL_RenderGeometry` implementation (and the recording double); the Vulkan geometry pipeline estimated, not built.
3. `RmlUiRenderInterface` / `SystemInterface` / `FileInterface` over `IRenderer`, `CoreManager::GetTime()`, `LOG_*` and `FileSystem`; `RmlUiDocumentComponent` feeding `UIInput`, the canvas bounds, scale and safe area, and emitting `ActionEvent` for `data-action`.
4. The Match3 menu and HUD as `.rml` / `.rcss`, through the size matrix with stub interfaces, plus a clean build time and binary size delta, and the runtime on the same 1,001-element screen.

Questions only this work answers: how large the clean build delta is, whether RmlUi's layout can be made to agree with the safe area and the scaler (the validator is ours, so it would need an adapter), and whether its features (scrolling, data binding, transitions, spatial navigation) are worth a second DOM. From its documentation (not checked here): RmlUi has flexbox, transitions and animations, data binding and spatial navigation, its render interface needs compile / render / release geometry, texture load and generate, and scissor, and its default font engine is FreeType.

## Reading the data

- **A already covers every screen the repository ships.** B covers them too, with no gap found, and adds a notation that models and designers know, at a cost of about 2,850 lines that live in one place and leave the runtime alone.
- **B is cheap to keep and cheap to drop:** nothing but a loader depends on it, and a screen written in HTML can be turned into the XML it compiles to (the compile output is the A document).
- **The features that A and B lack** are in stage 6 of the plan: scrolling, sliders, toggles, focus and gamepad navigation, text input, animation. RmlUi has some of them built in. Whether they are cheaper to build on the element tree (the plan estimates a control each, `UITextInput` being the largest) or to take with a second DOM is the open question that the missing C data would settle.
- If the owner chooses now without C: the data supports **A as the base with B as an optional second loader**, and the Mahjong game (the consumer of stage 6 in the roadmap) as the point where a missing control would bring RmlUi back for a second look.
