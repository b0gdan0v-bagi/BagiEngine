#pragma once

#include <BECore/Math/Vec2.h>
#include <Widgets/Viewport/GizmoGeometry.h>
#include <gtest/gtest.h>

namespace BECore::Tests::GizmoTest {

    inline constexpr float kTolerance = 1e-3f;
    inline constexpr float kThirtyDegreesCos = 0.8660254f;
    inline constexpr float kThirtyDegreesSin = 0.5f;

    inline void ExpectVec(Vec2 expected, Vec2 actual) {
        EXPECT_NEAR(expected.x, actual.x, kTolerance);
        EXPECT_NEAR(expected.y, actual.y, kTolerance);
    }

    inline Gizmo::Item MakeItem(float x, float y, float width, float height, float rotation = 0.0f, float pivotX = 0.5f, float pivotY = 0.5f) {
        Gizmo::Item item;
        item.x = x;
        item.y = y;
        item.width = width;
        item.height = height;
        item.rotation = rotation;
        item.pivotX = pivotX;
        item.pivotY = pivotY;
        return item;
    }

    /// A 100 x 50 item at the origin: its centre is kFlatCenter
    inline const Gizmo::Item kFlat = MakeItem(0.0f, 0.0f, 100.0f, 50.0f);
    inline constexpr Vec2 kFlatCenter{50.0f, 25.0f};

}  // namespace BECore::Tests::GizmoTest
