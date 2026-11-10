---
paths:
  - "src/Modules/BECore/UI/UIWeb*"
  - "src/Modules/BECore/UI/UIHtml*"
  - "src/Modules/BECore/UI/UICss*"
  - "tests/BECore/UIWebTest.cpp"
  - "**/*.ui.html"
  - "**/*.css"
---

# UI: screens in HTML and CSS

A source-to-source compiler: `UIWebCompiler::Compile(html, loadStylesheet, report)` turns HTML-like markup and CSS-like styles into the text of a `.ui.xml`; everything after that is the XML front-end (`src/Modules/BECore/UI/CLAUDE.md`). `UIWebDocumentLoader` takes `.ui.html` (`source="ui/menu.ui.html"` on a canvas), reads the `<link rel="stylesheet" href>` files beside it and logs what it left out with file and line. **A subset on purpose, not a browser:** whatever falls outside is an issue in `UIWebReport`, never silent.

```html
<link rel="stylesheet" href="hud.css">
<div id="Hud" class="hud">
  <span id="Score">Score: {score}</span>
  <button id="Reset" data-action="Match3Reset">Reset</button>
</div>
```
```css
.hud   { anchor: top-left; left: 16px; top: 16px; width: 220px; height: 96px; background: #000000A0; }
#Reset { anchor: bottom-right; right: 12px; bottom: 10px; width: 96px; height: 48px; }
button:hover { background: #3a6; }
@media (orientation: portrait) { .hud { anchor: top; left: 0; width: 90%; } }
```

## Elements

`id` is the `name`; `class` is for selectors; `hidden` hides; `html` / `body` are transparent; `head`, `style`, `link`, `script`, `meta`, `title` are skipped.

| HTML | Becomes |
|------|---------|
| `button` | `UIButton`: `data-action` (or `action`), `data-value`, `disabled`, the text is the label |
| container with `data-widget="segmented"` | `UISegmented`: `data-bind`, `disabled`, `<button data-value data-action>` children. Needs `display: flex` (reported otherwise) |
| `data-widget="toggle"` | `UIToggle`: text = caption, `data-bind`, `data-action`, `data-on` / `data-off` |
| `data-widget="slider"` | `UISlider`: `data-bind`, `data-action`, `data-min` / `data-max` / `data-step` |
| `data-widget="progress"` | `UIProgressBar`: `data-bind`, `data-min`, `data-max`; never takes input |
| `img` | `UIImage`: `src` = sprite name, `object-fit: fill` = `stretch` |
| element with only text / inline elements (`span`, `p`, `h1`-`h6`, `label`, `b`, `br`...) | `UILabel` (white space collapsed, `<br>` a line break) |
| any other with `background-image` | `UIPanel` |
| any other | `UIElement` (a group) |

A group takes clicks only with a visible `background` or `pointer-events: auto` (a full-screen wrapper must not swallow the game's clicks); `pointer-events: none` turns it off. Text beside block elements is dropped and reported; an unknown tag is read as `div` and reported; every element needs its end tag. `data-*` attributes standing where nothing reads them are reported.

## Cascade and properties

CSS's cascade: `!important`, `style=""`, specificity, source order. Selectors: tag, `*`, `#id`, `.class`, compounds, lists, descendant and `>`, `:hover` / `:active` / `:disabled` / `:checked` on the last part. Inherited: `color`, `font-size`, `font-family`, `text-align`, `line-height`, `white-space`. Shorthands and longhands are separate names: the later declaration wins by order.

| CSS | Engine |
|-----|--------|
| `display: none`, `visibility: hidden` | `visible="false"` |
| `display: flex`, `flex-direction`, `gap`, `padding`, `flex-wrap`, `align-items`, `flex` / `flex-grow` | `layout="Stack"`, `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign`, `flex`. No `justify-content` |
| `left` / `top` / `right` / `bottom` (px) | anchor `TopLeft` unless only right/bottom; all four or `inset` = `Stretch` with margins |
| `anchor` (engine-only): `top-left` ... `stretch` | `anchor` (centering needs it) |
| `width`, `height`, `min-*`, `max-*`, `aspect-ratio`, `object-fit: cover` | lengths, `aspectRatio`, `aspectMode="Fill"` |
| `background`, `background-image: url(Name)`, `border-image-slice` | `color`, `sprite`, `sliceBorder` |
| `border-radius` (1 to 4 px values, CSS order) and the four `border-*-radius` | `cornerRadius` (`999px` is a pill; the builder clamps) |
| `border: <width> [solid\|none] <color>`, `border-width`, `border-color`, `border-style` | `borderWidth`, `borderColor` |
| `linear-gradient([dir,] c1, c2)` in `background` or `background-image` | `color` = c1, `gradientColor` = c2, `gradientDirection` |
| `box-shadow: x y [blur [spread]] color` (one or two layers), `none` | `shadow`; two layers: the first `glow`, the second `shadow` |
| `-be-highlight` (engine-only color), `-be-shape: square\|round` | `highlight`; `partShape` (toggle, slider, progress only) |
| `color`, `font-size`, `font-family` (a font path), `text-align`, `vertical-align`, `text-overflow`, `white-space`, `line-height` | `textColor`, `fontSize`, `font`, `align`, `verticalAlign`, `overflow`, `wrap`, `lineSpacing` |
| `-be-text-fit: shrink`, `-be-min-font-size` | `overflow="AutoFit"`, `minFontSize` |
| `-be-track-color`, `-be-fill-color`, `-be-thumb-color` | of a toggle, slider, progress bar only |
| `:hover`, `:active`, `:disabled`, `:checked` with `background` | `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`. Not on toggle / slider / progress (their states are derived; reported) |

**The box** (`.claude/rules/ui-shapes.md`): px only. A border is drawn inside the rect, so it is not layout (as with `box-sizing: border-box`); the same for shadows. The gradient takes two colors with no stop positions and the directions `to bottom|right|top|left` or `180|90|0|270deg` (`to top` and `to left` swap the colors); it replaces `background-color`, a `url()` beside it is the sprite drawn over it, and a `background` shorthand after it resets it. `box-shadow` has no `inset`, a colour is required, a third layer is reported. A box effect alone does not make a group take clicks, a visible gradient start does. The shifts of a button's states are automatic from its fill: a state rule changes the background only (the far end of a gradient is derived from its start; a state rule that changes another box property, and a `@media` block that changes the box, are reported).

Lengths px and %; colors `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, a few names. Reported, not understood: `em`, `rem`, `vw`, `vh`, `calc()`, `var()`, `%` or `/` radii, `inset` shadows, per-side borders, a border style other than `solid` / `none`, `opacity`, `transform`, `transition`, `animation`, `z-index`, `overflow`, `font-weight`, `letter-spacing`, `margin` on a non-filling element, `justify-content`, `grid`, attribute selectors, `+`, `~`, other pseudo-classes and elements, `@import`, `@font-face`, `@keyframes`.

## `@media`

`and`-joined `(feature: value)` terms (`orientation`, `min-`/`max-` `width` / `height` / `aspect-ratio`), commas for "or" → a `UIVariantCondition` in canvas units (after the scaler), not browser pixels. The cascade runs again per block and what differs becomes the variant's overrides (visibility, anchor, offset, size, direction, font size); anything else in a block is reported. An element a variant names without an `id` gets one (`div-1`).

## Checking

The compile is pure, so a test gives it strings. `tests/BECore/UIWebTest.cpp` compares the Match3 menu, the choice panel (`kPanelHtml` / `kPanelCss`) and the settings panel (`kSettingsPanelHtml` / `kSettingsPanelCss`) with their XML at five sizes and runs `ValidateOnMatrix`. When a screen does not look as written, read the report.
