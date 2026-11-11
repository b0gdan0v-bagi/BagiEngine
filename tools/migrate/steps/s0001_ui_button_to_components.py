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
child of a `UISegmented` or a `UIDropdown`, which look for their segments and options by that class (`Cast<UIButton>`) and would not find a composed one: those go
with the step that composes the group.

What it refuses (the file is left as it was and the reason names the element):
- a button with an `icon`, with or without a caption. The icon of a button follows its hover, press and disabled state, and in a composed element only the element with
  the clicker is hovered or pressed: an image in a child element would keep its colour in every state, and the twin test (`ContentMigrationButtonTest`) shows it. An
  icon-only button can be written by hand as an image component on the element itself (it fits the whole rect); the form of an icon with a caption is a stack, and
  `ui-button-composition` designs both before this step takes them;
- `interactive="false"`: a composed button is always a target;
- a button that already has `<components>`;
- a variant `<set element="NAME" .../>` of the same canvas that sets an attribute that moved (a text, a clicker or a tooltip attribute) on a migrated button
  NAME: after the move the override names an attribute the element no longer has (layout and style attributes stay valid).
"""

import xmltext
from migrate import step

BUTTON_TYPE = "UIButton"
COMPOSED_TYPE = "UIElement"
GROUP_TYPES = ("UISegmented", "UIDropdown")
CANVAS_TYPE = "UICanvasComponent"

# The fields of UILabel that UITextComponent has under the same names; `textEffects` is the two attributes `textOutline` and `textShadow`
TEXT_ATTRIBUTES = (
    "text",
    "font",
    "fontFamily",
    "fontWeight",
    "textColor",
    "align",
    "verticalAlign",
    "overflow",
    "wrap",
    "lineSpacing",
    "minFontSize",
    "textOutline",
    "textShadow",
)
CLICKER_ATTRIBUTES = ("action", "value", "disabled")
TOOLTIP_ATTRIBUTE = "tooltip"
ICON_ATTRIBUTES = ("icon", "iconTint", "iconSize", "iconPlacement")
DROPPED_ATTRIBUTES = ("interactive",)
# What the constructor of UIButton sets and a UITextComponent does not
BUTTON_TEXT_DEFAULTS = (("align", "Center"), ("verticalAlign", "Center"), ("wrap", "false"))

_FALSE = ("false", "0")


def _text_of(element: xmltext.Element, name: str) -> str:
    """The value of an attribute as it can be written between double quotes: as it stands in the file when it can be, else escaped."""
    raw = element.get_raw(name)
    if raw is not None and '"' not in raw:
        return raw
    return xmltext.escape_attribute(element.get(name) or "")


def _attributes(pairs) -> str:
    return "".join(f' {name}="{value}"' for name, value in pairs)


def _component(type_name: str, pairs) -> str:
    return f'<component type="{type_name}"{_attributes(pairs)}/>'


def _group_segment(button: xmltext.Element) -> bool:
    holder = button.parent
    owner = holder.parent if holder is not None and holder.name == "children" else None
    return owner is not None and owner.name == "element" and owner.get("type") in GROUP_TYPES


def _canvas_of(button: xmltext.Element, doc: xmltext.Document) -> xmltext.Element:
    node = button.parent
    while node is not None:
        if node.name == "component" and node.get("type") == CANVAS_TYPE:
            return node
        node = node.parent
    return doc.root


def _moved_overrides(button: xmltext.Element, name: str, doc: xmltext.Document):
    """The attributes that moved off the button and that a `<set>` of its canvas names for it."""
    moved = set(TEXT_ATTRIBUTES) | set(CLICKER_ATTRIBUTES) | {TOOLTIP_ATTRIBUTE} | set(ICON_ATTRIBUTES)
    found = []
    for override in _canvas_of(button, doc).find_all(lambda e: e.name == "set" and e.get("element") == name):
        found.extend(a for a in override.attribute_names() if a in moved)
    return found


def _refusal(button: xmltext.Element, name: str, doc: xmltext.Document):
    label = f'button "{name}"' if name else "a button without a name"
    if button.child("components") is not None:
        return f"{label} has <components> already: write the composed form by hand"
    interactive = button.get("interactive")
    if interactive is not None and interactive.strip().lower() in _FALSE:
        return f'{label} has interactive="{interactive}": a composed button is always a hit target'
    if button.get("icon"):
        return f'{label} has icon="{button.get("icon")}": a button with an icon has no automatic form yet (an image in a child element would not follow the hover, press and disabled state): write it by hand'
    moved = _moved_overrides(button, name, doc) if name else []
    if moved:
        return f'{label} is composed, and a variant sets {", ".join(sorted(set(moved)))} on it: after the move that override names an attribute the element no longer has'
    return None


def _compose(button: xmltext.Element, name: str) -> str:
    """Rewrite one button in place; returns what it became, for the note."""
    # one fixed order of the attributes of a component, whatever order the button had them in
    text_pairs = [(n, _text_of(button, n)) for n in TEXT_ATTRIBUTES if button.has(n)]
    clicker_pairs = [(n, _text_of(button, n)) for n in CLICKER_ATTRIBUTES if button.has(n)]
    tooltip = _text_of(button, TOOLTIP_ATTRIBUTE) if button.has(TOOLTIP_ATTRIBUTE) else None
    if text_pairs:
        defaults = dict(BUTTON_TEXT_DEFAULTS)
        text_pairs = [(n, _text_of(button, n) if button.has(n) else defaults[n]) for n in TEXT_ATTRIBUTES if button.has(n) or n in defaults]

    components = []
    if text_pairs:
        components.append(("UITextComponent", text_pairs))
    components.append(("UIInputClicker", clicker_pairs))
    if tooltip:
        components.append(("UITooltipComponent", [("text", tooltip)]))

    moved = set(TEXT_ATTRIBUTES) | set(CLICKER_ATTRIBUTES) | {TOOLTIP_ATTRIBUTE} | set(ICON_ATTRIBUTES) | set(DROPPED_ATTRIBUTES)
    button.set("type", COMPOSED_TYPE)
    for attribute in [n for n in button.attribute_names() if n in moved]:
        button.remove(attribute)

    holder = button.add_child("<components/>", 0)
    for type_name, pairs in components:
        holder.add_child(_component(type_name, pairs))
    return " + ".join(["UIElement"] + [type_name for type_name, _ in components])


@step(1, "ui-button-to-components")
def run(doc, ctx):
    buttons = doc.find_all(lambda e: e.name == "element" and e.get("type") == BUTTON_TYPE and not _group_segment(e))
    for button in buttons:
        name = button.get("name") or ""
        reason = _refusal(button, name, doc)
        if reason:
            ctx.fail(button, reason)
            continue
        ctx.report(button, f"{name or '(no name)'}: {_compose(button, name)}")
