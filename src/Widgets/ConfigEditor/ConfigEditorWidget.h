#pragma once

#include "StagedChanges.h"

#include <BECore/Widgets/IWidget.h>

// Forward declaration - avoid including pugixml in header
namespace pugi {
    class xml_node;
}

namespace BECore {

    class IDeserializer;

    /**
     * @brief ImGui widget for browsing, editing, saving, and reloading XML configs
     *
     * Displays a two-panel layout:
     *   Left panel  - list of all loaded config names
     *   Right panel - recursive XML tree editor for the selected config
     *
     * Edits are staged in memory until the user clicks Save. Reload and config
     * switching warn the user if there are unsaved staged changes.
     */
    class ConfigEditorWidget : public IWidget {
        BE_CLASS(ConfigEditorWidget)
    public:
        ConfigEditorWidget() = default;
        ~ConfigEditorWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        /**
         * @brief Recursively render an XML node as an ImGui tree with typed field inspectors
         * @param node The pugixml node to render
         * @param nodePath Unique path string used as key for staged changes
         */
        void RenderXmlNode(pugi::xml_node node, const eastl::string& nodePath);

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
        bool _showReloadConfirm = false;
        bool _showSwitchConfirm = false;
    };

}  // namespace BECore
