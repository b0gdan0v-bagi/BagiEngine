"""The View menu and palette moves of bagi_qa_editor.py when the editor's labels and child windows are not unique: a menu item carries an `###<id>`, and a
panel has a child window of the same name as the palette's list."""

import bagi_qa_editor as editor

MENU_NAMES = ["Command Palette...", "Viewport", "Game Panel", "Layouts"]
PALETTE_LIST = "##Popup_c0f026a4/##list_9F383DB3"
CONFIG_LIST = "Config Editor###ConfigEditor/##list_1A2B3C4D"


def item(label, window):
    return {"kind": "item", "label": label, "window": window, "x": 0.0, "y": 0.0, "width": 10.0, "height": 10.0, "visible": True}


class FakeSession:
    """The View menu, open after a click until Escape, over the rows of the Config Editor and of the palette."""

    def __init__(self, palette_commands=()):
        self.menu_open = False
        self.palette_commands = palette_commands

    def imgui_find(self, window=None, kind=None, limit=None, label=None):
        items = [item(editor.MENU_BAR_VIEW, "##EditorRoot")]
        if self.menu_open:
            items += [item(f"{name}###id{index}", "View###Menu_00") for index, name in enumerate(MENU_NAMES)]
        items.append(item("ApplicationConfig##select", CONFIG_LIST))
        items += [item(command, PALETTE_LIST) for command in self.palette_commands]
        found = [match for match in items if (window is None or match["window"].startswith(window)) and (label is None or match["label"] == label)]
        return {"matches": found}

    def imgui_input(self, events):
        pass

    def imgui_click(self, **click):
        self.menu_open = True

    def imgui_key(self, key, mods=()):
        if key == "Escape":
            self.menu_open = False


def test_view_menu_items_are_known_by_their_names_without_the_ids():
    session = FakeSession()

    assert editor.view_panels(session) == (["Viewport", "Game Panel"], [])
    assert not session.menu_open


def test_palette_rows_are_the_popups_only():
    assert editor.palette_rows(FakeSession(["Save Scene", "Console"])) == ["Save Scene", "Console"]


def test_a_palette_without_rows_lists_none_although_a_panel_has_a_list():
    assert editor.palette_rows(FakeSession()) == []
