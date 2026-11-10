---
paths:
  - "src/Modules/BECore/UI/UIPanel*"
  - "src/Modules/BECore/UI/UIImage*"
  - "src/Modules/BECore/UI/UILabel*"
  - "src/Modules/BECore/UI/UIButton*"
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
- Style attributes are flat (`anchor`, `x`, `y`, `width`, `height`, `min*`, `max*`, `aspectRatio`, `aspectMode`, `margin`, `flex`, `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign`, `fontSize`, `visible`). A length is `220`, `220px` or `90%`; margins / padding one number or `l,t,r,b`; a color `r,g,b,a` or `#RRGGBB[AA]`. The reflected fields are `visible` and `inputEnabled`, not `enabled` (taken by factory vectors).
- A variant's `when` is `name:value;...` (`orientation`, `minAspect`, `maxAspect`, `minWidth`, ...). An override is written even when equal to the default.
- `source="assets/ui/hud.ui.xml"` loads a `UIDocument` (`<ui><elements/><variants/></ui>`); its elements come first, the inline ones on top; only the inline part is saved back.
- Attribute names of style and control fields share one `<element>` and must not clash. Do not save UI with a skip-defaults serializer (fields whose default is not `T{}` would be lost). Factory fields are declared `IntrusivePtrNonAtomic<UIElement>`: the generator finds them by that name.

## Controls

Each derives from `UIElement`, draws itself in `DrawSelf` in canvas units; `color` is a fill behind the content. Sprites come from `TextureLibrary` by name.

| Type | Fields | Behavior |
|------|--------|----------|
| `UIElement` | `cornerRadius`, `border*`, `gradient*`, `shadow`, `glow`, `highlight` | a box (flat unless set: `ui-shapes.md`) and children |
| `UIPanel` | `sprite`, `sliceBorder`, `spriteTint` | fill then sprite, nine-sliced with `sliceBorder`; `blocksInput` by default |
| `UIImage` | `sprite`, `tint`, `stretch` | sprite fitted, centered, proportional |
| `UILabel` | `text`, `font`, `textColor`, `align`, `verticalAlign`, `overflow` (`Ellipsis` default, `Clip`, `AutoFit`), `wrap`, `lineSpacing`, `minFontSize` | text at `fontSize` (24 when 0) x canvas scale in whole pixels |
| `UIButton` | `action`, `value`, `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`, `disabled` | a click emits `ActionEvent(action, value)`; state colors derived when transparent; `selected` adds a bar so selection is not colour alone; disabled still consumes the click; the states shift the box too |
| `UIStack` | — | a stack container |
| `UISegmented` | `bind`, `disabled` | radio group over direct `UIButton` children: the segment whose `value` equals the variable is `selected`. **Every segment needs an explicit `height`**, also with `flex` (else it is a 0-high rect: not drawn, not clickable, skipped by the validator) |
| `UIToggle` | `bind`, `action`, `onValue`, `offValue`, `disabled`, track / fill / thumb colors | a caption with a switch at the right; on when the variable equals `onValue`; a click emits the opposite value |
| `UIDropdown` | `bind`, `action`, `disabled`, `optionHeight`, `arrowColor` | a `UILabel` header over direct `UIButton` options (a popup): the header shows the `text` of the option whose `value` equals the variable (else its own `text`); a click on an option emits `ActionEvent(action, option value)` and closes the list. **Every option needs a height**: its own, else `optionHeight`, else the header's. The list opens under the header, above when it does not fit, else shifted into `contentBounds`; no scrolling |
| `UIProgressBar` | `bind`, `minValue`, `maxValue`, track / fill colors | read-only fraction of the width; anything that is not a number in range is 0; takes no input |
| `UISlider` | `bind`, `action`, `minValue`, `maxValue`, `step`, `disabled`, colors | horizontal; press and drag emit the value text under the pointer (only when the text changes), snapped to `step`. The thumb is as high as the element, so height decides the touch check |

- **A choice panel**: one `UISegmented` per choice in a modal canvas started hidden (`visible="false" modal="true"`): a dim, a plate, a row per choice, an OK button; a HUD button opens it with `SetVisibleByName`. A hidden canvas is not drawn, so its groups read the variables on the first draw after it is shown. Five values and up, or long words: a `UIDropdown`.
- **Adding a control:** derive from `UIElement` (or `UILabel`), `BE_CLASS` with `BE_REFLECT_FIELD`s, override `DrawSelf`, include the `.gen.hpp`; add it to `UIControlsTest`'s round trip.
- **Trying the widgets live:** no shipped scene uses the toggle, slider, progress bar or strings: copy `kSettingsXml` and `kStringsXml` of the tests into scratch `config/` files, run `--control --headless --manual-clock`, drive with `ui_tree`, `ui_click`, `step`, `screenshot`.

## Variables and strings

- **`UIVars`** (`CoreManager::GetUIVars().Set("score"_intern, 120)`): name → text, a version that never goes back; `{name}` in a label is replaced, an unset one is empty, `{{` / `}}` are literal braces.
- **`UIStrings`** (`config/UIStrings.xml`: `<UIStrings default="en"><language id="en"><string key="menu.start" text="Start"/>...`): `{#key}` resolves in the current language, then the default; **a missing key shows as the key** and is recorded once in `GetMissing()`. Strings resolve first, variables second, so a translation may hold `{score}`; a literal `{#x}` is `{{#x}}`. `SetLanguage(id)` is false and changes nothing for an unknown id. Problems of the file are warnings; the rest loads.
- **Cost:** a label re-expands only when its `text`, the `UIVars` version or (for a text with `{#`) the `UIStrings` version moved, and lays out again only when the result changed. A language switch rewords every label at the next draw; the control API and ids stay language-free.
