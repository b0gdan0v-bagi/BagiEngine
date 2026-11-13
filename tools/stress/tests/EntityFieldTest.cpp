#include <Stress/EntityField.h>
#include <gtest/gtest.h>

/// Coverage of EntityField: the orbit of a node is a pure function of its index.
namespace BECore::Stress::Tests {

    namespace {

        const FieldView kView{1920.0f, 1080.0f};
        constexpr uint32_t kNodes = 2000;
        constexpr float kEpsilon = 0.01f;

    }  // namespace

    TEST(EntityFieldTest, TheSameIndexGivesTheSameOrbit) {
        for (uint32_t i : {0u, 1u, 500u, 1999u}) {
            const EntityOrbit a = OrbitOf(i, kView);
            const EntityOrbit b = OrbitOf(i, kView);
            EXPECT_EQ(a.centerX, b.centerX);
            EXPECT_EQ(a.centerY, b.centerY);
            EXPECT_EQ(a.radius, b.radius);
            EXPECT_EQ(a.phase, b.phase);
            EXPECT_EQ(a.seconds, b.seconds);
        }
    }

    TEST(EntityFieldTest, EveryOrbitIsInsideTheViewAndWithinTheBounds) {
        for (uint32_t i = 0; i < kNodes; ++i) {
            const EntityOrbit orbit = OrbitOf(i, kView);
            EXPECT_GE(orbit.radius, kOrbitMinRadius);
            EXPECT_LE(orbit.radius, kOrbitMaxRadius);
            EXPECT_GE(orbit.seconds, kOrbitMinSeconds);
            EXPECT_LE(orbit.seconds, kOrbitMaxSeconds);
            EXPECT_GE(orbit.centerX - orbit.radius, -kEpsilon);
            EXPECT_LE(orbit.centerX + orbit.radius, kView.width + kEpsilon);
            EXPECT_GE(orbit.centerY - orbit.radius, -kEpsilon);
            EXPECT_LE(orbit.centerY + orbit.radius, kView.height + kEpsilon);
        }
    }

    TEST(EntityFieldTest, TheOrbitsDiffer) {
        const EntityOrbit a = OrbitOf(0, kView);
        const EntityOrbit b = OrbitOf(1, kView);
        EXPECT_TRUE(a.centerX != b.centerX || a.centerY != b.centerY || a.radius != b.radius);
    }

    TEST(EntityFieldTest, TheNodeIsOnTheCircleAndComesBackAfterATurn) {
        const EntityOrbit orbit = OrbitOf(7, kView);
        for (float turn : {0.0f, 0.25f, 0.6f}) {
            const OrbitPoint point = PointOn(orbit, turn);
            const float dx = point.x - orbit.centerX;
            const float dy = point.y - orbit.centerY;
            EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), orbit.radius, kEpsilon);
        }
        const OrbitPoint start = PointOn(orbit, 0.0f);
        const OrbitPoint end = PointOn(orbit, 1.0f);
        EXPECT_NEAR(start.x, end.x, kEpsilon);
        EXPECT_NEAR(start.y, end.y, kEpsilon);
        const OrbitPoint half = PointOn(orbit, 0.5f);
        EXPECT_NEAR(half.x, 2.0f * orbit.centerX - start.x, kEpsilon);
    }

}  // namespace BECore::Stress::Tests
