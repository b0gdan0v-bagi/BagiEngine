#include <BECore/Input/PointerId.h>
#include <BECore/Input/PointerShape.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {
        constexpr Rect kNode{10.0f, 20.0f, 100.0f, 40.0f};
    }

    TEST(PointerShapeTest, TheDefaultIsTheNodeRectWithItsEdges) {
        const PointerShape shape;

        EXPECT_TRUE(shape.Contains(kNode, 60.0f, 40.0f));
        EXPECT_TRUE(shape.Contains(kNode, 10.0f, 20.0f));
        EXPECT_TRUE(shape.Contains(kNode, 110.0f, 60.0f));
        EXPECT_FALSE(shape.Contains(kNode, 9.9f, 40.0f));
        EXPECT_FALSE(shape.Contains(kNode, 60.0f, 60.1f));
    }

    TEST(PointerShapeTest, AnExplicitRectReplacesTheNodeRect) {
        const PointerShape shape = PointerShape::InRect(Rect{200.0f, 200.0f, 10.0f, 10.0f});

        EXPECT_TRUE(shape.Contains(kNode, 205.0f, 205.0f));
        EXPECT_TRUE(shape.Contains(kNode, 210.0f, 210.0f));
        EXPECT_FALSE(shape.Contains(kNode, 60.0f, 40.0f));
    }

    TEST(PointerShapeTest, ATransformRectKindIgnoresAnyRectItCarries) {
        PointerShape shape;
        shape.rect = Rect{200.0f, 200.0f, 10.0f, 10.0f};

        EXPECT_TRUE(shape.Contains(kNode, 60.0f, 40.0f));
        EXPECT_FALSE(shape.Contains(kNode, 205.0f, 205.0f));
    }

    TEST(PointerShapeTest, ACircleIsInscribedInTheShorterSideAndCentered) {
        const PointerShape shape = PointerShape::CircleIn(Rect{0.0f, 0.0f, 100.0f, 40.0f});

        EXPECT_TRUE(shape.Contains(kNode, 50.0f, 20.0f));
        EXPECT_TRUE(shape.Contains(kNode, 70.0f, 20.0f));
        EXPECT_TRUE(shape.Contains(kNode, 50.0f, 0.0f));
        EXPECT_FALSE(shape.Contains(kNode, 71.0f, 20.0f));
        EXPECT_FALSE(shape.Contains(kNode, 30.0f - 1.0f, 20.0f));
        EXPECT_FALSE(shape.Contains(kNode, 0.0f, 0.0f));
        EXPECT_FALSE(shape.Contains(kNode, 66.0f, 6.0f));
    }

    TEST(PointerShapeTest, AnEllipseFillsTheRectWithoutItsCorners) {
        const PointerShape shape = PointerShape::EllipseIn(Rect{0.0f, 0.0f, 100.0f, 40.0f});

        EXPECT_TRUE(shape.Contains(kNode, 50.0f, 20.0f));
        EXPECT_TRUE(shape.Contains(kNode, 100.0f, 20.0f));
        EXPECT_TRUE(shape.Contains(kNode, 0.0f, 20.0f));
        EXPECT_TRUE(shape.Contains(kNode, 50.0f, 40.0f));
        EXPECT_TRUE(shape.Contains(kNode, 80.0f, 30.0f));
        EXPECT_FALSE(shape.Contains(kNode, 1.0f, 1.0f));
        EXPECT_FALSE(shape.Contains(kNode, 99.0f, 39.0f));
        EXPECT_FALSE(shape.Contains(kNode, 101.0f, 20.0f));
    }

    TEST(PointerShapeTest, ACircleAndAnEllipseWithoutARectUseTheNodeRect) {
        PointerShape ellipse;
        ellipse.kind = PointerShapeKind::Ellipse;
        PointerShape circle;
        circle.kind = PointerShapeKind::Circle;

        EXPECT_TRUE(ellipse.Contains(kNode, 60.0f, 40.0f));
        EXPECT_FALSE(ellipse.Contains(kNode, 11.0f, 21.0f));
        EXPECT_TRUE(circle.Contains(kNode, 60.0f, 40.0f));
        EXPECT_FALSE(circle.Contains(kNode, 12.0f, 40.0f));
    }

    TEST(PointerShapeTest, ACircleAndAnEllipseOfASquareAreTheSame) {
        const Rect square{0.0f, 0.0f, 20.0f, 20.0f};
        const PointerShape circle = PointerShape::CircleIn(square);
        const PointerShape ellipse = PointerShape::EllipseIn(square);

        for (const float x : {0.0f, 3.0f, 10.0f, 17.0f, 20.0f}) {
            for (const float y : {0.0f, 3.0f, 10.0f, 17.0f, 20.0f}) {
                EXPECT_EQ(circle.Contains(kNode, x, y), ellipse.Contains(kNode, x, y)) << x << "," << y;
            }
        }
    }

    TEST(PointerShapeTest, AZeroOrNegativeSizeContainsNothing) {
        for (const Rect& area : {Rect{0.0f, 0.0f, 0.0f, 10.0f}, Rect{0.0f, 0.0f, 10.0f, 0.0f}, Rect{0.0f, 0.0f, -10.0f, 10.0f}, Rect{0.0f, 0.0f, 10.0f, -10.0f}}) {
            EXPECT_FALSE(PointerShape::InRect(area).Contains(kNode, 0.0f, 0.0f));
            EXPECT_FALSE(PointerShape::CircleIn(area).Contains(kNode, 0.0f, 0.0f));
            EXPECT_FALSE(PointerShape::EllipseIn(area).Contains(kNode, 0.0f, 0.0f));
            EXPECT_FALSE(PointerShape{}.Contains(area, 0.0f, 0.0f));
        }
    }

    TEST(PointerShapeTest, APointThatIsNotANumberIsOutside) {
        const float nan = std::numeric_limits<float>::quiet_NaN();

        EXPECT_FALSE(PointerShape{}.Contains(kNode, nan, 40.0f));
        EXPECT_FALSE(PointerShape::CircleIn(kNode).Contains(kNode, 60.0f, nan));
        EXPECT_FALSE(PointerShape::EllipseIn(kNode).Contains(kNode, nan, nan));
    }

    TEST(PointerIdTest, TouchIsTheSynthesizedMouseAndTheFingerRange) {
        EXPECT_FALSE(IsTouchPointer(0));
        EXPECT_FALSE(IsTouchPointer(1));
        EXPECT_TRUE(IsTouchPointer(SDL_TOUCH_MOUSEID));
        EXPECT_TRUE(IsTouchPointer(kFingerPointerBase));
        EXPECT_TRUE(IsTouchPointer(kFingerPointerBase + kMaxFingerPointers - 1));
        EXPECT_FALSE(IsTouchPointer(kFingerPointerBase + kMaxFingerPointers));
        EXPECT_FALSE(IsTouchPointer(kFingerPointerBase - 1));
        EXPECT_FALSE(IsTouchPointer(SDL_PEN_MOUSEID));
    }

    TEST(PointerIdTest, OnlyThePenMouseIdIsAPen) {
        EXPECT_TRUE(IsPenPointer(SDL_PEN_MOUSEID));
        EXPECT_FALSE(IsPenPointer(0));
        EXPECT_FALSE(IsPenPointer(SDL_TOUCH_MOUSEID));
        EXPECT_FALSE(IsPenPointer(kFingerPointerBase));
    }

    TEST(PointerIdTest, AFingerIsNotTheMouseNorTheSdlIds) {
        EXPECT_GT(kFingerPointerBase, 1000U);
        EXPECT_LT(kFingerPointerBase + kMaxFingerPointers, SDL_PEN_MOUSEID);
    }

}  // namespace BECore::Tests
