#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    class SceneNode;

    /**
     * @brief The text that names a node of a scene: the names from below the scene root down to the node, joined by `/` (`Board/Cell[2]`).
     *
     * `\/`, `\[` and `\\` stand for the character itself inside a name, and no other escape exists. A segment that ends in an unescaped `[n]` is the
     * n-th child (from 0) of that name; without it, the first. An empty segment is a node with an empty name. Names are compared as text and
     * nothing is interned, so a path may be client data.
     */
    namespace SceneNodePathLimits {
        inline constexpr size_t kMaxPathBytes = 1024;
        inline constexpr size_t kMaxSegments = 64;
    }  // namespace SceneNodePathLimits

    /// The node at @p path below @p root; null when no node is there; why, when @p path is no path or is over a limit
    [[nodiscard]] std::expected<SceneNode*, StringView> FindSceneNodeByPath(SceneNode& root, StringView path);

    /// Appends to @p path the segment of @p node, with no separator: its name escaped, and `[n]` when n earlier children of its parent have the name
    void AppendSceneNodeSegment(String& path, const SceneNode& node);

    /// Appends to @p path the path of @p node from below the root of its tree; nothing for the root itself
    void AppendSceneNodePath(String& path, const SceneNode& node);

}  // namespace BECore
