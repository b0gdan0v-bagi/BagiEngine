---
paths:
  - "src/Modules/BECore/UI/UIBox*"
  - "src/Modules/BECore/UI/UIElement.*"
  - "src/Modules/BECore/UI/UIStateLook.*"
  - "src/Modules/BECore/UI/UISwitchComponent.*"
  - "src/Modules/BECore/UI/UISliderComponent.*"
  - "src/Modules/BECore/UI/UIProgressBar.*"
  - "src/Modules/BECore/UI/UIDropdown.*"
  - "tests/BECore/UIBox*"
---

# UI: rounded boxes, borders, gradients, shadows and glow

Every UI element draws a box. With the default style it is the plain `DrawFilledRect(color)` it always was (no test or screen changes); with a `UIBoxStyle` it is rounded, bordered, graded, shadowed or glowing, through the `IRenderer::DrawGeometry` of both backends (untextured triangles with per-vertex alpha: no backend code, no RmlUi). Files: `UIBoxMesh` (pure geometry), `UIBoxStyle` (the fields), `UIElement::DrawBox`, `UIStateLook` and `UIDropdown::GetCurrentBox`.

## The fields (flat attributes of any element)

| Attribute | Form | Meaning |
|-----------|------|---------|
| `cornerRadius` | `12` or `tl,tr,br,bl` | UI units; a radius that does not fit shrinks (two radii on a side never exceed it, all shrink by one factor as in CSS) |
| `borderWidth`, `borderColor` | units, colour | a ring inside the rect, drawn over the fill; no width or no alpha, no border |
| `gradientDirection`, `gradientColor` | `None` / `Vertical` / `Horizontal`, colour | `color` is the start (top or left), `gradientColor` the end |
| `shadow`, `glow` | `offsetX,offsetY,blur,spread,color` | the colour as `#RRGGBBAA` or `r,g,b,a`; the builder draws both alike (shadow first, then glow); no alpha, none |
| `highlight` | colour | a one-unit line just inside the top edge, following the top corners |

Only non-default values are saved. A malformed value is reported and the default stays. The names must not clash with a control field (`UIControlsTest` sets everything on every control). `ui_tree` gives a `box` object (`cornerRadius` as four numbers, `borderWidth`, `borderColor`, `gradient {direction, color}`, `shadow`, `glow`, `highlight`) only for an element whose authored style is not the default; the state shifts of a button are paint, not reported.

## Drawing

- **One `DrawGeometry` call per box**, so the paint order is the triangle order: shadow, glow, fill, border, highlight. The vectors are scratch on the element: a steady frame allocates nothing.
- **Anti-aliasing at any scale.** The builder takes `ViewTransform::scale` (pixels per unit). An edge is a ring of alpha 0 and, one output pixel inside it, a ring of full alpha, so the true edge is the middle of the ramp and the covered area stays the rect's. The segments of a corner follow its radius in pixels (a sagitta tolerance, clamped to a minimum and a maximum). A blur is a fixed number of concentric rings with the alpha falling by a smooth curve over `blur` units. The constants are in `UIBoxMesh.h`.
- **The core is no fan; the SDL software renderer is the cost model.** It pays every blended triangle the pixels of its **bounding rectangle** (a temporary surface and a blit), at the present, not at the draw call. A fan from one point over the ring multiplies the area of the box by the number of ring points, so the opaque core of a fill, shadow and glow is cut along the four arc centres (the quad of the centres, a fan per corner, two triangles per side); the ring strips are thin but each is a triangle, so a shadow is dear in triangles. A renderer says what it wants with `IRenderer::GetGeometryDetail()`: `Reduced` (the SDL **software** renderer only, not a GPU driver, not Vulkan) gives a shadow or glow fewer blur rings and a coarser corner; the fill, border and highlight keep the full detail, and `Full` is the mesh above, unchanged. `UIBoxMeshTest` holds the raster cost and that the core covers the shape once.
- **The SDL software rasterizer samples a triangle at the pixel corner**, so on a given diagonal a pixel may jump from the background to the fill with no blend at scale 1: a test of the curve counts the blended pixels of the whole corner square.
- **Paint outside `rect` is not layout.** A shadow or glow goes beyond the rect; `rect`, the layout, the hit test and the validator ignore it (a shadow can leave the canvas without an `OutsideCanvas`).
- A `UIPanel` draws its sprite over the rounded fill; the sprite is not clipped to the corners.
- A box that has an effect draws nothing for an empty rect; the default style draws nothing for a colour with no alpha.

## Cost on the software renderer

Measured by `UIBoxRasterCostTest` (`DISABLED_` benches: `bin/BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter='UIBoxRasterCost*'`, Release; the lines are `UIBoxRasterCost part=... present_ms=...`) on an Apple M4 Pro, `macos-release`, at the commit of WP 1 (`5c773021`) with the core/rim and texture-floor benches of WP 2, with other loops running (two runs agreed within 3 %). The screen is 23 skinned boxes in 1920x1080 output pixels (a 1200x800 panel with 12 buttons on it, three mid panels, seven tiles; vertical gradient, border, shadow, glow, highlight); each part row is the whole screen with that part only (`present_ms` is the median `Present()` of 30 frames). SDL blends **every** triangle of a `DrawGeometry` call through a temporary surface of its bounding box (the draw blend mode, not the vertex alpha), so `bbox_px` is the cost for all triangles and `blended_px` only says how much of it is translucent.

| Part (scale 1) | triangles | bbox_px | ms | of the screen | core ms / rim ms |
|----------------|-----------|---------|----|---------------|------------------|
| shadow | 10234 | 4.04 M | 5.6 | 31 % | 2.1 / 3.5 |
| glow | 9390 | 3.62 M | 4.7 | 26 % | 2.4 / 2.4 |
| fill, flat | 4050 | 3.33 M | 3.2 | (18 %) | 2.8 / 0.5 |
| fill, gradient | 4050 | 3.33 M | 6.5 | 36 % | 6.0 / 0.5 |
| border | 7824 | 0.24 M | 1.1 | 6 % | 0.4 / 0.8 |
| highlight | 2046 | 0.03 M | 0.3 | 2 % | 0.1 / 0.3 |
| **screen** (all parts, gradient) | 33544 | 11.2 M | **17.9** | 100 % | |
| the big panel alone | 2032 | 6.3 M | 8.1 | | |

At scale 2 (the same 1920x1080 output, the skin in units so every ring and radius is twice as many pixels) the screen is 46036 triangles, 13.6 M bbox pixels, **24.0 ms**; shadow 8.7, glow 6.4, fill 6.9 (gradient), border 1.5, highlight 0.4. The core is the plateau at the strongest alpha of a part (the quad of the arc centres, the corner fans and the side strips of the cut core), the rim the rest.

- **A triangle costs about 82 ns on its own and a blended pixel about 1.1 ns** (flat colour; 2 ns with a per-pixel gradient colour): 5000 tiny triangles over 400x400 pixels take 0.78 ms, two triangles over the same area 0.37 ms. So the 33.5 k triangles of the screen are about 2.8 ms of its 17.9; the rest is bounding-box area, 11.2 M pixels.
- **A box drawn as one stretched, blended texture** over the area its shadow covers (2.3 M pixels for the whole screen) costs **2.2 ms**, 12 % of the screen: the floor of route B (`texture_floor`).

**Route: B.** What route A can remove is the cores of the shadow and glow under an opaque fill: 2.1 + 2.4 = 4.5 ms of 17.9 (25 %; with a flat fill the opaque core of the fill as `DrawFilledRect` would add 2.8, but a gradient fill, the common skin, cannot). The visible blended area (the soft rims 6.4 ms, the gradient core 6.0, the border and the highlight 1.4) is 13.8 ms, 77 %: no trimming of triangles gets below a third (6.0 ms; 8.0 at scale 2). A cached texture pays each visible pixel once (0.9 ns) where the triangles pay shadow, glow and fill each over the box. Route A stays the fallback: when the nine-slice misses the target, WP 3 and WP 4 are done after it.

## Button states (`UIStateLook`: `UIDropdown::GetCurrentBox` and the box of any element with a `UIInputClicker` call it)

Hover lightens the fill (both gradient ends) and the border; pressed darkens, halves the shadow offset, cuts the blur by a third and drops the highlight; selected raises the border to `textColor` and adds a faint glow of it when the style has none, and the 4-unit bar stays inside the rounded bottom corners; disabled drops shadow and glow and fades fill, border and highlight. An explicit state colour replaces the start colour and the gradient end keeps its offset. A button with the default style is drawn exactly as before.

## Parts of the widgets

- **`partShape`** (`Square` default, `Round`; on `UISwitchComponent`, `UISliderComponent`, `UIProgressBar` only, since nothing else has parts) goes through `UIElement::DrawPart`. `Square` is the plain `DrawFilledRect` call the part always made. `Round` is a box whose only setting is a corner radius of half the shorter side (`DrawBoxIn`, the body of `DrawBox` for any rect): a pill for a track, a circle for a square thumb. Toggle: pill track, round thumb; slider: pill track and filled part, round thumb; progress bar: pill track and fill. The colours, shifts and sizes are the widget's own; the touch area and the layout do not change.
- A fill shorter than the track is high (a bar at 2 %) is a smaller pill, not a track-high slice: accepted.
- **An allocation test of a `Round` part calls `RecordingRenderer::ClearCalls()` between frames:** it keeps the recorder's geometry storage, while `calls.clear()` gives it up and the recorder's own allocations are counted (`UIPartShapeAllocTest`).
- **The plate of a toggle** (its `color` and `box`) and **the header of a dropdown** go through `DrawBox` like any element's. The dropdown has `GetCurrentBox` (a `UIStateLook::Look`): the fill and the gradient end shift on hover and press, disabled drops shadow and glow and fades border and highlight, hover and press leave border and highlight as authored (no state colours of its own). The options of a dropdown are composed buttons with their own box; its arrow stays strips.

## What is not here

The HTML + CSS front-end writes all of the above as attributes (`.claude/rules/ui-web.md`: `border-radius`, `border`, `linear-gradient`, `box-shadow`, `-be-highlight`, `-be-shape`). Not done: image skins, a sprite clipped to the corners, `%` radii, `inset` shadows, per-side borders, more than two shadow layers, per-state shape rules (a button has state colours only, so `:hover { border-color }` or `:hover { box-shadow }` is reported). The look on Vulkan is the owner's.

## Tests

`UIBoxMeshTest` (the geometry: winding, indices, areas, feather, blur falloff), `UIBoxStyleTest` (draw calls, XML round trip), `UIBoxButtonTest` (the states), `UIBoxCaptureTest` (pixels of a sample panel and button on the SDL software renderer at scales 1 and 2; set `BE_UI_BOX_PNG=<folder>` to keep the frames as PNG files for a look).
