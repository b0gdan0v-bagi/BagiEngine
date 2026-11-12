#pragma once

#include "HierarchyPanel.h"

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
     *   Left panel  - HierarchyPanel: tree of scene nodes
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
         * Edits the authored `style` of the element, live. It is saved in the scene file (for an instance of a node file the whole `<elements>` list as an override); the node file and a `.ui.xml` are
         * never edited.
         * @return true when an element changed
         */
        bool RenderUIElements(UICanvasComponent& canvas);

        /// Forgets the selected canvas element (the node changed or the scene was replaced)
        void ResetUISelection();

        /**
         * @brief Runs `scene.save` like the Save button (only with unsaved changes)
         */
        void OnCommand(const EditorCommandEvent& event);

        /// Takes what the hierarchy panel asked for in the last Draw(): the dirty mark, the forgotten UI selection of a removed subtree, the "Add Node" popup
        void ApplyHierarchyAnswers();

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
         *        through ConfigManager, with the node files found through the FileSystem. The other scenes of the file stay as they are;
         *        an instance that cannot be written as a reference is written inline and logged as a warning.
         */
        void SaveScene();

        HierarchyState _hierarchy;                    // the selected node, the rename and the pending tree edit of the hierarchy panel
        Scene* _lastScene = nullptr;                  // the scene the selection belongs to: a replaced scene drops it
        SceneNode* _uiSelectionNode = nullptr;        // the node whose canvas `_selectedUIElement` belongs to
        IntrusivePtr<UIElement> _selectedUIElement;   // held, so that a tree edit cannot leave it dangling
        IntrusivePtr<UIElement> _inexactUIElement;    // the element whose last edit the layout limits kept from being exact (the inspector warns)
        SceneNode* _contextMenuTargetNode = nullptr;  // node to add a child to (null = root)
        char _nodeNameBuffer[256] = {};
        bool _showAddNodePopup = false;
        bool _showAddComponentPopup = false;
        bool _isDirty = false;
    };

}  // namespace BECore
