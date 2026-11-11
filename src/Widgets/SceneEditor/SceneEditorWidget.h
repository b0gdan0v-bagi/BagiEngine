#pragma once

#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIElement.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace pugi {
    class xml_node;
}

namespace BECore {

    class UICanvasComponent;

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
         * @brief The "UI elements" section of a node with a canvas: the element tree and the rect inspector of the selected element
         *
         * Edits the authored `style` of the element, live; it is not saved to a node file or a `.ui.xml` yet.
         * @return true when an element changed
         */
        bool RenderUIElements(UICanvasComponent& canvas);

        /// Forgets the selected canvas element (the node changed or the scene was replaced)
        void ResetUISelection();

        /**
         * @brief Runs `scene.save` like the Save button (only with unsaved changes)
         */
        void OnCommand(const EditorCommandEvent& event);

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
         * @brief Replace the `<scene>` of the active scene in SceneConfig.xml (WriteSceneToConfig) and save the file
         *        through ConfigManager. The other scenes of the file stay as they are.
         */
        void SaveScene();

        SceneNode* _selectedNode = nullptr;
        Scene* _lastScene = nullptr;                  // the scene the selection belongs to: a replaced scene drops it
        SceneNode* _uiSelectionNode = nullptr;        // the node whose canvas `_selectedUIElement` belongs to
        IntrusivePtr<UIElement> _selectedUIElement;   // held, so that a tree edit cannot leave it dangling
        SceneNode* _contextMenuTargetNode = nullptr;  // node to add a child to (null = root)
        char _nodeNameBuffer[256] = {};
        bool _showAddNodePopup = false;
        bool _showAddComponentPopup = false;
        bool _isDirty = false;
    };

}  // namespace BECore
