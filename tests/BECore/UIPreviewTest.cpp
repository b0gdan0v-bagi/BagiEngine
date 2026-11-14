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

    TEST(UIPreviewTest, TheDeviceTableHasUniqueNamesAndSaneNumbers) {
        const auto devices = UIPreview::GetDevices();

        ASSERT_FALSE(devices.empty());
        for (size_t i = 0; i < devices.size(); ++i) {
            const PreviewDevice& device = devices[i];
            SCOPED_TRACE(String(device.name).c_str());
            EXPECT_FALSE(device.name.empty());
            EXPECT_GT(device.width, 0u);
            EXPECT_GT(device.height, device.width);  // portrait
            EXPECT_GE(device.cutout, 0.0f);
            EXPECT_GE(device.homeBar, 0.0f);
            EXPECT_LT(device.cutout, static_cast<float>(device.width));  // smaller than the short side
            EXPECT_LT(device.homeBar, static_cast<float>(device.width));
            for (size_t j = i + 1; j < devices.size(); ++j) {
                EXPECT_NE(device.name, devices[j].name);
            }
        }
    }

    TEST(UIPreviewTest, FreeAndEveryAspectTargetIsTheFitSizeAtFullZoom) {
        for (ViewportAspect aspect : {ViewportAspect::Free, ViewportAspect::Wide16x9, ViewportAspect::Standard4x3, ViewportAspect::UltraWide21x9, ViewportAspect::Portrait9x16}) {
            PreviewShape shape;
            shape.aspect = aspect;
            const PreviewTarget target = UIPreview::TargetFor(shape, 1000.0f, 700.0f);
            const auto fit = UIPreview::FitSize(aspect, 1000.0f, 700.0f);
            SCOPED_TRACE(EnumUtils<ViewportAspect>::ToString(aspect).data());
            EXPECT_EQ(target.width, fit.first);
            EXPECT_EQ(target.height, fit.second);
            EXPECT_FLOAT_EQ(target.zoom, 1.0f);
        }
    }

    TEST(UIPreviewTest, RotatingAnAspectTurnsItsRatioAndFreeIgnoresIt) {
        PreviewShape wide;
        wide.aspect = ViewportAspect::Wide16x9;
        wide.rotated = true;
        const PreviewTarget turned = UIPreview::TargetFor(wide, 1000.0f, 1000.0f);
        const auto portrait = UIPreview::FitSize(ViewportAspect::Portrait9x16, 1000.0f, 1000.0f);
        EXPECT_EQ(turned.width, portrait.first);
        EXPECT_EQ(turned.height, portrait.second);

        PreviewShape standard;
        standard.aspect = ViewportAspect::Standard4x3;
        standard.rotated = true;
        const PreviewTarget threeByFour = UIPreview::TargetFor(standard, 900.0f, 1200.0f);
        EXPECT_EQ(threeByFour.width, 900u);
        EXPECT_EQ(threeByFour.height, 1200u);

        PreviewShape free;
        free.rotated = true;
        const PreviewTarget whole = UIPreview::TargetFor(free, 800.0f, 600.0f);
        EXPECT_EQ(whole.width, 800u);
        EXPECT_EQ(whole.height, 600u);
    }

    TEST(UIPreviewTest, ADeviceIsRenderedAtItsExactSizeAndShownAtMostAtFullZoom) {
        const auto devices = UIPreview::GetDevices();
        for (size_t i = 0; i < devices.size(); ++i) {
            for (bool rotated : {false, true}) {
                PreviewShape shape;
                shape.device = static_cast<int32_t>(i);
                shape.rotated = rotated;
                const uint32_t expectedWidth = rotated ? devices[i].height : devices[i].width;
                const uint32_t expectedHeight = rotated ? devices[i].width : devices[i].height;
                SCOPED_TRACE(Format("{} rotated {}", devices[i].name, rotated).c_str());

                const PreviewTarget big = UIPreview::TargetFor(shape, 5000.0f, 5000.0f);
                EXPECT_EQ(big.width, expectedWidth);
                EXPECT_EQ(big.height, expectedHeight);
                EXPECT_FLOAT_EQ(big.zoom, 1.0f);

                const PreviewTarget small = UIPreview::TargetFor(shape, 300.0f, 200.0f);
                EXPECT_EQ(small.width, expectedWidth);  // the size does not follow the panel
                EXPECT_EQ(small.height, expectedHeight);
                EXPECT_GT(small.zoom, 0.0f);
                EXPECT_LT(small.zoom, 1.0f);
                EXPECT_LE(static_cast<float>(small.width) * small.zoom, 300.0f + kTolerance);
                EXPECT_LE(static_cast<float>(small.height) * small.zoom, 200.0f + kTolerance);
                // It is the largest: one side of the area is used up
                EXPECT_TRUE(static_cast<float>(small.width) * small.zoom > 300.0f - kTolerance || static_cast<float>(small.height) * small.zoom > 200.0f - kTolerance);
            }
        }
    }

    TEST(UIPreviewTest, ACustomSizeIsClampedAndRotates) {
        PreviewShape shape;
        shape.custom = true;
        shape.device = 0;  // custom wins
        shape.customWidth = 10;
        shape.customHeight = 100000;
        const PreviewTarget clamped = UIPreview::TargetFor(shape, 5000.0f, 2048.0f);
        EXPECT_EQ(clamped.width, kMinPreviewSide);
        EXPECT_EQ(clamped.height, kMaxPreviewSide);
        EXPECT_FLOAT_EQ(clamped.zoom, 0.5f);

        shape.customWidth = 300;
        shape.customHeight = 500;
        shape.rotated = true;
        const PreviewTarget rotated = UIPreview::TargetFor(shape, 5000.0f, 5000.0f);
        EXPECT_EQ(rotated.width, 500u);
        EXPECT_EQ(rotated.height, 300u);
        EXPECT_FLOAT_EQ(rotated.zoom, 1.0f);
    }

    TEST(UIPreviewTest, AnUnknownDeviceIndexIsFree) {
        PreviewShape shape;
        shape.aspect = ViewportAspect::Wide16x9;
        shape.device = static_cast<int32_t>(UIPreview::GetDevices().size());

        const PreviewTarget target = UIPreview::TargetFor(shape, 800.0f, 600.0f);

        EXPECT_EQ(target.width, 800u);
        EXPECT_EQ(target.height, 600u);
        EXPECT_FLOAT_EQ(target.zoom, 1.0f);
    }

    TEST(UIPreviewTest, AnEmptyAreaGivesNoTargetForAnyShape) {
        PreviewShape device;
        device.device = 0;
        PreviewShape custom;
        custom.custom = true;
        for (const PreviewShape& shape : {PreviewShape{}, device, custom}) {
            for (const auto& area : {std::pair<float, float>{0.0f, 600.0f}, std::pair<float, float>{800.0f, 0.5f}}) {
                const PreviewTarget target = UIPreview::TargetFor(shape, area.first, area.second);
                EXPECT_EQ(target.width, 0u);
                EXPECT_EQ(target.height, 0u);
                EXPECT_FLOAT_EQ(target.zoom, 0.0f);
            }
        }
    }

    TEST(UIPreviewTest, TheShapeComparesByEveryField) {
        PreviewShape a;
        PreviewShape b;
        EXPECT_EQ(a, b);
        b.rotated = true;
        EXPECT_FALSE(a == b);
    }

    TEST(UIPreviewTest, ADeviceSafeAreaInPortraitCutsTheTopAndTheBottom) {
        const PreviewDevice device{"test", 400, 800, 50.0f, 30.0f};

        const Rect safe = UIPreview::SimulatedSafeArea(SafeAreaSimulation::Device, &device, 400.0f, 800.0f);

        EXPECT_FLOAT_EQ(safe.x, 0.0f);
        EXPECT_FLOAT_EQ(safe.y, 50.0f);
        EXPECT_FLOAT_EQ(safe.w, 400.0f);
        EXPECT_FLOAT_EQ(safe.h, 800.0f - 50.0f - 30.0f);
    }

    TEST(UIPreviewTest, ADeviceSafeAreaInLandscapeCutsBothSidesAndTheBottom) {
        const PreviewDevice device{"test", 400, 800, 50.0f, 30.0f};

        const Rect safe = UIPreview::SimulatedSafeArea(SafeAreaSimulation::Device, &device, 800.0f, 400.0f);

        EXPECT_FLOAT_EQ(safe.x, 50.0f);
        EXPECT_FLOAT_EQ(safe.y, 0.0f);
        EXPECT_FLOAT_EQ(safe.w, 800.0f - 100.0f);
        EXPECT_FLOAT_EQ(safe.h, 400.0f - 30.0f);
    }

    TEST(UIPreviewTest, ADeviceSafeAreaWithNoDeviceIsTheWholeOutputAndOtherSimulationsKeepTheirShape) {
        const Rect none = UIPreview::SimulatedSafeArea(SafeAreaSimulation::Device, nullptr, 400.0f, 800.0f);
        EXPECT_FLOAT_EQ(none.x, 0.0f);
        EXPECT_FLOAT_EQ(none.y, 0.0f);
        EXPECT_FLOAT_EQ(none.w, 400.0f);
        EXPECT_FLOAT_EQ(none.h, 800.0f);

        const PreviewDevice device{"test", 400, 800, 50.0f, 30.0f};
        for (SafeAreaSimulation simulation : {SafeAreaSimulation::None, SafeAreaSimulation::TopNotch, SafeAreaSimulation::SideInsets}) {
            const Rect with = UIPreview::SimulatedSafeArea(simulation, &device, 640.0f, 480.0f);
            const Rect without = UIPreview::SimulatedSafeArea(simulation, 640.0f, 480.0f);
            EXPECT_FLOAT_EQ(with.x, without.x);
            EXPECT_FLOAT_EQ(with.y, without.y);
            EXPECT_FLOAT_EQ(with.w, without.w);
            EXPECT_FLOAT_EQ(with.h, without.h);
        }
    }

    TEST(UIPreviewTest, EveryDeviceSafeAreaStaysInsideTheOutputAndIsNeverEmpty) {
        const PreviewDevice huge{"huge", 100, 200, 10000.0f, 10000.0f};
        eastl::vector<const PreviewDevice*> devices{&huge};
        for (const PreviewDevice& device : UIPreview::GetDevices()) {
            devices.push_back(&device);
        }
        for (const PreviewDevice* device : devices) {
            for (const auto& size : {std::pair<float, float>{device->width, device->height}, std::pair<float, float>{device->height, device->width}, std::pair<float, float>{64.0f, 64.0f}}) {
                const Rect safe = UIPreview::SimulatedSafeArea(SafeAreaSimulation::Device, device, size.first, size.second);
                SCOPED_TRACE(Format("{} {}x{}", device->name, size.first, size.second).c_str());
                EXPECT_GE(safe.x, 0.0f);
                EXPECT_GE(safe.y, 0.0f);
                EXPECT_LE(safe.x + safe.w, size.first + kTolerance);
                EXPECT_LE(safe.y + safe.h, size.second + kTolerance);
                EXPECT_GT(safe.w, 0.0f);
                EXPECT_GT(safe.h, 0.0f);
            }
        }
    }

}  // namespace BECore::Tests
