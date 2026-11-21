#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Events/ConfigEvents.h>
#include <Widgets/ConfigEditor/StagedChanges.h>
#include <Widgets/EditorLayout/EditorCommands.h>

// Forward declaration - avoid including pugixml in header
namespace pugi {
    class xml_node;
}

namespace BECore {

    class ConfigManager;
    class IDeserializer;

    /**
     * @brief ImGui panel for browsing, editing, saving and reloading XML configs
     *
     * A list of the loaded configs on the left, a recursive XML tree editor on the right. Edits are staged in
     * memory until Save; Reload and switching configs warn when staged changes exist.
     */
    class ConfigEditorWidget : public IWidget {
        BE_CLASS(ConfigEditorWidget)
    public:
        ConfigEditorWidget() = default;
        ~ConfigEditorWidget() override = default;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

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

        /// What a click on the list does: selects @p name and starts without staged edits
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

    private:
        ConfigManager& Manager();

        bool _open = true;  // the window's close button and its `view.*` command; a closed panel is skipped like a hidden dock tab
        /**
         * @brief Runs `config.save` like the Save button (only with staged changes)
         */
        void OnCommand(const EditorCommandEvent& event);

        void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event);

        /// Reads the selected config again; the event this causes is the panel's own
        void ReloadSelectedConfig();

        /**
         * @brief Recursively render an XML node as an ImGui tree with typed field inspectors
         * @param node The pugixml node to render
         * @param nodePath Unique path string used as key for staged changes
         */
        void RenderXmlNode(pugi::xml_node node, const String& nodePath);

        /**
         * @brief Render the reload confirmation popup
         * @return True if the user confirmed the reload
         */
        bool RenderReloadConfirmPopup();

        /**
         * @brief Clear all transient state for the current config selection
         */
        void ClearTransientState();

        PoolString _selectedConfig;
        PoolString _pendingSwitchConfig;  // config to switch to after confirming discard

        StagedChanges _staged;
        ConfigManager* _configManager = nullptr;  // null: CoreManager's
        bool _ownWrite = false;                   // set while the panel itself saves or reloads, so it ignores the event it causes
        bool _showChangedOnDisk = false;
        bool _showReloadConfirm = false;
        bool _showSwitchConfirm = false;
    };

}  // namespace BECore
