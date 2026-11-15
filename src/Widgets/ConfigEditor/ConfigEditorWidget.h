#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/ConfigEditor/StagedChanges.h>
#include <Widgets/EditorLayout/EditorCommands.h>

// Forward declaration - avoid including pugixml in header
namespace pugi {
    class xml_node;
}

namespace BECore {

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

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        /**
         * @brief Runs `config.save` like the Save button (only with staged changes)
         */
        void OnCommand(const EditorCommandEvent& event);

        /**
         * @brief Applies the staged changes to the selected config and saves its file; the one path of the button and the command
         */
        void SaveSelectedConfig();

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
        bool _showReloadConfirm = false;
        bool _showSwitchConfirm = false;
    };

}  // namespace BECore
