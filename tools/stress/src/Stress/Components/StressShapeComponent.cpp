#include "StressShapeComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/StressShapeComponent.gen.hpp>
#include <cmath>
#include <numbers>

namespace BECore::Stress {

    namespace {

        constexpr size_t kIndicesPerTriangle = 3;
        constexpr float kHalf = 0.5f;
        constexpr Color kShapeColor{240, 180, 40, 200};  // translucent, so that it shows what lies under it

        /// A fan around the centre (vertex 0): one triangle per side of the hexagon
        constexpr Array<int, StressShapeComponent::kCorners * kIndicesPerTriangle> MakeFanIndices() {
            Array<int, StressShapeComponent::kCorners * kIndicesPerTriangle> indices{};
            for (size_t side = 0; side < StressShapeComponent::kCorners; ++side) {
                indices[side * kIndicesPerTriangle] = 0;
                indices[side * kIndicesPerTriangle + 1] = static_cast<int>(side + 1);
                indices[side * kIndicesPerTriangle + 2] = static_cast<int>((side + 1) % StressShapeComponent::kCorners + 1);
            }
            return indices;
        }

        constexpr auto kFanIndices = MakeFanIndices();

    }  // namespace

    void StressShapeComponent::OnAttached() {
        _transform = GetNode().GetComponent<TransformComponent>();
        if (!_transform) {
            return;
        }
        const float halfWidth = _transform->_width * kHalf;
        const float halfHeight = _transform->_height * kHalf;
        constexpr float kCornerAngle = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kCorners);
        _vertices[0] = GeometryVertex{halfWidth, halfHeight, kShapeColor};
        for (size_t corner = 0; corner < kCorners; ++corner) {
            const float angle = kCornerAngle * static_cast<float>(corner);
            _vertices[corner + 1] = GeometryVertex{halfWidth + halfWidth * std::cos(angle), halfHeight + halfHeight * std::sin(angle), kShapeColor};
        }
    }

    void StressShapeComponent::OnDraw() {
        const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer();
        if (renderer && _transform) {
            renderer->DrawGeometry(nullptr, {_vertices.data(), _vertices.size()}, {kFanIndices.data(), kFanIndices.size()}, _transform->_x, _transform->_y);
        }
    }

}  // namespace BECore::Stress
