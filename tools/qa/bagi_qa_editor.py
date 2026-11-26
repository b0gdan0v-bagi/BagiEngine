"""The editor's moves over a `QaSession` of an editor run (`launch(editor=True)`): what a person does with the Scene Editor, as ImGui events.

The editor is the engine's, so a game's charter script uses this file as it is. Every name the editor shows (a window, a menu item, a button, a hidden
label) is a constant here, the one place to fix when the editor renames it. What the control API cannot do is done the way a person does it:

- **No scene method exists**, so nothing here opens a scene (a game's `reset` shows one in the Scene Editor) or reloads one.
- **A click is a move first.** A header or a field takes a press only when the pointer was over it in an earlier frame, so `click_item` moves, then clicks.
- **A control's name is not always its label.** The generic inspector gives a number's field the label `##<index>` (the index of the field in its component)
  and draws the name after it as an item of its own (`<name>##label_<index>`), which a drag changes the number by; so a number is found by its component
  and index (`field_label`).
- **A number is typed**: Ctrl+click on a drag field turns it into a text field (Ctrl is Cmd on macOS, which `imgui_input` means by `ctrl`), select all, text,
  Enter. A text field takes the same without the Ctrl.
- **The dirty state is not an item** ("(unsaved changes)" is text): `dirty` reads the scene document of `editor_state`, which is what the marker shows.
- **A workspace is a button of the main toolbar** and shows its own layout and panels a few frames after the click; "Reset to Defaults" brings back the
  ready ones of the active workspace only.
- **A panel is as wide as the dock gave it.** The default layout leaves the Scene Editor too narrow for its inspector, which is then clipped and has no
  items; `widen_left_dock` drags the splitter, as a person would.

A move that cannot do what was asked raises `SessionError` (kept in `session_errors` by the session) naming what the editor showed instead.
"""

import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR))

from bagi_session import SessionError  # noqa: E402

MENU_BAR_VIEW = "View"
VIEW_MENU_WINDOW = "View"  # an open menu is a window `View###Menu_00`; imgui_find names it by what precedes the ##
SCENE_EDITOR = "Scene Editor"
CHILD_WINDOWS = f"{SCENE_EDITOR}/"  # a child window is `<parent>/##<name>_<hash>`: the part before the ## is the parent and a slash
HIERARCHY_CHILD = "##hierarchy"
INSPECTOR_CHILD = "##inspector"
SEARCH_FIELD = "##search"
CLEAR_SEARCH_BUTTON = "clear_search"
SAVE_BUTTON = "Save"
SCENE_KIND = "scene"  # the `kind` of the scene row of `editor_state`
ADD_NODE_BUTTON = "Add Node"
NAME_FIELD = "##node_name"
LAYER_FIELD = "##node_layer"
PALETTE_QUERY_FIELD = "##query"
PALETTE_LIST_CHILD = "##list"
PALETTE_POPUP = "##Popup"  # starts the palette's window name; a panel may have a child called `##list` too (the Config Editor)
PALETTE_CHORD = ("P", ("ctrl",))  # `view.commandPalette`, rebindable
GAME_PANEL_CHORD = ("G", ("ctrl", "shift"))  # `view.game`, rebindable
PALETTE_ESCAPES = 2  # the first Escape leaves the focused query field, the second closes the popup
FIELD_LABEL_FORMAT = "##{}"
ICON_ROW = re.compile("^[\ue000-\uf8ff] (?P<name>[^#]+)$")  # a hierarchy row is a Lucide glyph, a space and the node's name
FIELD_LABEL = re.compile(r"^##\d+$")
# the header of a component's card in the inspector: a Lucide glyph, a space, the type name and `##card`
COMPONENT_TYPE = re.compile("^[\ue000-\uf8ff] (?P<type>[A-Za-z_][A-Za-z0-9_]*)##card$")
CARD_MENU_MARK = "##card_menu"  # ends the label of the "..." button on a card's header, which is neither a header nor a field

LEFT_DOCK_SPLITTER_OFFSET = 1.0  # from the right edge of the Scene Editor into the gap to the viewport (output pixels)
LEFT_DOCK_PRESS_Y = 400.0  # a height at which the left column is beside the viewport
LEFT_DOCK_DRAG_TO = (500.0, 700.0)
ROW_CLICK_OFFSET = 60.0  # a hierarchy row also holds hide and lock toggles at its right end: click near its left
ITEM_LIMIT = 500
MENU_RESET_LAYOUT = "Reset to Defaults"
TOOLBAR_CHILD = "##MainToolbar"  # the child window of the main toolbar; a workspace's button there is labelled with the workspace's name alone
WORKSPACE_FRAMES = 6  # the editor loads or builds the layout in the frames after the click, then docks the windows
# the panels the ready layout of each workspace opens (`Workspaces.cpp`), by menu item
WORKSPACE_PANELS = {
    "Scene": ("Viewport", "Scene Editor", "Config Editor", "Sprite Library"),
    "UI": ("Viewport", "Scene Editor", "Asset Browser", "Sprite Library", "Config Editor"),
    "Animation": ("Viewport", "Scene Editor", "Asset Browser", "Sprite Library"),
    "Profiling": ("Viewport", "Scene Editor", "Console", "Profiler"),
}
PANEL_CLOSE_FRAMES = 12  # at the default step, longer than the fade of a closing panel, whose window is drawn until it ends


@dataclass(frozen=True)
class Panel:
    """A panel of the View menu: its menu item and how `imgui_find` names its window (`contains` when the title carries a changing part)."""

    menu_item: str
    window: str
    contains: bool = False


PANELS = (
    Panel("Viewport", "Viewport"),
    Panel("Scene Editor", SCENE_EDITOR),
    Panel("Config Editor", "Config Editor"),
    Panel("File Changes", "File changes"),
    Panel("Sprite Library", "Sprite Library"),
    Panel("Asset Browser", "Asset Browser"),
    Panel("Game Panel", "Game"),
    Panel("Console", "Console", contains=True),  # the title counts the errors and warnings
    Panel("Generate Art", "Generate", contains=True),  # the window is `Generate###Generate`
    Panel("Profiler", "Profiler"),
    Panel("Field Help", "Field Help"),
)
VIEW_MENU_COMMANDS = ("Command Palette...", "Preferences...", "Layouts", "Save Layout As...", "Delete Layout", MENU_RESET_LAYOUT)  # the items that are no panel


@dataclass(frozen=True)
class HierarchyRow:
    name: str
    item: dict[str, Any]


def field_label(index: int) -> str:
    """The hidden label of the `index`-th field of a component in the generic inspector (`##0` is the first reflected field)."""
    return FIELD_LABEL_FORMAT.format(index)


def click_item(session: Any, item: dict[str, Any], **click: Any) -> dict[str, Any]:
    """Moves the pointer to the centre of an `imgui_find` match, then clicks it (`mods`, `count`, `button` as `imgui_click` takes them)."""
    x, y = item["x"] + item["width"] / 2, item["y"] + item["height"] / 2
    session.imgui_input([{"type": "move", "x": x, "y": y}])
    return session.imgui_click(x=x, y=y, **click)


def find_items(session: Any, window: str | None = None, child: str | None = None, **filters: Any) -> list[dict[str, Any]]:
    """The items `imgui_find` lists in `window` (and, with `child`, only those of the child window whose name holds that text), in the order they were drawn."""
    matches = session.imgui_find(window=window, kind="item", limit=ITEM_LIMIT, **filters)["matches"]
    return [match for match in matches if child is None or child in match.get("window", "")]


def find_item(session: Any, window: str | None = None, child: str | None = None, **filters: Any) -> dict[str, Any]:
    """The one visible item, else a SessionError that lists the labels the window does show."""
    found = [item for item in find_items(session, window, child, **filters) if item["visible"]]
    if len(found) == 1:
        return found[0]
    where = f"'{window}'" if window else "the editor"
    shown = ", ".join(repr(item["label"]) for item in find_items(session, window, child) if item["label"] and item["visible"])
    raise SessionError(f"{len(found)} visible items match {filters} in {where}{f' (child {child})' if child else ''}; it shows: {shown}")


def _panel(title: str) -> Panel:
    for panel in PANELS:
        if title in (panel.menu_item, panel.window):
            return panel
    raise SessionError(f"'{title}' is not a panel this module knows: {[panel.menu_item for panel in PANELS]}")


def panel_open(session: Any, title: str) -> bool:
    """Whether the panel's window was drawn in the last frame (a closed panel, and one hidden behind another tab, draws none)."""
    panel = _panel(title)
    filters = {"label_contains": panel.window} if panel.contains else {"label": panel.window}
    return bool(session.imgui_find(kind="window", **filters)["matches"])


def view_menu_items(session: Any) -> list[str]:
    """The names of the View menu's items in the order it lists them (a label without its `###<id>`); the menu is closed again (Escape) when this returns."""
    click_item(session, find_item(session, label=MENU_BAR_VIEW))
    labels = [item["label"].split("##")[0] for item in find_items(session, VIEW_MENU_WINDOW) if item["label"]]
    session.imgui_key("Escape")
    return labels


def view_panels(session: Any) -> tuple[list[str], list[str]]:
    """The View menu's items that this module knows as panels, and the ones it does not know at all (a new panel, or a new command)."""
    items = view_menu_items(session)
    known = [panel.menu_item for panel in PANELS]
    return [item for item in items if item in known], [item for item in items if item not in known and item not in VIEW_MENU_COMMANDS]


def open_panel(session: Any, title: str) -> None:
    """Opens a panel through the View menu; one that is already open is left alone (the menu item toggles). SessionError when no window shows up."""
    if panel_open(session, title):
        return
    _toggle_from_menu(session, title)
    if not panel_open(session, title):
        raise SessionError(f"the panel '{title}' did not open after View, {_panel(title).menu_item}")


def close_panel(session: Any, title: str) -> None:
    """Closes an open panel through the View menu; a closed one is left alone."""
    if not panel_open(session, title):
        return
    _toggle_from_menu(session, title)
    session.step(PANEL_CLOSE_FRAMES)
    if panel_open(session, title):
        raise SessionError(f"the panel '{title}' did not close after View, {_panel(title).menu_item}")


def _toggle_from_menu(session: Any, title: str) -> None:
    _click_view_item(session, _panel(title).menu_item)


def _click_view_item(session: Any, menu_item: str) -> None:
    click_item(session, find_item(session, label=MENU_BAR_VIEW))
    click_item(session, find_item(session, VIEW_MENU_WINDOW, label=menu_item))


def switch_workspace(session: Any, name: str) -> None:
    """Clicks the workspace's button in the main toolbar and lets the layout settle; the active one's button changes nothing."""
    if name not in WORKSPACE_PANELS:
        raise SessionError(f"'{name}' is not a workspace this module knows: {list(WORKSPACE_PANELS)}")
    click_item(session, find_item(session, child=TOOLBAR_CHILD, label=name))
    session.step(WORKSPACE_FRAMES)


def closed_workspace_panels(session: Any, name: str) -> list[str]:
    """The panels of the workspace's ready layout whose window was not drawn in the last frame."""
    return [title for title in WORKSPACE_PANELS[name] if not panel_open(session, title)]


def reset_layout(session: Any) -> None:
    """View, "Reset to Defaults": the ready layout and panels of the active workspace."""
    _click_view_item(session, MENU_RESET_LAYOUT)
    session.step(WORKSPACE_FRAMES)


def widen_left_dock(session: Any) -> None:
    """Drags the splitter between the left column and the viewport to the right, until the Scene Editor shows its inspector next to its hierarchy.

    A press needs the pointer over the splitter in an earlier frame and a drag moves a step per frame, so each stage is its own call. The splitter is
    found by the Scene Editor's window: how wide the column starts depends on the window's size and on the `imgui.ini` an earlier run left."""
    windows = session.imgui_find(kind="window", label=SCENE_EDITOR)["matches"]
    if not windows:
        raise SessionError(f"no '{SCENE_EDITOR}' window: the left dock cannot be widened")
    x, y = windows[0]["x"] + windows[0]["width"] + LEFT_DOCK_SPLITTER_OFFSET, LEFT_DOCK_PRESS_Y
    session.imgui_input([{"type": "move", "x": x, "y": y}])
    session.step(2)
    session.imgui_input([{"type": "down"}])
    session.step(2)
    for target in LEFT_DOCK_DRAG_TO:
        session.imgui_input([{"type": "move", "x": target, "y": y}])
        session.step(1)
    session.imgui_input([{"type": "up"}])
    session.step(2)


def hierarchy_rows(session: Any) -> list[HierarchyRow]:
    """The node rows of the Scene Editor's hierarchy that were drawn, in drawn order (a collapsed node hides its children; one scrolled out is `visible: False`)."""
    rows = []
    for item in find_items(session, CHILD_WINDOWS, HIERARCHY_CHILD):
        match = ICON_ROW.match(item["label"])
        if match:
            rows.append(HierarchyRow(match["name"], item))
    if rows and rows[-1].name == ADD_NODE_BUTTON:
        rows.pop()
    return rows


def hierarchy_names(session: Any) -> list[str]:
    return [row.name for row in hierarchy_rows(session)]


def select_node(session: Any, name: str) -> None:
    """Selects the node called `name` by a click on its hierarchy row. A node under a collapsed one is found through the search field, which opens the way
    to its matches (and is cleared afterwards). SessionError when no row, or more than one, has the name, or when no inspector shows after the click."""
    rows = _visible_rows(session, name)
    filtered = not rows
    if filtered:
        _filter_hierarchy(session, name)
        rows = _visible_rows(session, name)
    try:
        if len(rows) != 1:
            raise SessionError(f"{len(rows)} hierarchy rows are called '{name}'; the rows drawn are {hierarchy_names(session)}")
        item = rows[0].item
        x, y = item["x"] + min(item["width"] / 2, ROW_CLICK_OFFSET), item["y"] + item["height"] / 2
        session.imgui_input([{"type": "move", "x": x, "y": y}])
        session.imgui_click(x=x, y=y)
    finally:
        if filtered:
            _clear_hierarchy_filter(session)
    if not find_items(session, CHILD_WINDOWS, INSPECTOR_CHILD, label_contains=NAME_FIELD):
        raise SessionError(f"the inspector shows no node after the click on '{name}': widen the dock (`widen_left_dock`) or the node is not selectable")


def rows_matching(session: Any, text: str) -> list[str]:
    """The names of the rows the hierarchy's search field leaves for `text` (the way to each match is opened); the field is cleared afterwards."""
    _filter_hierarchy(session, text)
    try:
        return hierarchy_names(session)
    finally:
        _clear_hierarchy_filter(session)


def _filter_hierarchy(session: Any, text: str) -> None:
    _type_into(session, find_item(session, CHILD_WINDOWS, HIERARCHY_CHILD, label_contains=SEARCH_FIELD), text)


def _clear_hierarchy_filter(session: Any) -> None:
    click_item(session, find_item(session, CHILD_WINDOWS, HIERARCHY_CHILD, label_contains=CLEAR_SEARCH_BUTTON))


def _visible_rows(session: Any, name: str) -> list[HierarchyRow]:
    return [row for row in hierarchy_rows(session) if row.name == name and row.item["visible"]]


def inspector_labels(session: Any) -> list[str]:
    """The labels of the inspector's items that have one, in drawn order: the name and layer fields, the card headers and menu buttons, the `##<index>` fields and their names."""
    return [item["label"] for item in find_items(session, CHILD_WINDOWS, INSPECTOR_CHILD) if item["label"]]


def open_component(session: Any, component: str) -> None:
    """Opens the inspector section of a component type when it shows no control. A section that is open and empty looks the same as a closed one."""
    header = _component_header(session, component)
    if _section_items(session, header):
        return
    click_item(session, header)
    if not _section_items(session, _component_header(session, component)):
        raise SessionError(f"the section '{component}' shows no control after a click on its header")


def _component_header(session: Any, component: str) -> dict[str, Any]:
    items = find_items(session, CHILD_WINDOWS, INSPECTOR_CHILD)
    found = [item for item in items if item["visible"] and (match := COMPONENT_TYPE.match(item["label"])) and match["type"] == component]
    if len(found) != 1:
        shown = ", ".join(repr(item["label"]) for item in items if item["label"] and item["visible"])
        raise SessionError(f"{len(found)} visible component cards are headed '{component}' in the inspector; it shows: {shown}")
    return found[0]


def _section_items(session: Any, header: dict[str, Any]) -> list[dict[str, Any]]:
    """The items of a component's card under its header, down to the next card's header; the cards' menu buttons are left out."""
    items = find_items(session, CHILD_WINDOWS, INSPECTOR_CHILD)
    below = [item for item in items if COMPONENT_TYPE.match(item["label"]) and item["y"] > header["y"]]
    end = below[0]["y"] if below else float("inf")
    return [item for item in items if header["y"] < item["y"] < end and item["label"] and not item["label"].endswith(CARD_MENU_MARK)]


def _field(session: Any, label: str, component: str | None) -> dict[str, Any]:
    if component is None:
        return find_item(session, CHILD_WINDOWS, INSPECTOR_CHILD, label_contains=label)
    open_component(session, component)
    found = [item for item in _section_items(session, _component_header(session, component)) if item["label"] == label]
    if len(found) != 1:
        raise SessionError(f"{len(found)} controls called '{label}' in '{component}'; the section shows {[item['label'] for item in _section_items(session, _component_header(session, component))]}")
    return found[0]


def _type_into(session: Any, item: dict[str, Any], text: str, *, number: bool = False) -> None:
    answer = click_item(session, item, mods=["ctrl"] if number else [])
    if not answer["wantTextInput"]:
        raise SessionError(f"the field '{item['label']}' did not take the focus for text ({'Ctrl+click' if number else 'click'}): ImGui's answer {answer}")
    session.imgui_key("A", ["ctrl"])
    session.imgui_type(text)
    session.imgui_key("Enter")


def set_text_field(session: Any, label: str, text: str, component: str | None = None) -> None:
    """Replaces the text of the inspector's text field called `label` (`NAME_FIELD`): click, select all, type, Enter."""
    _type_into(session, _field(session, label, component), text)


def set_number_field(session: Any, label: str, value: float, component: str | None = None) -> None:
    """Sets a number field by the gesture ImGui gives a drag or an input field: Ctrl+click makes it a text field, then select all, the number, Enter.

    `label` is `LAYER_FIELD` or `field_label(index)`; with `component` the field is looked for under that component's header (opened first), since every
    component has a `##0`."""
    _type_into(session, _field(session, label, component), str(value), number=True)


def rename_selected(session: Any, name: str) -> None:
    """Renames the selected node through the inspector's name field (the hierarchy row follows in the next frame)."""
    set_text_field(session, NAME_FIELD, name)


def save_button(session: Any) -> dict[str, Any]:
    return find_item(session, SCENE_EDITOR, label_contains=SAVE_BUTTON)


def scene_document(session: Any) -> dict[str, Any]:
    """The one `scene` row of `editor_state` (`id`, `path`, `file`, `dirty`); `file` is the file the Save button writes (the overlay's copy under an overlay)."""
    scenes = [document for document in session.editor_state()["documents"] if document["kind"] == SCENE_KIND]
    if len(scenes) != 1:
        raise SessionError(f"{len(scenes)} scene documents in editor_state ({[document['id'] for document in scenes]}): is a scene active in the editor?")
    return scenes[0]


def dirty(session: Any) -> bool:
    """Whether the scene has unsaved changes: the flag of the Save button and of the "(unsaved changes)" marker."""
    return bool(scene_document(session)["dirty"])


def any_dirty(session: Any) -> bool:
    """Whether any editor panel has unsaved changes (also pending node-file edits, the Config Editor and the Sprite Library)."""
    return bool(session.editor_state()["anyDirty"])


def save(session: Any) -> None:
    """Clicks the Save button. It is disabled while the scene is clean, so a clean scene is a SessionError; and the scene must be clean afterwards."""
    if not dirty(session):
        raise SessionError("the Scene Editor shows no unsaved changes, so its Save button is disabled")
    click_item(session, save_button(session))
    session.step(1)
    if dirty(session):
        raise SessionError("the Scene Editor still has unsaved changes after a click on Save")


def press_chord(session: Any, chord: tuple[str, Sequence[str]]) -> None:
    key, mods = chord
    session.imgui_key(key, mods)


def palette_open(session: Any) -> bool:
    return bool(session.imgui_find(label_contains=PALETTE_QUERY_FIELD, kind="item")["matches"])


def open_palette(session: Any) -> None:
    """Opens the command palette by its chord; its query field has the focus, so `imgui_type` filters it."""
    press_chord(session, PALETTE_CHORD)
    if not palette_open(session):
        raise SessionError("the command palette did not open after its chord")


def palette_rows(session: Any) -> list[str]:
    """The commands the open palette lists, in order."""
    rows = find_items(session, child=PALETTE_LIST_CHILD)
    return [item["label"] for item in rows if item["window"].startswith(PALETTE_POPUP) and item["label"] and item["visible"]]


def close_palette(session: Any) -> None:
    for _ in range(PALETTE_ESCAPES):
        if not palette_open(session):
            return
        session.imgui_key("Escape")
    if palette_open(session):
        raise SessionError("the command palette is still open after Escape")
