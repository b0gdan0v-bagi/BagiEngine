"""The moves of bagi_qa_editor.py against a model of what the real editor answers (the shapes were read from a run of BagiStress --editor).

`FakeEditor` stands in for a QaSession: the same `imgui_*` methods, answering like `imgui_find` and `imgui_input`, with the traps of the real editor in it: a
header takes a press only when the pointer was over it in an earlier call, a number is typed after Ctrl+click only, the inspector is clipped (no items)
until the left dock is widened, and the unsaved changes are no item: `editor_state` answers them, as the engine does.
"""

import pytest

import bagi_qa_editor as editor
from bagi_session import SessionError

DISPLAY = {"width": 1920.0, "height": 1080.0, "scaleX": 1.0, "scaleY": 1.0}
VIEW_ITEM = {"x": 94.0, "y": 0.0, "width": 40.0, "height": 24.0}
MENU_ROW_HEIGHT = 20.0
SAVE = {"x": 10.0, "y": 96.0, "width": 63.0, "height": 24.0}
ROW_HEIGHT = 30.0
FIELD_HEIGHT = 29.0
SPLITTER_X = 211.0
WIDE_DOCK = 400.0
PANEL_TITLES = {"Viewport": "Viewport", "Scene Editor": "Scene Editor", "Console": "Console (1 errors)###Console", "Game Panel": "Game###GamePanel", "Profiler": "Profiler"}
MENU_ITEMS = ["Command Palette...", "Preferences...", "Viewport", "Scene Editor", "Game Panel", "Console", "Profiler", "Layouts", "Reset to Defaults"]
PALETTE_COMMANDS = ["Game Panel", "Switch Game and Editor", "Console", "Save Scene"]
TRANSFORM_FIELDS = 7
CARD_ICON = "\ue194"  # any glyph of the Lucide range
CARD_MENU = "\ue0b7##card_menu"
HIERARCHY = "Scene Editor/##hierarchy_8D0B9DE4"
INSPECTOR = "Scene Editor/##inspector_828F5624"


def item(label, window, x, y, width, height, visible=True):
    return {"kind": "item", "label": label, "window": window, "x": x, "y": y, "width": width, "height": height, "visible": visible, "hovered": False, "id": "0x1"}


def part_before_hashes(name):
    return name.split("##")[0]


def card_header(component):
    return f"{CARD_ICON} {component}##card"


class FakeEditor:
    """A scene `Root` with the children Field and Run, a Transform with seven fields and a dirty flag, in the shape the real editor draws them."""

    def __init__(self, menu_items=None, save_clears=True):
        self.open_panels = {"Viewport", "Scene Editor"}
        self.menu_items = menu_items or MENU_ITEMS
        self.save_clears = save_clears
        self.menu_open = False
        self.root_open = False
        self.search = ""
        self.names = {"Field": "Field", "Run": "Run"}
        self.selected = None
        self.dirty_flag = False
        self.scene_rows = 1
        self.extra_documents = []
        self.not_editor = False
        self.sections = set()
        self.values = {}
        self.dock_width = SPLITTER_X - 1
        self.dragging = False
        self.focus = None
        self.pointer = (0.0, 0.0)
        self.hover_target = None
        self.palette = False
        self.query = ""
        self.keys = []
        self.typed = []
        self.steps = 0

    # -- what is drawn

    def children(self):
        rows = [key for key in ("Field", "Run") if not self.search or self.search.lower() in self.names[key].lower()]
        return rows if (self.root_open or self.search) else []

    def items(self):
        found = [item("View", "##EditorRoot", **VIEW_ITEM)]
        if self.menu_open:
            for index, label in enumerate(self.menu_items):
                found.append(item(label, "View###Menu_00", 94.0, 155.0 + index * MENU_ROW_HEIGHT, 217.0, MENU_ROW_HEIGHT))
        if self.palette:
            found.append(item("##query", "##Popup_c0f026a4", 730.0, 224.0, 459.0, 24.0))
            commands = [command for command in PALETTE_COMMANDS if self.query in command.lower()]
            for index, command in enumerate(commands):
                found.append(item(command, "##Popup_c0f026a4/##list_9F383DB3", 730.0, 251.0 + index * MENU_ROW_HEIGHT, 459.0, MENU_ROW_HEIGHT))
        if "Scene Editor" in self.open_panels:
            found.append(item(" Save", "Scene Editor", **SAVE))
            found.append(item("##search", HIERARCHY, 20.0, 166.0, 248.0, 24.0))
            found.append(item("####clear_search", HIERARCHY, 276.0, 166.0, 24.0, 24.0, visible=bool(self.search)))
            found.append(item("", HIERARCHY, 20.0, 196.0, 279.0, 24.0))
            found.append(item(" Root", HIERARCHY, 20.0, 226.0, 280.0, 14.0))
            found.append(item("####hide", HIERARCHY, 244.0, 226.0, 24.0, 24.0))
            for index, key in enumerate(self.children()):
                y = 256.0 + index * ROW_HEIGHT
                found.append(item(f" {self.names[key]}", HIERARCHY, 38.0, y, 262.0, 14.0))
                found.append(item("####hide", HIERARCHY, 244.0, y, 24.0, 24.0))
            found.append(item(" Add Node", HIERARCHY, 20.0, 300.0, 279.0, 24.0))
            if self.selected and self.dock_width >= WIDE_DOCK:
                found += self.inspector_items()
        return found

    def inspector_items(self):
        x = 318.0
        found = [item("##node_name", INSPECTOR, x + 54, 166.0, 241.0, 24.0), item("##node_layer", INSPECTOR, x + 52, 196.0, 183.0, 23.0), item("-", INSPECTOR, x + 241, 196.0, 23.0, 23.0)]
        y = 251.0
        for component, fields in (("TransformComponent", TRANSFORM_FIELDS), ("StressRunComponent", 0)):
            found.append(item(card_header(component), INSPECTOR, x + 5, y, 361.0, 24.0))
            # a pixel lower than the header: the helper must leave it out by its label, not by its place
            found.append(item(CARD_MENU, INSPECTOR, x + 342, y + 1, 23.0, 23.0))
            y += 30.0
            if component in self.sections:
                for index in range(fields):
                    found.append(item(f"##{index}", INSPECTOR, x + 28, y, 241.0, 23.0))
                    y += FIELD_HEIGHT
        found.append(item(" Add Component", INSPECTOR, x + 10, y + 6, 350.0, 24.0))
        return found

    def windows(self):
        return [{"kind": "window", "label": PANEL_TITLES.get(title, title), "window": None, "x": 0, "y": 0, "width": 100, "height": 100, "visible": True, "hovered": False, "id": "0x2"} for title in sorted(self.open_panels)]

    # -- the session's methods

    def imgui_find(self, label=None, label_contains=None, window=None, kind=None, visible_only=False, limit=None, **ignored):
        found = []
        for match in (self.windows() if kind != "item" else []) + (self.items() if kind != "window" else []):
            if label is not None and label not in (match["label"], part_before_hashes(match["label"])):
                continue
            if label_contains is not None and label_contains not in match["label"]:
                continue
            if window is not None and match["kind"] == "item" and window not in (match["window"], part_before_hashes(match["window"])):
                continue
            if visible_only and not match["visible"]:
                continue
            found.append(match)
        return {"frame": 1, "display": DISPLAY, "wantTextInput": self.focus is not None, "matches": found[: limit or 50]}

    def hit(self, x, y):
        for match in reversed(self.items()):
            if match["visible"] and match["x"] <= x < match["x"] + match["width"] and match["y"] <= y < match["y"] + match["height"]:
                return match
        return None

    def imgui_input(self, events, space=None):
        for event in events:
            kind = event["type"]
            if kind == "move":
                self.pointer = (event["x"], event["y"])
                target = self.hit(*self.pointer)
                self.hover_target = None if target is None else (target["label"], target["window"])
                if self.dragging:
                    self.dock_width = event["x"]
            elif kind == "down":
                self.dragging = abs(self.pointer[0] - SPLITTER_X) <= 2
            elif kind == "up":
                self.dragging = False
            elif kind == "click":
                self.click(event)
            elif kind == "key":
                self.keys.append((event["key"], tuple(event.get("mods", ()))))
                self.key(event)
            elif kind == "text":
                self.typed.append(event["text"])
                if self.focus == "##search":
                    self.search = event["text"]
                if self.focus == "##query":
                    self.query = event["text"]
        return {"events": len(events), "frames": 3, "wantCaptureKeyboard": True, "wantCaptureMouse": True, "wantTextInput": self.focus is not None}

    def imgui_click(self, label=None, x=None, y=None, mods=(), count=1, **ignored):
        assert label is None, "the moves click points: a label click would not move first"
        return self.imgui_input([{"type": "click", "x": x, "y": y, "mods": list(mods)}])

    def imgui_key(self, key, mods=()):
        return self.imgui_input([{"type": "key", "key": key, "mods": list(mods)}])

    def imgui_type(self, text):
        return self.imgui_input([{"type": "text", "text": text}])

    def step(self, frames=1, dt=None):
        self.steps += frames

    def editor_state(self):
        if self.not_editor:
            raise SessionError("the game does not run in the editor run mode: launch it with editor=True")
        documents = [dict(document) for document in self.extra_documents]
        if self.scene_rows >= 1:
            documents.append({"id": "scene:Main", "kind": "scene", "name": "Main", "dirty": self.dirty_flag, "path": "config/SceneConfig.xml", "file": "/overlay/config/SceneConfig.xml"})
        if self.scene_rows >= 2:
            documents.append({"id": "scene:Other", "kind": "scene", "name": "Other", "dirty": False, "path": "config/Other.xml", "file": "/overlay/config/Other.xml"})
        documents.sort(key=lambda document: document["id"])
        return {"anyDirty": any(document["dirty"] for document in documents), "documents": documents}

    # -- what a click or a key does

    def click(self, event):
        x, y = event["x"], event["y"]
        was_hovering = self.hover_target
        target = self.hit(x, y)
        self.hover_target = None if target is None else (target["label"], target["window"])
        self.focus = None if target is None or target["label"] not in ("##search", "##node_name", "##node_layer") else target["label"]
        if target is None:
            self.menu_open = False
            return
        label, window = target["label"], target["window"]
        if window == "View###Menu_00":
            self.menu_open = False
            self.toggle_panel(label)
        elif label == "View":
            self.menu_open = not self.menu_open
        elif label == "####clear_search":
            self.search = ""
        elif window == HIERARCHY and label.startswith(" "):
            self.selected = next(key for key in self.names if f" {self.names[key]}" == label)
        elif label.endswith("##card") and was_hovering == self.hover_target:
            self.sections ^= {label.split(" ", 1)[1].split("##")[0]}
        elif label.startswith("##") and label[2:].isdigit() and "ctrl" in event.get("mods", []):
            self.focus = label
        elif label.startswith("##") and label[2:].isdigit():
            self.focus = None
        elif label == " Save" and self.save_clears:
            self.dirty_flag = False
        self.pointer = (x, y)

    def toggle_panel(self, label):
        self.open_panels ^= {label}

    def key(self, event):
        if event["key"] == "P" and "ctrl" in event.get("mods", []) and not self.palette:
            self.palette, self.focus, self.query = True, "##query", ""
        if event["key"] == "Escape" and self.palette:
            if self.focus == "##query":
                self.focus = None
            else:
                self.palette = False
            return
        if event["key"] == "Enter" and self.focus is not None:
            if self.focus == "##node_name" and self.typed:
                self.names[self.selected] = self.typed[-1]
            if self.focus != "##search":
                self.dirty_flag = True
                self.values[self.focus] = self.typed[-1] if self.typed else None
            self.focus = None
        if event["key"] == "Escape":
            self.menu_open = False


@pytest.fixture
def ed():
    return FakeEditor()


def test_hierarchy_rows_follow_the_drawn_order_and_leave_the_add_button_out(ed):
    assert editor.hierarchy_names(ed) == ["Root"]
    ed.root_open = True
    assert editor.hierarchy_names(ed) == ["Root", "Field", "Run"]


def test_a_node_under_a_collapsed_one_is_found_through_the_search_and_the_filter_is_cleared(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    assert ed.selected == "Run" and ed.search == ""
    assert "##node_name" in editor.inspector_labels(ed)


def test_an_unknown_node_is_an_error_that_lists_the_rows(ed):
    editor.widen_left_dock(ed)
    with pytest.raises(SessionError, match=r"0 hierarchy rows are called 'Nope'.*\['Root'\]"):
        editor.select_node(ed, "Nope")
    assert ed.search == "", "the filter is cleared after a failed search too"


def test_a_clipped_inspector_is_named_when_the_dock_was_not_widened(ed):
    with pytest.raises(SessionError, match="widen_left_dock"):
        editor.select_node(ed, "Run")


def test_widen_left_dock_drags_the_splitter_so_the_inspector_has_items(ed):
    ed.selected = "Run"
    assert editor.inspector_labels(ed) == []
    editor.widen_left_dock(ed)
    assert "##node_layer" in editor.inspector_labels(ed)


def test_a_number_is_typed_after_ctrl_click_under_the_header_it_belongs_to(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    editor.set_number_field(ed, editor.field_label(0), 37, "TransformComponent")
    assert ed.typed[-1] == "37" and ("A", ("ctrl",)) in ed.keys and ed.keys[-1][0] == "Enter"
    assert editor.inspector_labels(ed).count("##0") == 1
    assert editor.dirty(ed)


def test_a_section_that_is_open_is_not_toggled_shut_by_another_field(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    editor.set_number_field(ed, editor.field_label(0), 1, "TransformComponent")
    editor.set_number_field(ed, editor.field_label(1), 2, "TransformComponent")
    assert ed.sections == {"TransformComponent"}


def test_the_menu_button_of_a_card_is_neither_its_header_nor_a_field(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    editor.open_component(ed, "TransformComponent")
    assert ed.sections == {"TransformComponent"}
    with pytest.raises(SessionError, match=r"0 controls called '.*##card_menu'"):
        editor.set_number_field(ed, CARD_MENU, 1, "TransformComponent")
    with pytest.raises(SessionError, match=r"0 visible component cards are headed 'QuadRendererComponent'.*##card"):
        editor.open_component(ed, "QuadRendererComponent")


def test_a_field_that_is_not_in_the_section_lists_what_is(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    with pytest.raises(SessionError, match=r"0 controls called '##9' in 'TransformComponent'.*##0"):
        editor.set_number_field(ed, editor.field_label(9), 1, "TransformComponent")


def test_a_text_field_replaces_its_text_and_the_row_follows(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    editor.rename_selected(ed, "Runner")
    ed.root_open = True
    assert editor.hierarchy_names(ed) == ["Root", "Field", "Runner"]
    assert ("A", ("ctrl",)) in ed.keys


def test_dirty_is_the_scene_document_of_editor_state_and_save_clears_it(ed):
    editor.widen_left_dock(ed)
    editor.select_node(ed, "Run")
    assert not editor.dirty(ed)
    editor.set_number_field(ed, editor.LAYER_FIELD, 3)
    assert editor.dirty(ed)
    editor.save(ed)
    assert not editor.dirty(ed)


def test_save_of_a_clean_scene_is_an_error_and_a_marker_that_stays_is_one_too():
    clean = FakeEditor()
    with pytest.raises(SessionError, match="no unsaved changes"):
        editor.save(clean)
    stuck = FakeEditor(save_clears=False)
    editor.widen_left_dock(stuck)
    editor.select_node(stuck, "Run")
    editor.set_number_field(stuck, editor.LAYER_FIELD, 3)
    with pytest.raises(SessionError, match="still has unsaved changes"):
        editor.save(stuck)


def test_a_panel_opens_through_the_view_menu_and_an_open_one_is_not_toggled_shut(ed):
    assert editor.panel_open(ed, "Scene Editor") and not editor.panel_open(ed, "Game Panel")
    editor.open_panel(ed, "Scene Editor")
    assert editor.panel_open(ed, "Scene Editor")
    editor.open_panel(ed, "Game Panel")
    assert editor.panel_open(ed, "Game Panel") and not ed.menu_open
    editor.close_panel(ed, "Game Panel")
    editor.close_panel(ed, "Game Panel")
    assert not editor.panel_open(ed, "Game Panel")


def test_a_panel_whose_title_changes_is_found_by_part_of_it(ed):
    editor.open_panel(ed, "Console")
    assert editor.panel_open(ed, "Console")


def test_a_panel_that_does_not_appear_is_an_error():
    broken = FakeEditor()
    broken.toggle_panel = lambda label: None
    with pytest.raises(SessionError, match="did not open"):
        editor.open_panel(broken, "Profiler")


def test_the_view_menu_lists_known_panels_and_names_what_it_does_not_know():
    known, unknown = editor.view_panels(FakeEditor())
    assert known == ["Viewport", "Scene Editor", "Game Panel", "Console", "Profiler"] and unknown == []
    known, unknown = editor.view_panels(FakeEditor(menu_items=[*MENU_ITEMS, "Timeline"]))
    assert unknown == ["Timeline"]


def test_an_item_that_is_not_there_names_the_labels_the_window_shows(ed):
    with pytest.raises(SessionError, match=r"0 visible items match .*'Scene Editor'.*Save"):
        editor.find_item(ed, "Scene Editor", label="Open")


def test_rows_matching_gives_the_rows_the_search_leaves_and_clears_it(ed):
    editor.widen_left_dock(ed)
    assert editor.rows_matching(ed, "run") == ["Root", "Run"] or editor.rows_matching(ed, "run") == ["Run"]
    assert ed.search == "" and editor.hierarchy_names(ed) == ["Root"]


def test_the_palette_opens_by_its_chord_filters_and_takes_two_escapes_to_close(ed):
    editor.open_palette(ed)
    ed.imgui_type("game")
    assert editor.palette_rows(ed) == ["Game Panel", "Switch Game and Editor"]
    ed.imgui_type("zzzz")
    assert editor.palette_rows(ed) == []
    editor.close_palette(ed)
    assert not editor.palette_open(ed) and ed.keys.count(("Escape", ())) == 2


def test_a_palette_that_does_not_open_is_an_error(ed):
    ed.palette = False
    ed.key = lambda event: None
    with pytest.raises(SessionError, match="did not open"):
        editor.open_palette(ed)


def test_any_dirty_counts_a_dirty_node_file_row_that_dirty_leaves_out(ed):
    ed.extra_documents = [{"id": "nodeFile:ui/Hud.xml", "kind": "nodeFile", "name": "Hud", "dirty": True, "path": "ui/Hud.xml", "file": "/overlay/ui/Hud.xml"}]
    assert not editor.dirty(ed)
    assert editor.any_dirty(ed)


def test_scene_document_names_the_file_the_save_writes(ed):
    assert editor.scene_document(ed)["file"] == "/overlay/config/SceneConfig.xml"


def test_no_scene_row_or_two_are_errors_that_name_the_ids(ed):
    ed.scene_rows = 0
    with pytest.raises(SessionError, match=r"0 scene documents"):
        editor.dirty(ed)
    ed.scene_rows = 2
    with pytest.raises(SessionError, match=r"2 scene documents.*scene:Main.*scene:Other"):
        editor.scene_document(ed)


def test_a_game_that_is_not_the_editor_passes_its_error_through(ed):
    ed.not_editor = True
    with pytest.raises(SessionError, match="editor run mode"):
        editor.dirty(ed)
