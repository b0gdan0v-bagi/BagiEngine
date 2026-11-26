# Scene Editor: the component inspector

The rest of the Scene Editor (selection, hierarchy, dialogs, Save, the viewport): `.claude/rules/widgets-module.md`, "Scene Editor Conventions".

## Component cards

`DrawComponentCard` (`ComponentCard.h`) draws one component of the inspected node: the frame, the header with the category stripe, the class icon and name, the touch flash and the "..." menu, and the body `SceneEditorWidget` gives it (the "UI elements" section of a canvas, then `RenderComponentInspector`).

- The card changes no node: "Remove" is an answer, which the widget acts on after the loop over the components.
- "Reset" is `ComponentDefaults::ResetAll`; it drops every mark of the component, makes the scene unsaved and never touches a canvas's elements.
- "Copy" puts the `<component>` element of a scene file on the engine's clipboard (`AppendComponentElement` with `skipDefaults`, the writer Save uses). There is no paste.
- The header's item label is `<glyph> <type name>##card`, the menu button's ends with `##card_menu`: `tools/qa/bagi_qa_editor.py` finds a card and its controls by them.

## Defaults

`ComponentDefaults` (`ComponentDefaults.h`) says what differs from a fresh component and restores it field by field; the contract is in the header. It is a member of `SceneEditorWidget`, so the prototypes die with the widget. Only top-level fields are compared: a change inside a struct or a list marks the whole field, and a reset replaces it whole. A reset runs `OnPropertyWritten` and nothing else, so a component that derives one field from another does it there.

## Marks and reset arrows

`RenderComponentInspector` with a `ComponentDefaults` marks each differing top-level field (`DrawDefaultMark`): the accent bar and the reset arrow stand in the indent left of the field and take no room, so a field at its default draws exactly as without defaults and no field id moves.

- A `readonly` field and a disabled scope (a locked node) have the bar and no arrow. A field whose visitor draws nothing has neither; the card's "Reset" still covers it.
- A custom inspector cannot mark its own controls, so the card lists one line per differing field above it; its signature does not change, so a game's inspector gets the arrows too.
- A click is applied after the inspector has run, makes the scene unsaved and removes that field's touch mark only.

## Numbers

A number of the generic inspector and of the sprite's source rect is drawn by `DrawNumber` (`NumberField.h`), which another custom inspector may call; the UI element section (`UIRectInspector`) draws its own fields. The decisions are in `FieldPresentation`, which needs no ImGui.

- A press on the name and a horizontal drag change the number: the value at the press plus the whole drag times the presentation's speed, through `Constrain`. Counted from the press, so nothing drifts, an integer moves under a speed below one, and a value clamped at an end comes back with the pointer. A click that never moved is no edit.
- A drag is an edit like any other (it makes the scene unsaved and drops the touch mark); a disabled field does not drag.
- The name is an item of its own, `<name>##label_<id>`, over the text and without room in the layout. The field keeps `##<id>`, which the QA helpers match, so the name's label never holds it.
- A field named exactly `x`, `y` or `z`, at any depth (a transform, a `Vec2`, a `Rect`), has its frame mixed with the axis colour of the palette.

## Selection history

`SelectionHistory` (`SelectionHistory.h`, no ImGui) keeps the primary selections of the active scene by runtime id; the contract is in the header. `SceneEditorWidget` records the primary node once per drawn frame and clears the history when the scene is replaced.

- A step (`sceneEditor.selectionBack`, `sceneEditor.selectionForward`) finds the node with `FindNodeByRuntimeId`, makes it the whole selection, reveals it and brings the window to the front like a select request. It is not a new selection, so what is ahead stays.
- The chords are ordinary rows of the command table. While a text field has the keyboard ImGui keeps the arrows for it (Option+arrows move by word on macOS), so the widget needs no guard; `SelectionChordTest` holds that.
