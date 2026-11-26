# Config Editor

Edits any loaded XML config. The panel is an asset, `config/panels/ConfigEditorPanel.xml` (`<widget type="AssetPanelWidget" asset="ConfigEditorPanel"/>`: `asset` is the config stem, the asset's `id` is `ConfigEditor`), so the window is `Config Editor###ConfigEditor`. The format and the host: `src/Widgets/PanelAssets/CLAUDE.md`. Editor only (`BE_WITH_EDITOR`).

## The model and its commands

`ConfigEditorPanelModel` (an `IPanelModel`, source `editor`) owns the selection, the pending switch, the open edits (a `ReflectedConfigDocument` or `StagedChanges`, "Typed and text modes"), the flags "changed on disk" / own write and the `editor_state` document source (`EditorDocumentsHandle`). `HasUnsavedEdits` is the one answer both modes give to Save, the popups and `editor_state`. The asset binds only to its reflected view `ConfigEditorView` (flags such as `hasStaged`, `showTree`, `textMode`, `configMissing`, the two popup bools and their messages). The view is rebuilt when the selection, `ConfigManager::GetConfigCount()`, the unsaved flag or a notice changed, never per frame; `Update` asks for the unsaved flag, because the drawers change it while the slot is drawn.

Commands are rows of the command table (`config.save` and `configEditor.select|reload|reloadDiscard|reloadCancel|switchDiscard|switchCancel`; the last six are not rebindable). Like every asset panel's commands they reach the model **only while the panel is open**, so `config.save` does not save the edits of a closed panel. `view.configEditor` is the asset's `command`. A popup is open while its bool in the view is true; its buttons run a command that clears the bool.

`Initialize()` (the `IPanelModel` hook, once after registration) subscribes to `ConfigChangedEvent` (commands are not an event: `AssetPanelWidget::OnCommand` calls the model's `RunCommand` while the panel is open), adds the `editor_state` source and registers the slot `configTree`. Tests reach the model through `SelectConfig`, `SaveSelectedConfig`, `GetDocument`, `GetStagedChanges`, `HasUnsavedEdits`, `DropStagedForChange`, `ShowsChangedOnDiskNotice`, `AppendDocuments` and `UseConfigManager`.

## Typed and text modes

A selected config opens as **reflected objects** when `ReflectedConfigDocument::Open` takes it, and as its **XML tree** otherwise, with a line saying why (`textModeNote`). Both are drawn in the slot `configTree`; the slot stays C++ because neither fits an asset without a language. The slot's controls keep their own `PushID`s; the ids of the control API are the chain of the asset's element ids followed by them. A frame without an edit allocates nothing of the panel's own.

- **How a config gets typed editing:** its stem has a `CookableConfig<T>` registration (`BECore/Cook/CLAUDE.md`, "Registering a config"; a game registers its own the same way). A cooker written by hand has no root object and stays text.
- **No hooks on load.** The objects are made and deserialized, never initialized: `Initialize` would start what the program already runs (a sink subscribes to the log, a `FileSink` truncates its file). The reflected load of a pointer does initialize, so the document makes the elements of the root's factory lists itself, and a pointer anywhere else makes text mode.
- **Constructors and destructors do run.** A type whose destructor changes the program is refused by name (`kTypesThatChangeTheProgram`). Nothing detects another one: a game's widget or element type whose constructor or destructor touches global state is made and destroyed by the editor like any other, so it must guard itself.
- **Read as the cook reads:** strict values; an unknown or missing `type` and a malformed value make text mode, an unknown key does not.
- **Save writes what changed** into the loaded DOM, object by object, against what each object serialized right after its load: comments, disabled elements, unknown attributes and fields left to their default stay as written; an edit undone is no edit. A changed `type` keeps the attributes the new type does not read. A type that cannot read the element is refused with a line in the panel. Elements are not added or removed.

`ReflectedConfigDrawer` draws the objects through `ImGuiPropertyVisitor` (`src/Widgets/SceneEditor/`): a field needs no editor code. `ConfigTreeDrawer` draws the XML node tree of a text config with a text field for every attribute and text, staged on Enter. It guesses no type from a value: a control other than text comes from a reflected type only. `ConfigFieldSchema` is a stub of deprecated no-ops.

## Unsaved edits

Edits stay in memory until Save. Both kinds hold node handles into the config's document, so a `ConfigChangedEvent` for the **selected** config drops them, opens it again (`DropStagedForChange`) and shows "changed on disk"; the panel's own Save and Reload ignore the event they cause, and another config changes nothing. Reload and switching configs ask first when there are unsaved edits. A save that cannot write the file keeps the config unsaved.

## Tests (only with `BE_WITH_EDITOR`)

`ReflectedConfigDocumentTest` covers the document over copies of shipped configs. `ConfigEditorSaveTest` tests the save path without ImGui: the edit goes through the document and `Apply` on a copy, `XmlDocument::SaveToFile` writes a temporary file, and the file is loaded back into the real consumer; never test it through `ConfigManager::SaveConfig` on a shipped config, that overwrites the file in the repository. `ConfigEditorPanelModelTest` covers the model and `ConfigEditorAssetTest` draws the shipped asset through a real `AssetPanelWidget` over `ConfigEditorTestModel` (the model on a test's own `ConfigManager`).

## The window id

The window is `Config Editor###ConfigEditor`: ImGui keys `imgui.ini` and named layouts by the `###` id, and the default layout's dock builder and "Reset to Defaults" name the window by it. An ini or layout saved before the asset named the window `Config Editor`; `MigrateLegacyWindowNames` renames that section on load.
