#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    class IComponent;
    class SceneNode;
    class TransformComponent;

    /// What SceneDrawSystem does for one entry: draws the node's quad or its sprite itself, or calls IComponent::OnDraw()
    CORE_ENUM(SceneDrawKind, uint8_t, Quad, Sprite, Custom)

    /// One drawing component of a visible node. The list holds them sorted by (layer, scene order, index of the component in its node).
    struct SceneDrawEntry {
        IComponent* component = nullptr;                ///< the QuadRendererComponent, the SpriteRendererComponent or the one whose OnDraw() is called
        const TransformComponent* transform = nullptr;  ///< the node's, for a quad and a sprite; null for Custom
        int32_t clip = -1;                              ///< index in SceneDrawList::clips of the nearest clip owner, -1 for none
        SceneDrawKind kind = SceneDrawKind::Custom;
    };

    /// A node that clips its subtree: it has a ClipRectComponent and a TransformComponent
    struct SceneClipOwner {
        const TransformComponent* transform = nullptr;
        int32_t outer = -1;  ///< index of the enclosing owner, -1 for none
    };

    /// The place of one node in the draw order, written when the draw list is rebuilt. Its index in SceneDrawList::items is the node's rank (Scene::GetDrawRank()).
    struct SceneDrawItem {
        int32_t layer = 0;   ///< SceneNode::GetLayer() at the rebuild
        uint32_t order = 0;  ///< index of the node in the depth-first walk of the tree (the scene order)
        SceneNode* node = nullptr;
        bool hidden = false;              ///< the node or an ancestor has EditorFlags::Hidden: the node keeps its place in the order but draws nothing
        const SceneNode* clip = nullptr;  ///< the nearest self-or-ancestor with a ClipRectComponent and a TransformComponent, or null
        int32_t clipIndex = -1;           ///< index of that owner in SceneDrawList::clips, -1 for none
    };

    /**
     * @brief What Scene::Draw() walks: the drawing components of the visible nodes in draw order, the clip owners they name, and the node items they come from.
     *
     * Filled by SceneDrawSystem when the scene says it is stale (Scene::IsDrawListValid()) and by nothing else. The pointers are raw:
     * they are valid because every change of a node or a component makes the list stale before the next pass reads it. The vectors are
     * kept between rebuilds, so a pass over a list that did not change allocates nothing.
     */
    struct SceneDrawList {
        Vector<SceneDrawEntry> entries;
        Vector<SceneClipOwner> clips;
        Vector<SceneDrawItem> items;  ///< one per node, sorted by (layer, scene order): the draw order, hidden nodes included
    };

}  // namespace BECore
