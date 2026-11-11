---
paths:
  - "src/Modules/BECore/UI/UIBox*"
  - "src/Modules/BECore/UI/UIElement.*"
  - "src/Modules/BECore/UI/UIButton.*"
  - "src/Modules/BECore/UI/UIToggle.*"
  - "src/Modules/BECore/UI/UISlider.*"
  - "src/Modules/BECore/UI/UIProgressBar.*"
  - "src/Modules/BECore/UI/UIDropdown.*"
  - "tests/BECore/UIBox*"
---

# UI: rounded boxes, borders, gradients, shadows and glow

Every UI element draws a box. With the default style it is the plain `DrawFilledRect(color)` it always was (no test or screen changes); with a `UIBoxStyle` it is rounded, bordered, graded, shadowed or glowing, through the `IRenderer::DrawGeometry` of both backends (untextured triangles with per-vertex alpha: no backend code, no RmlUi). Files: `UIBoxMesh` (pure geometry), `UIBoxStyle` (the fields), `UIElement::DrawBox`, `UIButton::GetCurrentBox`.

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
- **The SDL software rasterizer samples a triangle at the pixel corner**, so on a given diagonal a pixel may jump from the background to the fill with no blend at scale 1: a test of the curve counts the blended pixels of the whole corner square.
- **Paint outside `rect` is not layout.** A shadow or glow goes beyond the rect; `rect`, the layout, the hit test and the validator ignore it (a shadow can leave the canvas without an `OutsideCanvas`).
- A `UIPanel` draws its sprite over the rounded fill; the sprite is not clipped to the corners.
- A box that has an effect draws nothing for an empty rect; the default style draws nothing for a colour with no alpha.

## Button states (`UIStateLook`: `UIButton::GetCurrentBox` and the box of any element with a `UIInputClicker` call it)

Hover lightens the fill (both gradient ends) and the border; pressed darkens, halves the shadow offset, cuts the blur by a third and drops the highlight; selected raises the border to `textColor` and adds a faint glow of it when the style has none, and the 4-unit bar stays inside the rounded bottom corners; disabled drops shadow and glow and fades fill, border and highlight. An explicit state colour replaces the start colour and the gradient end keeps its offset. A button with the default style is drawn exactly as before.

## Parts of the widgets

- **`partShape`** (`Square` default, `Round`; on `UIToggle`, `UISlider`, `UIProgressBar` only, since nothing else has parts) goes through `UIElement::DrawPart`. `Square` is the plain `DrawFilledRect` call the part always made. `Round` is a box whose only setting is a corner radius of half the shorter side (`DrawBoxIn`, the body of `DrawBox` for any rect): a pill for a track, a circle for a square thumb. Toggle: pill track, round thumb; slider: pill track and filled part, round thumb; progress bar: pill track and fill. The colours, shifts and sizes are the widget's own; the touch area and the layout do not change.
- A fill shorter than the track is high (a bar at 2 %) is a smaller pill, not a track-high slice: accepted.
- **The plate of a toggle** (its `color` and `box`) and **the header of a dropdown** go through `DrawBox` like any element's. The dropdown has `GetCurrentBox` (the `UIButton::Look`): the fill and the gradient end shift on hover and press, disabled drops shadow and glow and fades border and highlight, hover and press leave border and highlight as authored (no state colours of its own). The options of a dropdown are `UIButton`s with their own box; its arrow stays strips.

## What is not here

The HTML + CSS front-end writes all of the above as attributes (`.claude/rules/ui-web.md`: `border-radius`, `border`, `linear-gradient`, `box-shadow`, `-be-highlight`, `-be-shape`). Not done: image skins, a sprite clipped to the corners, `%` radii, `inset` shadows, per-side borders, more than two shadow layers, per-state shape rules (a button has state colours only, so `:hover { border-color }` or `:hover { box-shadow }` is reported). The look on Vulkan is the owner's.

## Tests

`UIBoxMeshTest` (the geometry: winding, indices, areas, feather, blur falloff), `UIBoxStyleTest` (draw calls, XML round trip), `UIBoxButtonTest` (the states), `UIBoxCaptureTest` (pixels of a sample panel and button on the SDL software renderer at scales 1 and 2; set `BE_UI_BOX_PNG=<folder>` to keep the frames as PNG files for a look).
