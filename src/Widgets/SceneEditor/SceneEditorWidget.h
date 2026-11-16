#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIElement.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>
#include <Widgets/SceneEditor/SceneSelection.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <Widgets/SceneEditor/UINodeFileSession.h>
#include <Widgets/Viewport/ViewportEvents.h>

namespace pugi {
    class xml_node;
}

namespace BECore {

    class UICanvasComponent;

    /**
     * @brief ImGui panel for live scene editing: the hierarchy tree on the left, the inspector of the selected node
     * and its components on the right. Edits apply at once, without staging.
     */
    class SceneEditorWidget : public IWidget {
        BE_CLASS(SceneEditorWidget)
    public:
        SceneEditorWidget() = default;
        /// Clears the editor flags (hidden, locked) of every scene, so Game mode is never left with a hidden node. A switch of the active scene clears nothing by itself
        ~SceneEditorWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

    private:
        /**
         * @brief The "UI elements" section of a node with a canvas: the element tree and the rect inspector of the selected element
         *
         * Edits the authored `style.rect` of the element, live, and sends UIRectEditEvent for each changed frame. Save writes the rect into the node file the node is an instance of when
         * nothing else in the scene uses that file (UINodeFileSession); otherwise, and for a node written in the scene file, the edit is saved in the scene file (an instance: as an `<elements>`
         * override). A `.ui.xml` is never edited.
         * @return true when an element changed
         */
        bool RenderUIElements(UICanvasComponent& canvas);

        /// One line under the "UI elements" title that says where the edit of the selected node goes on Save (its node file, or the scene file and why)
        void DrawUIFileStatus();

        /// Forgets the selected canvas element (the node changed or the scene was replaced)
        void ResetUISelection();

        /**
         * @brief Runs `scene.save` like the Save button (only with unsaved changes)
         */
        void OnCommand(const EditorCommandEvent& event);

        /// Answers SelectSceneNodeRequestEvent: selects and reveals the node when it is alive in the active scene, else does nothing
        void OnSelectRequest(const SelectSceneNodeRequestEvent& event);

        /// Answers SelectUIElementRequestEvent: like OnSelectRequest, and the element is shown in the "UI elements" section when it is still in the canvas of the node
        void OnSelectUIElementRequest(const SelectUIElementRequestEvent& event);

        /// Shows @p element (compared by pointer, so a stale one is safe) of the canvas of @p node in the "UI elements" section, when @p node is the primary selection and has a canvas
        void SelectUIElement(SceneNode& node, const UIElement* element);

        /// Answers a left click of the viewport in the Edit input mode: picks the topmost node or canvas element under it (ScenePick) and applies it with ApplyClick (Ctrl toggles, Shift
        /// adds, a plain click replaces the selection; a plain click on nothing clears it, with Ctrl or Shift a click on nothing changes nothing). The primary node is revealed like a
        /// request does, only if it stays selected. A hit on a canvas element also selects that element in the "UI elements" section
        void OnViewportClick(const ViewportEditClickEvent& event);

        /// Answers the end of a rubber-band drag: the nodes the box touches (ScenePick::PickInRect) replace the selection, or are added to it with Ctrl or Shift
        void OnViewportBoxSelect(const ViewportBoxSelectEvent& event);

        /// Answers the end of a gizmo drag: a Commit marks the scene as changed (Begin, Change and Cancel leave the nodes as the drag found them or put them back)
        void OnTransformEdit(const NodeTransformEditEvent& event);

        /// Answers a drag of a canvas element in the viewport: a Change is a UIRectEditEvent like the inspector's (an undo stack merges the frames) and tells whether the layout kept the spec exact, a
        /// Commit notes the element for the Save and marks the scene as changed, a Cancel forgets the inexact mark
        void OnUIRectDrag(const UIRectDragEvent& event);

        /// Brings the selection and HierarchyState::selected back in line after the hierarchy panel or a handler changed one of them: applies the clicked row, drops the nodes
        /// that are no longer in the tree, takes a node that only HierarchyState::selected names (a created, duplicated or revealed one) as the whole selection, and makes the
        /// primary node the selected one
        void SyncSelection(Scene& scene);

        /// Sends SceneNodeSelectedEvent when the primary node (or the scene it belongs to) differs from the last one announced, and SceneSelectionChangedEvent when the set does
        void AnnounceSelection(const Scene* scene);

        /// Sends UIElementSelectedEvent when the (node, element) pair of the "UI elements" selection, or its scene, differs from the last one announced
        void AnnounceUIElement(const Scene* scene);

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

        HierarchyState _hierarchy;           // the primary node, the rename and the pending tree edit of the hierarchy panel
        SceneSelection _selection;           // every selected node; its primary is `_hierarchy.selected`
        Vector<SceneNode*> _boxHits;         // the nodes of a rubber band, kept for its capacity
        Vector<SceneNode*> _announcedNodes;  // what SceneSelectionChangedEvent last told
        const Scene* _announcedNodesScene = nullptr;
        HierarchyModel::SelectionTracker _announcedSelection;  // what SceneNodeSelectedEvent last told
        Scene* _lastScene = nullptr;                           // the scene the selection belongs to: a replaced scene drops it
        SceneNode* _uiSelectionNode = nullptr;                 // the node whose canvas `_selectedUIElement` belongs to
        IntrusivePtr<UIElement> _selectedUIElement;            // held, so that a tree edit cannot leave it dangling
        UINodeFileSession _uiFileSession;                      // the elements changed since the last save, whose rects go into their node files
        IntrusivePtr<UIElement> _inexactUIElement;             // the element whose last edit the layout limits kept from being exact (the inspector warns)
        SceneNode* _contextMenuTargetNode = nullptr;           // node to add a child to (null = root)
        char _nodeNameBuffer[256] = {};
        bool _showAddNodePopup = false;
        bool _showAddComponentPopup = false;
        bool _isDirty = false;
        bool _focusRequested = false;  // a reveal was asked for: the window comes to the front on the next Update (never inside the event), so a background dock tab shows the hierarchy

        // What UIElementSelectedEvent last told: compared, never dereferenced
        const Scene* _announcedUIScene = nullptr;
        const SceneNode* _announcedUINode = nullptr;
        const UIElement* _announcedUIElement = nullptr;
    };

}  // namespace BECore
