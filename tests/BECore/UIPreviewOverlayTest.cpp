#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        void ExpectRect(const Rect& actual, const Rect& expected) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance);
            EXPECT_NEAR(actual.y, expected.y, kTolerance);
            EXPECT_NEAR(actual.w, expected.w, kTolerance);
            EXPECT_NEAR(actual.h, expected.h, kTolerance);
        }

        /// An element whose children are a popup, closed or open by the flag
        class PopupElement : public UIElement {
        public:
            [[nodiscard]] bool HasPopupChildren() const override {
                return true;
            }
            [[nodiscard]] bool IsPopupOpen() const override {
                return open;
            }

            bool open = false;
        };

        UILayoutContext Output(float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            return context;
        }

        IntrusivePtr<UIElement> AddBox(UIElement& parent, const char* name, float x, float y, float width, float height, bool interactive = false) {
            auto box = parent.AddChild(PoolString::Intern(name));
            box->style.rect.x = UILength::Px(x);
            box->style.rect.y = UILength::Px(y);
            box->style.rect.width = UILength::Px(width);
            box->style.rect.height = UILength::Px(height);
            box->interactive = interactive;
            return box;
        }

        UICanvasLayout MakeCanvas(CanvasScaleMode mode = CanvasScaleMode::ConstantPixelSize) {
            UICanvasLayout canvas;
            canvas.params.scaleMode = mode;
            return canvas;
        }

        const TouchMark* Find(const Vector<TouchMark>& marks, PoolString name) {
            for (const TouchMark& mark : marks) {
                if (mark.name == name) {
                    return &mark;
                }
            }
            return nullptr;
        }

    }  // namespace

    TEST(UIPreviewOverlayTest, TheViewInAWiderOutputIsCenteredWithBarsOnTheSides) {
        ExpectRect(UIPreviewOverlay::ViewRect(400.0f, 300.0f, 800.0f, 300.0f), {200.0f, 0.0f, 400.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, TheViewInATallerOutputIsCenteredWithBarsAboveAndBelow) {
        ExpectRect(UIPreviewOverlay::ViewRect(400.0f, 300.0f, 400.0f, 600.0f), {0.0f, 150.0f, 400.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, TheViewIsScaledToFitTheOutput) {
        ExpectRect(UIPreviewOverlay::ViewRect(1280.0f, 720.0f, 640.0f, 640.0f), {0.0f, 140.0f, 640.0f, 360.0f});
    }

    TEST(UIPreviewOverlayTest, ASceneWithoutAViewSizeIsTheWholeOutput) {
        ExpectRect(UIPreviewOverlay::ViewRect(0.0f, 0.0f, 390.0f, 844.0f), {0.0f, 0.0f, 390.0f, 844.0f});
        ExpectRect(UIPreviewOverlay::ViewRect(400.0f, 0.0f, 390.0f, 844.0f), {0.0f, 0.0f, 390.0f, 844.0f});
    }

    TEST(UIPreviewOverlayTest, AButtonUnderTheMinimumIsTooSmallAndOneOverItIsNot) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Small", 10.0f, 10.0f, 100.0f, 20.0f, true);
        AddBox(*canvas.root, "Big", 10.0f, 100.0f, 100.0f, 32.0f, true);
        canvas.Update(Output(1000.0f, 500.0f));

        const auto marks = UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f);

        ASSERT_EQ(marks.size(), 2u);
        ASSERT_NE(Find(marks, "Small"_intern), nullptr);
        ASSERT_NE(Find(marks, "Big"_intern), nullptr);
        EXPECT_TRUE(Find(marks, "Small"_intern)->tooSmall);
        EXPECT_FALSE(Find(marks, "Big"_intern)->tooSmall);
        ExpectRect(Find(marks, "Small"_intern)->rect, {10.0f, 10.0f, 100.0f, 20.0f});
    }

    TEST(UIPreviewOverlayTest, TheDensityTurnsTheSizeIntoWindowUnits) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Button", 10.0f, 10.0f, 60.0f, 40.0f, true);
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_FALSE(UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f)[0].tooSmall);
        EXPECT_TRUE(UIPreviewOverlay::TouchTargets(canvas, 32.0f, 2.0f)[0].tooSmall);
    }

    TEST(UIPreviewOverlayTest, ElementsThatAreNotInteractiveOrAreHiddenGiveNoMark) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Decoration", 10.0f, 10.0f, 5.0f, 5.0f);
        auto hidden = AddBox(*canvas.root, "Hidden", 10.0f, 100.0f, 100.0f, 50.0f, true);
        hidden->style.visible = false;
        auto parent = AddBox(*canvas.root, "HiddenParent", 10.0f, 200.0f, 100.0f, 100.0f);
        parent->style.visible = false;
        AddBox(*parent, "Inner", 0.0f, 0.0f, 50.0f, 50.0f, true);
        AddBox(*canvas.root, "Shown", 300.0f, 10.0f, 100.0f, 50.0f, true);
        canvas.Update(Output(1000.0f, 500.0f));

        const auto marks = UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f);

        ASSERT_EQ(marks.size(), 1u);
        EXPECT_EQ(marks[0].name, "Shown"_intern);
    }

    TEST(UIPreviewOverlayTest, TheChildrenOfAClosedPopupAreNotWalkedAndThoseOfAnOpenOneAre) {
        UICanvasLayout canvas = MakeCanvas();
        auto popup = New<PopupElement>();
        popup->name = "Popup"_intern;
        canvas.root->children.push_back(popup);
        auto option = AddBox(*popup, "Option", 0.0f, 0.0f, 100.0f, 40.0f, true);
        canvas.Update(Output(1000.0f, 500.0f));
        option->rect = {10.0f, 10.0f, 100.0f, 40.0f};  // as an open popup would place it

        EXPECT_TRUE(UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f).empty());

        popup->open = true;
        const auto marks = UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f);
        ASSERT_EQ(marks.size(), 1u);
        EXPECT_EQ(marks[0].name, "Option"_intern);
    }

    TEST(UIPreviewOverlayTest, ASceneViewCanvasMapsItsElementsThroughTheFitOfTheView) {
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        AddBox(*canvas.root, "Button", 10.0f, 10.0f, 100.0f, 50.0f, true);
        UILayoutContext context = Output(400.0f, 600.0f);
        context.sceneViewWidth = 400.0f;
        context.sceneViewHeight = 300.0f;
        canvas.Update(context);

        const auto marks = UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f);

        ASSERT_EQ(marks.size(), 1u);
        ExpectRect(marks[0].rect, {10.0f, 160.0f, 100.0f, 50.0f});  // the view is 150 below the top of the output
    }

    TEST(UIPreviewOverlayTest, AScreenCanvasOnAPhoneMapsItsElementsThroughTheScale) {
        UICanvasLayout canvas = MakeCanvas(CanvasScaleMode::ScaleWithScreen);
        AddBox(*canvas.root, "Button", 20.0f, 30.0f, 200.0f, 80.0f, true);
        canvas.Update(Output(390.0f, 844.0f));
        const float scale = canvas.GetResolved().scale;
        ASSERT_NE(scale, 1.0f);

        const auto marks = UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f);

        ASSERT_EQ(marks.size(), 1u);
        ExpectRect(marks[0].rect, {20.0f * scale, 30.0f * scale, 200.0f * scale, 80.0f * scale});
        EXPECT_EQ(marks[0].tooSmall, 80.0f * scale < 32.0f - 0.01f);
    }

    TEST(UIPreviewOverlayTest, TheTooSmallMarksAreExactlyTheTouchIssuesOfTheValidator) {
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen};
        const float sizes[][2] = {{390.0f, 844.0f}, {844.0f, 390.0f}, {1280.0f, 720.0f}, {2560.0f, 1440.0f}, {320.0f, 568.0f}};
        for (CanvasScaleMode mode : modes) {
            for (const auto& size : sizes) {
                UICanvasLayout canvas = MakeCanvas(mode);
                AddBox(*canvas.root, "Tiny", 10.0f, 10.0f, 30.0f, 30.0f, true);
                AddBox(*canvas.root, "Edge", 10.0f, 60.0f, 80.0f, 32.0f, true);
                AddBox(*canvas.root, "Wide", 10.0f, 100.0f, 300.0f, 20.0f, true);
                AddBox(*canvas.root, "Fat", 10.0f, 130.0f, 90.0f, 90.0f, true);
                auto group = AddBox(*canvas.root, "Group", 200.0f, 10.0f, 150.0f, 150.0f);
                AddBox(*group, "Nested", 5.0f, 5.0f, 40.0f, 25.0f, true);
                AddBox(*canvas.root, "Hidden", 10.0f, 300.0f, 10.0f, 10.0f, true)->style.visible = false;
                canvas.Update(Output(size[0], size[1]));
                SCOPED_TRACE(Format("{}x{} scale {}", size[0], size[1], canvas.GetResolved().scale).c_str());

                UILayoutValidatorOptions options;
                const auto issues = canvas.Validate(options);
                const auto marks = UIPreviewOverlay::TouchTargets(canvas, options.minTouchSize, options.pixelDensity);

                size_t tooSmall = 0;
                for (const TouchMark& mark : marks) {
                    tooSmall += mark.tooSmall ? 1u : 0u;
                }
                size_t touchIssues = 0;
                for (const UILayoutIssue& issue : issues) {
                    if (issue.kind != UILayoutIssueKind::TouchTargetTooSmall) {
                        continue;
                    }
                    ++touchIssues;
                    const TouchMark* mark = Find(marks, issue.element);
                    ASSERT_NE(mark, nullptr) << String(issue.element.ToStringView()).c_str();
                    EXPECT_TRUE(mark->tooSmall) << String(issue.element.ToStringView()).c_str();
                }
                EXPECT_EQ(tooSmall, touchIssues);
            }
        }
    }

    TEST(UIPreviewOverlayTest, ACanvasWithNoLayoutGivesNoMarks) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Button", 10.0f, 10.0f, 100.0f, 50.0f, true);

        EXPECT_TRUE(UIPreviewOverlay::TouchTargets(canvas, 32.0f, 1.0f).empty());
    }

    TEST(UIPreviewOverlayTest, TheBoundsOfACanvasWithASafeAreaAreInOutputPixels) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.params.useSafeArea = true;
        UILayoutContext context = Output(400.0f, 600.0f);
        context.safeArea = {0.0f, 40.0f, 400.0f, 500.0f};
        canvas.Update(context);

        ExpectRect(UIPreviewOverlay::CanvasBounds(canvas.GetResolved()), {0.0f, 0.0f, 400.0f, 600.0f});
        ExpectRect(UIPreviewOverlay::ContentBounds(canvas.GetResolved()), {0.0f, 40.0f, 400.0f, 500.0f});
    }

    TEST(UIPreviewOverlayTest, TheBoundsOfASceneViewCanvasFollowTheFit) {
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        UILayoutContext context = Output(400.0f, 600.0f);
        context.sceneViewWidth = 400.0f;
        context.sceneViewHeight = 300.0f;
        canvas.Update(context);

        ExpectRect(UIPreviewOverlay::CanvasBounds(canvas.GetResolved()), {0.0f, 150.0f, 400.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, RegionsComeBackInOutputPixelsInTheOrderOfPublishing) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.regions.Set(PoolString::Intern("Hand"), {10.0f, 20.0f, 300.0f, 80.0f});
        canvas.regions.Set(PoolString::Intern("Board"), {0.0f, 100.0f, 400.0f, 300.0f});
        canvas.Update(Output(400.0f, 600.0f));

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_EQ(marks[0].name, PoolString::Intern("Hand"));
        ExpectRect(marks[0].rect, {10.0f, 20.0f, 300.0f, 80.0f});
        EXPECT_EQ(marks[1].name, PoolString::Intern("Board"));
        ExpectRect(marks[1].rect, {0.0f, 100.0f, 400.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, ARegionOfASceneViewCanvasMapsThroughTheFit) {
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        canvas.regions.Set(PoolString::Intern("Board"), {40.0f, 30.0f, 200.0f, 100.0f});
        UILayoutContext context = Output(400.0f, 600.0f);
        context.sceneViewWidth = 400.0f;
        context.sceneViewHeight = 300.0f;
        canvas.Update(context);

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 1u);
        ExpectRect(marks[0].rect, ViewTransform::Fit(400.0f, 300.0f, 400.0f, 600.0f).ToOutput(40.0f, 30.0f, 200.0f, 100.0f));
        // The fit puts the view in the middle of a taller output, so the region is below the top of the image
        EXPECT_GT(marks[0].rect.y, 150.0f);
    }

    TEST(UIPreviewOverlayTest, ACanvasWithNoLayoutOrNoRegionsGivesNoRegionMarks) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.regions.Set(PoolString::Intern("Hand"), {10.0f, 20.0f, 300.0f, 80.0f});
        EXPECT_TRUE(UIPreviewOverlay::RegionMarks(canvas).empty());

        UICanvasLayout empty = MakeCanvas();
        empty.Update(Output(400.0f, 600.0f));
        EXPECT_TRUE(UIPreviewOverlay::RegionMarks(empty).empty());
    }

    TEST(UIPreviewOverlayTest, AZeroSizedRegionAndOneOutsideTheCanvasAreKept) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.regions.Set(PoolString::Intern("Point"), {50.0f, 60.0f, 0.0f, 0.0f});
        canvas.regions.Set(PoolString::Intern("Spill"), {-30.0f, 500.0f, 200.0f, 300.0f});
        canvas.Update(Output(400.0f, 600.0f));

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 2u);
        ExpectRect(marks[0].rect, {50.0f, 60.0f, 0.0f, 0.0f});
        ExpectRect(marks[1].rect, {-30.0f, 500.0f, 200.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, ANamePublishedTwiceShowsOnceWithTheLastRect) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.regions.Set(PoolString::Intern("Hand"), {10.0f, 20.0f, 300.0f, 80.0f});
        canvas.regions.Set(PoolString::Intern("Hand"), {0.0f, 0.0f, 100.0f, 50.0f});
        canvas.Update(Output(400.0f, 600.0f));

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 1u);
        ExpectRect(marks[0].rect, {0.0f, 0.0f, 100.0f, 50.0f});
    }

}  // namespace BECore::Tests
