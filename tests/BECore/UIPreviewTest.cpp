#include <BECore/UI/UIPreview.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        float RatioOf(const std::pair<uint32_t, uint32_t>& size) {
            return static_cast<float>(size.first) / static_cast<float>(size.second);
        }

    }  // namespace

    TEST(UIPreviewTest, FreeTakesTheWholeAvailableArea) {
        const auto size = UIPreview::FitSize(ViewportAspect::Free, 800.5f, 600.9f);

        EXPECT_EQ(size.first, 800u);
        EXPECT_EQ(size.second, 600u);
    }

    TEST(UIPreviewTest, APresetFitsInsideTheAvailableAreaWithItsOwnRatio) {
        struct Case {
            ViewportAspect aspect;
            float ratio;
        };
        const Case cases[] = {
            {ViewportAspect::Wide16x9, 16.0f / 9.0f}, {ViewportAspect::Standard4x3, 4.0f / 3.0f}, {ViewportAspect::UltraWide21x9, 21.0f / 9.0f}, {ViewportAspect::Portrait9x16, 9.0f / 16.0f}};
        const float areas[][2] = {{1280.0f, 720.0f}, {400.0f, 900.0f}, {1000.0f, 1000.0f}, {3000.0f, 200.0f}};
        for (const Case& c : cases) {
            for (const auto& area : areas) {
                const auto size = UIPreview::FitSize(c.aspect, area[0], area[1]);
                SCOPED_TRACE(Format("{} in {}x{}", EnumUtils<ViewportAspect>::ToString(c.aspect), area[0], area[1]).c_str());
                EXPECT_LE(static_cast<float>(size.first), area[0]);
                EXPECT_LE(static_cast<float>(size.second), area[1]);
                EXPECT_NEAR(RatioOf(size), c.ratio, 0.02f * c.ratio + 1.0f / static_cast<float>(size.second));  // whole pixels
                // It is the largest: one side of the area is used up
                EXPECT_TRUE(static_cast<float>(size.first) > area[0] - 1.5f || static_cast<float>(size.second) > area[1] - 1.5f);
            }
        }
    }

    TEST(UIPreviewTest, ASixteenByNineAreaGivesExactlyThatShape) {
        const auto size = UIPreview::FitSize(ViewportAspect::Wide16x9, 1600.0f, 900.0f);

        EXPECT_EQ(size.first, 1600u);
        EXPECT_EQ(size.second, 900u);
    }

    TEST(UIPreviewTest, AnAreaWithNoSizeGivesNoImageAndATinyOneIsAtLeastOnePixel) {
        EXPECT_EQ(UIPreview::FitSize(ViewportAspect::Free, 0.0f, 600.0f), (std::pair<uint32_t, uint32_t>{0, 0}));
        EXPECT_EQ(UIPreview::FitSize(ViewportAspect::Wide16x9, 600.0f, 0.5f), (std::pair<uint32_t, uint32_t>{0, 0}));

        const auto tiny = UIPreview::FitSize(ViewportAspect::UltraWide21x9, 1.0f, 100.0f);
        EXPECT_GE(tiny.first, 1u);
        EXPECT_GE(tiny.second, 1u);
    }

    TEST(UIPreviewTest, AspectOfFreeIsZero) {
        EXPECT_FLOAT_EQ(UIPreview::AspectOf(ViewportAspect::Free), 0.0f);
        EXPECT_NEAR(UIPreview::AspectOf(ViewportAspect::Portrait9x16), 0.5625f, kTolerance);
    }

    TEST(UIPreviewTest, NoSimulationIsTheWholeOutput) {
        const Rect safe = UIPreview::SimulatedSafeArea(SafeAreaSimulation::None, 1280.0f, 720.0f);

        EXPECT_FLOAT_EQ(safe.x, 0.0f);
        EXPECT_FLOAT_EQ(safe.y, 0.0f);
        EXPECT_FLOAT_EQ(safe.w, 1280.0f);
        EXPECT_FLOAT_EQ(safe.h, 720.0f);
    }

    TEST(UIPreviewTest, ANotchCutsTheTopByAShareOfTheShortSide) {
        const Rect landscape = UIPreview::SimulatedSafeArea(SafeAreaSimulation::TopNotch, 1280.0f, 720.0f);
        EXPECT_NEAR(landscape.y, 720.0f * UIPreview::kNotchShare, kTolerance);
        EXPECT_NEAR(landscape.h, 720.0f - 720.0f * UIPreview::kNotchShare, kTolerance);
        EXPECT_FLOAT_EQ(landscape.w, 1280.0f);

        const Rect portrait = UIPreview::SimulatedSafeArea(SafeAreaSimulation::TopNotch, 720.0f, 1280.0f);
        EXPECT_NEAR(portrait.y, 720.0f * UIPreview::kNotchShare, kTolerance);  // the short side is the width here
    }

    TEST(UIPreviewTest, SideInsetsCutBothSidesAndTheBottom) {
        const Rect safe = UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, 1000.0f, 500.0f);

        EXPECT_NEAR(safe.x, 500.0f * UIPreview::kSideInsetShare, kTolerance);
        EXPECT_NEAR(safe.w, 1000.0f - 2.0f * 500.0f * UIPreview::kSideInsetShare, kTolerance);
        EXPECT_FLOAT_EQ(safe.y, 0.0f);
        EXPECT_NEAR(safe.h, 500.0f - 500.0f * UIPreview::kBottomInsetShare, kTolerance);
    }

    TEST(UIPreviewTest, EverySimulatedSafeAreaStaysInsideTheOutput) {
        for (SafeAreaSimulation simulation : {SafeAreaSimulation::None, SafeAreaSimulation::TopNotch, SafeAreaSimulation::SideInsets}) {
            const Rect safe = UIPreview::SimulatedSafeArea(simulation, 640.0f, 480.0f);
            EXPECT_GE(safe.x, 0.0f);
            EXPECT_GE(safe.y, 0.0f);
            EXPECT_LE(safe.x + safe.w, 640.0f + kTolerance);
            EXPECT_LE(safe.y + safe.h, 480.0f + kTolerance);
            EXPECT_GT(safe.w, 0.0f);
            EXPECT_GT(safe.h, 0.0f);
        }
    }

}  // namespace BECore::Tests
