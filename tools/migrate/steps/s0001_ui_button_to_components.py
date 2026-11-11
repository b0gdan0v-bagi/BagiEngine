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
- `tooltip` becomes a `UITooltipComponent`.
- An icon with no caption (`icon`, `iconTint`, `iconSize`) becomes a first child element centred in the button, as large as `UIButton::GetParts` makes the icon:
  the square of 60 % of the shorter side (`aspectRatio="1"` fits the largest square inside 60 % x 60 %), or `iconSize` kept inside the rect. `iconPlacement` only
  matters beside a caption, so it is dropped.
- Everything else (name, layout, `fontSize`, the colours of the fill and of its states, `box`, `blocksInput`, `<children>`) stays on the element, in its place.

What it leaves alone: anything that is not a `UIButton` (an element already composed is not matched again: the step is idempotent) and a `UIButton` that is a direct
child of a `UISegmented` or a `UIDropdown`, which look for their segments and options by that class (`Cast<UIButton>`) and would not find a composed one: those go
with the step that composes the group.

What it refuses (the file is left as it was and the reason names the element):
- a button with an icon and a caption: its stack form is not written yet, and a guess that looks the same is not the same button;
- `interactive="false"`: a composed button is always a target;
- a button that already has `<components>`;
- a variant `<set element="NAME" .../>` of the same canvas that sets an attribute that moved (a text, a clicker or a tooltip attribute) on a migrated button
  NAME: after the move the override names an attribute the element no longer has (layout and style attributes stay valid).
"""

import re

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
# The icon of a button with no caption is this share of the shorter side of the rect (`UIButton::kIconOnlyShare`)
ICON_ONLY_SHARE = "60%"

_HEX_COLOR = re.compile(r"#([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?\Z")
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


def _alpha(value: str):
    """The alpha of a colour as the engine reads it (`r,g,b,a` or `#RRGGBB[AA]`), None for a text it would not read."""
    value = value.strip()
    match = _HEX_COLOR.match(value)
    if match:
        return int(match.group(2), 16) if match.group(2) else 255
    parts = value.split(",")
    if len(parts) == 4 and all(p.strip().isdigit() for p in parts):
        return int(parts[3])
    return None


def _positive_number(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0.0
    return number if number > 0 else 0.0


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


def _icon_child(button: xmltext.Element, tint: str, name: str) -> str:
    size = _positive_number(button.get("iconSize"))
    if size > 0:
        # the side is `iconSize`, kept inside the rect (min of the three), square
        geometry = f'width="{_text_of(button, "iconSize")}" height="{_text_of(button, "iconSize")}" maxWidth="100%" maxHeight="100%"'
    else:
        geometry = f'width="{ICON_ONLY_SHARE}" height="{ICON_ONLY_SHARE}"'
    child_name = f' name="{xmltext.escape_attribute(name + "/Icon")}"' if name else ""
    image = _component("UIImageComponent", [("sprite", _text_of(button, "icon"))] + ([("tint", tint)] if tint else []))
    return f'<element type="{COMPOSED_TYPE}"{child_name} anchor="Center" {geometry} aspectRatio="1"><components>{image}</components></element>'


def _icon_tint(button: xmltext.Element) -> str:
    """The tint `UIButton::GetCurrentIconColor` starts from: `iconTint`, or `textColor` when `iconTint` has no alpha. Empty: the default (white) of both."""
    tint = button.get("iconTint")
    if tint is not None and (_alpha(tint) or 0) > 0:
        return _text_of(button, "iconTint")
    return _text_of(button, "textColor") if button.has("textColor") else ""


def _refusal(button: xmltext.Element, name: str, doc: xmltext.Document):
    label = f'button "{name}"' if name else "a button without a name"
    if button.child("components") is not None:
        return f"{label} has <components> already: write the composed form by hand"
    interactive = button.get("interactive")
    if interactive is not None and interactive.strip().lower() in _FALSE:
        return f'{label} has interactive="{interactive}": a composed button is always a hit target'
    has_icon = bool(button.get("icon"))
    if has_icon and button.get("text"):
        return f"{label} has an icon and a caption: a button with an icon and a caption has no automatic form yet: write the stack by hand"
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
    icon = _icon_child(button, _icon_tint(button), name) if button.get("icon") else None

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
    if icon:
        children = button.child("children") or button.add_child("<children/>")
        children.add_child(icon, 0)
    return " + ".join(["UIElement"] + [type_name for type_name, _ in components] + (["icon child"] if icon else []))


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
