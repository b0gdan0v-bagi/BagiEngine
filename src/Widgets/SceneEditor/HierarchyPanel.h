#pragma once

#include "HierarchyModel.h"
#include "SceneSelection.h"

namespace BECore {

    /**
     * @brief What the hierarchy panel shows and asks for, owned by SceneEditorWidget (which keeps the selection and the navigation)
     *
     * The panel changes the scene only through one pending Operation per frame, applied after the walk of the tree. The fields below
     * @c dirty are the answers the widget reads after Draw() and clears.
     */
    struct HierarchyState {
        SceneNode* selected = nullptr;      ///< the primary node of the selection; the widget keeps it equal to SceneSelection::Primary() after each Draw()
        SceneNode* renameTarget = nullptr;  ///< the row that shows a text field instead of its name
        char renameBuffer[HierarchyModel::kMaxNodeNameLength + 1] = {};
        bool renameFocus = false;  ///< the text field takes the keyboard focus on its first frame
        HierarchyModel::Operation pending;
        SceneNode* revealNode = nullptr;      ///< opened on the next walk, so a moved, copied or added node is seen
        eastl::vector<SceneNode*> openNodes;  ///< opened on the next walk (a reveal request: the ancestors of the node); kept as a member for its capacity
        SceneNode* scrollTo = nullptr;        ///< the row scrolls into view on the next walk

        // The filter: matches stay with their ancestors, the rest is hidden
        char searchBuffer[HierarchyModel::kMaxNodeNameLength + 1] = {};
        StringView componentType;                  ///< empty = any; a name from ClassMeta, which lives as long as the program
        eastl::vector<StringView> componentTypes;  ///< what the type combo offers, made again each time it opens
        bool typeComboOpen = false;
        bool focusSearch = false;       ///< Ctrl+F: the search box takes the keyboard focus
        SceneNode* dragNode = nullptr;  ///< the node being dragged, once found alive in the tree (the payload holds only a pointer)

        // Answers to the widget
        SceneNode* clickedNode = nullptr;    ///< a row was clicked: the widget applies the click to its SceneSelection (@c clickModifiers) and clears this
        uint32_t clickModifiers = 0;         ///< the kSelectionModifier* bits held with that click
        bool dirty = false;                  ///< the scene changed
        bool nodeRemoved = false;            ///< a subtree is gone: the widget drops what it kept of the nodes (the UI selection)
        bool addNodeRequested = false;       ///< the widget opens its "Add Node" popup
        SceneNode* addNodeParent = nullptr;  ///< ... under this node, the root when null

        /// Forgets every pointer into the tree (the scene was replaced)
        void Reset();
    };

    namespace HierarchyPanel {

        /// Draws the "Hierarchy" section into the current ImGui window: the tree, its context menu, F2 / Delete / Ctrl+D while the window is focused, and the Add Node button. Needs a frame in
        /// progress. Every row of @p selection is highlighted; a click on a row only sets state.selected, clickedNode and clickModifiers (Ctrl or Cmd toggles), the widget changes the selection.
        /// F2, Delete, Ctrl+D, the context menu and a drag act on state.selected, the primary node, alone
        void Draw(Scene& scene, HierarchyState& state, const SceneSelection& selection);

        /**
         * @brief Selects @p node and prepares the next Draw() to show it: its ancestors open, its row scrolls into view, a filter that hides it is cleared
         *
         * @p node is looked up in the tree of @p active by pointer and is never dereferenced before it is found. A request for another scene (@p scene is not
         * @p active), for null or for a node that is not in the tree changes nothing and gives false. A locked node is revealed like any other. Needs no frame.
         */
        bool Reveal(Scene& active, HierarchyState& state, const Scene* scene, const SceneNode* node);

        /// Starts renaming @p node: its row shows a text field with the current name. A locked node (EditorFlags::Locked) is not renamed
        void BeginRename(HierarchyState& state, SceneNode& node);

    }  // namespace HierarchyPanel

}  // namespace BECore
