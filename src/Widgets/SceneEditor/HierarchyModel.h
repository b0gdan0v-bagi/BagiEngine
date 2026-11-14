#pragma once

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>

namespace BECore::HierarchyModel {

    /// A name the user types for a node is at most this many characters (the panel's rename buffer is one more, for the terminator)
    inline constexpr size_t kMaxNodeNameLength = 255;

    /// Where a dragged row lands on the row under the pointer: Before = the sibling right above the target, Into = the last child of the target, After = the sibling right below it
    CORE_ENUM(DropPlace, uint8_t, Before, Into, After)

    /// The new place of a dragged node, as the arguments of Scene::MoveNode: @c index counts the children of @c parent after the node left its old place
    struct DropTarget {
        SceneNode* parent = nullptr;
        size_t index = 0;
    };

    /// The one change of the tree a frame of the panel asks for. The panel walks the tree, notes the request and applies it after the walk (ApplyOperation), because Delete and Move change the child
    /// lists being walked. Delete removes @c node with its subtree, Duplicate makes a copy right after it, Move puts @c node at @c index under @c parent (the arguments of Scene::MoveNode)
    CORE_ENUM(OperationKind, uint8_t, None, Delete, Duplicate, Move)

    struct Operation {
        OperationKind kind = OperationKind::None;
        SceneNode* node = nullptr;
        SceneNode* parent = nullptr;  ///< Move: the new parent
        size_t index = 0;             ///< Move: the place among its children
    };

    struct OperationResult {
        bool changed = false;          ///< the tree is different, so the scene is dirty
        SceneNode* created = nullptr;  ///< Duplicate: the copy
        SceneNode* reveal = nullptr;   ///< the node whose children the panel opens so the result is seen
    };

    /**
     * @brief Applies @p operation to @p scene; a request that cannot be done (the root, a node of another scene, a refused move) changes nothing
     *
     * A locked node (EditorFlags::Locked) is not deleted or moved, and nothing is moved into a locked parent; Duplicate of a locked node is done.
     * After a Delete the node is gone: the caller must note beforehand whether the selection lies in its subtree (IsSelfOrAncestorOf).
     */
    OperationResult ApplyOperation(Scene& scene, const Operation& operation);

    /// True when @p node is @p ancestor or lies below it. Null on either side gives false
    bool IsSelfOrAncestorOf(const SceneNode* ancestor, const SceneNode* node);

    /// True when @p node is @p root or lies in its tree. Compares pointers only, so a node that is already gone is safe to ask about
    bool ContainsNode(const SceneNode& root, const SceneNode* node);

    /// The node of the tree under @p root (the root included) that is @p wanted, or null. Compares pointers only and never dereferences @p wanted, so a
    /// pointer that is stale or from another scene is safe to ask about
    SceneNode* FindNode(SceneNode& root, const SceneNode* wanted);

    /// Replaces the contents of @p out with the ancestors of @p node from the root down to its parent: the nodes whose rows must be open for @p node to be seen.
    /// @p out keeps its capacity
    void AncestorsOf(const SceneNode& node, Vector<SceneNode*>& out);

    /// Remembers the selection that was last told to others, so a selection event is sent once per change
    class SelectionTracker {
    public:
        /// True when (@p scene, @p node) differs from the last pair seen, which it then remembers. The pair before the first call is (null, null)
        bool Changed(const Scene* scene, const SceneNode* node) {
            if (scene == _scene && node == _node) {
                return false;
            }
            _scene = scene;
            _node = node;
            return true;
        }

    private:
        const Scene* _scene = nullptr;
        const SceneNode* _node = nullptr;
    };

    /// True when a search text or a component type is set, so the tree shows only what the filter keeps
    bool IsFilterActive(StringView query, StringView componentType);

    /**
     * @brief Whether the node itself passes the filter of the hierarchy panel
     *
     * The name must contain @p query (ASCII letters compared without case; empty keeps every name) and, when @p componentType is not empty, the node
     * must carry a component of that type name. Allocates nothing.
     */
    bool NodeMatchesFilter(const SceneNode& node, StringView query, StringView componentType);

    /// True when the node or any node below it passes the filter: an ancestor of a match stays in the tree (the panel dims it and opens its path)
    bool SubtreeMatchesFilter(const SceneNode& node, StringView query, StringView componentType);

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
    bool ValidateName(StringView text);

    /// Replaces the contents of @p out with the type names of the components in the tree below @p root (the root included), sorted and without repeats. @p out keeps its capacity
    void CollectComponentTypes(const SceneNode& root, Vector<StringView>& out);

}  // namespace BECore::HierarchyModel
