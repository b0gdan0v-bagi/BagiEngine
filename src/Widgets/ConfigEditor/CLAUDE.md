# Config Editor

Edits any loaded XML config. The panel is an asset, `config/panels/ConfigEditorPanel.xml` (`<widget type="AssetPanelWidget" asset="ConfigEditorPanel"/>`: `asset` is the config stem, the asset's `id` is `ConfigEditor`), so the window is `Config Editor###ConfigEditor`. The format and the host: `src/Widgets/PanelAssets/CLAUDE.md`. Editor only (`BE_WITH_EDITOR`).

## The model and its commands

`ConfigEditorPanelModel` (an `IPanelModel`, source `editor`) owns the selection, the pending switch, `StagedChanges`, the flags "changed on disk" / own write and the `editor_state` document source (`EditorDocumentsHandle`). The asset binds only to its reflected view `ConfigEditorView` (flags such as `hasStaged`, `showTree`, `configMissing`, the two popup bools and their messages). The view is rebuilt when the selection, `ConfigManager::GetConfigCount()`, a staged flag or a notice changed, never per frame; `hasStaged` is also refreshed in `Update`, because the inspectors change it while the tree is drawn.

Commands are rows of the command table (`config.save` and `configEditor.select|reload|reloadDiscard|reloadCancel|switchDiscard|switchCancel`; the last six are not rebindable). Like every asset panel's commands they reach the model **only while the panel is open**, so `config.save` does not save the staged edits of a closed panel. `view.configEditor` is the asset's `command`. A popup is open while its bool in the view is true; its buttons run a command that clears the bool.

`Initialize()` (the `IPanelModel` hook, once after registration) subscribes to `ConfigChangedEvent` (commands are not an event: `AssetPanelWidget::OnCommand` calls the model's `RunCommand` while the panel is open), adds the `editor_state` source, registers the field-schema entries and the slot `configTree`. Tests reach the model through `SelectConfig`, `SaveSelectedConfig`, `GetStagedChanges`, `DropStagedForChange`, `ShowsChangedOnDiskNotice`, `AppendDocuments` and `UseConfigManager`.

## The tree stays C++

`ConfigTreeDrawer` draws the XML node tree with the typed inspectors behind the slot `configTree`: a recursive tree of arbitrary nodes with per-attribute widgets does not fit an asset without a language. The slot's controls keep their own `PushID`s; the ids of the control API are the chain of the asset's element ids followed by them. The tree builds its keys on a scratch scope and its controls sit under `PushID` with fixed labels, so a frame allocates nothing (an edit does).

## Staged edits

Edits stay in `StagedChanges` until Save. They hold node handles into the document, so a `ConfigChangedEvent` for the **selected** config drops them (`DropStagedForChange`) and shows "changed on disk"; the panel's own Save and Reload ignore the event they cause, and another config changes nothing. Reload and switching configs ask first when staged edits exist.

## The field schema

Field typing comes from `ConfigFieldSchema` (registered fields per config path, for example `LoggerConfig` -> `sink/@type`), the controls from `FieldInspectors`. New config attributes with a fixed value set are registered in the model's `Initialize`:

- a `type` attribute that the loader resolves through `AbstractFactory<Base>` is registered with `RegisterFactoryTypes<Base>(config, "element/@type")`. The options are the class names the factory has registered (sorted), exactly the strings the loader accepts, so the list cannot drift from the code. Do not mirror them in an enum: the `CORE_ENUM` that the generator emits for a `FACTORY_BASE` has other names and another order;
- an attribute read through `EnumUtils<E>` is registered with `RegisterEnum<E>`; `RegisterFlags` takes the names of a `|`-joined value (the SDL window flags come from `SDLUtils::GetWindowFlagNames()`, the table the loader reads). A token of the value that is not an option stays in the Flags control as a checked row marked `(not a known value)` until the user unchecks it (`FlagValue.h`, pure helpers).

`Resolve` returns a reference valid until the next `Register*`. An `Enum` combo shows a current value that is not among the options (a typo, or a name the code no longer has) instead of an empty box and marks it `(not a known value)`; choosing an option replaces it.

## Tests (only with `BE_WITH_EDITOR`)

`ConfigFieldSchemaTest` checks that the options are creatable by the factory and that the shipped `LoggerConfig.xml` uses only offered values. `ConfigEditorSaveTest` tests the save path without ImGui: the edit goes through `StagedChanges::Set` and `ApplyTo` on a copy, `XmlDocument::SaveToFile` writes a temporary file, and the file is loaded back into the real consumer; never test it through `ConfigManager::SaveConfig` on a shipped config, that overwrites the file in the repository. `ConfigEditorPanelModelTest` covers the model and `ConfigEditorAssetTest` draws the shipped asset through a real `AssetPanelWidget` over `ConfigEditorTestModel` (the model on a test's own `ConfigManager`).

## The window id

The window is `Config Editor###ConfigEditor`: ImGui keys `imgui.ini` and named layouts by the `###` id, and the ready layouts of the workspaces dock the window by it. An ini or layout saved before the asset named the window `Config Editor`; `MigrateLegacyWindowNames` renames that section on load.
