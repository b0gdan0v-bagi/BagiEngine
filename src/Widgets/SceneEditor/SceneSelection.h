#pragma once

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>

namespace BECore {

    /// The keys held with a click, as bits of the @c modifiers field of the viewport events
    inline constexpr uint32_t kSelectionModifierCtrl = 1;   ///< Ctrl, or Cmd on macOS: toggles
    inline constexpr uint32_t kSelectionModifierShift = 2;  ///< Shift: adds

    /**
     * @brief The nodes the scene editor has selected, in the order they were picked; the last one is the primary (the one the inspector shows and the gizmo pivots on).
     *
     * Raw pointers: the set never dereferences one, and Prune() drops those that are no longer in the tree, which the owner calls every frame because a node can be
     * deleted by anything. Every change returns whether the set (or its primary) is different, so the owner announces a change once. The vector keeps its capacity.
     */
    class SceneSelection {
    public:
        /// The selection becomes @p node alone (nothing for null)
        bool Set(SceneNode* node);

        /// Adds @p node when it is not selected, removes it when it is; null does nothing. An added node becomes the primary
        bool Toggle(SceneNode* node);

        /// Adds @p node when it is not selected (it becomes the primary); a node that is selected stays where it is. Null does nothing
        bool Add(SceneNode* node);

        bool Clear();

        [[nodiscard]] bool Contains(const SceneNode* node) const;

        /// The last selected node, or null for none
        [[nodiscard]] SceneNode* Primary() const;

        [[nodiscard]] Span<SceneNode* const> Nodes() const;

        [[nodiscard]] size_t Count() const;

        /// Drops the nodes that are not in the tree of @p scene (compared by pointer, never dereferenced)
        bool Prune(Scene& scene);

    private:
        Vector<SceneNode*> _nodes;
    };

    /**
     * @brief Applies a click on @p node to @p selection: no modifier replaces the selection by the node (a click on nothing clears it), Ctrl toggles the node, Shift adds it.
     * A click on nothing with a modifier changes nothing. Ctrl wins when both are held.
     */
    bool ApplyClick(SceneSelection& selection, SceneNode* node, uint32_t modifiers);

}  // namespace BECore
