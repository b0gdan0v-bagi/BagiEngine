#pragma once

#include <BECore/Scene/SceneNode.h>

namespace BECore::HierarchyModel {

    /// A name the user types for a node is at most this many characters (the panel's rename buffer is one more, for the terminator)
    inline constexpr size_t kMaxNodeNameLength = 255;

    /// Where a dragged row lands on the row under the pointer
    enum class DropPlace : uint8_t {
        Before,  ///< as the sibling right above the target
        Into,    ///< as the last child of the target
        After,   ///< as the sibling right below the target
    };

    /// The new place of a dragged node, as the arguments of Scene::MoveNode: @c index counts the children of @c parent after the node left its old place
    struct DropTarget {
        SceneNode* parent = nullptr;
        size_t index = 0;
    };

    /// True when a search text or a component type is set, so the tree shows only what the filter keeps
    bool IsFilterActive(eastl::string_view query, eastl::string_view componentType);

    /**
     * @brief Whether the node itself passes the filter of the hierarchy panel
     *
     * The name must contain @p query (ASCII letters compared without case; empty keeps every name) and, when @p componentType is not empty, the node
     * must carry a component of that type name. Allocates nothing.
     */
    bool NodeMatchesFilter(const SceneNode& node, eastl::string_view query, eastl::string_view componentType);

    /// True when the node or any node below it passes the filter: an ancestor of a match stays in the tree (the panel dims it and opens its path)
    bool SubtreeMatchesFilter(const SceneNode& node, eastl::string_view query, eastl::string_view componentType);

    /// The upper quarter of a row drops before it, the lower quarter after it, the middle into it. @p fractionOfRowHeight is 0 at the top of the row, 1 at the bottom
    DropPlace ClassifyDropPlace(float fractionOfRowHeight);

    /**
     * @brief Whether @p dragged may be dropped at @p place of @p target
     *
     * Refused: the root; a drop onto itself or into its own subtree; a locked node being dragged; a drop whose new parent (the target for Into, the
     * target's parent for Before and After) is locked or does not exist (Before or After the root); a drop that would leave the node where it is.
     * Only the node's own lock counts, not the locks above it.
     */
    bool CanDrop(const SceneNode& dragged, const SceneNode& target, DropPlace place);

    /// The new parent and index of a drop that CanDrop() allows, ready for Scene::MoveNode. The target itself for Into (as its last child). Undefined for a refused drop
    DropTarget ResolveDrop(const SceneNode& dragged, SceneNode& target, DropPlace place);

    /// True when @p text has a character other than whitespace and is at most kMaxNodeNameLength long
    bool ValidateName(eastl::string_view text);

    /// Replaces the contents of @p out with the type names of the components in the tree below @p root (the root included), sorted and without repeats. @p out keeps its capacity
    void CollectComponentTypes(const SceneNode& root, eastl::vector<eastl::string_view>& out);

}  // namespace BECore::HierarchyModel
