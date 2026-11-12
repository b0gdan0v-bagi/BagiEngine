#include <Widgets/Viewport/ViewportCamera.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // The unzoomed image: 800 x 600 target pixels shown at 0.5 in a panel of 400 x 300 whose top left is (100, 50)
        const Rect kPanel{100.0f, 50.0f, 400.0f, 300.0f};
        constexpr float kBase = 0.5f;
        constexpr Vec2 kPanelSize{400.0f, 300.0f};

        void ExpectVec(Vec2 expected, Vec2 actual) {
            EXPECT_NEAR(expected.x, actual.x, kTolerance);
            EXPECT_NEAR(expected.y, actual.y, kTolerance);
        }

    }  // namespace

    TEST(ViewportCameraTest, TheDefaultCameraIsTheIdentityAndShowsTheImageAsTheFitDoes) {
        const ViewportCamera camera;

        EXPECT_TRUE(camera.IsIdentity());
        ExpectVec({100.0f, 50.0f}, camera.ToScreen({0.0f, 0.0f}, kPanel, kBase));
        ExpectVec({500.0f, 350.0f}, camera.ToScreen({800.0f, 600.0f}, kPanel, kBase));
        ExpectVec({300.0f, 200.0f}, camera.ToScreen({400.0f, 300.0f}, kPanel, kBase));
    }

    TEST(ViewportCameraTest, AnyCameraRoundTripsThroughTheScreen) {
        ViewportCamera camera;
        camera.zoom = 3.0f;
        camera.centerX = -120.0f;
        camera.centerY = 75.0f;

        for (const Vec2 point : {Vec2{0.0f, 0.0f}, Vec2{123.0f, 456.0f}, Vec2{800.0f, 600.0f}, Vec2{-50.0f, 900.0f}}) {
            ExpectVec(point, camera.ToTarget(camera.ToScreen(point, kPanel, kBase), kPanel, kBase));
        }
    }

    TEST(ViewportCameraTest, TheCenterIsTheTargetPointAtTheMiddleOfThePanel) {
        ViewportCamera camera;
        camera.zoom = 2.0f;
        camera.centerX = 100.0f;  // from the target's centre (400, 300): the point (500, 300)
        camera.centerY = 0.0f;

        ExpectVec({300.0f, 200.0f}, camera.ToScreen({500.0f, 300.0f}, kPanel, kBase));
    }

    TEST(ViewportCameraTest, ZoomingKeepsThePointUnderThePointerFixed) {
        ViewportCamera camera;
        camera.centerX = 30.0f;
        const Vec2 pointer{180.0f, 120.0f};
        const Vec2 under = camera.ToTarget(pointer, kPanel, kBase);

        camera.ZoomAt(1.5f, pointer, kPanel, kBase);
        EXPECT_NEAR(1.5f, camera.zoom, kTolerance);
        ExpectVec(under, camera.ToTarget(pointer, kPanel, kBase));
        ExpectVec(pointer, camera.ToScreen(under, kPanel, kBase));

        camera.ZoomAt(1.0f / 1.5f / 2.0f, pointer, kPanel, kBase);
        ExpectVec(under, camera.ToTarget(pointer, kPanel, kBase));
    }

    TEST(ViewportCameraTest, ZoomingAtTheMiddleOfThePanelKeepsTheCenter) {
        ViewportCamera camera;
        camera.ZoomAt(2.0f, {300.0f, 200.0f}, kPanel, kBase);

        EXPECT_NEAR(2.0f, camera.zoom, kTolerance);
        EXPECT_NEAR(0.0f, camera.centerX, kTolerance);
        EXPECT_NEAR(0.0f, camera.centerY, kTolerance);
    }

    TEST(ViewportCameraTest, TheZoomIsClamped) {
        ViewportCamera camera;
        for (int step = 0; step < 100; ++step) {
            camera.ZoomAt(2.0f, {300.0f, 200.0f}, kPanel, kBase);
        }
        EXPECT_FLOAT_EQ(ViewportCamera::kMaxZoom, camera.zoom);

        for (int step = 0; step < 100; ++step) {
            camera.ZoomAt(0.5f, {300.0f, 200.0f}, kPanel, kBase);
        }
        EXPECT_FLOAT_EQ(ViewportCamera::kMinZoom, camera.zoom);
    }

    TEST(ViewportCameraTest, AtTheClampThePointerStillHoldsItsPoint) {
        ViewportCamera camera;
        camera.zoom = ViewportCamera::kMaxZoom;
        const Vec2 pointer{150.0f, 300.0f};
        const Vec2 under = camera.ToTarget(pointer, kPanel, kBase);

        camera.ZoomAt(2.0f, pointer, kPanel, kBase);

        EXPECT_FLOAT_EQ(ViewportCamera::kMaxZoom, camera.zoom);
        ExpectVec(under, camera.ToTarget(pointer, kPanel, kBase));
    }

    TEST(ViewportCameraTest, PanningMovesTheImageWithThePointer) {
        ViewportCamera camera;
        camera.zoom = 4.0f;
        const Vec2 before = camera.ToScreen({400.0f, 300.0f}, kPanel, kBase);

        camera.PanBy({40.0f, -20.0f}, kBase);

        ExpectVec({before.x + 40.0f, before.y - 20.0f}, camera.ToScreen({400.0f, 300.0f}, kPanel, kBase));
    }

    TEST(ViewportCameraTest, ASmallRectIsFramedToFillThePanelWithTheMargin) {
        ViewportCamera camera;
        camera.FrameRect({300.0f, 200.0f, 100.0f, 50.0f}, kPanelSize, kBase, 10.0f);

        // 380 x 280 screen units of room for 100 x 50 target pixels: the width limits (3.8 per pixel), zoom = 3.8 / 0.5
        EXPECT_NEAR(7.6f, camera.zoom, kTolerance);
        ExpectVec({300.0f, 200.0f}, camera.ToScreen({350.0f, 225.0f}, kPanel, kBase));
        EXPECT_NEAR(110.0f, camera.ToScreen({300.0f, 225.0f}, kPanel, kBase).x, kTolerance);
        EXPECT_NEAR(490.0f, camera.ToScreen({400.0f, 225.0f}, kPanel, kBase).x, kTolerance);
    }

    TEST(ViewportCameraTest, AHugeRectZoomsOutAndAClampStops) {
        ViewportCamera camera;
        camera.FrameRect({0.0f, 0.0f, 1600.0f, 600.0f}, kPanelSize, kBase, 0.0f);
        EXPECT_NEAR(0.5f, camera.zoom, kTolerance);  // 400 / 1600 = 0.25 per pixel over a base of 0.5

        camera.FrameRect({0.0f, 0.0f, 1.0e7f, 1.0e7f}, kPanelSize, kBase, 0.0f);
        EXPECT_FLOAT_EQ(ViewportCamera::kMinZoom, camera.zoom);

        camera.FrameRect({0.0f, 0.0f, 0.01f, 0.01f}, kPanelSize, kBase, 0.0f);
        EXPECT_FLOAT_EQ(ViewportCamera::kMaxZoom, camera.zoom);
    }

    TEST(ViewportCameraTest, ADegenerateRectGivesZoomOneCenteredOnIt) {
        ViewportCamera camera;
        camera.zoom = 5.0f;
        camera.FrameRect({500.0f, 100.0f, 0.0f, 0.0f}, kPanelSize, kBase, 10.0f);

        EXPECT_FLOAT_EQ(1.0f, camera.zoom);
        ExpectVec({300.0f, 200.0f}, camera.ToScreen({500.0f, 100.0f}, kPanel, kBase));
    }

    TEST(ViewportCameraTest, AMarginThatLeavesNoRoomIsTreatedAsDegenerate) {
        ViewportCamera camera;
        camera.FrameRect({0.0f, 0.0f, 100.0f, 100.0f}, kPanelSize, kBase, 500.0f);

        EXPECT_FLOAT_EQ(1.0f, camera.zoom);
    }

    TEST(ViewportCameraTest, ResetGoesBackToTheIdentity) {
        ViewportCamera camera;
        camera.ZoomAt(3.0f, {120.0f, 80.0f}, kPanel, kBase);
        camera.PanBy({10.0f, 10.0f}, kBase);
        EXPECT_FALSE(camera.IsIdentity());

        camera.Reset();

        EXPECT_TRUE(camera.IsIdentity());
    }

}  // namespace BECore::Tests
