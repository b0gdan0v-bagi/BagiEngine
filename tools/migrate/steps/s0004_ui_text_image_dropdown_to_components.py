"""Step 4 to 5: a `UILabel` and a `UIImage` become composed elements and the header text of a `UIDropdown` a `UITextComponent`, like the controls before them.

    <element type="UILabel" name="Score" x="96" y="12" text="Score: {score}" fontSize="28" verticalAlign="Center" color="0,0,0,80"/>
becomes
    <element type="UIElement" name="Score" x="96" y="12" fontSize="28" color="0,0,0,80">
      <components><component type="UITextComponent" text="Score: {score}" verticalAlign="Center"/></components>
    </element>

    <element type="UIImage" name="Icon" sprite="icons.star" tint="255,200,0,255" stretch="true" width="24" height="24"/>
becomes
    <element type="UIElement" name="Icon" width="24" height="24">
      <components><component type="UIImageComponent" sprite="icons.star" tint="255,200,0,255" stretch="true"/></components>
    </element>

    <element type="UIDropdown" name="Level" bind="level" action="SetLevel" text="Pick one" fontSize="22" overflow="AutoFit" color="70,70,100,255">...</element>
becomes
    <element type="UIDropdown" name="Level" bind="level" action="SetLevel" fontSize="22" color="70,70,100,255">
      <components><component type="UITextComponent" text="Pick one" verticalAlign="Center" overflow="AutoFit" wrap="false" fadeWhenDisabled="true"/></components>
      ...
    </element>

The text attributes (`text`, `font`, `fontFamily`, `fontWeight`, `textColor`, `align`, `verticalAlign`, `overflow`, `wrap`, `lineSpacing`, `minFontSize`, `textOutline`, `textShadow`)
and the picture attributes (`sprite`, `tint`, `stretch`) go to the component with the values as written; an attribute the file does not name is not written (the class and the component
have the same defaults). The component is always made, even without an attribute: code sets the text of a label later. Everything else (`name`, size, `color`, `box`, `fontSize`,
`visible`, `flex`, `bind`, `action`, `arrowColor`, `<children>` ...) stays on the element, in its place. A dropdown shows the chosen option's caption and its `text` is the placeholder; its
constructor set `verticalAlign="Center"` and `wrap="false"` and faded the header while disabled, which the component says (`fadeWhenDisabled="true"`) unless the file names the attribute.

It matches `element type="UILabel"`, `"UIImage"` and `"UIDropdown"` at any depth. A dropdown that has a `UITextComponent` already is not matched again (the step is idempotent).

A `UIImage` with an ancestor that has a `UIInputClicker` in the same file (a tile picture on a claim button) is given `hoverTint`, `pressedTint` and `disabledTint` equal to its `tint`
(`255,255,255,255` when it names none; a theme token is copied as written): a picture component follows the state of the clicker above it and would derive a lighter, darker or
half transparent tint, the class never did, and a tint that is given replaces the derived one.

Refused, with the reason (the file stays as it was): `<components>` already (other than a dropdown's `UITextComponent`), and a variant `<set element="NAME" .../>` of the same canvas
that sets an attribute that moved on a migrated element NAME (after the move it names an attribute the element no longer has). The mapping is `_ui_text.py`.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # the steps are loaded by path: the shared modules next to them are found through the folder

import _ui_text as shared  # noqa: E402
from migrate import step  # noqa: E402


@step(4, "ui-text-image-dropdown-to-components")
def run(doc, ctx):
    elements = doc.find_all(lambda e: e.name == "element" and e.get("type") in (shared.LABEL_TYPE, shared.IMAGE_TYPE, shared.DROPDOWN_TYPE))
    shared.compose_texts(doc, ctx, elements)
