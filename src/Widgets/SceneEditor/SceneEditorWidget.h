#pragma once

#include <BECore/Scene/SceneNode.h>
#include <BECore/Widgets/IWidget.h>
#include <EASTL/string.h>

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
         * @brief Recursively render the hierarchy tree starting from a node
         * @param node The node to render and its children
         * @param nodeId Unique identifier for ImGui tree node
         */
        void RenderHierarchyNode(SceneNode& node, int nodeId);

        /**
         * @brief Render the inspector panel for the selected node
         */
        void RenderInspector();

        /**
         * @brief Show a popup to add a new child node
         */
        void RenderAddNodePopup();

        /**
         * @brief Show a popup to add a new component to the selected node
         */
        void RenderAddComponentPopup();

        SceneNode* _selectedNode = nullptr;
        eastl::string _nodeNameBuffer;
        bool _showAddNodePopup = false;
        bool _showAddComponentPopup = false;
        int _hierarchyNodeCounter = 0;
    };

}  // namespace BECore
