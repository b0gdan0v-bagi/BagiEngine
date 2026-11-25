#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/ConfigEditor/ConfigTreeDrawer.h>
#include <Widgets/ConfigEditor/StagedChanges.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PanelAssets/PanelSlots.h>

namespace BECore {

    class ConfigManager;

    /// A row of the list of loaded configs; `selected` highlights the one the editor shows
    struct ConfigListRow {
        BE_CLASS(ConfigListRow)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD bool selected = false;
    };

    /**
     * @brief What `ConfigEditorPanel.xml` binds to: the selection state of the Config Editor as bools, texts and rows
     *
     * A condition of an asset is one bool (the format has no AND), so every combination the panel shows is a field of its own.
     */
    struct ConfigEditorView {
        BE_CLASS(ConfigEditorView)

        BE_REFLECT_FIELD Vector<ConfigListRow> configs;
        BE_REFLECT_FIELD String configsCount;
        BE_REFLECT_FIELD bool hasSelection = false;
        BE_REFLECT_FIELD bool noSelection = true;
        BE_REFLECT_FIELD String configName;
        /// The file of the selected config in brackets, empty when it has none
        BE_REFLECT_FIELD String filePathText;
        BE_REFLECT_FIELD bool hasPath = false;
        BE_REFLECT_FIELD bool hasStaged = false;
        BE_REFLECT_FIELD bool changedOnDisk = false;
        /// The selected config is loaded: the tree slot shows; `configMissing` is the same selection with no document to show
        BE_REFLECT_FIELD bool showTree = false;
        BE_REFLECT_FIELD bool configMissing = false;
        BE_REFLECT_FIELD bool reloadConfirmOpen = false;
        BE_REFLECT_FIELD bool switchConfirmOpen = false;
        BE_REFLECT_FIELD String reloadMessage;
        BE_REFLECT_FIELD String switchMessage;
    };

    /**
     * @brief The model of the Config Editor asset: the selection, the staged edits and the save / reload rules behind a reflected view
     *
     * Edits are staged in memory until Save; Reload and switching configs ask first when staged edits exist. The tree of XML nodes is the slot `configTree`
     * (`ConfigTreeDrawer`), registered by `Initialize`. The staged edits hold node handles into the document, so a change of the selected config under the
     * editor drops them (`DropStagedForChange`).
     *
     * Commands (`EditorCommandIds`): `config.save`, `configEditor.select` (the argument is the name of a loaded config; any other text does nothing),
     * `configEditor.reload` and the four that answer the two popups. The view is rebuilt only when the selection, the number of configs, a staged flag or a
     * notice changed, so a steady frame allocates nothing.
     */
    class ConfigEditorPanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(ConfigEditorPanelModel)
    public:
        ConfigEditorPanelModel() = default;

        /// Registers the field schema entries, the subscription, the `editor_state` source and the slot; called once by the widget that hosts the model
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        /**
         * @brief The config of the panel's selection changed under it (the file was edited on disk, or a save / reload ran)
         *
         * The staged edits hold node handles into the replaced document, so they are dropped, and one line says so when there were any. Not for the
         * panel's own Save and Reload: they ignore the event they cause. A config that is not the selected one changes nothing.
         * @return true when staged edits were dropped
         */
        bool DropStagedForChange(PoolString changedConfig);

        /// Whether the panel shows the "changed on disk" line (until the next selection, save or reload of its own)
        [[nodiscard]] bool ShowsChangedOnDiskNotice() const {
            return _showChangedOnDisk;
        }

        /// The selected config with its unsaved flag (staged edits), for `editor_state` (`.claude/rules/control-editor-api.md`); nothing with no selection
        void AppendDocuments(Vector<EditorDocumentState>& out);

        /// What a click on the list does without staged edits: selects @p name and starts clean
        void SelectConfig(PoolString name);

        /// Applies the staged changes to the selected config and saves its file; the one path of the Save button and the command
        void SaveSelectedConfig();

        /// The edits not saved yet: the inspectors stage into it, a test stages through it
        StagedChanges& GetStagedChanges() {
            return _staged;
        }

        /// Works on @p manager instead of CoreManager's (a test over a config folder of its own); null goes back to CoreManager's
        void UseConfigManager(ConfigManager* manager) {
            _configManager = manager;
        }

        [[nodiscard]] const ConfigEditorView& GetView() const {
            return _view;
        }

    private:
        ConfigManager& Manager() const;
        void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event);
        void SelectByName(StringView name);
        void RequestReload();
        /// Reads the selected config again; the event this causes is the panel's own
        void ReloadSelectedConfig();
        void ClearTransientState();
        void DrawTree();
        void RebuildView();

        PoolString _selectedConfig;
        PoolString _pendingSwitchConfig;  // config to switch to after confirming discard

        StagedChanges _staged;
        ConfigTreeDrawer _tree{_staged};
        ConfigManager* _configManager = nullptr;  // null: CoreManager's
        bool _ownWrite = false;                   // set while the panel itself saves or reloads, so it ignores the event it causes
        bool _showChangedOnDisk = false;
        bool _showReloadConfirm = false;
        bool _showSwitchConfirm = false;

        ConfigEditorView _view;
        size_t _builtConfigCount = 0;
        bool _builtConfigPresent = false;
        bool _viewDirty = true;

        // The registrations come last: they end first, while the members their callbacks read are alive
        EditorDocumentsHandle _documents;  // the source of `editor_state`
        PanelSlotHandle _treeSlot;
    };

}  // namespace BECore
