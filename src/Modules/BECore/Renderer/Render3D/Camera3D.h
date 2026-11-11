#pragma once

#include <BECore/Math/Mat4.h>
#include <numbers>

namespace BECore {

    /// A perspective camera, set as a position, a point to look at and the world's up (not an orientation: the scene's camera is aimed at the table
    /// and moves with a target; `scene-3d` converts a node's rotation to a target when it needs to). Right-handed, Y up (`Math/CLAUDE.md`).
    struct Camera3D {
        static constexpr float kDefaultFovYRadians = std::numbers::pi_v<float> / 3.0f;
        static constexpr float kDefaultNearPlane = 0.1f;
        static constexpr float kDefaultFarPlane = 100.0f;

        Vec3 position{0.0f, 0.0f, 5.0f};
        Vec3 target{0.0f, 0.0f, 0.0f};
        Vec3 up{0.0f, 1.0f, 0.0f};  ///< must not be parallel to the view direction
        float fovYRadians = kDefaultFovYRadians;
        float nearPlane = kDefaultNearPlane;
        float farPlane = kDefaultFarPlane;

        /// World to view space
        [[nodiscard]] Mat4 ViewMatrix() const {
            return Mat4::LookAt(position, target, up);
        }

        /// View to clip space for a target of @p aspect (width over height): depth 0 to 1, Y up
        [[nodiscard]] Mat4 ProjectionMatrix(float aspect) const {
            return Mat4::Perspective(fovYRadians, aspect, nearPlane, farPlane);
        }
    };

}  // namespace BECore
