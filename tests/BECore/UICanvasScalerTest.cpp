#include <BECore/UI/UICanvasScaler.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            EXPECT_NEAR(rect.x, x, kTolerance);
            EXPECT_NEAR(rect.y, y, kTolerance);
            EXPECT_NEAR(rect.w, w, kTolerance);
            EXPECT_NEAR(rect.h, h, kTolerance);
        }

        UILayoutContext Output(float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            return context;
        }

        UICanvasScaleParams Screen(CanvasScaleMode mode) {
            UICanvasScaleParams params;
            params.space = CanvasSpace::Screen;
            params.scaleMode = mode;
            return params;
        }

    }  // namespace

    // ---- Screen: the scale modes ------------------------------------------------------------------------------------------

    TEST(UICanvasScalerTest, ConstantPixelSizeIsOneUnitPerWindowUnit) {
        UILayoutContext context = Output(1920.0f, 1080.0f);
        const UICanvasScaleParams params = Screen(CanvasScaleMode::ConstantPixelSize);

        UICanvasResolved resolved = UICanvasScaler::Resolve(params, context);
        EXPECT_FLOAT_EQ(resolved.scale, 1.0f);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 1920.0f, 1080.0f);

        context.pixelDensity = 2.0f;  // a Retina window: twice the pixels for the same units
        resolved = UICanvasScaler::Resolve(params, context);
        EXPECT_FLOAT_EQ(resolved.scale, 2.0f);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 960.0f, 540.0f);
    }

    TEST(UICanvasScalerTest, ConstantPhysicalSizeAlsoFollowsTheDisplayScale) {
        UILayoutContext context = Output(1920.0f, 1080.0f);
        context.pixelDensity = 2.0f;
        context.displayScale = 1.5f;

        const UICanvasResolved resolved = UICanvasScaler::Resolve(Screen(CanvasScaleMode::ConstantPhysicalSize), context);

        EXPECT_FLOAT_EQ(resolved.scale, 3.0f);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 640.0f, 360.0f);
    }

    TEST(UICanvasScalerTest, ScaleWithScreenIsOneAtTheReferenceSizeAndFollowsIt) {
        const UICanvasScaleParams params = Screen(CanvasScaleMode::ScaleWithScreen);  // 1280 x 720

        EXPECT_NEAR(UICanvasScaler::Resolve(params, Output(1280.0f, 720.0f)).scale, 1.0f, kTolerance);
        EXPECT_NEAR(UICanvasScaler::Resolve(params, Output(2560.0f, 1440.0f)).scale, 2.0f, kTolerance);
        EXPECT_NEAR(UICanvasScaler::Resolve(params, Output(640.0f, 360.0f)).scale, 0.5f, kTolerance);
    }

    TEST(UICanvasScalerTest, MatchPicksTheWidthTheHeightOrTheBlend) {
        UICanvasScaleParams params = Screen(CanvasScaleMode::ScaleWithScreen);
        params.minScale = 0.0f;
        params.maxScale = 0.0f;
        const UILayoutContext wide = Output(2560.0f, 720.0f);  // twice the reference width, the reference height

        params.match = 0.0f;
        EXPECT_NEAR(UICanvasScaler::Resolve(params, wide).scale, 2.0f, kTolerance);
        params.match = 1.0f;
        EXPECT_NEAR(UICanvasScaler::Resolve(params, wide).scale, 1.0f, kTolerance);
        params.match = 0.5f;
        EXPECT_NEAR(UICanvasScaler::Resolve(params, wide).scale, std::sqrt(2.0f), kTolerance);  // the geometric blend

        params.match = 7.0f;  // out of range is clamped to 1
        EXPECT_NEAR(UICanvasScaler::Resolve(params, wide).scale, 1.0f, kTolerance);
    }

    TEST(UICanvasScalerTest, ABadReferenceSizeGivesScaleOne) {
        UICanvasScaleParams params = Screen(CanvasScaleMode::ScaleWithScreen);
        params.referenceWidth = 0.0f;

        EXPECT_FLOAT_EQ(UICanvasScaler::Resolve(params, Output(1000.0f, 500.0f)).scale, 1.0f);
        EXPECT_FLOAT_EQ(UICanvasScaler::Resolve(Screen(CanvasScaleMode::ScaleWithScreen), Output(0.0f, 0.0f)).scale, 1.0f);
    }

    TEST(UICanvasScalerTest, TheUserScaleMultipliesTheModeScale) {
        UILayoutContext context = Output(1280.0f, 720.0f);
        context.userScale = 1.5f;

        const UICanvasResolved resolved = UICanvasScaler::Resolve(Screen(CanvasScaleMode::ScaleWithScreen), context);

        EXPECT_NEAR(resolved.scale, 1.5f, kTolerance);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 1280.0f / 1.5f, 720.0f / 1.5f);
    }

    TEST(UICanvasScalerTest, TheScaleStaysBetweenTheLimits) {
        UICanvasScaleParams params = Screen(CanvasScaleMode::ScaleWithScreen);
        params.minScale = 0.5f;
        params.maxScale = 2.0f;

        EXPECT_FLOAT_EQ(UICanvasScaler::Resolve(params, Output(320.0f, 180.0f)).scale, 0.5f);
        EXPECT_FLOAT_EQ(UICanvasScaler::Resolve(params, Output(3840.0f, 2160.0f)).scale, 2.0f);

        UILayoutContext context = Output(3840.0f, 2160.0f);
        context.userScale = 1.5f;  // the limit holds after the user scale
        EXPECT_FLOAT_EQ(UICanvasScaler::Resolve(params, context).scale, 2.0f);

        params.minScale = 0.0f;  // 0 turns a limit off
        params.maxScale = 0.0f;
        EXPECT_NEAR(UICanvasScaler::Resolve(params, Output(3840.0f, 2160.0f)).scale, 3.0f, kTolerance);
    }

    TEST(UICanvasScalerTest, TheBoundsTimesTheScaleAreTheOutputInEveryMode) {
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        const float sizes[][2] = {{480.0f, 270.0f}, {1280.0f, 720.0f}, {1000.0f, 1000.0f}, {720.0f, 1560.0f}, {3840.0f, 2160.0f}, {3840.0f, 1080.0f}};
        for (CanvasScaleMode mode : modes) {
            for (const auto& size : sizes) {
                UILayoutContext context = Output(size[0], size[1]);
                context.userScale = 1.25f;
                const UICanvasResolved resolved = UICanvasScaler::Resolve(Screen(mode), context);
                SCOPED_TRACE(Format("{} {}x{}", EnumUtils<CanvasScaleMode>::ToString(mode), size[0], size[1]).c_str());
                EXPECT_NEAR(resolved.bounds.w * resolved.scale, size[0], 0.01f);
                EXPECT_NEAR(resolved.bounds.h * resolved.scale, size[1], 0.01f);
                EXPECT_NEAR(resolved.transform.scale, resolved.scale, kTolerance);
            }
        }
    }

    // ---- Screen: the safe area --------------------------------------------------------------------------------------------

    TEST(UICanvasScalerTest, TheSafeAreaIsTheWholeOutputWhenTheContextHasNone) {
        const UICanvasResolved resolved = UICanvasScaler::Resolve(Screen(CanvasScaleMode::ConstantPixelSize), Output(800.0f, 400.0f));

        ExpectRect(resolved.safeBounds, 0.0f, 0.0f, 800.0f, 400.0f);
    }

    TEST(UICanvasScalerTest, TheSafeAreaIsMappedIntoUnitsAndTheContentUsesItOnlyWhenAsked) {
        UILayoutContext context = Output(2560.0f, 1440.0f);
        context.safeArea = {100.0f, 60.0f, 2360.0f, 1320.0f};                   // a notch on the left and the top, in output pixels
        UICanvasScaleParams params = Screen(CanvasScaleMode::ScaleWithScreen);  // scale 2

        UICanvasResolved resolved = UICanvasScaler::Resolve(params, context);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 1280.0f, 720.0f);
        ExpectRect(resolved.safeBounds, 50.0f, 30.0f, 1180.0f, 660.0f);
        ExpectRect(resolved.contentBounds, 0.0f, 0.0f, 1280.0f, 720.0f);  // the full canvas stays available to backgrounds

        params.useSafeArea = true;
        resolved = UICanvasScaler::Resolve(params, context);
        ExpectRect(resolved.contentBounds, 50.0f, 30.0f, 1180.0f, 660.0f);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 1280.0f, 720.0f);
    }

    TEST(UICanvasScalerTest, ASafeAreaThatSticksOutOfTheOutputIsCutToIt) {
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {-50.0f, -50.0f, 2000.0f, 2000.0f};

        const UICanvasResolved resolved = UICanvasScaler::Resolve(Screen(CanvasScaleMode::ConstantPixelSize), context);

        ExpectRect(resolved.safeBounds, 0.0f, 0.0f, 1000.0f, 500.0f);
    }

    // ---- SceneView and World ----------------------------------------------------------------------------------------------

    TEST(UICanvasScalerTest, SceneViewIsTheScenesViewFittedLikeSceneDraw) {
        UICanvasScaleParams params;
        params.space = CanvasSpace::SceneView;
        UILayoutContext context = Output(1920.0f, 1080.0f);
        context.sceneViewWidth = 840.0f;
        context.sceneViewHeight = 760.0f;

        const UICanvasResolved resolved = UICanvasScaler::Resolve(params, context);
        const ViewTransform fit = ViewTransform::Fit(840.0f, 760.0f, 1920.0f, 1080.0f);

        EXPECT_FLOAT_EQ(resolved.scale, fit.scale);
        EXPECT_FLOAT_EQ(resolved.transform.scale, fit.scale);
        EXPECT_FLOAT_EQ(resolved.transform.offsetX, fit.offsetX);
        EXPECT_FLOAT_EQ(resolved.transform.offsetY, fit.offsetY);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 840.0f, 760.0f);
    }

    TEST(UICanvasScalerTest, SceneViewIgnoresTheScaleModeTheUserScaleAndTheLimits) {
        UICanvasScaleParams params;
        params.space = CanvasSpace::SceneView;
        params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        params.maxScale = 0.1f;
        UILayoutContext context = Output(1600.0f, 900.0f);
        context.sceneViewWidth = 800.0f;
        context.sceneViewHeight = 450.0f;
        context.userScale = 2.0f;

        EXPECT_FLOAT_EQ(UICanvasScaler::Resolve(params, context).scale, 2.0f);  // only the fit
    }

    TEST(UICanvasScalerTest, SceneViewOfASceneWithoutAViewSizeIsTheOutputAtScaleOne) {
        UICanvasScaleParams params;
        params.space = CanvasSpace::SceneView;

        const UICanvasResolved resolved = UICanvasScaler::Resolve(params, Output(1280.0f, 720.0f));

        EXPECT_FLOAT_EQ(resolved.scale, 1.0f);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 1280.0f, 720.0f);
        EXPECT_FLOAT_EQ(resolved.transform.offsetX, 0.0f);
    }

    TEST(UICanvasScalerTest, SceneViewSafeAreaGoesThroughTheFitIntoSceneUnits) {
        UICanvasScaleParams params;
        params.space = CanvasSpace::SceneView;
        UILayoutContext context = Output(2000.0f, 1000.0f);  // a 1000 x 1000 view fills the height: scale 1, bars of 500 on each side
        context.sceneViewWidth = 1000.0f;
        context.sceneViewHeight = 1000.0f;
        context.safeArea = {0.0f, 100.0f, 2000.0f, 900.0f};  // a notch at the top of the output

        const UICanvasResolved resolved = UICanvasScaler::Resolve(params, context);

        ExpectRect(resolved.safeBounds, 0.0f, 100.0f, 1000.0f, 900.0f);  // the bars are outside the view, so only the top is cut
    }

    TEST(UICanvasScalerTest, WorldIsTheBoxOfTheNodeAndFollowsIt) {
        UICanvasScaleParams params;
        params.space = CanvasSpace::World;
        UILayoutContext context = Output(1920.0f, 1080.0f);
        context.sceneViewWidth = 840.0f;
        context.sceneViewHeight = 760.0f;
        context.worldBox = {100.0f, 60.0f, 80.0f, 20.0f};
        const ViewTransform fit = ViewTransform::Fit(840.0f, 760.0f, 1920.0f, 1080.0f);

        UICanvasResolved resolved = UICanvasScaler::Resolve(params, context);
        ExpectRect(resolved.bounds, 0.0f, 0.0f, 80.0f, 20.0f);
        EXPECT_FLOAT_EQ(resolved.scale, fit.scale);

        // The origin of the canvas lands where the scene draws the point (100, 60), the far corner where it draws (180, 80)
        const Rect origin = resolved.transform.ToOutput(0.0f, 0.0f, 0.0f, 0.0f);
        const Rect scenePoint = fit.ToOutput(100.0f, 60.0f, 0.0f, 0.0f);
        EXPECT_NEAR(origin.x, scenePoint.x, kTolerance);
        EXPECT_NEAR(origin.y, scenePoint.y, kTolerance);
        const Rect corner = resolved.transform.ToOutput(80.0f, 20.0f, 0.0f, 0.0f);
        const Rect sceneCorner = fit.ToOutput(180.0f, 80.0f, 0.0f, 0.0f);
        EXPECT_NEAR(corner.x, sceneCorner.x, kTolerance);
        EXPECT_NEAR(corner.y, sceneCorner.y, kTolerance);

        context.worldBox.x = 300.0f;  // the node moved
        resolved = UICanvasScaler::Resolve(params, context);
        EXPECT_NEAR(resolved.transform.ToOutput(0.0f, 0.0f, 0.0f, 0.0f).x, fit.ToOutput(300.0f, 0.0f, 0.0f, 0.0f).x, kTolerance);
    }

    TEST(UICanvasScalerTest, WorldHasNoSafeAreaOfItsOwn) {
        UICanvasScaleParams params;
        params.space = CanvasSpace::World;
        params.useSafeArea = true;
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {100.0f, 100.0f, 100.0f, 100.0f};
        context.worldBox = {0.0f, 0.0f, 50.0f, 20.0f};

        const UICanvasResolved resolved = UICanvasScaler::Resolve(params, context);

        ExpectRect(resolved.contentBounds, 0.0f, 0.0f, 50.0f, 20.0f);
    }

    TEST(UICanvasScalerTest, TheContextsComparisonSeesEveryField) {
        const UILayoutContext base = Output(1280.0f, 720.0f);
        EXPECT_TRUE(base == Output(1280.0f, 720.0f));

        UILayoutContext changed = base;
        changed.outputWidth = 1281.0f;
        EXPECT_FALSE(base == changed);
        changed = base;
        changed.pixelDensity = 2.0f;
        EXPECT_FALSE(base == changed);
        changed = base;
        changed.displayScale = 2.0f;
        EXPECT_FALSE(base == changed);
        changed = base;
        changed.safeArea.y = 1.0f;
        EXPECT_FALSE(base == changed);
        changed = base;
        changed.userScale = 1.5f;
        EXPECT_FALSE(base == changed);
        changed = base;
        changed.sceneViewHeight = 5.0f;
        EXPECT_FALSE(base == changed);
        changed = base;
        changed.worldBox.w = 5.0f;
        EXPECT_FALSE(base == changed);
    }

}  // namespace BECore::Tests
