#include <BECore/Renderer/ViewTransform.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(ViewTransform{}.scale == 1.0f && ViewTransform{}.offsetX == 0.0f && ViewTransform{}.offsetY == 0.0f);
    static_assert(ViewTransform::Fit(400.0f, 200.0f, 800.0f, 400.0f).scale == 2.0f);

    TEST(ViewTransformTest, DefaultIsTheIdentity) {
        const ViewTransform identity;
        const Rect rect = identity.ToOutput(10.0f, 20.0f, 30.0f, 40.0f);

        EXPECT_FLOAT_EQ(rect.x, 10.0f);
        EXPECT_FLOAT_EQ(rect.y, 20.0f);
        EXPECT_FLOAT_EQ(rect.w, 30.0f);
        EXPECT_FLOAT_EQ(rect.h, 40.0f);
        EXPECT_FLOAT_EQ(identity.ToViewX(7.0f), 7.0f);
        EXPECT_FLOAT_EQ(identity.ToViewY(9.0f), 9.0f);
    }

    TEST(ViewTransformTest, FitScalesByTheTighterSideAndCentersTheOther) {
        // 400x200 into 800x600: the width allows 2x, the height 3x, so 2x and bars above and below.
        const ViewTransform wide = ViewTransform::Fit(400.0f, 200.0f, 800.0f, 600.0f);
        EXPECT_FLOAT_EQ(wide.scale, 2.0f);
        EXPECT_FLOAT_EQ(wide.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(wide.offsetY, 100.0f);

        // 400x200 into 1600x400: the height allows 2x, the width 4x, so 2x and bars left and right.
        const ViewTransform tall = ViewTransform::Fit(400.0f, 200.0f, 1600.0f, 400.0f);
        EXPECT_FLOAT_EQ(tall.scale, 2.0f);
        EXPECT_FLOAT_EQ(tall.offsetX, 400.0f);
        EXPECT_FLOAT_EQ(tall.offsetY, 0.0f);
    }

    TEST(ViewTransformTest, FitShrinksAViewThatIsLargerThanTheOutput) {
        const ViewTransform shrink = ViewTransform::Fit(800.0f, 400.0f, 400.0f, 400.0f);

        EXPECT_FLOAT_EQ(shrink.scale, 0.5f);
        EXPECT_FLOAT_EQ(shrink.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(shrink.offsetY, 100.0f);
    }

    TEST(ViewTransformTest, FitIsTheIdentityWhenASizeIsNotPositive) {
        for (const ViewTransform& t : {ViewTransform::Fit(0.0f, 200.0f, 800.0f, 600.0f), ViewTransform::Fit(400.0f, 0.0f, 800.0f, 600.0f), ViewTransform::Fit(400.0f, 200.0f, 0.0f, 600.0f), ViewTransform::Fit(400.0f, 200.0f, 800.0f, 0.0f), ViewTransform::Fit(-1.0f, 200.0f, 800.0f, 600.0f)}) {
            EXPECT_FLOAT_EQ(t.scale, 1.0f);
            EXPECT_FLOAT_EQ(t.offsetX, 0.0f);
            EXPECT_FLOAT_EQ(t.offsetY, 0.0f);
        }
    }

    TEST(ViewTransformTest, ToOutputScalesTheRectAndOffsetsItsOrigin) {
        const ViewTransform t = ViewTransform::Fit(400.0f, 200.0f, 800.0f, 600.0f);
        const Rect rect = t.ToOutput(10.0f, 20.0f, 30.0f, 40.0f);

        EXPECT_FLOAT_EQ(rect.x, 20.0f);
        EXPECT_FLOAT_EQ(rect.y, 140.0f);
        EXPECT_FLOAT_EQ(rect.w, 60.0f);
        EXPECT_FLOAT_EQ(rect.h, 80.0f);
    }

    TEST(ViewTransformTest, ToViewIsTheInverseOfToOutput) {
        const ViewTransform t = ViewTransform::Fit(840.0f, 760.0f, 1632.0f, 910.0f);
        const Rect rect = t.ToOutput(100.0f, 60.0f, 0.0f, 0.0f);

        EXPECT_NEAR(t.ToViewX(rect.x), 100.0f, 1e-3f);
        EXPECT_NEAR(t.ToViewY(rect.y), 60.0f, 1e-3f);
    }

    TEST(ViewTransformTest, TheViewCornersLandOnTheFittedArea) {
        const ViewTransform t = ViewTransform::Fit(840.0f, 760.0f, 1632.0f, 910.0f);
        const Rect whole = t.ToOutput(0.0f, 0.0f, 840.0f, 760.0f);

        // The height is the tight side: the view fills it and is centered across.
        EXPECT_NEAR(whole.y, 0.0f, 1e-3f);
        EXPECT_NEAR(whole.h, 910.0f, 1e-3f);
        EXPECT_NEAR(whole.x + whole.w * 0.5f, 1632.0f * 0.5f, 1e-3f);
    }

}  // namespace BECore::Tests
