"""Step 3 to 4: a `UIToggle` and a `UISlider` become composed elements (`UIToggleComponent` + `UISwitchComponent`, `UISliderComponent`), like the buttons before them.

    <element type="UIToggle" name="Sound" bind="soundOn" action="SetSound" text="Sound" fontSize="22" width="300" height="56" color="70,70,100,255"/>
becomes
    <element type="UIElement" name="Sound" width="300" height="56" color="70,70,100,255" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
      <components>
        <component type="UIInputClicker"/>
        <component type="UIToggleComponent" bind="soundOn" action="SetSound"/>
      </components>
      <children>
        <element type="UIElement" name="SoundCaption" flex="1" height="100%" fontSize="22">
          <components><component type="UITextComponent" text="Sound" verticalAlign="Center" wrap="false"/></components>
        </element>
        <element type="UIElement" name="SoundSwitch" height="100%" aspectRatio="1.25">
          <components><component type="UISwitchComponent"/></components>
        </element>
      </children>
    </element>

    <element type="UISlider" name="Volume" bind="volume" action="SetVolume" minValue="0" maxValue="1" step="0.1" height="56"/>
becomes
    <element type="UIElement" name="Volume" height="56">
      <components>
        <component type="UIInputClicker" stateLook="false"/>
        <component type="UISliderComponent" bind="volume" action="SetVolume" minValue="0" maxValue="1" step="0.1"/>
      </components>
    </element>

The row of a toggle keeps the attributes of the element (`name`, size, `color`, `box`, `blocksInput`, `visible`, `flex` ...); `disabled` goes to the clicker, `bind`, `action`, `onValue` and
`offValue` to the toggle component, the text attributes (`text`, `font`, `textColor`, `align`, `overflow` ... and `verticalAlign="Center"`, `wrap="false"`, which `UIToggle` set in its
constructor) to the text component of the caption, `fontSize` to the caption element, `trackColor`, `fillColor`, `thumbColor` and `partShape` to the switch component. The class derived
the off track (its `textColor` at 0.35 alpha) and the thumb (its `textColor`) from its own text colour, which the switch child cannot see: when `textColor` is a literal other than white
the step writes `trackColor` and `thumbColor` (those the file gives stay). A slider's attributes go to its component and its `disabled` to the clicker, whose `stateLook="false"` keeps
the plate still as the class did.

Refused, with the reason (the file stays as it was): `<components>` or `<children>` already, `layout`, `direction`, `spacing`, `padding`, `crossAlign` or `wrapChildren` on a toggle
(its row becomes a stack), `interactive="false"`, a `<set>` of a variant that names an attribute that moved, a `textColor` that is no colour, and a `textColor` that is a theme token
without a `trackColor` (the alpha of a token cannot be written; with one the token goes to `thumbColor`).

It matches `element type="UIToggle"` and `element type="UISlider"` at any depth. The mapping is `_ui_value.py`.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # the steps are loaded by path: the shared module next to them is found through the folder

import _ui_value as shared  # noqa: E402
from migrate import step  # noqa: E402


@step(3, "ui-toggle-slider-to-components")
def run(doc, ctx):
    elements = doc.find_all(lambda e: e.name == "element" and e.get("type") in (shared.TOGGLE_TYPE, shared.SLIDER_TYPE))
    shared.compose_controls(doc, ctx, elements)
