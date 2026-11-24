#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    class IComponent;
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

    /**
     * @brief What Scene::Draw() walks: the drawing components of the visible nodes in draw order, and the clip owners they name.
     *
     * Filled by SceneDrawSystem when the scene says it is stale (Scene::IsDrawListValid()) and by nothing else. The pointers are raw:
     * they are valid because every change of a node or a component makes the list stale before the next pass reads it. Both vectors are
     * kept between rebuilds, so a pass over a list that did not change allocates nothing.
     */
    struct SceneDrawList {
        Vector<SceneDrawEntry> entries;
        Vector<SceneClipOwner> clips;
    };

}  // namespace BECore
