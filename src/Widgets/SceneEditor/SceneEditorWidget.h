#pragma once

#include "HierarchyPanel.h"
#include "SceneSelection.h"
#include "SceneSelectionEvents.h"

#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIElement.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Viewport/ViewportEvents.h>

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
        /// Clears the editor flags (hidden, locked) of every scene, so Game mode is never left with a hidden node. A switch of the active scene clears nothing by itself
        ~SceneEditorWidget() override;

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

        /// Answers SelectSceneNodeRequestEvent: selects and reveals the node when it is alive in the active scene, else does nothing
        void OnSelectRequest(const SelectSceneNodeRequestEvent& event);

        /// Answers a left click of the viewport in the Edit input mode: picks the topmost node or canvas element under it (ScenePick) and reveals it like a request does. A hit on a canvas
        /// element also selects that element in the "UI elements" section; a click on nothing changes nothing
        void OnViewportClick(const ViewportEditClickEvent& event);

        /// Answers the end of a rubber-band drag: the nodes the box touches (ScenePick::PickInRect) replace the selection, or are added to it with Ctrl or Shift
        void OnViewportBoxSelect(const ViewportBoxSelectEvent& event);

        /// Answers the end of a gizmo drag: a Commit marks the scene as changed (Begin, Change and Cancel leave the nodes as the drag found them or put them back)
        void OnTransformEdit(const NodeTransformEditEvent& event);

        /// Brings the selection and HierarchyState::selected back in line after the hierarchy panel or a handler changed one of them: applies the clicked row, drops the nodes
        /// that are no longer in the tree, takes a node that only HierarchyState::selected names (a created, duplicated or revealed one) as the whole selection, and makes the
        /// primary node the selected one
        void SyncSelection(Scene& scene);

        /// Sends SceneNodeSelectedEvent when the primary node (or the scene it belongs to) differs from the last one announced, and SceneSelectionChangedEvent when the set does
        void AnnounceSelection(const Scene* scene);

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

        HierarchyState _hierarchy;                  // the primary node, the rename and the pending tree edit of the hierarchy panel
        SceneSelection _selection;                  // every selected node; its primary is `_hierarchy.selected`
        eastl::vector<SceneNode*> _boxHits;         // the nodes of a rubber band, kept for its capacity
        eastl::vector<SceneNode*> _announcedNodes;  // what SceneSelectionChangedEvent last told
        const Scene* _announcedNodesScene = nullptr;
        HierarchyModel::SelectionTracker _announcedSelection;  // what SceneNodeSelectedEvent last told
        Scene* _lastScene = nullptr;                           // the scene the selection belongs to: a replaced scene drops it
        SceneNode* _uiSelectionNode = nullptr;                 // the node whose canvas `_selectedUIElement` belongs to
        IntrusivePtr<UIElement> _selectedUIElement;            // held, so that a tree edit cannot leave it dangling
        IntrusivePtr<UIElement> _inexactUIElement;             // the element whose last edit the layout limits kept from being exact (the inspector warns)
        SceneNode* _contextMenuTargetNode = nullptr;           // node to add a child to (null = root)
        char _nodeNameBuffer[256] = {};
        bool _showAddNodePopup = false;
        bool _showAddComponentPopup = false;
        bool _isDirty = false;
        bool _focusRequested = false;  // a reveal was asked for: the window comes to the front on the next Update (never inside the event), so a background dock tab shows the hierarchy
    };

}  // namespace BECore
