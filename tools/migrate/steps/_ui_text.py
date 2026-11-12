"""The mapping of a `UILabel`, a `UIImage` and the text of a `UIDropdown` to components (`s0004_ui_text_image_dropdown_to_components.py`). What it writes and refuses is the
docstring of step 4. A module whose name starts with `_` is not a step. The helpers that write a component are those of `_ui_button.py`."""

import _ui_button as shared
import xmltext

LABEL_TYPE = "UILabel"
IMAGE_TYPE = "UIImage"
DROPDOWN_TYPE = "UIDropdown"
COMPOSED_TYPE = shared.COMPOSED_TYPE
TEXT_COMPONENT = "UITextComponent"
IMAGE_COMPONENT = "UIImageComponent"
CLICKER_COMPONENT = "UIInputClicker"

TEXT_ATTRIBUTES = shared.TEXT_ATTRIBUTES  # the fields of UILabel, which UITextComponent has under the same names
IMAGE_ATTRIBUTES = ("sprite", "tint", "stretch")
# What stays on a dropdown: its own fields; the rest of the attributes are the element's
DROPDOWN_ATTRIBUTES = ("bind", "action", "disabled", "arrowColor", "optionHeight")
# What the constructor of UIDropdown set and a UITextComponent does not (the dropdown no longer sets them: the component says them)
DROPDOWN_TEXT_DEFAULTS = (("verticalAlign", "Center"), ("wrap", "false"))
# The class faded its header text while disabled (as UIToggle its caption); the component says so for a colour of any kind
FADE_HEADER = ("fadeWhenDisabled", "true")


def kind_of(element: xmltext.Element) -> str:
    return {LABEL_TYPE: "label", IMAGE_TYPE: "picture", DROPDOWN_TYPE: "dropdown"}[element.get("type")]


def _moved(element_type: str):
    return set(IMAGE_ATTRIBUTES) if element_type == IMAGE_TYPE else set(TEXT_ATTRIBUTES)


def has_text_component(element: xmltext.Element) -> bool:
    """Whether the element carries a `UITextComponent` already: a dropdown with one was migrated (the step is idempotent)."""
    holder = element.child("components")
    return holder is not None and any(c.name == "component" and c.get("type") == TEXT_COMPONENT for c in holder.elements)


def _moved_overrides(element: xmltext.Element, name: str, doc: xmltext.Document):
    """The attributes that move off the element and that a `<set>` of its canvas names for it."""
    moved = _moved(element.get("type"))
    found = []
    for override in shared._canvas_of(element, doc).find_all(lambda e: e.name == "set" and e.get("element") == name):
        found.extend(a for a in override.attribute_names() if a in moved)
    return found


def _clicker_ancestor(element: xmltext.Element):
    """The nearest ancestor element that carries a `UIInputClicker` in its `<components>`, or None."""
    node = element.parent
    while node is not None:
        if node.name == "element":
            holder = node.child("components")
            if holder is not None and any(c.name == "component" and c.get("type") == CLICKER_COMPONENT for c in holder.elements):
                return node
        node = node.parent
    return None


def refusal(element: xmltext.Element, name: str, doc: xmltext.Document):
    kind = kind_of(element)
    label = f'{kind} "{name}"' if name else f"a {kind} without a name"
    if element.child("components") is not None:
        return f"{label} has <components> already: write the composed form by hand"
    if element.get("type") == IMAGE_TYPE:
        owner = _clicker_ancestor(element)
        if owner is not None:
            holder = f'"{owner.get("name")}"' if owner.get("name") else "an element without a name"
            return f"{label} sits under {holder}, which has a UIInputClicker: a picture component follows the state of the clicker and derives a hover tint, the class did not: write it by hand"
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


def _compose(element: xmltext.Element, component, became: str) -> str:
    element.add_child("<components/>", 0).add_child(shared._component(*component))
    return became


def compose_label(label: xmltext.Element) -> str:
    text = _pairs(label, TEXT_ATTRIBUTES)
    _remove_moved(label)
    label.set("type", COMPOSED_TYPE)
    return _compose(label, (TEXT_COMPONENT, text), "UIElement + UITextComponent")


def compose_image(image: xmltext.Element) -> str:
    picture = _pairs(image, IMAGE_ATTRIBUTES)
    _remove_moved(image)
    image.set("type", COMPOSED_TYPE)
    return _compose(image, (IMAGE_COMPONENT, picture), "UIElement + UIImageComponent")


def compose_dropdown(dropdown: xmltext.Element) -> str:
    """The header text of a dropdown moves to a `UITextComponent` on the dropdown, which stays a `UIDropdown`."""
    defaults = dict(DROPDOWN_TEXT_DEFAULTS)
    text = [(n, shared._text_of(dropdown, n) if dropdown.has(n) else defaults[n]) for n in TEXT_ATTRIBUTES if dropdown.has(n) or n in defaults]
    text.append(FADE_HEADER)
    _remove_moved(dropdown)
    return _compose(dropdown, (TEXT_COMPONENT, text), "UIDropdown + UITextComponent")


def compose_texts(doc: xmltext.Document, ctx, elements) -> None:
    """Compose each of `elements` (a `UILabel`, a `UIImage` or a `UIDropdown` of `doc`), or fail it with the reason; one note per element."""
    for element in elements:
        if element.get("type") == DROPDOWN_TYPE and has_text_component(element):
            continue  # migrated already
        name = element.get("name") or ""
        reason = refusal(element, name, doc)
        if reason:
            ctx.fail(element, reason)
            continue
        compose = {LABEL_TYPE: compose_label, IMAGE_TYPE: compose_image, DROPDOWN_TYPE: compose_dropdown}[element.get("type")]
        ctx.report(element, f"{name or '(no name)'}: {compose(element)}")
