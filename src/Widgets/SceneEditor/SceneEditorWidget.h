#pragma once

#include <BECore/Scene/SceneNode.h>
#include <BECore/Widgets/IWidget.h>

namespace pugi {
    class xml_node;
}

namespace BECore {

    /**
     * @brief ImGui widget for real-time scene editing
     *
     * Displays a two-panel layout:
     *   Left panel  - Hierarchy tree of scene nodes
     *   Right panel - Inspector for selected node and its components
     *
     * All edits are applied live without staging.
     */
    class SceneEditorWidget : public IWidget {
        BE_CLASS(SceneEditorWidget)
    public:
        SceneEditorWidget() = default;
        ~SceneEditorWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        /**
         * @brief Recursively render the hierarchy tree starting from a node.
         *        Uses the node pointer as the ImGui tree node ID.
         */
        void RenderHierarchyNode(SceneNode& node);

        /**
         * @brief Render the inspector panel for the selected node
         */
        void RenderInspector();

        /**
         * @brief Render the "Add Node" modal popup
         */
        void RenderAddNodePopup();

        /**
         * @brief Render the "Add Component" modal popup
         */
        void RenderAddComponentPopup();

        /**
         * @brief Serialize the active scene to SceneConfig.xml via ConfigManager
         */
        void SaveScene();

        SceneNode* _selectedNode = nullptr;
        SceneNode* _contextMenuTargetNode = nullptr;  // node to add a child to (null = root)
        char _nodeNameBuffer[256] = {};
        bool _showAddNodePopup = false;
        bool _showAddComponentPopup = false;
        bool _isDirty = false;
    };

}  // namespace BECore
