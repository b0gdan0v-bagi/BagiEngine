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
