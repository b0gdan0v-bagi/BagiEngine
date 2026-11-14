#include <BECore/Renderer/DrawTransform.h>
#include <cmath>
#include <numbers>

namespace BECore {

    namespace {
        constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
    }  // namespace

    //-NoStd: as the declaration
    std::array<Vec2, 4> MakeTransformedQuad(const Rect& rect, const DrawTransform& transform) {
        const float radians = transform.rotation * kDegreesToRadians;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);

        // x grows right and y grows down, so this matrix turns clockwise on the screen
        auto turn = [&](float x, float y) {
            const float dx = x - transform.pivotX;
            const float dy = y - transform.pivotY;
            return Vec2{transform.pivotX + dx * cosine - dy * sine, transform.pivotY + dx * sine + dy * cosine};
        };

        return {turn(rect.x, rect.y), turn(rect.x + rect.w, rect.y), turn(rect.x, rect.y + rect.h), turn(rect.x + rect.w, rect.y + rect.h)};
    }

}  // namespace BECore
