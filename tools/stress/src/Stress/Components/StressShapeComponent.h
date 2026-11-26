#pragma once

#include <BECore/Memory/Pooled.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/GeometryVertex.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore::Stress {

    /**
     * @brief A hexagon that fills the node's rect, one untextured `IRenderer::DrawGeometry` call: the shape of a mixed field (`--stress-mix`).
     *
     * The vertices are made from the size the node's `TransformComponent` has when this is attached, so that component goes on the node first;
     * a draw only passes the node's position as the translation.
     */
    class StressShapeComponent : public IComponent {
        BE_CLASS(StressShapeComponent)
    public:
        BE_POOLED_SINGLE_THREAD(StressShapeComponent)

        static constexpr size_t kCorners = 6;

        StressShapeComponent() = default;
        ~StressShapeComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        Array<GeometryVertex, kCorners + 1> _vertices{};  // the centre, then the corners, from the node's top-left corner
        IntrusivePtr<TransformComponent> _transform;
    };

}  // namespace BECore::Stress
