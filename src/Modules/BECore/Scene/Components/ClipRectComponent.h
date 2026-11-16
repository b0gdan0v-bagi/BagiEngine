#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    /**
     * @brief Clips the node and its whole subtree to the node's TransformComponent rect: what they draw and what they hit.
     *
     * A marker with no fields: SceneDrawSystem finds it while it writes the draw items and pushes the clip around every item of the
     * subtree, whatever its layer (the draw order is by layer, not by subtree). The rect is the unrotated transform rect in scene units;
     * a node without a TransformComponent clips nothing. Clips of nested nodes intersect, up to SceneDrawSystem::kMaxClipDepth; deeper
     * ones are ignored. A PointerTargetComponent or a ClickableComponent below reads the renderer's clip as it draws, so the area outside
     * the rect is no target.
     */
    class ClipRectComponent : public IComponent {
        BE_CLASS(ClipRectComponent, ICON = crop)
    public:
        ClipRectComponent() = default;
        ~ClipRectComponent() override = default;
    };

}  // namespace BECore
