"""The mapping of a `UIToggle` and a `UISlider` to composed elements (`s0003_ui_toggle_slider_to_components.py`). What it writes and refuses is the docstring of step 3.
A module whose name starts with `_` is not a step. The helpers that write a component are those of `_ui_button.py`."""

import re
import struct

import _ui_button as shared
import xmltext

TOGGLE_TYPE = "UIToggle"
SLIDER_TYPE = "UISlider"
COMPOSED_TYPE = shared.COMPOSED_TYPE

# The attributes of a toggle by the component that takes them (the order is the order they are written in)
TOGGLE_ATTRIBUTES = ("bind", "action", "onValue", "offValue")
SWITCH_ATTRIBUTES = ("trackColor", "fillColor", "thumbColor", "partShape")
SLIDER_ATTRIBUTES = ("bind", "action", "minValue", "maxValue", "step") + SWITCH_ATTRIBUTES
CLICKER_ATTRIBUTES = ("disabled",)
DROPPED_ATTRIBUTES = shared.DROPPED_ATTRIBUTES  # `interactive`: a composed element with a clicker is always a hit target
FONT_SIZE = shared.CAPTION_ATTRIBUTE  # the size of the caption: on the caption child, not on the row
TEXT_COLOR = "textColor"
TRACK_COLOR = "trackColor"
THUMB_COLOR = "thumbColor"
# What the constructor of UIToggle sets and a UITextComponent does not
TOGGLE_TEXT_DEFAULTS = (("verticalAlign", "Center"), ("wrap", "false"))

# The row of a toggle: a horizontal stack of the caption and the switch (`UIToggle::kCaptionGap`); the tests hold the numbers to the headers
CAPTION_GAP = "8"
# The switch element is as high as the row and this many times wider (`UISwitchComponent::kAreaAspect`): the track plus a quarter of the height of air at the right end
SWITCH_AREA_ASPECT = "1.25"
# The off track derives from the caption's colour at this alpha (`UISwitchComponent::kOffTrackAlpha`)
OFF_TRACK_ALPHA = 0.35
WHITE = (255, 255, 255, 255)  # the caption colour the switch component's own fallbacks assume

_BYTE_MAX = 255
_HEX_PATTERN = re.compile(r"#([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})?")
_BYTE_PATTERN = re.compile(r"\d+")
_RGBA_PARTS = 4
_TOKEN_SIGN = "@"


def _float32(value: float) -> float:
    return struct.unpack("f", struct.pack("f", value))[0]


def with_alpha(color, factor: float):
    """`WithAlpha` of UIColor.h: the alpha times `factor` in single precision, cut to a byte (not rounded)."""
    r, g, b, a = color
    return (r, g, b, int(_float32(float(a) * _float32(factor))))


def parse_color(text: str):
    """`UIText::ParseColorRef`: ('literal', (r, g, b, a)) for "r,g,b,a", "#RRGGBB" and "#RRGGBBAA"; ('token', text) for "@name"; None for anything else."""
    text = text.strip(" \t")
    if text.startswith(_TOKEN_SIGN):
        return ("token", text) if len(text) > 1 else None
    match = _HEX_PATTERN.fullmatch(text)
    if match:
        channels = [int(part, 16) for part in match.groups() if part is not None]
        return ("literal", tuple(channels + [_BYTE_MAX] * (_RGBA_PARTS - len(channels))))
    parts = [part.strip(" \t") for part in text.split(",")]
    if len(parts) == _RGBA_PARTS and all(_BYTE_PATTERN.fullmatch(part) and int(part) <= _BYTE_MAX for part in parts):
        return ("literal", tuple(int(part) for part in parts))
    return None


def format_color(color) -> str:
    return ",".join(str(channel) for channel in color)


def _is_given(element: xmltext.Element, name: str) -> bool:
    """Whether the file gives a colour that the class would use: a transparent literal is 'derive me' to the class."""
    text = element.get(name)
    if not text:
        return False
    parsed = parse_color(text)
    return parsed is None or parsed[0] == "token" or parsed[1][3] > 0


def _derived_colors(toggle: xmltext.Element):
    """The class derived the off track and the thumb from its own text colour; the switch child cannot see the caption. Returns (track, thumb, reason): the text to write for
    `trackColor` and `thumbColor` (None: write nothing, the file's own or the component's fallback stands), or the reason it cannot be done."""
    text = toggle.get(TEXT_COLOR)
    if not text:
        return None, None, None
    parsed = parse_color(text)
    if parsed is None:
        return None, None, f'textColor="{text}" is not a colour'
    kind, value = parsed
    if kind == "token":
        if not _is_given(toggle, TRACK_COLOR):
            return None, None, f'textColor="{text}" is a theme token and the alpha of a token cannot be written: give a trackColor'
        return None, None if _is_given(toggle, THUMB_COLOR) else shared._text_of(toggle, TEXT_COLOR), None
    if value == WHITE:
        return None, None, None
    track = None if _is_given(toggle, TRACK_COLOR) else format_color(with_alpha(value, OFF_TRACK_ALPHA))
    thumb = None if _is_given(toggle, THUMB_COLOR) else format_color(value)
    return track, thumb, None


def _moved(element_type: str):
    if element_type == TOGGLE_TYPE:
        return set(shared.TEXT_ATTRIBUTES) | {FONT_SIZE} | set(TOGGLE_ATTRIBUTES) | set(SWITCH_ATTRIBUTES) | set(CLICKER_ATTRIBUTES) | set(DROPPED_ATTRIBUTES)
    return set(SLIDER_ATTRIBUTES) | set(CLICKER_ATTRIBUTES) | set(DROPPED_ATTRIBUTES)


def _moved_overrides(element: xmltext.Element, name: str, doc: xmltext.Document):
    """The attributes that move off the element and that a `<set>` of its canvas names for it."""
    moved = _moved(element.get("type"))
    found = []
    for override in shared._canvas_of(element, doc).find_all(lambda e: e.name == "set" and e.get("element") == name):
        found.extend(a for a in override.attribute_names() if a in moved)
    return found


def refusal(element: xmltext.Element, name: str, doc: xmltext.Document):
    is_toggle = element.get("type") == TOGGLE_TYPE
    label = f'{"toggle" if is_toggle else "slider"} "{name}"' if name else f'a {"toggle" if is_toggle else "slider"} without a name'
    if element.child("components") is not None:
        return f"{label} has <components> already: write the composed form by hand"
    if element.child("children") is not None:
        return f"{label} has <children>: the composed form lays out or draws nothing else: write it by hand"
    interactive = element.get("interactive")
    if interactive is not None and interactive.strip().lower() in shared._FALSE:
        return f'{label} has interactive="{interactive}": a composed {"toggle" if is_toggle else "slider"} is always a hit target'
    if is_toggle:
        own = [n for n in shared.STACK_ATTRIBUTES if element.has(n)]
        if own:
            return f"{label} has {', '.join(own)}: the row of a toggle becomes a stack of its own: write it by hand"
        _, _, reason = _derived_colors(element)
        if reason:
            return f"{label}: {reason}"
    moved = _moved_overrides(element, name, doc) if name else []
    if moved:
        return f'{label} is composed, and a variant sets {", ".join(sorted(set(moved)))} on it: after the move that override names an attribute the element no longer has'
    return None


def _pairs(element: xmltext.Element, names):
    return [(n, shared._text_of(element, n)) for n in names if element.has(n)]


def _remove_moved(element: xmltext.Element) -> None:
    moved = _moved(element.get("type"))
    for attribute in [n for n in element.attribute_names() if n in moved]:
        element.remove(attribute)


def _holder(element: xmltext.Element, components) -> xmltext.Element:
    holder = element.add_child("<components/>", 0)
    for type_name, pairs in components:
        holder.add_child(shared._component(type_name, pairs))
    return holder


def compose_toggle(toggle: xmltext.Element, name: str) -> str:
    """Rewrite one toggle in place; returns what it became, for the note."""
    track, thumb, _ = _derived_colors(toggle)
    clicker = _pairs(toggle, CLICKER_ATTRIBUTES)
    own = _pairs(toggle, TOGGLE_ATTRIBUTES)
    defaults = dict(TOGGLE_TEXT_DEFAULTS)
    text = [(n, shared._text_of(toggle, n) if toggle.has(n) else defaults[n]) for n in shared.TEXT_ATTRIBUTES if toggle.has(n) or n in defaults]
    colors = dict(_pairs(toggle, SWITCH_ATTRIBUTES))
    colors.update({TRACK_COLOR: track} if track is not None else {})
    colors.update({THUMB_COLOR: thumb} if thumb is not None else {})
    switch = [(n, colors[n]) for n in SWITCH_ATTRIBUTES if n in colors]

    caption = ([("name", f"{name}Caption")] if name else []) + [("flex", "1"), ("height", "100%")]
    if toggle.has(FONT_SIZE):
        caption.append((FONT_SIZE, shared._text_of(toggle, FONT_SIZE)))
    switch_element = ([("name", f"{name}Switch")] if name else []) + [("height", "100%"), ("aspectRatio", SWITCH_AREA_ASPECT)]

    _remove_moved(toggle)
    toggle.set("type", COMPOSED_TYPE)
    for attribute, value in (("layout", "Stack"), ("direction", "Horizontal"), ("spacing", CAPTION_GAP), ("crossAlign", "Center")):
        toggle.set(attribute, value)
    _holder(toggle, [("UIInputClicker", clicker), ("UIToggleComponent", own)])
    children = toggle.add_child("<children/>")
    children.add_child(f'<element type="{COMPOSED_TYPE}"{shared._attributes(caption)}><components>{shared._component("UITextComponent", text)}</components></element>')
    children.add_child(f'<element type="{COMPOSED_TYPE}"{shared._attributes(switch_element)}><components>{shared._component("UISwitchComponent", switch)}</components></element>')
    return "UIElement + UIInputClicker + UIToggleComponent + caption child + switch child"


def compose_slider(slider: xmltext.Element, name: str) -> str:
    """Rewrite one slider in place; returns what it became, for the note."""
    clicker = [("stateLook", "false")] + _pairs(slider, CLICKER_ATTRIBUTES)
    own = _pairs(slider, SLIDER_ATTRIBUTES)
    _remove_moved(slider)
    slider.set("type", COMPOSED_TYPE)
    _holder(slider, [("UIInputClicker", clicker), ("UISliderComponent", own)])
    return "UIElement + UIInputClicker + UISliderComponent"


def compose_controls(doc: xmltext.Document, ctx, elements) -> None:
    """Compose each of `elements` (a `UIToggle` or a `UISlider` of `doc`), or fail it with the reason; one note per element composed."""
    for element in elements:
        name = element.get("name") or ""
        reason = refusal(element, name, doc)
        if reason:
            ctx.fail(element, reason)
            continue
        became = compose_toggle(element, name) if element.get("type") == TOGGLE_TYPE else compose_slider(element, name)
        ctx.report(element, f"{name or '(no name)'}: {became}")
