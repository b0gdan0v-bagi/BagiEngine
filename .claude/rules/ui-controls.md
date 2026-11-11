---
paths:
  - "src/Modules/BECore/UI/UIPanel*"
  - "src/Modules/BECore/UI/UIImage*"
  - "src/Modules/BECore/UI/UILabel*"
  - "src/Modules/BECore/UI/UIButton*"
  - "src/Modules/BECore/UI/UIComponent*"
  - "src/Modules/BECore/UI/UIImageComponent*"
  - "src/Modules/BECore/UI/UITextComponent*"
  - "src/Modules/BECore/UI/UIInputClicker*"
  - "src/Modules/BECore/UI/UITooltipComponent*"
  - "src/Modules/BECore/UI/UIStateLook*"
  - "src/Modules/BECore/UI/UITextPainter*"
  - "src/Modules/BECore/UI/UIStack*"
  - "src/Modules/BECore/UI/UISegmented*"
  - "src/Modules/BECore/UI/UIToggle*"
  - "src/Modules/BECore/UI/UIDropdown*"
  - "src/Modules/BECore/UI/UIProgressBar*"
  - "src/Modules/BECore/UI/UISlider*"
  - "src/Modules/BECore/UI/UIVars*"
  - "src/Modules/BECore/UI/UIStrings*"
  - "src/Modules/BECore/UI/UIDocument*"
  - "src/Modules/BECore/UI/UIScreenContent*"
  - "src/Modules/BECore/UI/UIAttributeIO*"
  - "src/Modules/BECore/UI/UIValueText*"
  - "src/Modules/BECore/UI/UISprite*"
  - "src/Modules/BECore/UI/UINineSlice*"
  - "tests/BECore/UIControls*"
  - "tests/BECore/UIStrings*"
  - "tests/BECore/UIVars*"
  - "tests/BECore/UIDocument*"
  - "**/*.ui.xml"
---

# UI controls, screens in XML, variables and strings

The module's contract: `src/Modules/BECore/UI/CLAUDE.md`; layout and the screen test: `ui-layout.md`.

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
- Style attributes are flat (`anchor`, `anchorMin`, `anchorMax`, `pivot`, `x`, `y`, `width`, `height`, `min*`, `max*`, `aspectRatio`, `aspectMode`, `margin`, `flex`, `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign`, `fontSize`, `visible`). A length is `220`, `220px` or `90%`; margins / padding one number or `l,t,r,b`; `anchorMin` / `anchorMax` / `pivot` two numbers `x,y` (`0.65,0` or `65%,0`; a malformed one is reported and leaves the shorthand's); a color `r,g,b,a` or `#RRGGBB[AA]`. The reflected fields are `visible` and `inputEnabled`, not `enabled` (taken by factory vectors).
- Edge anchors: a side panel is `<element type="UIPanel" name="Side" anchorMin="0.65,0" anchorMax="1,1" margin="8"/>` (no `x`, `width`), a bar pinned to the bottom edge and 80% wide is `anchorMin="10%,1" anchorMax="90%,1" pivot="0.5,1" y="-16" height="56"`; a portrait variant moves it with `<set element="Side" anchorMin="0,0.6"/>` (`ui-layout.md`, "Edge anchors").
- A variant's `when` is `name:value;...` (`orientation`, `minAspect`, `maxAspect`, `minWidth`, ...). An override can set `visible`, `anchor`, `anchorMin`, `anchorMax`, `pivot`, `x`, `y`, `width`, `height`, `margin`, `region`, `direction`, `fontSize`, and is written even when equal to the default.
- `source="assets/ui/hud.ui.xml"` loads a `UIDocument` (`<ui><elements/><variants/></ui>`); its elements come first, the inline ones on top; only the inline part is saved back.
- Attribute names of style and control fields share one `<element>` and must not clash. Do not save UI with a skip-defaults serializer (fields whose default is not `T{}` would be lost). Factory fields are declared `IntrusivePtrNonAtomic<UIElement>`: the generator finds them by that name.

## Controls

Each derives from `UIElement`, draws itself in `DrawSelf` in canvas units; `color` is a fill behind the content. Sprites come from `TextureLibrary` by name.

| Type | Fields | Behavior |
|------|--------|----------|
| `UIElement` | `cornerRadius`, `border*`, `gradient*`, `shadow`, `glow`, `highlight` | a box (flat unless set: `ui-shapes.md`) and children |
| `UIPanel` | `sprite`, `sliceBorder`, `spriteTint` | fill then sprite, nine-sliced with `sliceBorder`; `blocksInput` by default |
| `UIImage` | `sprite`, `tint`, `stretch` | sprite fitted, centered, proportional |
| `UILabel` | `text`, `font` (a file path, wins), `fontFamily` + `fontWeight` (a face of the `families` table of `TextConfig.xml`; the layout check measures with it), `textColor`, `align`, `verticalAlign`, `overflow` (`Ellipsis` default, `Clip`, `AutoFit`), `wrap`, `lineSpacing`, `minFontSize`, `textOutline` (`width,color`), `textShadow` (`offsetX,offsetY,color`) | text at `fontSize` (24 when 0) x canvas scale in whole pixels. The outline and the hard shadow are UI units x scale, paint only (never measured, cut by `Clip`); every control that draws its caption through `UILabel::DrawText` has them, faded with a disabled caption |
| `UIButton` | (the form `ui-button-composition` replaces by the components below; still used by the games, segments, dropdown options) `action`, `value`, `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`, `disabled`, `icon`, `iconTint`, `iconPlacement`, `iconSize`, `tooltip` | a click emits `ActionEvent(action, value)`; state colors derived when transparent; `selected` adds a bar so selection is not colour alone; disabled still consumes the click; the states shift the box too. `icon` (a sprite name) alone in the rect, or `Left` / `Above` the caption (`GetParts`; `CheckContent` measures the caption in what the icon leaves); tinted by `iconTint` or `textColor`, shifted by hover / press / disabled (`GetCurrentIconColor`); the rect stays the touch target. `tooltip` is held here and shown by the canvas |
| `UIStack` | — | a stack container |
| `UISegmented` | `bind`, `disabled` | radio group over direct `UIButton` children: the segment whose `value` equals the variable is `selected`. **Every segment needs an explicit `height`**, also with `flex` (else it is a 0-high rect: not drawn, not clickable, skipped by the validator) |
| `UIToggle` | `bind`, `action`, `onValue`, `offValue`, `disabled`, track / fill / thumb colors, `partShape` | a caption with a switch at the right; on when the variable equals `onValue`; a click emits the opposite value. `partShape="Round"`: pill track, round thumb; its own plate is the `box` of any element |
| `UIDropdown` | `bind`, `action`, `disabled`, `optionHeight`, `arrowColor` | a `UILabel` header (the box of any element, with the shifts of a button) over direct `UIButton` options (a popup): the header shows the `text` of the option whose `value` equals the variable (else its own `text`); a click on an option emits `ActionEvent(action, option value)` and closes the list. **Every option needs a height**: its own, else `optionHeight`, else the header's. The list opens under the header, above when it does not fit, else shifted into `contentBounds`; no scrolling |
| `UIProgressBar` | `bind`, `minValue`, `maxValue`, track / fill colors, `partShape` | read-only fraction of the width; anything that is not a number in range is 0; takes no input. `partShape="Round"`: pill track and fill |
| `UISlider` | `bind`, `action`, `minValue`, `maxValue`, `step`, `disabled`, colors, `partShape` | horizontal; press and drag emit the value text under the pointer (only when the text changes), snapped to `step`. The thumb is as high as the element, so height decides the touch check. `partShape="Round"`: pill track and fill, round thumb |

- **A tooltip** is the `tooltip` text of a `UIButton` or a `UITooltipComponent`, shown by its canvas (`UITooltip`, not an element of the tree): a mouse resting on the button `kHoverDelay`, or a finger held on it `kLongPress`, which is then no click; it may hold `{#key}` and `{var}`. Give an icon-only button one so it says what it does.
- **A choice panel**: one `UISegmented` per choice in a modal canvas started hidden (`visible="false" modal="true"`): a dim, a plate, a row per choice, an OK button; a HUD button opens it with `SetVisibleByName`. A hidden canvas is not drawn, so its groups read the variables on the first draw after it is shown. Five values and up, or long words: a `UIDropdown`.
- **Adding a control:** derive from `UIElement` (or `UILabel`), `BE_CLASS` with `BE_REFLECT_FIELD`s, override `DrawSelf`, include the `.gen.hpp`; add it to `UIControlsTest`'s round trip.
- **Trying the widgets live:** no shipped scene uses the toggle, slider, progress bar or strings: copy `kSettingsXml` and `kStringsXml` of the tests into scratch `config/` files, run `--control --headless --manual-clock`, drive with `ui_tree`, `ui_click`, `step`, `screenshot`.

## Components

A button is no class but a set of components on one `UIElement` (`<components><component type="..."/></components>`, `UIElement::components`, factory base `UIComponent`). Icon-only, captioned and icon-left buttons are all this; `UIButton` is the older form.

| Component | Fields | Does |
|-----------|--------|------|
| `UIImageComponent` | `sprite`, `tint`, `hoverTint`, `pressedTint`, `disabledTint`, `stretch` | the sprite fitted and centred in the owner's rect (`UISprite::FitInto`); a transparent state tint is derived (`UIStateLook::ResolveTint`) |
| `UITextComponent` | the text fields of `UILabel` under the same names, `hoverTextColor`, `pressedTextColor`, `disabledTextColor`, `selectedTextColor` | the text in the owner's rect, at the owner's `fontSize`; a transparent state colour leaves `textColor`; measured by `CheckContent` |
| `UIInputClicker` | `action`, `value`, `disabled` | a click emits `ActionEvent(action, value)`; makes the element `IsInteractive()` (hit target, touch size, `ui_click`) and gives its box the state look; a disabled one still takes the click |
| `UITooltipComponent` | `text` | the tooltip of a hit-testable element (a clicker or `blocksInput`), not only of a clicker |

- **One component of each type per element:** `GetComponent<T>()` is the first, `AddComponent<T>()` returns the existing one, a second in a file is logged and dropped.
- **Order:** the element draws its box, then its components in list order, then its children. `OnClick` / `OnPress` / `OnDrag` and `CheckContent` reach the components with the owner as the argument; a subclass that overrides them is unchanged.
- **State** lives on the element: `hovered`, `pressed`, `selected`, `groupDisabled`, `IsDisabled()` (`groupDisabled` or the clicker's `disabled`) and the fills `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`; the box takes the state look only when a component says `GivesStateLook()` (the clicker), so a `blocksInput` panel does not light up. Image and text have per-state variants of their own.
- **Icon and caption together** are two children of a stack (an image element and a text element), the clicker on the stack: the target is the rect of the stack, never a picture. `ui_tree` and `ui_click` answer a composed element as a button (`ui-control-api.md`).

```xml
<element type="UIStack" name="Play" direction="Horizontal" spacing="8" crossAlign="Center" width="200" height="48" color="40,60,90,255" cornerRadius="8">
  <components><component type="UIInputClicker" action="Play"/><component type="UITooltipComponent" text="{#tip.play}"/></components>
  <children>
    <element type="UIElement" name="PlayIcon" width="32" height="32"><components><component type="UIImageComponent" sprite="icons.play"/></components></element>
    <element type="UIElement" name="PlayText" width="100" height="32"><components><component type="UITextComponent" text="{#menu.play}" verticalAlign="Center"/></components></element>
  </children>
</element>
```


## Variables and strings

- **`UIVars`** (`CoreManager::GetUIVars().Set("score"_intern, 120)`): name → text, a version that never goes back; `{name}` in a label is replaced, an unset one is empty, `{{` / `}}` are literal braces.
- **`UIStrings`** (`config/UIStrings.xml`: `<UIStrings default="en"><language id="en"><string key="menu.start" text="Start"/>...`): `{#key}` resolves in the current language, then the default; **a missing key shows as the key** and is recorded once in `GetMissing()`. Strings resolve first, variables second, so a translation may hold `{score}`; a literal `{#x}` is `{{#x}}`. `SetLanguage(id)` is false and changes nothing for an unknown id. Problems of the file are warnings; the rest loads.
- **Cost:** a label re-expands only when its `text`, the `UIVars` version or (for a text with `{#`) the `UIStrings` version moved, and lays out again only when the result changed. A language switch rewords every label at the next draw; the control API and ids stay language-free.
