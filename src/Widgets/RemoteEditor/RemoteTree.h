#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// A node of the attached program's scene
    struct RemoteTreeNode {
        /// What the scene methods name the node by (`SceneNodePath`)
        String path;
        String name;
        int32_t layer = 0;
        /// The type names of its components, in their order: `scene_get` counts namesakes in it
        Vector<String> components;
        /// Indices into `RemoteTree::GetNodes()`, in the order of the scene
        Vector<uint32_t> children;
    };

    /**
     * @brief The scene tree of the attached program, read from the result of its `scene_tree` method
     *
     * The method lists the nodes depth first with no root and no parent: the parent of a node is the node whose path is its own without the last
     * segment. A node whose parent was not listed before it, one nested deeper than a path may be, and an entry with no path are left out.
     */
    class RemoteTree {
    public:
        RemoteTree() = default;
        RemoteTree(const RemoteTree&) = delete;
        RemoteTree& operator=(const RemoteTree&) = delete;

        /// Rebuilds from @p result when its text differs from the last one; true when it did. Pointers into the tree die with a rebuild
        bool Load(StringView result);

        void Clear();

        /// Depth first, a parent before its children
        [[nodiscard]] const Vector<RemoteTreeNode>& GetNodes() const {
            return _nodes;
        }

        /// The nodes below the scene root
        [[nodiscard]] const Vector<uint32_t>& GetTopNodes() const {
            return _topNodes;
        }

        /// The node at @p path (the first of that path), or null
        [[nodiscard]] const RemoteTreeNode* Find(StringView path) const;

        [[nodiscard]] StringView GetSceneName() const {
            return _sceneName;
        }

        /// The program left nodes out of its answer
        [[nodiscard]] bool IsTruncated() const {
            return _truncated;
        }

    private:
        String _text;
        JsonTape _tape;
        Vector<RemoteTreeNode> _nodes;
        Vector<uint32_t> _topNodes;
        /// The ancestors of the node being read, the top node first
        Vector<uint32_t> _openNodes;
        String _sceneName;
        bool _truncated = false;
    };

}  // namespace BECore
