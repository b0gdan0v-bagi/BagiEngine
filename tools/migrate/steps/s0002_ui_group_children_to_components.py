"""Step 2 to 3: a `UIButton` that is a segment of a `UISegmented` or an option of a `UIDropdown` becomes a composed element, like any button (step 1).

    <element type="UISegmented" name="Language" bind="language">
      <children>
        <element type="UIButton" name="LangEn" text="English" value="en" selectedColor="..."/>
becomes
        <element type="UIElement" name="LangEn" selectedColor="...">
          <components>
            <component type="UITextComponent" text="English" align="Center" verticalAlign="Center" wrap="false"/>
            <component type="UIInputClicker" value="en"/>
          </components>
        </element>

The groups find their children through `UIChoice` (a `UIButton` or an element with a `UIInputClicker`), so the composed child behaves as the `UIButton` did. What moves, what
stays and what is refused is step 1's (`_ui_button.py` holds the mapping): the icon forms (icon only, icon left of a caption), the tooltip, the clicker; `Above`, `<children>` or
layout attributes with an icon, `<components>` already, `interactive="false"` and a variant `<set>` of a moved attribute are refused with the reason. Two details of a group child:
- an option of a `UIDropdown` has no `action` (the dropdown emits it): its clicker has `value` (and `disabled`) only;
- `selected` is the group's runtime flag, not a file attribute; `selectedColor` stays an attribute of the element (the style of the chosen segment) and the bar takes its accent
  from the caption.

It matches only a `UIButton` whose `<children>` parent is a `UISegmented` or a `UIDropdown`: a button anywhere else is step 1's, and a file already at version 2 keeps it.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # the steps are loaded by path: the shared module next to them is found through the folder

import _ui_button as shared  # noqa: E402
from migrate import step  # noqa: E402


@step(2, "ui-group-children-to-components")
def run(doc, ctx):
    buttons = doc.find_all(lambda e: e.name == "element" and e.get("type") == shared.BUTTON_TYPE and shared.is_group_child(e))
    shared.compose_buttons(doc, ctx, buttons)
