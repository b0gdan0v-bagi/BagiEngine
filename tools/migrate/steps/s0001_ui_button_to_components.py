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
CAPTION_ATTRIBUTE = "fontSize"  # on the button of a plain composed one, on the caption child of an icon-left one
ICON_ATTRIBUTES = ("icon", "iconTint", "iconSize", "iconPlacement")
DROPPED_ATTRIBUTES = ("interactive",)
# The forms of an icon (`UIButton::kIconOnlyShare`, `kIconWithTextShare`, `kIconPadding`, `kIconGap`; the tests hold them to the header)
ICON_ONLY_SHARE = "60%"
ICON_WITH_TEXT_SHARE = "50%"
ICON_PADDING = "8"
ICON_GAP = "6"
# What the button lays out by itself when it has an icon: a stack of its own children
STACK_ATTRIBUTES = ("layout", "direction", "spacing", "padding", "crossAlign", "wrapChildren")
PLACEMENT_ABOVE = "Above"
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


def _has_text(button: xmltext.Element) -> bool:
    return bool(button.get("text"))


def _icon_left(button: xmltext.Element) -> bool:
    return bool(button.get("icon")) and _has_text(button)


def _moved_overrides(button: xmltext.Element, name: str, doc: xmltext.Document):
    """The attributes that moved off the button and that a `<set>` of its canvas names for it."""
    moved = set(TEXT_ATTRIBUTES) | set(CLICKER_ATTRIBUTES) | {TOOLTIP_ATTRIBUTE} | set(ICON_ATTRIBUTES)
    if _icon_left(button):
        moved.add(CAPTION_ATTRIBUTE)
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
        if _has_text(button) and button.get("iconPlacement") == PLACEMENT_ABOVE:
            return f'{label} has iconPlacement="{PLACEMENT_ABOVE}": a vertical stack cannot give the icon a share of the height with a top padding (the owner decides): write it by hand'
        own = [n for n in STACK_ATTRIBUTES if button.has(n)]
        if button.child("children") is not None or own:
            what = "<children>" if button.child("children") is not None else ", ".join(own)
            return f"{label} has an icon and {what}: the form of an icon is a stack of its own children: write it by hand"
    moved = _moved_overrides(button, name, doc) if name else []
    if moved:
        return f'{label} is composed, and a variant sets {", ".join(sorted(set(moved)))} on it: after the move that override names an attribute the element no longer has'
    return None


def _icon_child(button: xmltext.Element, name: str, caption: bool) -> str:
    """The fragment of the child that holds the icon: `<N>Icon`, the image of the forms in the module docstring."""
    tint = None
    for attribute in ("iconTint", "textColor"):
        if button.get(attribute):
            tint = _text_of(button, attribute)
            break
    size = _text_of(button, "iconSize") if button.get("iconSize") else None
    if size is not None:
        sides = [("width", size), ("height", size)]
    elif caption:
        sides = [("height", ICON_WITH_TEXT_SHARE), ("aspectRatio", "1")]
    else:
        sides = [("width", ICON_ONLY_SHARE), ("height", ICON_ONLY_SHARE), ("aspectRatio", "1")]
    pairs = ([("name", f"{name}Icon")] if name else []) + ([] if caption else [("anchor", "Center")]) + sides
    if size is not None and not caption:
        pairs += [("maxWidth", "100%"), ("maxHeight", "100%")]
    image = [("sprite", _text_of(button, "icon"))] + ([("tint", tint)] if tint is not None else [])
    return f'<element type="{COMPOSED_TYPE}"{_attributes(pairs)}><components>{_component("UIImageComponent", image)}</components></element>'


def _caption_child(button: xmltext.Element, name: str, text_pairs) -> str:
    pairs = ([("name", f"{name}Caption")] if name else []) + [("flex", "1"), ("height", "100%")]
    if button.has(CAPTION_ATTRIBUTE):
        pairs.append((CAPTION_ATTRIBUTE, _text_of(button, CAPTION_ATTRIBUTE)))
    return f'<element type="{COMPOSED_TYPE}"{_attributes(pairs)}><components>{_component("UITextComponent", text_pairs)}</components></element>'


def _compose(button: xmltext.Element, name: str) -> str:
    """Rewrite one button in place; returns what it became, for the note."""
    # one fixed order of the attributes of a component, whatever order the button had them in
    text_pairs = [(n, _text_of(button, n)) for n in TEXT_ATTRIBUTES if button.has(n)]
    clicker_pairs = [(n, _text_of(button, n)) for n in CLICKER_ATTRIBUTES if button.has(n)]
    tooltip = _text_of(button, TOOLTIP_ATTRIBUTE) if button.has(TOOLTIP_ATTRIBUTE) else None
    if text_pairs:
        defaults = dict(BUTTON_TEXT_DEFAULTS)
        text_pairs = [(n, _text_of(button, n) if button.has(n) else defaults[n]) for n in TEXT_ATTRIBUTES if button.has(n) or n in defaults]

    has_icon = bool(button.get("icon"))
    icon_left = _icon_left(button)
    children = []
    components = []
    if has_icon:
        children.append(_icon_child(button, name, icon_left))
        if icon_left:
            children.append(_caption_child(button, name, text_pairs))
        # The text attributes went to the caption child, or mean nothing without a caption, except the colour: it is the colour of the bar of a selected button, which the element
        # reads from its own text component or from the one of a child
        text_pairs = [("textColor", _text_of(button, "textColor"))] if not icon_left and button.get("textColor") else []
    if text_pairs:
        components.append(("UITextComponent", text_pairs))
    components.append(("UIInputClicker", clicker_pairs))
    if tooltip:
        components.append(("UITooltipComponent", [("text", tooltip)]))

    moved = set(TEXT_ATTRIBUTES) | set(CLICKER_ATTRIBUTES) | {TOOLTIP_ATTRIBUTE} | set(ICON_ATTRIBUTES) | set(DROPPED_ATTRIBUTES)
    if icon_left:
        moved.add(CAPTION_ATTRIBUTE)
    button.set("type", COMPOSED_TYPE)
    for attribute in [n for n in button.attribute_names() if n in moved]:
        button.remove(attribute)
    if icon_left:
        for attribute, value in (("layout", "Stack"), ("direction", "Horizontal"), ("padding", f"{ICON_PADDING},0,0,0"), ("spacing", ICON_GAP), ("crossAlign", "Center")):
            button.set(attribute, value)

    holder = button.add_child("<components/>", 0)
    for type_name, pairs in components:
        holder.add_child(_component(type_name, pairs))
    if children:
        container = button.add_child("<children/>")
        for fragment in children:
            container.add_child(fragment)
    return " + ".join(["UIElement"] + [type_name for type_name, _ in components] + (["icon child"] if has_icon else []) + (["caption child"] if icon_left else []))


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
