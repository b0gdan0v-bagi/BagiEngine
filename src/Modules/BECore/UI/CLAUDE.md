# UI Module Guidelines

In-game UI drawn through `IRenderer`, in both backends and both run modes. ImGui is not involved (it is the tool UI: `.claude/rules/widgets-module.md`). The authoring decision and its data: `AuthoringDecision.md`. Screens in HTML + CSS: `.claude/rules/ui-web.md`.

**Adaptivity is the foundation.** Window size, aspect, orientation, safe area, pixel density and user scale are inputs of every layout, and **every screen must pass `ValidateOnMatrix` with no issues before it is done**.

```
UICanvasComponent (scene component; reflected fields: space, scaleMode, ...)
  ▼ owns and feeds with the frame's UILayoutContext
UICanvasLayout (pure) ── UICanvasScaler::Resolve → scale, bounds, safeBounds, contentBounds, ViewTransform
                      ── UIVariants::Apply       → `active` style of every element
                      ── UITreeLayout::Run       → `rect` of every element (UILayout: the rules)
                      ── HitTest, Validate (UILayoutValidator)
  ▼
UIElement tree (root = the canvas)
```

| Class | Role |
|-------|------|
| `UILength`, `UIRectSpec`, `UIStackSpec` | a size in units or percent; how an element finds its rect; a stack |
| `UILayout` | pure rules: `ResolveRect`, `LayoutStack` |
| `UILayoutContext` | everything layout depends on besides the tree (output size, density, display scale, safe area, user scale, view size, World node box); `operator==` |
| `UIEnvironment` (`CoreManager::GetUIEnvironment()`) | `UISettings` from `config/UIConfig.xml` (`userScale`, `minTouchSize`, `hotReloadInterval`), `MakeContext`; `SetSafeAreaOverride` / `SetPixelDensityOverride` for the editor viewport |
| `UIElement` | `FACTORY_BASE` node: `name`, `layout`, `style` (authored) / `active` (after variants), `rect`, `children`, `blocksInput`, `interactive`, `ignoreSafeArea`, `color`, virtual `CheckContent()` |
| `UIElementStyle` | what a variant can change: `visible`, `rect`, `stack`, `fontSize` |
| `UIColor` | a color in one attribute; the shared state shifts (`ShiftColor`, `kHoverBrighten`, `kPressDarken`, `kDisabledAlpha`, `kFillFallback`) |
| `UIText` / `UIAttr` | text forms of lengths, edges, colors, numbers (`ParseFiniteFloat`, `FractionInRange`, `ValueInRange`) |
| `UIVars`, `UIStrings` | variables and the string table (below) |
| `UISprite`, `UINineSlice` | sprite by name; nine-slice cutting |
| `UIScreenContent`, `UIDocument`, `UIDocumentLoader` | `elements` + `variants` of a screen; a `.ui.xml` file, `Instantiate` per canvas |
| `UIVariant` | `when` + `overrides`; `UIVariants::Apply` |
| `UICanvasLayout` | the pure pipeline with a cache, `HitTest`, `Validate`, counters for tests |
| `UILayoutValidator` | `Validate(root, resolved, options)` → `UILayoutIssue`s |
| `UIPreview` | the editor viewport's preview shapes and safe areas |
| `UIControl` | control API `ui_tree` / `ui_click` |

## Rules

- **UI code reads the context, never the window.** `UICanvasLayout`, `UILayout`, `UICanvasScaler`, `UIVariants` and the validator stay pure (no scene, window or renderer).
- **Edit the tree, then `Invalidate()`.** The cache notices context and canvas-setting changes itself, not element edits. A reflected field changed in the inspector is noticed on the next draw.
- **Draw only in `OnDraw()`**, input only through `UIInput`; `OnDraw()` adds or removes no nodes or components.
- `CoreManager` declares `_uiInput` and `_uiEnvironment` before `_sceneManager`: a canvas unregisters from `UIInput` in its destructor.
- **Widgets own no state.** A choice group, a toggle and a slider only emit `ActionEvent(action, value)`; the game applies it and sets the bound `UIVars` variable (game → `UIVars` → widget), so the screen cannot disagree with the game. Publish **ids** in variables, **words** in the string table.
- `UIVars` and `UIStrings` are global: a test clears them in `TearDown`.

## Canvases

| `space` | Fills | Unit | Scale |
|---------|-------|------|-------|
| `Screen` | the render output | by `scaleMode` | mode x user scale, within `minScale` / `maxScale` |
| `SceneView` | the scene's fitted view | a scene unit | the scene's fit |
| `World` | the node's `TransformComponent` box | a scene unit | the scene's fit; follows the node |

`scaleMode` (Screen only): `ConstantPixelSize`, `ConstantPhysicalSize`, `ScaleWithScreen` (against `referenceWidth` x `referenceHeight`; `match` 0 width, 1 height, between them Unity's geometric blend). `useSafeArea`: `contentBounds` is the safe area, `bounds` stays the canvas (a root child with `ignoreSafeArea` uses `bounds`).

- **Draw order** is the scene's (layer, then scene order); **input order** the reverse (refreshed every draw, also while hidden).
- **Input:** a canvas consumes a mouse event on a hit-testable element (`blocksInput` or `interactive`) by the last draw's layout; hover over one is consumed except a drag that began elsewhere (the game's); a press it took is followed to its release; a `modal` canvas consumes everything while shown; hidden / `inputEnabled` off / inactive-scene canvases consume nothing. Hit testing: child before parent, later sibling first; children are not clipped.
- **Popups** (`UIElement::HasPopupChildren`, a dropdown's list): the children are out of the flow, with no rect while closed (so the validator, hit test, draw and `ui_tree` do not see them); open, `LayoutPopup` places them in `contentBounds`, they are drawn after the whole tree and hit-tested before it. A left press anywhere else closes the popup and is **consumed** with its release (no click on what is below); a click ending inside the list goes to the popup's `OnPopupChoice`, not to the child's `OnClick`. One popup is open per canvas (the newest wins); a canvas that cannot take input closes it. The open state is runtime UI state; the canvas invalidates when it changes.
- **Pointer hooks:** `OnClick` on release over the same element; `OnPress(x, y)` on the left press of an `interactive` element and `OnDrag(x, y)` on motion while held, in canvas units; they run last, on a copy of the pointer.
- **Show / hide:** `UICanvasComponent::SetVisibleByName(scene, node, visible)`; one element: `SetElementVisible(name, ...)` or `SetElementVisibleByName(scene, name, ...)` (sets `style.visible`; a variant still wins).
- The reflected fields are `visible` and `inputEnabled`, not `enabled` (that attribute name is taken by factory vectors, `Reflection/DataAccessor.h`).

## Layout

- **Anchor:** nine points and `Stretch`; the pivot is the same point of the element, so `BottomRight` with `x=-12` moves inwards. `Stretch` fills the parent minus `margin`.
- **Order:** size (units or percent) → `min*` / `max*` (min wins) → aspect (`Fit` / `Fill`) → anchor and offset.
- **Stack:** `direction`, `spacing`, `padding`, `wrap`, `crossAlign`; `flex` shares what is left on a line (clamped, not redistributed). A stack ignores children's anchor, offset, margin and aspect. **`crossAlign="Stretch"` overrides a child's explicit cross size**: a row holding a thin control (slider, progress bar) uses `Center`.
- **Hidden elements** get an empty rect and take no room.
- **Variants:** a condition on canvas `bounds` in units (`orientation` (a square is portrait), aspect, width, height ranges) and overrides of `visible`, `anchor`, `x`, `y`, `width`, `height`, `direction`, `fontSize`. Applied in order (later wins), always from `style`; re-chosen only when the bounds change.

## Validator and the size matrix

Issues: `OutsideCanvas`, `OutsideSafeArea`, `SiblingsOverlap` (in a stack), `TouchTargetTooSmall` (`min(w, h) * scale / pixelDensity` < `minTouchSize`), `TextDoesNotFit` (only content that cannot shrink).

`tests/Common/UILayoutMatrix.h` runs a canvas over ten aspects at 480..2160 px short sides, with no insets / a notch / side insets, each scale mode and user scales 1.0 and 1.5. A canvas from a scene needs one `UpdateLayout(w, h)` first. One line: `EXPECT_TRUE(ValidateOnMatrix(canvas).empty()) << DescribeMatrixFailures(failures)`.

**A screen test** builds the screen through the reflected path a game uses, sets the variables to their longest values, runs the matrix **once per language**, and also asks `CheckContent(width, height, scale).fits` of every shown label, button and toggle — **an ellipsis label "can shrink", so the validator alone misses a cut word**. It holds a deliberately broken copy for each check. Templates: `tests/BECore/UIScreenTest.cpp` (a HUD), `UISelectorScreenTest.cpp` (a modal panel of choice groups, `kSelectorXml`), `UISettingsScreenTest.cpp` (toggle, slider, progress bar, language group, a deliberately long test language).

**Design rules:** anchors and percents, not absolute positions; interactive elements ≥ 48 units at the reference size with a `minScale` that keeps them touch targets (56 in an 840-unit view); a variant for the shapes that do not fit (usually portrait); text that wraps, cuts with an ellipsis or shrinks.

## Writing a screen in XML

The elements and variants are reflected fields of `UICanvasComponent`, so a screen is data in `SceneConfig.xml` (or a node file, `Scene/CLAUDE.md`, "Node files"):

```xml
<component type="UICanvasComponent" space="Screen" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5" minScale="0.7" useSafeArea="true">
  <elements>
    <element type="UIPanel" name="Hud" x="16" y="16" width="240" height="120" color="0,0,0,160">
      <children>
        <element type="UILabel" name="Score" x="12" y="10" width="216" height="32" text="{#hud.score}: {score}" fontSize="28"/>
        <element type="UIButton" name="Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="48" text="{#hud.reset}" action="Match3Reset"/>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait"><overrides><set element="Hud" anchor="Top" x="0" width="90%"/></overrides></variant>
  </variants>
</component>
```

- `type` is the class name in `AbstractFactory<UIElement>`; unknown → deserialize error, a plain `UIElement`.
- Style attributes are flat (`anchor`, `x`, `y`, `width`, `height`, `min*`, `max*`, `aspectRatio`, `aspectMode`, `margin`, `flex`, `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign`, `fontSize`, `visible`). A length is `220`, `220px` or `90%`; margins / padding one number or `l,t,r,b`; a color `r,g,b,a` or `#RRGGBB[AA]`. A malformed value is reported and the default stays.
- A variant's `when` is `name:value;...` (`orientation`, `minAspect`, `maxAspect`, `minWidth`, ...). An override is written even when equal to the default.
- `source="assets/ui/hud.ui.xml"` loads a `UIDocument` (`<ui><elements/><variants/></ui>`); its elements come first, the inline ones on top; only the inline part is saved back. Code-added elements (`GetRoot().AddChild`) are runtime-only.
- Attribute names of style and control fields share one `<element>` and must not clash (`UIControlsTest.EveryPropertyOfEveryControlSurvivesARoundTrip`).
- Do not save UI with a skip-defaults serializer (fields whose default is not `T{}` would be lost).
- Fields are declared `IntrusivePtrNonAtomic<UIElement>`: the generator finds factory fields by that name.
- RmlUi (`RmlUiHost`) is a separate front-end with its own DOM; the editor, the validator and `ui_tree` do not see into it.

## Controls

Each derives from `UIElement`, draws itself in `DrawSelf` in canvas units; `color` is a fill behind the content.

| Type | Fields | Behavior |
|------|--------|----------|
| `UIElement` | — | a box and children |
| `UIPanel` | `sprite`, `sliceBorder`, `spriteTint` | fill then sprite, nine-sliced with `sliceBorder`; `blocksInput` by default |
| `UIImage` | `sprite`, `tint`, `stretch` | sprite fitted, centered, proportional |
| `UILabel` | `text`, `font`, `textColor`, `align`, `verticalAlign`, `overflow` (`Ellipsis` default, `Clip`, `AutoFit`), `wrap`, `lineSpacing`, `minFontSize` | text at `fontSize` (24 when 0) x canvas scale in whole pixels |
| `UIButton` | `action`, `value`, `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`, `disabled` | a click emits `ActionEvent(action, value)`; state colors derived when transparent; `selected` adds a 4-unit bar so selection is not colour alone; disabled still consumes the click |
| `UIStack` | — | a stack container |
| `UISegmented` | `bind`, `disabled` | radio group over direct `UIButton` children: the segment whose `value` equals the variable is `selected`. **Every segment needs an explicit `height`**, also with `flex` (else it is a 0-high rect: not drawn, not clickable, skipped by the validator) |
| `UIToggle` | `bind`, `action`, `onValue`, `offValue`, `disabled`, track / fill / thumb colors | a caption with a switch at the right; on when the variable equals `onValue`; a click emits the opposite value. The caption is measured left of the switch (`GetParts`) |
| `UIProgressBar` | `bind`, `minValue`, `maxValue`, track / fill colors | read-only fraction of the width; anything that is not a number in range is 0; takes no input |
| `UISlider` | `bind`, `action`, `minValue`, `maxValue`, `step`, `disabled`, colors | horizontal; press and drag emit the value text under the pointer (only when the text changes); snapped to `step`; text `FormatValue` (≤ 6 significant digits, no trailing zeros). Thumb as high as the element, so height decides the touch check |

- Sprites come from `TextureLibrary` by name (code: `GetSprite().SetTexture(...)`).
- **A choice panel** (pick one of a few options): one `UISegmented` per choice in a modal canvas started hidden (`visible="false" modal="true"`): a dim, a plate, a row per choice, an OK button; a HUD button opens it with `SetVisibleByName`. A hidden canvas is not drawn, so its groups read the variables on the first draw after it is shown.
- **Adding a control:** derive from `UIElement` (or `UILabel`), `BE_CLASS` with `BE_REFLECT_FIELD`s, override `DrawSelf`, include the `.gen.hpp`; add it to `UIControlsTest`'s round trip.

## Variables and strings

- **`UIVars`** (`CoreManager::GetUIVars().Set("score"_intern, 120)`): name → text, a version that never goes back; `{name}` in a label is replaced, an unset one is empty, `{{` / `}}` are literal braces.
- **`UIStrings`** (`config/UIStrings.xml`: `<UIStrings default="en"><language id="en"><string key="menu.start" text="Start"/>...`): `{#key}` resolves in the current language, then the default; **a missing key shows as the key** and is recorded once in `GetMissing()` (the label logs it). Strings resolve first, variables second, so a translation may hold `{score}`; a literal `{#x}` is written `{{#x}}`. `SetLanguage(id)` (false and unchanged for an unknown id); the version moves on any change. Problems of the file are warnings; the rest loads.
- **Cost:** a label re-expands only when its `text`, the `UIVars` version or (for a text with `{#`) the `UIStrings` version moved, and lays out again only when the result changed. A text with no `{` costs nothing.
- A language switch rewords every label at the next draw. The control API and ids stay language-free.

## Control API (`ui_tree`, `ui_click`)

On the canvases of the **active scene**, by the layout of the last draw (`step` a frame first). `ui_tree` gives every canvas (node, space, visibility, modal, input, scale, bounds, validator `issues`) and its elements (`type`, `name`, `visible`, `interactive`, `rect` in canvas units, `outputRect` in screenshot pixels, `text` with variables, and the control fields: a button's `action` / `value` / `selected` / `disabled`, a group's `bind` / `selectedValue`, a toggle's `checked`, a bar's / slider's `fraction` and `value`). Bound values are read when asked, so no draw is needed after `UIVars::Set`. `ui_click {name, canvas?, at?}` presses the middle (`at` 0..1 sets the horizontal point; the way to set a slider) of the first visible interactive element through `UIInput`, exactly as a user would, after checking it is topmost in its canvas; it answers the `action` / `value` of the click, or `not_found`, `not_clickable` (why), `no_scene`. A widget changes nothing by a click, so check with `ui_tree`, `ui_click`, `step`, `ui_tree`. Client names are looked up with `PoolString::Find`, never interned. The MCP server has both as tools (`tools/control/CLAUDE.md`).

## Editor preview

`ViewportWidget` has an aspect preset and a safe-area simulation; during its scene pass it holds `SetSafeAreaOverride` and `SetPixelDensityOverride(1)`, so canvases never see the window's metrics. Numbers: `UIPreview`.

## Trying the widgets live

No shipped scene of the engine uses the toggle, slider, progress bar or strings. Copy `kSettingsXml` (view 840 x 800, `Settings` made visible) into a scratch `config/SceneConfig.xml` and `kStringsXml` into `config/UIStrings.xml` of a game on your machine, run with `--control --headless --manual-clock` and drive it with `ui_tree`, `ui_click`, `step`, `screenshot`. Without game code the widgets only emit; set the variables by hand. Never commit the scratch files.

## Not done

Button sounds, a dropdown, focus and gamepad / keyboard navigation, text input, scrolling, clipping of children, a vertical or range slider, a drag over the control API, OS language detection, plural forms, hot reload of `.ui.xml` / `.ui.html`, the debug overlay, the scene editor's tree of canvases (stage 7). The look of the controls on the Vulkan backend (the colors are first guesses) is the owner's.

## Tests

`tests/BECore/UI*Test.cpp`: one file per class (`UILayout`, `UICanvasScaler`, `UIVariant`, `UICanvasLayout`, `UILayoutValidator`, `UICanvasComponent` (drawing per space, input order, pointer hooks), `UIControlApi`, `UIReflection`, `UIDocument`, `UIWeb`, `UIVars`, `UIStrings`, `UIControls` (every property round trip), `UISlider`, `UIProgressBar`, `UIToggle`, `UISegmented`, `UINineSlice`, `UIPreview`, `UISettings`) and the three screen templates above.
