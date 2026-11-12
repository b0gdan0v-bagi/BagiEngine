#pragma once

#include "HierarchyModel.h"

namespace BECore {

    /**
     * @brief What the hierarchy panel shows and asks for, owned by SceneEditorWidget (which keeps the selection and the navigation)
     *
     * The panel changes the scene only through one pending Operation per frame, applied after the walk of the tree. The fields below
     * @c dirty are the answers the widget reads after Draw() and clears.
     */
    struct HierarchyState {
        SceneNode* selected = nullptr;
        SceneNode* renameTarget = nullptr;  ///< the row that shows a text field instead of its name
        char renameBuffer[HierarchyModel::kMaxNodeNameLength + 1] = {};
        bool renameFocus = false;  ///< the text field takes the keyboard focus on its first frame
        HierarchyModel::Operation pending;
        SceneNode* revealNode = nullptr;  ///< opened on the next walk, so a moved, copied or added node is seen

        // The filter: matches stay with their ancestors, the rest is hidden
        char searchBuffer[HierarchyModel::kMaxNodeNameLength + 1] = {};
        eastl::string_view componentType;                  ///< empty = any; a name from ClassMeta, which lives as long as the program
        eastl::vector<eastl::string_view> componentTypes;  ///< what the type combo offers, made again each time it opens
        bool typeComboOpen = false;
        bool focusSearch = false;       ///< Ctrl+F: the search box takes the keyboard focus
        SceneNode* dragNode = nullptr;  ///< the node being dragged, once found alive in the tree (the payload holds only a pointer)

        // Answers to the widget
        bool dirty = false;                  ///< the scene changed
        bool nodeRemoved = false;            ///< a subtree is gone: the widget drops what it kept of the nodes (the UI selection)
        bool addNodeRequested = false;       ///< the widget opens its "Add Node" popup
        SceneNode* addNodeParent = nullptr;  ///< ... under this node, the root when null

        /// Forgets every pointer into the tree (the scene was replaced)
        void Reset();
    };

    namespace HierarchyPanel {

        /// Draws the "Hierarchy" section into the current ImGui window: the tree, its context menu, F2 / Delete / Ctrl+D while the window is focused, and the Add Node button. Needs a frame in
        /// progress
        void Draw(Scene& scene, HierarchyState& state);

        /// Starts renaming @p node: its row shows a text field with the current name. A locked node (EditorFlags::Locked) is not renamed
        void BeginRename(HierarchyState& state, SceneNode& node);

    }  // namespace HierarchyPanel

}  // namespace BECore
