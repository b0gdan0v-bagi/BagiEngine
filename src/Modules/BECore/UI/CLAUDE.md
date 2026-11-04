# UI Module Guidelines

## Overview

In-game UI that the engine draws through `IRenderer`, in both backends and both run modes. ImGui is not involved (it stays the tool UI: editor panels in `src/Widgets/`, `.claude/rules/widgets-module.md`). Plan and the stages still to come: the UI plan in the local plan store (the `todo/` entries link it); the authoring decision and its data: `AuthoringDecision.md` in this folder.

**Adaptivity is the foundation.** Window size, aspect ratio, orientation, safe area, pixel density and a user scale are inputs of every layout, and every screen must pass `ValidateOnMatrix` (below) with no issues before it is considered done.

What exists (plan stages 3 and 4, and the choice group of 6.4): canvases, the scaler, the layout rules, responsive variants, the validator, the host component, the reflection of the element tree and the variants, so a screen can be written in `SceneConfig.xml` or in a `.ui.xml` file of its own (see "Writing a screen in XML"), and the controls: `UIPanel`, `UIImage`, `UILabel`, `UIButton`, `UIStack`, `UISegmented` (see "Controls") and `UIVars` for text that follows the game (see "Variables"), and the pattern of a panel that a button opens (see "Choice panels"). What does not exist yet: see "Not done".

```
UICanvasComponent (scene component, reflected fields: space, scaleMode, ...)
  │  owns, and feeds with the context of each frame
  ▼
UICanvasLayout (pure) ── UICanvasScaler::Resolve(params, UILayoutContext) → scale, bounds, safeBounds, contentBounds, ViewTransform
  │                    ── UIVariants::Apply(root, variants, bounds)      → `active` style of every element
  │                    ── UITreeLayout::Run(root, contentBounds, bounds)  → `rect` of every element  (UILayout: the rules)
  │                    ── HitTest, Validate (UILayoutValidator)
  ▼
UIElement tree (root = the canvas; children are the elements)
```

| Class | Role |
|-------|------|
| `UILength`, `UIRectSpec`, `UIStackSpec` | Data: a size in units or percent; how an element finds its rect (anchor, offset, size, limits, aspect ratio, margins, flex); a stack container |
| `UILayout` | Pure rules: `ResolveRect(parent, spec)` (anchor layout) and `LayoutStack(parent, stack, specs, rects)` |
| `UILayoutContext` | Everything layout depends on besides the tree: output size, pixel density, display scale, safe area, user scale, the scene's view size, the node box of a World canvas. Has `operator==` |
| `UICanvasScaler` | The one pure function `Resolve(params, context)` → `UICanvasResolved` |
| `UIEnvironment` (`CoreManager::GetUIEnvironment()`) | `UISettings` from `config/UIConfig.xml` (`userScale`, `minTouchSize`, `hotReloadInterval` for the RmlUi host) and `MakeContext(window, outputW, outputH)`: puts the window's metrics into a context. `SetSafeAreaOverride()` is for the editor viewport |
| `UIElement` | A node, `FACTORY_BASE` with the XML element name `element`: `name`, `layout` (Anchor or Stack), `style` (authored) and `active` (after variants), `rect` (laid out), `children`, `blocksInput`, `interactive`, `ignoreSafeArea`, `color`; virtual `CheckContent()`. The fields except `active` and `rect` are reflected |
| `UIElementStyle` | The authored properties that a variant can change (`visible`, `rect`, `stack`, `fontSize`); written as flat attributes of its element |
| `UIColor` | A `Color` that a file writes in one attribute (`color="0,0,0,160"` or `"#000000A0"`) |
| `UIText` (`UIValueText.h`) | Pure text forms of `UILength` (`"90%"`), `UIEdges` (`"8"` or `"l,t,r,b"`), colors, numbers; `UIAttr` (`UIAttributeIO.h`) reads and writes them as attributes |
| `UIPanel`, `UIImage`, `UILabel`, `UIButton`, `UIStack`, `UISegmented` | The controls, derived from `UIElement`; one section below |
| `UIVars` (`CoreManager::GetUIVars()`) | The variables that label text is bound to: name to text, with a version that grows on every change; `Expand()` replaces `{name}` |
| `UISprite`, `UINineSlice` | The texture of a sprite name (TextureLibrary, or given by code); the pure cutting of a picture into nine pieces |
| `UIScreenContent` | The reflected pair `elements` + `variants` that a screen is made of; what a document and any other front-end reads |
| `UIDocument` (`IResource`), `UIDocumentLoader` | A screen in a `.ui.xml` file: the parsed XML, `Instantiate(content)` builds fresh elements from it for each canvas |
| `UIWebCompiler`, `UIHtmlParser`, `UICssParser`, `UIWebDocumentLoader` | The web-style front-end: HTML + CSS in, the text of a `.ui.xml` out (see "Writing a screen in HTML and CSS"); the loader takes `.ui.html` |
| `UIVariant` | `when` (`UIVariantCondition`, text form `orientation:portrait;minAspect:1.5`) + `overrides` per named element (`UIElementOverride`); `UIVariants::Apply` |
| `UITreeLayout::Run` | Lays out a whole tree |
| `UICanvasLayout` | The pure pipeline above with a cache, `HitTest`, `Validate`, the counters tests read. Public fields: `params`, `root`, `variants` |
| `UILayoutValidator` | `Validate(root, resolved, options)` → `UILayoutIssue`s |
| `UICanvasComponent` | The scene host: draw, `UIInput` handler, show / hide by name |
| `UIPreview` | The numbers of the editor viewport preview: preset shapes and simulated safe areas |
| `UIControl` (`RegisterUIControl`) | The control API methods `ui_tree` and `ui_click` over the canvases of the active scene; one section below |

## Canvases

A scene may hold any number of canvases (a `UICanvasComponent` on a node each). `space` picks what a canvas fills:

| Space | Fills | UI unit | Scale |
|-------|-------|---------|-------|
| `Screen` | the render output, so its edges are the window's edges | by `scaleMode` | mode scale x user scale, kept between `minScale` and `maxScale` |
| `SceneView` | the scene's fitted view (`viewWidth` x `viewHeight`) | a scene unit | the fit of `Scene::Draw()` (`ViewTransform::Fit`) |
| `World` | the box of the node's `TransformComponent` | a scene unit | the same fit; the canvas origin is the node's position, so it follows the node |

`scaleMode` (Screen only): `ConstantPixelSize` (a unit is a window unit, so the scale is the pixel density), `ConstantPhysicalSize` (that times the display scale), `ScaleWithScreen` (follows the output size against `referenceWidth` x `referenceHeight`; `match` 0 follows the width, 1 the height, between them the geometric blend like Unity's). `SceneView` and `World` ignore the mode, the user scale and the limits: the board they belong to is scaled by the scene's fit. A scene without a view size has the identity fit, so a SceneView canvas is then the output at scale 1. `useSafeArea` applies to Screen and SceneView: `contentBounds` is the safe area, and `bounds` stays the whole canvas for backgrounds (`UIElement::ignoreSafeArea` on a child of the root lays it out against `bounds`).

- **Draw order** is the scene's: node `layer`, then scene order. `OnDraw()` sets the canvas's own `ViewTransform` for its elements and gives the scene's back.
- **Input order** is the reverse. The component registers with `UIInput` with `Scene::GetDrawRank(node)` as the order and refreshes it on every draw (also while hidden), because a scene attaches its nodes one by one while it fills its tree.
- **Input rules:** a canvas consumes a mouse event that lands on a hit-testable element (`blocksInput` or `interactive`), by the layout of its last draw; motion over one is a hover and is consumed, except a drag that began elsewhere (a button held whose press this canvas did not take) which is the game's; a press the canvas took is followed to its release wherever the pointer goes; a `modal` canvas consumes everything while shown. A hidden canvas, one with `inputEnabled` off and one of an inactive scene consume nothing. Hit testing finds the topmost element: a child before its parent, a later sibling before an earlier one; the rect of a child is not clipped by its parent.
- **Visibility by name:** `UICanvasComponent::SetVisibleByName(scene, nodeName, visible)`.
- The reflected fields are named `visible` and `inputEnabled`, not `enabled`: a vector of factory-made components writes an `enabled` attribute of its own on every element (`Reflection/DataAccessor.h`), which a field of that name would collide with.

## Layout rules (`UILayout`)

All rects are in the UI units of one canvas.

- **Anchor layout.** `UIAnchor`: the nine points and `Stretch`. The element's pivot is the same point of itself, so `BottomRight` with `x = -12, y = -10` moves up and left into the parent. `Stretch` fills the parent minus `margin` and ignores offset and size.
- **Order of the steps:** size (`UILength`: units, or a percent of the parent on the same axis) → `min*`/`max*` limits (the minimum wins when they disagree) → aspect ratio (`Fit`: the largest rect of the ratio inside the box, `Fill`: the smallest that covers it) → pin to the anchor and offset. A Stretch element that a limit made smaller is centered in its area.
- **Stack.** `UIStackSpec`: `direction`, `spacing`, `padding`, `wrap`, `crossAlign` (`Start`, `Center`, `End`, `Stretch`). A child's main size is its spec size clamped by its limits; `flex` > 0 takes a share of what the fixed children and the spacing leave on its line (shares follow the weights; each is clamped by the child's limits and the clamp does not redistribute). With `wrap` a child starts a new line when its spec size does not fit the rest of the line (a flex child counts with its spec size there); without it the line is as wide as the content on the cross axis. A stack ignores its children's anchor, offset, margin and aspect ratio.
- **Hidden elements** (`style.visible` false, or an ancestor's) get an empty rect and take no room in a stack.

## Responsive variants

A canvas holds `variants` (`UICanvasLayout::variants`): `UIVariant { when, overrides }`. `when` is a condition on the canvas `bounds` in UI units: `orientation` (portrait is height >= width, a square is portrait, like CSS), an aspect range, width and height ranges (a limit of 0 is off, ends are included; every set part must hold). An override names an element and replaces some of: `visible`, `anchor`, `x`, `y`, `width`, `height`, stack `direction`, `fontSize`. Variants apply in list order, so a later one wins like CSS, and `Apply` always starts from `style`, so leaving a shape restores the authored values. The variants are chosen again only when the bounds change (or after `Invalidate()`): a new safe area or a new density that leaves the bounds alone only lays the tree out again (`UICanvasLayout::GetVariantRuns()` / `GetLayoutRuns()`).

## Validator and the size matrix

`UILayoutValidator::Validate` checks a laid-out canvas, visible elements only:

| Issue | Meaning |
|-------|---------|
| `OutsideCanvas` | An element sticks out of the canvas (by more than 0.01 unit) |
| `OutsideSafeArea` | Inside the canvas but outside `contentBounds` (not for `ignoreSafeArea` children of the root) |
| `SiblingsOverlap` | Two children of a stack overlap |
| `TouchTargetTooSmall` | An `interactive` element is smaller than `minTouchSize` **as the user sees it**: `min(w, h) * scale / pixelDensity` in window units |
| `TextDoesNotFit` | `CheckContent()` says there is content that does not fit and cannot shrink (ellipsis or auto-fit) |

`tests/Common/UILayoutMatrix.h` runs a canvas over the matrix: ten aspect ratios (16:9, 16:10, 3:2, 4:3, 1:1, 21:9, 32:9, 9:16, 9:19.5, 3:4) at short sides of 480 to 2160 px (long side up to 3840), with no insets, a top notch and side insets, with each scale mode and user scales 1.0 and 1.5 for a Screen canvas. A canvas that comes from a scene needs one `UpdateLayout(w, h)` first: the reflected fields reach the layout's `params` on its first update, and the matrix would otherwise run the default `Screen` params on it (a `SceneView` HUD would look like a full-window screen). A screen test is one line: `EXPECT_TRUE(ValidateOnMatrix(canvas).empty()) << DescribeMatrixFailures(failures)`. The matrix finds real defects. For example a 48-unit button on a `ScaleWithScreen` canvas (reference 1280 x 720) is only 24 px on a 480 x 480 window, below the 32 window units of `minTouchSize`; the fixes are a bigger button, or `minScale` (0.7 keeps 48 units above 32 px).

**A screen test** builds the screen through the same reflected path a game uses (`UIScreenContent::Deserialize` on the XML of the screen, or a scene with an inline canvas), sets the variables to the longest values the game can show, and runs `ValidateOnMatrix`. `tests/BECore/UIScreenTest.cpp` is the template; copy it for each screen the repository ships. The first HUD written with 48-unit buttons, `minScale="0.7"` and `useSafeArea="true"` passed the matrix as it was; the same HUD with a 30-unit button and no `minScale` fails on `TouchTargetTooSmall`.

**Design rules the matrix enforces:** use anchors and percents, not absolute positions; give interactive elements at least 48 units at the reference size and set `minScale` so they stay touch targets; give a screen a variant for the shapes where it does not fit (a portrait canvas is the usual one); let text wrap, cut with an ellipsis or shrink.

## Writing a screen in XML

The elements and variants of a canvas are reflected fields of `UICanvasComponent` (`_elements`, `_variants`), so a screen is data in `SceneConfig.xml`:

```xml
<node name="Hud" layer="10"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5" minScale="0.7" useSafeArea="true">
    <elements>
      <element type="UIElement" name="Panel" anchor="TopLeft" x="16" y="16" width="220" height="96" color="0,0,0,160" blocksInput="true">
        <children>
          <element type="UIElement" name="Score" x="12" y="10" width="40%" height="28"/>
        </children>
      </element>
    </elements>
    <variants>
      <variant when="orientation:portrait">
        <overrides><set element="Panel" anchor="Top" x="0" y="16" width="90%"/></overrides>
      </variant>
    </variants>
  </component>
</components></node>
```

- **An element** is `<element type="...">`, `type` is the class name from `AbstractFactory<UIElement>` (a control is one more derived class). An unknown type is reported as a deserialize error (and the element becomes a plain `UIElement`). Children go in a `<children>` array.
- **The style is flat:** `visible`, `anchor`, `x`, `y`, `width`, `height`, `minWidth`, `maxWidth`, `minHeight`, `maxHeight`, `aspectRatio`, `aspectMode`, `margin`, `flex`, `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign`, `fontSize` are attributes of the element, and only values that differ from the default are written. A length is `220`, `220px` or `90%`; `margin` and `padding` are one number or `left,top,right,bottom`; a color is `r,g,b,a` or `#RRGGBB[AA]`. A malformed value is reported with the attribute name and the default stays.
- **A variant** is `when` (the set parts as `name:value` separated by `;`: `orientation` (`portrait`, `landscape`, `any`), `minAspect`, `maxAspect`, `minWidth`, `maxWidth`, `minHeight`, `maxHeight`) and a list of `<set element="Name" .../>` overrides with only the properties to change (`visible`, `anchor`, `x`, `y`, `width`, `height`, `direction`, `fontSize`). An override is written even when its value equals the type's default (`x="0"` is a decision).
- **Saving:** `_elements` and `_variants` are the inline part of the screen; `Initialize()` hands them to the layout once (the same element objects, so edits through either are the same edits). Elements added in code with `GetRoot().AddChild()` are runtime-only and are not saved back.
- **A screen in a file of its own:** `source="assets/ui/hud.ui.xml"` on the canvas loads a `UIDocument`; the file is `<ui><elements>...</elements><variants>...</variants></ui>`, the same two blocks as inline. The document's elements come first, the inline ones after them (drawn on top; a later variant wins). The elements of a file belong to the file: `_elements` stays the inline part, and only that is saved back. A missing or unreadable file is logged and the canvas keeps its inline screen; a bad value in a file is logged with its line and the rest is built. Every canvas gets elements of its own (`Instantiate`), because the layout results live in the elements. `ResourceManager` picks the loader by the longest suffix of the name, so `x.ui.xml` goes to `UIDocumentLoader` and a plain `x.xml` to `XmlResourceLoader`.
- **A front-end with a tree of its own** is not this: RmlUi (`src/Modules/RmlUiHost/CLAUDE.md`, off by default) draws a document with its own DOM and layout from a scene component beside the canvases; the scene editor, the validator and `ui_tree` do not see into it.
- **Another front-end** needs no new tree: it resolves its own syntax into the attributes above and hands them to `XmlDeserializer`. The web-style one writes the XML text (next section); `UIDocumentTest` also builds such a tree in memory with pugixml (`XmlDeserializer::LoadFromXmlNode(XmlNode(node))`, no file and no string) and gets the same elements as from the XML.
- A binary save (`BinarySerializer`) carries the same tree.
- Why `IntrusivePtrNonAtomic<UIElement>` and not `IntrusivePtr<UIElement>` in the field declarations: the generator looks for the long name to find the factory field (`.claude/rules/reflection-module.md`).

## Writing a screen in HTML and CSS

The second front-end is a source-to-source compiler: `UIWebCompiler::Compile(html, loadStylesheet, report)` turns HTML-like markup and CSS-like styles into the text of a `.ui.xml`, and everything after that is the XML front-end (the same factories, layout, variants, validator and tests). `UIWebDocumentLoader` takes `.ui.html` files (`source="ui/menu.ui.html"` on a canvas, like `.ui.xml`), reads the `<link rel="stylesheet" href>` files next to it, and logs what it left out as warnings with the file and line. It is a subset on purpose, not a browser: what falls outside is an issue in `UIWebReport` (also the numbers: elements, rules, declarations, declarations unsupported, variants), never silent.

```html
<link rel="stylesheet" href="hud.css">
<div id="Hud" class="hud">
  <span id="Score">Score: {score}</span>
  <button id="Reset" data-action="Match3Reset">Reset</button>
</div>
```
```css
.hud    { anchor: top-left; left: 16px; top: 16px; width: 220px; height: 96px; background: #000000A0; }
#Score  { left: 12px; top: 10px; width: 150px; height: 32px; font-size: 28px; }
#Reset  { anchor: bottom-right; right: 12px; bottom: 10px; width: 96px; height: 48px; font-size: 24px; }
button  { background: rgb(40, 90, 60); }
button:hover { background: #3a6; }
@media (orientation: portrait) { .hud { anchor: top; left: 0; width: 90%; } }
```

**Elements.** `id` is the `name`. `class` is for selectors only. `hidden` hides. `html` and `body` are transparent, `head`, `style`, `link`, `script`, `meta`, `title` are skipped.

| HTML | Becomes |
|------|---------|
| `button` | `UIButton`: `data-action` (or `action`) is the action, `data-value` the `value` that goes out with it, `disabled` disables, the text is the label |
| a `div` (any container) with `data-widget="segmented"` | `UISegmented`, a choice group: `data-bind` is `bind` (the `UIVars` variable), `disabled` disables the group, its `<button data-value=".." data-action="..">` children are the segments. It needs `display: flex` (a row or a column, `gap`); without it the group is still written, laid out as a column with no `gap`, and the front-end reports it |
| `img` | `UIImage`: `src` is the sprite name, `object-fit: fill` is `stretch` |
| an element with text of its own and only inline elements (`b`, `span`, `br`, ...) in it, or a `span`, `p`, `h1`-`h6`, `label`, `strong`... with only text in it, also an empty one | `UILabel`: the text with white space collapsed, `<br>` a line break; its `background` is the fill behind the text |
| any other, with a `background-image` | `UIPanel` |
| any other | `UIElement` (a group) |

A group takes clicks only when it has a visible `background` (or `pointer-events: auto`): a full-screen wrapper must not swallow the clicks of the game under it. A panel and a label with a background do, as a plate does in a browser. `pointer-events: none` turns it off. Text beside block elements is reported and dropped (put it in a `<span>`); a tag outside the table is read as a `<div>` and reported; every element needs its end tag (`<p>` and `<li>` are not closed by the next sibling).

**The cascade** is CSS's: `!important`, then a `style=""` attribute, then specificity (ids, classes and pseudo-classes, tags), then source order, across `<style>` and `<link>` in document order. Selectors: tag, `*`, `#id`, `.class`, compounds, a selector list, the descendant (space) and child (`>`) combinators, `:hover` / `:active` / `:disabled` / `:checked` on the last part. Inherited: `color`, `font-size`, `font-family`, `text-align`, `line-height`, `white-space` (a button keeps its own alignment and wrapping, like a browser's default style sheet). Shorthands and longhands are separate names in the cascade, so the later declaration of the two wins by order, not by CSS's expansion rules.

**Properties.**

| CSS | Engine |
|-----|--------|
| `display: none`, `visibility: hidden` | `visible="false"` |
| `display: flex`, `flex-direction: row\|column`, `gap`, `padding`, `flex-wrap: wrap`, `align-items`, `flex` / `flex-grow` | `layout="Stack"`, `direction` (a row is the default of CSS, so `Horizontal` is written), `spacing`, `padding`, `wrapChildren`, `crossAlign`, `flex` (the first number). `justify-content` has no counterpart |
| `left`, `top`, `right`, `bottom` (px) | The anchor is `TopLeft` unless only `right` / `bottom` is set; `x = left - right`, `y = top - bottom`. All four, or `inset`, is `Stretch` with the distances (plus `margin`) as the margin |
| `anchor` (not CSS): `top-left`, `top`, `center`, ..., `stretch` | `anchor` (centering needs it: `left: 50%` is not an offset in units) |
| `width`, `height`, `min-`, `max-` (px or %), `aspect-ratio`, `object-fit: cover` on a box with a ratio | the lengths, `aspectRatio`, `aspectMode="Fill"` |
| `background`, `background-color`, `background-image: url(Name)`, `border-image-slice` | `color`, `sprite`, `sliceBorder` (sprite names are TextureLibrary names) |
| `color`, `font-size` (px), `font-family` (a path to a font file; a generic family is the default font), `text-align`, `vertical-align: top\|middle\|bottom`, `text-overflow: ellipsis\|clip`, `white-space: normal\|nowrap`, `line-height` (a number, or px against the font size) | `textColor`, `fontSize`, `font`, `align`, `verticalAlign`, `overflow`, `wrap`, `lineSpacing` |
| `-be-text-fit: shrink`, `-be-min-font-size` (engine-only) | `overflow="AutoFit"`, `minFontSize` |
| `position: absolute\|relative\|static\|fixed`, `box-sizing`, `cursor`, `user-select` | accepted, mean nothing |
| `button:hover`, `:active`, `:disabled`, `:checked` with `background` | `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor` (anything else in a state rule is reported). `:checked` is the look of a selected segment; whether a segment is selected is decided at run time by its group, not by the markup |

Lengths are px and %, a color is `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()` or a few names. Not understood and reported (the list the decision record counts): `em`, `rem`, `vw`, `vh`, `calc()`, `var()`, `opacity`, `border`, `border-radius`, `box-shadow`, `transform`, `transition`, `animation`, `z-index`, `overflow`, `font-weight`, `letter-spacing`, `margin` on an element that does not fill its parent (a stack has `gap`), `justify-content`, `grid`, attribute selectors, `+`, `~`, other pseudo-classes and pseudo-elements, `@import`, `@font-face`, `@keyframes`.

**Choice groups.** `data-widget`, `data-bind` and `data-value` are the only attributes of the markup the front-end reads beyond the standard ones. They are reported, never silent, when they stand where nothing reads them: a `data-widget` that is not `segmented` (the element is read as a plain one), `segmented` on a `<button>` or `<img>`, `data-bind` without the widget, `data-value` on anything but a `<button>`, and a segmented group that is not `display: flex`. The widget name is not case sensitive. A group with a background is a plate and takes clicks (the usual rule), so give it `pointer-events: none` if it should not. The `UISelectorScreenTest` panel in HTML + CSS (`UIWebTest`, `kPanelHtml` / `kPanelCss`) is the template: the same rects as the XML in landscape and portrait.

**`@media`.** A query is `and`-joined `(feature: value)` terms (`orientation`, `min-` / `max-` `width`, `height` and `aspect-ratio`, `aspect-ratio`) with commas for "or"; it becomes a `UIVariantCondition`, so widths are in canvas units (the bounds after the scaler), not browser pixels. For each block the cascade runs again with its rules and what differs from the base becomes the overrides of the variant (`UIElementOverride`: visibility, anchor, offset, size, direction, font size), so `display: block` can undo a base `display: none`. A block that changes anything else (a background, a margin) reports it. An element a variant names but that has no `id` gets a name (`div-1`).

**Checking a screen.** The compile is pure, so a test gives it strings: `tests/BECore/UIWebTest.cpp` compares the Match3 menu in HTML + CSS with the shipped XML (same rect for every element at five sizes) and runs it through `ValidateOnMatrix`. The report is the place to look when a screen does not look as written.

## Controls

A control is a `UIElement` subclass registered in `AbstractFactory<UIElement>`; the `type` of an `<element>` is its class name. Each draws itself in `DrawSelf(IRenderer&)` (the canvas calls it for a visible element, then draws the children over it) with the view transform of the canvas in effect, so it works in canvas units. `color` of every element is a fill behind its content.

| Type | Fields (besides the common ones) | Behavior |
|------|-----------------------------------|----------|
| `UIElement` | none | A plain box: the fill, and children. Registers itself (`REGISTER_SELF`) so `type="UIElement"` is a group |
| `UIPanel` | `sprite`, `sliceBorder`, `spriteTint` | Fill, then the sprite. With a `sliceBorder` (one number or `left,top,right,bottom`, in sprite pixels) the sprite is cut in nine (`UINineSlice`): corners keep their size, one unit per pixel. `blocksInput` is true by default |
| `UIImage` | `sprite`, `tint`, `stretch` | The sprite as large as fits the rect, centered, keeping its proportions (`stretch="true"` fills the rect). The aspect option for art whose cell is not square (the Chess pieces in BagiEngine-Chess) |
| `UILabel` | `text`, `font`, `textColor`, `align`, `verticalAlign`, `overflow`, `wrap`, `lineSpacing`, `minFontSize` | Text in the rect. The size is `fontSize` of the style (24 when 0), so a variant can change it; laid out and rasterized at that size times the canvas scale in whole output pixels, the way `TextComponent` does (`Text/CLAUDE.md`). `overflow` defaults to `Ellipsis`; `Clip` cuts the drawing at the rect and the validator reports it (`TextDoesNotFit`); `AutoFit` shrinks down to `minFontSize` |
| `UIButton` | `action`, `value`, `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`, `disabled` | A label on a fill that follows the pointer. `interactive`, centered text, no wrap. A click emits `InputEvents::ActionEvent(action, value)` (`value` empty for a plain button). Runtime, not in a file: `selected`, `groupDisabled` |
| `UIStack` | none | A container whose layout is Stack from the start; `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign` are the style attributes |
| `UISegmented` | `bind`, `disabled` | A `UIStack` of `UIButton` segments that is a choice group (below). `bind` names a `UIVars` variable; the segment whose `value` equals it is `selected`. `disabled` greys the group. A stack from the start |

```xml
<element type="UIPanel" name="Hud" x="16" y="16" width="240" height="120" color="0,0,0,160">
  <children>
    <element type="UILabel" name="Score" x="12" y="10" width="216" height="32" text="Score" fontSize="28"/>
    <element type="UIButton" name="Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="48" text="Reset" action="Match3Reset" color="40,90,60,255"/>
  </children>
</element>
```

- **Sprites** come from the `TextureLibrary` by name (`sprite="Panel9"`); the texture is loaded on the first draw and again when the name changes, and a name the library does not know is logged once. Code and tests give a texture directly: `panel->GetSprite().SetTexture(texture, srcRect)`.
- **Button states.** The canvas sets `UIElement::hovered` and `pressed` from the mouse: the pointer over the topmost hit-testable element is hovered (not while a button is held that the canvas did not take: that is the game's drag), a left press on an `interactive` element presses it, and the release calls `OnClick()` only when it lands on the same element, so dragging off the button cancels the click. The fill is `color`, `hoverColor` under the pointer, `pressedColor` while held on the button (a press that moved off is shown as normal), `disabledColor` when `disabled`. A color left transparent is derived from `color`: lighter, darker, half transparent. A disabled button still consumes the click. A hidden canvas clears hover and press. A canvas below a canvas that consumes the motion is not asked, so its hover stays where it was. **Selected and the value.** `value` goes out with the action (`ActionEvent::value`, empty for a plain button), so buttons that share an action tell the listener which one was pressed. `selected` (runtime, set by whoever owns the choice) fills the button from `selectedColor`, derived from `color` when transparent and lighter than the hover shift, and draws a bar of `UIButton::kSelectedBarHeight` (4) units in `textColor` along the bottom edge, so the selection does not rest on color alone; hover and press shift from the selected fill and an explicit `hoverColor` / `pressedColor` still wins. `disabled` wins over `selected` for the fill (the bar stays, at half alpha, so a disabled choice still shows what is chosen). `groupDisabled` (runtime) counts as `disabled` in the fill and in the click.
- **Choice groups.** `UISegmented` (`bind="side"`) is a radio group over its direct `UIButton` children: on every draw (`DrawSelf`, before the canvas draws the segments; `SyncChildren()` does the same without a draw) the segment whose `value` equals `CoreManager::GetUIVars().Get(bind)` gets `selected`, every other one does not, and `disabled` sets `groupDisabled` on all of them. An unset variable, an empty `bind` and a segment with an empty `value` select nothing; children that are not buttons are skipped and a nested group manages its own children. **The group owns no choice:** a click emits `ActionEvent(action, value)` and changes nothing in the group; the game answers by setting the variable, so the direction is the one of the labels (game -> `UIVars` -> widget) and the screen cannot disagree with the game. It keeps no state of its own, so an unchanged screen costs a comparison per segment and lays nothing out again. Two segments are the toggle of a binary choice, four are a level; a segment is a normal button (give it `flex="1"` to share the width). **In XML every segment needs an explicit `height` (56 in an 840-unit view; at least `minTouchSize` / scale of the narrowest window of the matrix, 32 / 0.571), also with `flex="1"`:** flex shares the main axis, so a segment without a height gets a rect of height 0, is not drawn, cannot be hit (`ui_click` answers "hidden") and the validator skips an empty rect without a word (a caption in a row that a variant turns vertical needs one too). A hidden canvas is not drawn, so its groups take the variables on the first draw once it is shown. `UISegmentedTest` is its test; `tests/BECore/UISelectorScreenTest.cpp` is the screen test of a panel of four groups (a hidden modal canvas, caption beside the group or above it in a portrait view, the words on the size matrix and in the narrowest window, show and hide, clicks, a deliberately broken copy for each check) and the template to copy.
- **Vertical alignment** is the label's own (`TextLayout` has none): the line box is placed at the top, the middle or the bottom of the rect.
- **The common attribute names are shared.** The style attributes of an element (`wrapChildren`, `direction`, `fontSize`, ...) and the fields of a control are attributes of one `<element>`, so they must not clash: the stack's `wrap` was renamed `wrapChildren` because a label has the text `wrap`. `UIControlsTest.EveryPropertyOfEveryControlSurvivesARoundTrip` sets every property of every control and fails on a clash.
- **Skip-defaults.** A control field that defaults to something other than its type's zero (`wrap` true, `textColor` white) is dropped by a serializer that skips defaults, because the check is "equal to `T{}`". The editor does not use that mode; do not save UI with it.
- Adding a control: derive from `UIElement` (or `UILabel`), `BE_CLASS(Name)` with `BE_REFLECT_FIELD`s, override `DrawSelf`, include the `.gen.hpp` in the `.cpp`. Names that are hard to guess go in the attribute list above, and the class registers itself, also when it derives from another control.

## Choice panels

A screen where the person picks one of a few options (the side, the engine, the strength of a game) is built from `UISegmented` groups, one per choice. The worked example is the shape of the Chess opponent panel, tested on the size matrix by `tests/BECore/UISelectorScreenTest.cpp` (its XML constant `kSelectorXml` is the template to copy; `UIWebTest` has the same panel in HTML + CSS, `kPanelHtml` / `kPanelCss`).

- **The game publishes ids, not words.** Next to the words of its HUD it sets one `UIVars` variable per choice to the id of the current option (`opponentMode` = `two` | `computer`, `opponentColor` = `white` | `black`, `opponentEngine` = `stockfish` | `own`, `opponentLevel` = `easy` .. `max`: the ids of its control API). The words live in the `text` of the segments.
- **The segments carry `action` + `value`.** All segments of a group share one action and differ in `value`; the game subscribes to `InputEvents::ActionEvent`, reads `value` and applies the choice to its own state.
- **The answer sets the variable.** The group never selects by itself (game -> `UIVars` -> widget): the game sets the variable after it accepted the choice (or leaves it when it refused, and the screen keeps showing the truth). A client checks it with `ui_tree`, `ui_click`, `step`, `ui_tree`.

```xml
<element type="UISegmented" name="Mode" bind="opponentMode" height="56" direction="Horizontal" spacing="2" color="30,30,30,255">
  <children>
    <element type="UIButton" name="ModeTwo"      flex="1" height="56" text="Two players" value="two"      action="ChessSetMode" color="70,70,100,255"/>
    <element type="UIButton" name="ModeComputer" flex="1" height="56" text="Computer"    value="computer" action="ChessSetMode" color="70,70,100,255"/>
  </children>
</element>
```

- **A panel opened by a button** is a modal canvas on a node of its own, started hidden (`visible="false"`, `modal="true"`): a dim element that blocks input, a plate (a `UIStack`, `anchor="Center"`, `width="94%"` with a `maxWidth`) with a title, a row per choice (a caption `UILabel` beside the group, or above it in a portrait variant) and an "OK" button. A button of the always-visible HUD canvas has an action; the game answers it with `UICanvasComponent::SetVisibleByName(scene, "Selector", true)` and the OK button's action with `false`. While hidden the canvas draws nothing and takes no click (the board under it does); while shown, modal, it takes every click. A hidden canvas is not drawn, so the groups take the variables on the first draw after it is shown.
- **What the screen test holds** (copy it for a new panel): `ValidateOnMatrix` empty in the landscape and the portrait view; every word of every group asked with `CheckContent().fits` (an ellipsis label can shrink, so the validator alone misses a cut word; a button keeps 8 units of air), also in the narrowest window alone (480 px wide with side insets, safe width about 689 units); each segment at least 56 units high; show and hide; one segment of each group selected from the variables; every click emitting the right action and value; and a deliberately broken copy for each check, so the test can fail.
- **Not built here:** the Chess HUD itself (the game's entry `chess-hud-opponent-panel` copies the shape above); the game decides whether the panel is a modal canvas or lives in the margins.

## Variables

A label shows game state without the game touching the label:

```cpp
CoreManager::GetUIVars().Set("score"_intern, 120);   // a number is written like Format() writes it; a string works too
```

```xml
<element type="UILabel" name="Score" text="Score: {score}" .../>
```

- `UIVars` is a table of name to text and nothing more (no expressions). `Set` with the value that is already there changes nothing; `Remove` and `Clear` count as changes. The version never goes back, so a label that expanded its text at an older version knows to do it again.
- **Placeholders.** `{name}` is replaced by the variable of that name; a variable nobody set is empty; `{{` and `}}` are literal braces; an unclosed `{` stays as written.
- **Cost.** A label expands its text again only when its `text` or the version of the variables changed, and lays it out again only when the expanded text (or the size, the font, an option) changed, so an unchanged screen costs a comparison per label (`UILabel::GetLayoutCount()` in the tests). A variable the label does not show moves the version but not the shown text, so nothing is laid out.
- `UILayoutValidator` checks the text that is shown (`CheckContent()` uses `GetDisplayText()`): a screen test sets the variables to their longest realistic values and runs the matrix.
- The table is global, so a test clears it in `TearDown` (`CoreManager::GetUIVars().Clear()`).
- **Show and hide by name.** A whole screen: `UICanvasComponent::SetVisibleByName(scene, nodeName, visible)`. One element: `canvas->SetElementVisible(name, visible)` or, in every canvas of a scene, `UICanvasComponent::SetElementVisibleByName(scene, name, visible)`. They set the authored `style.visible`, so a hidden element gets an empty rect, draws nothing, takes no clicks and takes no room in a stack; a variant that sets `visible` for the same element still wins.

## Building a screen in code

```cpp
void MyHud::OnAttached() {  // the node has a UICanvasComponent before this component in its list
    auto canvas = GetNode().GetComponent<UICanvasComponent>();
    auto hud = canvas->GetRoot().AddChild("Hud"_intern);
    hud->style.rect.x = 16.0f;
    hud->style.rect.y = 16.0f;
    hud->style.rect.width = UILength::Px(220.0f);
    hud->style.rect.height = UILength::Px(96.0f);
    hud->color = Color{0, 0, 0, 160};
    hud->blocksInput = true;
    canvas->Invalidate();  // after editing the tree or the variants
}
```

Code and a file can be mixed: the canvas of a node comes from the file, a component adds more elements in `OnAttached()`.

## Control API (`ui_tree`, `ui_click`)

`RegisterUIControl()` (`UIControl.h`, called by `CoreManager::InitializeCoreServices()` after the builtin methods) gives a client that drives a game over `--control` eyes and a finger on the UI. They work on the canvases of the **active scene** by the layout of the last draw, so `step` a frame before reading. `ui_tree` answers `{scene, canvases}`: each canvas has its node, space, `visible`, `modal`, `inputEnabled`, `takesInput`, `laidOut`, the `scale` and the bounds in UI units, the element tree and the validator's `issues`; each element has `type`, `name`, `visible`, `interactive`, `rect` (UI units of the canvas) and `outputRect` (render output pixels: the pixels of a `screenshot`), `text` for a label or button (variables replaced), for a button `action`, `disabled` (its own flag or its group's: whether a click does anything), `value` (what a click emits with the action, `""` for a plain button) and `selected`, for a `UISegmented` `bind`, `selectedValue` and `disabled`. `ui_tree` calls `SyncChildren()` on every group before it describes the segments, so `selected` and `selectedValue` are those of this moment (a variable the game set since the last draw is already in), while the rects are those of the last draw. A canvas never drawn has the structure and no rects. `ui_click {name, canvas?}` presses the left button on the middle of the first visible, interactive element of that name through `CoreManager::GetUIInput()` (a move, a press, a release), so it is what a user would do: a modal canvas above takes the click, a click that no canvas takes reaches game code. The answer carries the button's `action` and `value` (the pair of the `ActionEvent`); a segment of a choice group changes nothing by the click, so a client checks a choice with `ui_tree`, `ui_click`, `step`, `ui_tree`. It checks first that the element is the topmost of its own canvas at that point and answers `not_found`, `not_clickable` (the message says: hidden, not interactive, covered by which element, canvas takes no input, not drawn yet) or `no_scene`. Names and node names from the client are looked up with `PoolString::Find`, never interned. Not seen: the DOM of an `RmlUiDocumentComponent`. The MCP server has both as tools (`tools/control/CLAUDE.md`), and the Match3 control smoke plays the menu through them. Protocol details: `UIControl.h`, `Control/CLAUDE.md`.

## Editor preview

`ViewportWidget` has a toolbar: an aspect preset (Free, 16:9, 4:3, 21:9, 9:16) that fits the image into the panel with that shape, and a safe-area simulation (None, Notch, Side insets) that is drawn as an outline. The viewport is not the window, so during the scene pass `UIEnvironment::SetSafeAreaOverride()` holds the simulated safe area (the whole target for None), and canvases never see the window's. The numbers come from `UIPreview` (`UIPreviewTest`), and the matrix of the tests uses the same safe-area shapes. Editor input goes through `ViewportWidget::ForwardMouse` into `UIInput`.

## Rules

- **UI code reads the context, never the window.** Everything layout needs is in `UILayoutContext`; a pure test sets it by hand.
- **`UICanvasLayout`, `UILayout`, `UICanvasScaler`, `UIVariants` and the validator stay pure:** no scene, window or renderer, so they are tested without one.
- **Edit the tree, then `Invalidate()`.** The cache notices a changed context and changed canvas settings by itself, not edits of elements.
- **A change of a reflected field of the component** (inspector) is noticed on the next draw (the fields are copied into `params` and compared).
- **Draw only in `OnDraw()`**, input only through `UIInput` (`BECore/Input/UIInput.h`); `OnDraw()` must not add or remove nodes or components.
- `CoreManager` declares `_uiInput` and `_uiEnvironment` before `_sceneManager`, so they outlive the scenes: a canvas unregisters from `UIInput` in its destructor.

## Not done

Of stage 6.4: a `UIToggle` (a single on/off switch with its own label; a two-segment `UISegmented` does the binary choice of a panel), `UISlider`, `UIProgressBar`, the string table (the words of a HUD are still literals in game code), button sounds, and a dropdown (four options fit one row; not judged needed). Also: focus and gamepad navigation, keyboard and gamepad activation of a button, a `UITextInput`, scrolling, hot reload of a `.ui.xml` / `.ui.html` document (the documents of the RmlUi host have it, `src/Modules/RmlUiHost/CLAUDE.md`), clipping of children to their parent, the debug overlay, an inspector for variants and the scene editor's tree view of canvases and elements (stage 7). Each control waits for a screen that asks for it (Mahjong is named as the first consumer of the rest). The manual look on the Vulkan backend of the menu, the HUDs and the selected-segment style (fill plus a 4-unit bar, a first guess) is the owner's.

## Tests

`tests/BECore/`: `UILayoutTest` (anchors, stretch, limits, aspect, stack), `UICanvasScalerTest` (every space and mode, safe area, the context comparison), `UISettingsTest` (settings including `hotReloadInterval`, `UIEnvironment`), `UIVariantTest` (conditions, order, restoring, tree layout), `UICanvasLayoutTest` (cache, variant runs, hit testing, the matrix itself and shipped-style screens), `UILayoutValidatorTest`, `UICanvasComponentTest` (a scene from XML into a `RecordingRenderer`: drawing in each space, the view transform handed back, input order, press following, modal, inactive scenes), `UIControlApiTest` (`ui_tree` and `ui_click` through a `ControlService` of its own: rects in canvas units and output pixels, text with variables, the validator's findings, a click in a SceneView canvas, a modal above, every refusal, client names not interned; a choice group: `value`, `selected`, `bind`, `selectedValue`, the selection following the variable between two `ui_tree` calls with no draw, a disabled group, the value of a click in the answer and in the `ActionEvent`, a plain button with an empty value), `UIPreviewTest`, `UIReflectionTest` (the text forms, the tree and variants through XML and binary, malformed values, an unknown type, a screen from a scene file laid out in both orientations), `UIWebTest` (the HTML reader, the CSS reader, the Match3 menu and a HUD in HTML + CSS against the XML, the choice panel of `UISelectorScreenTest` in HTML + CSS against its XML (the same attributes, the same rects at five sizes in a landscape and a portrait view, the selection following the variable, `:checked` with `:hover`, the reported misuses), the cascade, each property group, `@media`, the report, a `.ui.html` file through the resource manager), `UIDocumentTest` (a file, inline XML and a tree built in memory give the same elements; the loader choice; canvases of one file do not share layout; file and inline together; a missing, a broken and a partly bad file), `UINineSliceTest`, `UIScreenTest` (a HUD of controls through `ValidateOnMatrix`, the same HUD drawn in landscape and portrait, at twice the size, with a variant that hides an element and restores it, with a bound score; a deliberately broken one is caught), `UIVarsTest` (set, get, version, expand, escapes, unset names, Cyrillic), `UIControlsTest` (every control from a file and back, every property of every control, hit testing, draw calls of panel, nine-slice, image and label, scale and variants, overflow and the validator, variables in labels and their layout cost, show and hide by name, button states and clicks from synthetic mouse events, the value a click carries, the selected fill and bar, a group-disabled button), `UISegmentedTest` (the selection follows the variable, empty bind / value / variable, direct buttons only, nested groups, a plain stack untouched, the bar, flex widths, disabled, a click reports and selects nothing, hiding, XML and binary round trips), `UISelectorScreenTest` (a panel of four choice groups in a hidden modal canvas on the size matrix, in a landscape and a portrait view and in the narrowest window; every word of every group; show and hide, clicks, a deliberately broken copy for each check).
