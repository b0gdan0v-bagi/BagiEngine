#pragma once

#include <BECore/Widgets/IWidget.h>
#include <BECore/PoolString/PoolString.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <array>

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
     * Editing modifies the live pugixml DOM directly. Save/Reload propagate
     * changes to/from disk via ConfigManager.
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
         * @brief Recursively render an XML node as an ImGui tree with editable attributes
         * @param node The pugixml node to render
         * @param nodePath Unique path string used as key for edit buffers
         */
        void RenderXmlNode(pugi::xml_node node, const eastl::string& nodePath);

        /**
         * @brief Get (or create) a persistent char buffer for an ImGui InputText field
         * @param key Unique key for the buffer (nodePath + attribute name)
         * @param initialValue Value to initialise the buffer with on first access
         */
        char* GetOrCreateBuffer(const eastl::string& key, eastl::string_view initialValue);

        PoolString _selectedConfig;
        bool _hasUnsavedChanges = false;

        // Maps unique field key → persistent InputText buffer (128 chars each)
        eastl::unordered_map<eastl::string, eastl::array<char, 128>> _editBuffers;
    };

}  // namespace BECore
