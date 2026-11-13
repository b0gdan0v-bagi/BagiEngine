#include "EntityField.h"

#include <Stress/StressHash.h>
#include <cmath>
#include <numbers>

namespace BECore::Stress {

    namespace {

        constexpr uint64_t kSaltCenterX = 11;
        constexpr uint64_t kSaltCenterY = 12;
        constexpr uint64_t kSaltRadius = 13;
        constexpr uint64_t kSaltPhase = 14;
        constexpr uint64_t kSaltSeconds = 15;

        constexpr float kFullTurn = 2.0f * std::numbers::pi_v<float>;

    }  // namespace

    EntityOrbit OrbitOf(uint32_t index, const FieldView& view) {
        EntityOrbit orbit;
        orbit.radius = Between(kOrbitMinRadius, kOrbitMaxRadius, Unit(index, kSaltRadius));
        orbit.centerX = Between(orbit.radius, std::max(orbit.radius, view.width - orbit.radius), Unit(index, kSaltCenterX));
        orbit.centerY = Between(orbit.radius, std::max(orbit.radius, view.height - orbit.radius), Unit(index, kSaltCenterY));
        orbit.phase = Unit(index, kSaltPhase) * kFullTurn;
        orbit.seconds = Between(kOrbitMinSeconds, kOrbitMaxSeconds, Unit(index, kSaltSeconds));
        return orbit;
    }

    OrbitPoint PointOn(const EntityOrbit& orbit, float turn) {
        const float angle = orbit.phase + turn * kFullTurn;
        return {orbit.centerX + orbit.radius * std::cos(angle), orbit.centerY + orbit.radius * std::sin(angle)};
    }

}  // namespace BECore::Stress
