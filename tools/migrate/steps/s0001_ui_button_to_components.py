"""Step 1 to 2: a `UIButton` becomes a composed element, a `UIElement` with the components of a button.

    <element type="UIButton" name="Play" text="Play" action="Go" color="..."/>
becomes
    <element type="UIElement" name="Play" color="...">
      <components>
        <component type="UITextComponent" text="Play" align="Center" verticalAlign="Center" wrap="false"/>
        <component type="UIInputClicker" action="Go"/>
      </components>
    </element>

What moves, by the fields of `UIButton` and `UILabel`:
- the text fields (`TEXT_ATTRIBUTES`) go to a `UITextComponent`, made when the button has any of them. A `UIButton` centres its text and does not wrap, a
  `UITextComponent` is a `UILabel` (left, top, wrapping): the step writes `align="Center" verticalAlign="Center" wrap="false"` unless the file says otherwise.
- `action`, `value` and `disabled` go to a `UIInputClicker`, always made: a button is a hit target even without an action, and the clicker is what makes the
  element one. `interactive` is dropped for the same reason.
- `tooltip` becomes a `UITooltipComponent`. `iconTint`, `iconSize` and `iconPlacement` of a button with no `icon` mean nothing and are dropped.
- Everything else (name, layout, `fontSize`, the colours of the fill and of its states, `box`, `blocksInput`, `<children>`) stays on the element, in its place.

What it leaves alone: anything that is not a `UIButton` (an element already composed is not matched again: the step is idempotent) and a `UIButton` that is a direct
child of a `UISegmented` or a `UIDropdown`: those go with step 2 (`s0002_ui_group_children_to_components.py`), which maps them the same way.
The mapping itself is in `_ui_button.py`, shared with step 2.

A button with an `icon` becomes one of two forms (the icon is an image in a child element, and a child follows the hover, press and disabled state of the button it is
in). `<N>` is the button's `name`; a button without one has children without. The element keeps its box, colours and style and loses `icon`, `iconTint`, `iconSize`,
`iconPlacement`, `text` and the other text attributes. The tint `T` of the image is `iconTint`, else `textColor` as written (a token stays a token), else nothing (white).
- Icon only (no `text`): one child `<N>Icon`, `UIImageComponent sprite=icon tint=T`, `anchor="Center" width="60%" height="60%" aspectRatio="1"` (the square that fits:
  0.6 of the shorter side, `UIButton::kIconOnlyShare`); with `iconSize` S `width="S" height="S" maxWidth="100%" maxHeight="100%"`. A `textColor` of the button stays on
  the element as the colour of a `UITextComponent` with no text, so that the bar of a selected button keeps its colour (the element takes the accent from it).
- Icon left of a caption (`text`, `iconPlacement` absent or `Left`): the button becomes a horizontal stack (`padding` `kIconPadding`, `spacing` `kIconGap`, `crossAlign`
  Center) of `<N>Icon` (`height="50%" aspectRatio="1"`, or `width="S" height="S"`) and `<N>Caption` (`flex="1" height="100%"`, the `UITextComponent`, the `fontSize` of the
  button, the centring defaults of a `UIButton`). A caption of an icon-left button reads its own `fontSize`, so an `iconSize` taller than the button is the one case where
  the stack and `UIButton` (which clamps the icon to the rect) differ.

What it refuses (the file is left as it was and the reason names the element):
- `iconPlacement="Above"` of a button with an icon and a caption: a vertical stack cannot give "half the height of the button, 8 units from the top" (the top padding
  changes the base of a percent); the owner decides;
- a button with an icon that has `<children>` or any of `layout`, `direction`, `spacing`, `padding`, `crossAlign`, `wrapChildren` of its own: the form is a stack of its
  own children;
- `interactive="false"`: a composed button is always a target;
- a button that already has `<components>`;
- a variant `<set element="NAME" .../>` of the same canvas that sets an attribute that moved (a text, a clicker, a tooltip or an icon attribute, and `fontSize` of an
  icon-left button: the caption is another element now) on a migrated button NAME: after the move the override names an attribute the element no longer has (layout and
  style attributes stay valid).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # the steps are loaded by path: the shared module next to them is found through the folder

import _ui_button as shared  # noqa: E402
from migrate import step  # noqa: E402


@step(1, "ui-button-to-components")
def run(doc, ctx):
    buttons = doc.find_all(lambda e: e.name == "element" and e.get("type") == shared.BUTTON_TYPE and not shared.is_group_child(e))
    shared.compose_buttons(doc, ctx, buttons)
