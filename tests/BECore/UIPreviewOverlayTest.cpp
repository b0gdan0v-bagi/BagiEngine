#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

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

        const ElementMark* FindMark(const Vector<ElementMark>& marks, const char* name) {
            for (const ElementMark& mark : marks) {
                if (mark.name == PoolString::Intern(name)) {
                    return &mark;
                }
            }
            return nullptr;
        }

        Rect ToOutput(const UICanvasLayout& canvas, const Rect& rect) {
            return canvas.GetResolved().transform.ToOutput(rect.x, rect.y, rect.w, rect.h);
        }

        void ExpectPoint(const Vec2& actual, float x, float y) {
            EXPECT_NEAR(actual.x, x, kTolerance);
            EXPECT_NEAR(actual.y, y, kTolerance);
        }

        /// A canvas of every kind of parent the marks tell apart: a corner button, a bar between two anchors, a stack with a child, a popup, and a hidden group
        struct MarksFixture {
            UICanvasLayout canvas = MakeCanvas(CanvasScaleMode::ScaleWithScreen);
            IntrusivePtr<UIElement> corner;
            IntrusivePtr<UIElement> bar;
            IntrusivePtr<UIElement> stack;
            IntrusivePtr<UIElement> item;
            IntrusivePtr<PopupElement> popup;
            IntrusivePtr<UIElement> option;
            IntrusivePtr<UIElement> hidden;
            IntrusivePtr<UIElement> inner;

            MarksFixture() {
                canvas.params.useSafeArea = true;
                corner = AddBox(*canvas.root, "Corner", -10.0f, -10.0f, 100.0f, 50.0f);
                corner->style.rect.anchor = UIAnchor::BottomRight;
                bar = AddBox(*canvas.root, "Bar", 0.0f, 0.0f, 0.0f, 40.0f);
                bar->style.rect.anchorMin = Vec2(0.1f, 1.0f);
                bar->style.rect.anchorMax = Vec2(0.9f, 1.0f);
                bar->style.rect.pivot = Vec2(0.5f, 1.0f);
                stack = AddBox(*canvas.root, "Stack", 20.0f, 20.0f, 200.0f, 200.0f);
                stack->layout = UILayoutKind::Stack;
                item = AddBox(*stack, "Item", 0.0f, 0.0f, 60.0f, 30.0f);
                popup = New<PopupElement>();
                popup->name = "Popup"_intern;
                popup->style.rect.x = UILength::Px(300.0f);
                popup->style.rect.y = UILength::Px(20.0f);
                popup->style.rect.width = UILength::Px(80.0f);
                popup->style.rect.height = UILength::Px(30.0f);
                canvas.root->children.push_back(popup);
                option = AddBox(*popup, "Option", 0.0f, 0.0f, 100.0f, 40.0f);
                hidden = AddBox(*canvas.root, "Hidden", 400.0f, 20.0f, 100.0f, 100.0f);
                hidden->style.visible = false;
                inner = AddBox(*hidden, "Inner", 0.0f, 0.0f, 50.0f, 50.0f);
            }

            void Update(const UILayoutContext& context) {
                canvas.Update(context);
                option->rect = {310.0f, 60.0f, 100.0f, 40.0f};  // as an open popup would place it
            }
        };

        class ContentElement : public UIElement {
        public:
            UIContentFit CheckContent(float /*width*/, float /*height*/, float /*scale*/) const override {
                return {true, false, false};
            }
        };

        /// A canvas with findings of six kinds: the notch (OutsideSafeArea), an element off the canvas, a small button, content that does not fit, overlapping rows of a stack and a missing region
        struct FindingsFixture {
            UICanvasLayout canvas = MakeCanvas();
            UILayoutContext context = Output(1000.0f, 500.0f);

            FindingsFixture() {
                context.safeArea = {0.0f, 40.0f, 1000.0f, 420.0f};
                canvas.params.useSafeArea = true;
                AddBox(*canvas.root, "Notch", 0.0f, -20.0f, 100.0f, 30.0f);
                AddBox(*canvas.root, "Out", 950.0f, 100.0f, 100.0f, 50.0f);
                AddBox(*canvas.root, "Tiny", 100.0f, 100.0f, 10.0f, 10.0f, true);
                auto stuck = New<ContentElement>();
                stuck->name = "Stuck"_intern;
                stuck->style.rect.x = UILength::Px(300.0f);
                stuck->style.rect.y = UILength::Px(100.0f);
                stuck->style.rect.width = UILength::Px(20.0f);
                stuck->style.rect.height = UILength::Px(20.0f);
                canvas.root->children.push_back(stuck);
                auto row = AddBox(*canvas.root, "Row", 200.0f, 200.0f, 200.0f, 200.0f);
                row->layout = UILayoutKind::Stack;
                row->style.stack.spacing = -10.0f;
                AddBox(*row, "First", 0.0f, 0.0f, 100.0f, 40.0f);
                AddBox(*row, "Second", 0.0f, 0.0f, 100.0f, 40.0f);
                AddBox(*canvas.root, "Plate", 500.0f, 100.0f, 50.0f, 50.0f)->style.rect.region = "gone"_intern;
            }
        };

        UILayoutContext NotchOutput(float width, float height) {
            UILayoutContext context = Output(width, height);
            context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::TopNotch, width, height);
            return context;
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
        canvas.regions.Set("Hand"_intern, {10.0f, 20.0f, 300.0f, 80.0f});
        canvas.regions.Set("Board"_intern, {0.0f, 100.0f, 400.0f, 300.0f});
        canvas.Update(Output(400.0f, 600.0f));

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_EQ(marks[0].name, "Hand"_intern);
        ExpectRect(marks[0].rect, {10.0f, 20.0f, 300.0f, 80.0f});
        EXPECT_EQ(marks[1].name, "Board"_intern);
        ExpectRect(marks[1].rect, {0.0f, 100.0f, 400.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, ARegionOfASceneViewCanvasMapsThroughTheFit) {
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        canvas.regions.Set("Board"_intern, {40.0f, 30.0f, 200.0f, 100.0f});
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
        canvas.regions.Set("Hand"_intern, {10.0f, 20.0f, 300.0f, 80.0f});
        EXPECT_TRUE(UIPreviewOverlay::RegionMarks(canvas).empty());

        UICanvasLayout empty = MakeCanvas();
        empty.Update(Output(400.0f, 600.0f));
        EXPECT_TRUE(UIPreviewOverlay::RegionMarks(empty).empty());
    }

    TEST(UIPreviewOverlayTest, AZeroSizedRegionAndOneOutsideTheCanvasAreKept) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.regions.Set("Point"_intern, {50.0f, 60.0f, 0.0f, 0.0f});
        canvas.regions.Set("Spill"_intern, {-30.0f, 500.0f, 200.0f, 300.0f});
        canvas.Update(Output(400.0f, 600.0f));

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 2u);
        ExpectRect(marks[0].rect, {50.0f, 60.0f, 0.0f, 0.0f});
        ExpectRect(marks[1].rect, {-30.0f, 500.0f, 200.0f, 300.0f});
    }

    TEST(UIPreviewOverlayTest, ANamePublishedTwiceShowsOnceWithTheLastRect) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.regions.Set("Hand"_intern, {10.0f, 20.0f, 300.0f, 80.0f});
        canvas.regions.Set("Hand"_intern, {0.0f, 0.0f, 100.0f, 50.0f});
        canvas.Update(Output(400.0f, 600.0f));

        const auto marks = UIPreviewOverlay::RegionMarks(canvas);
        ASSERT_EQ(marks.size(), 1u);
        ExpectRect(marks[0].rect, {0.0f, 0.0f, 100.0f, 50.0f});
    }

    TEST(UIPreviewOverlayTest, ElementMarksAreTheRectsOfTheVisibleElementsThroughTheTransformOnTheWholeMatrix) {
        MarksFixture fixture;
        for (const Tests::UIMatrixCase& matrixCase : Tests::UILayoutMatrix()) {
            SCOPED_TRACE(matrixCase.label.c_str());
            fixture.Update(matrixCase.context);

            const auto marks = UIPreviewOverlay::ElementMarks(fixture.canvas);

            // Draw order: the tree in order, the root left out, the hidden subtree and the closed popup's child skipped
            ASSERT_EQ(marks.size(), 5u);
            const char* const order[] = {"Corner", "Bar", "Stack", "Item", "Popup"};
            for (size_t i = 0; i < marks.size(); ++i) {
                EXPECT_EQ(marks[i].name, PoolString::Intern(order[i]));
                ASSERT_NE(marks[i].element, nullptr);
                EXPECT_EQ(marks[i].element->name, marks[i].name);
                ExpectRect(marks[i].rect, ToOutput(fixture.canvas, marks[i].element->rect));
            }
            EXPECT_EQ(marks[0].depth, 0u);
            EXPECT_EQ(marks[3].depth, 1u);
            EXPECT_EQ(marks[3].element, fixture.item.Get());
        }
    }

    TEST(UIPreviewOverlayTest, ElementMarksOfAnOpenPopupListItsChildren) {
        MarksFixture fixture;
        fixture.Update(Output(1000.0f, 600.0f));
        fixture.popup->open = true;

        const auto marks = UIPreviewOverlay::ElementMarks(fixture.canvas);

        ASSERT_EQ(marks.size(), 6u);
        const ElementMark* option = FindMark(marks, "Option");
        ASSERT_NE(option, nullptr);
        EXPECT_EQ(option->depth, 1u);
        ExpectRect(option->rect, ToOutput(fixture.canvas, fixture.option->rect));
    }

    TEST(UIPreviewOverlayTest, ElementMarksSkipAnElementWithNoRectAndKeepTheCapacityOfTheList) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Empty", 10.0f, 10.0f, 0.0f, 20.0f);
        AddBox(*canvas.root, "Shown", 10.0f, 40.0f, 30.0f, 20.0f);
        EXPECT_TRUE(UIPreviewOverlay::ElementMarks(canvas).empty());  // no layout yet
        canvas.Update(Output(1000.0f, 500.0f));

        Vector<ElementMark> marks;
        UIPreviewOverlay::ElementMarks(canvas, marks);
        ASSERT_EQ(marks.size(), 1u);
        EXPECT_EQ(marks[0].name, "Shown"_intern);
        const size_t capacity = marks.capacity();
        UIPreviewOverlay::ElementMarks(canvas, marks);
        EXPECT_EQ(marks.size(), 1u);
        EXPECT_EQ(marks.capacity(), capacity);
    }

    TEST(UIPreviewOverlayTest, TheAnchorsOfACornerElementAreAPointOfTheContentBoxAndThePivotIsItsCorner) {
        MarksFixture fixture;
        fixture.Update(NotchOutput(390.0f, 844.0f));
        const Rect box = ToOutput(fixture.canvas, fixture.canvas.GetResolved().contentBounds);

        const auto mark = UIPreviewOverlay::AnchorsOf(fixture.canvas, *fixture.corner);

        ASSERT_TRUE(mark.has_value());
        ExpectRect(mark->box, box);
        ExpectRect(mark->anchors, {box.x + box.w, box.y + box.h, 0.0f, 0.0f});
        const Rect rect = ToOutput(fixture.canvas, fixture.corner->rect);
        ExpectPoint(mark->pivot, rect.x + rect.w, rect.y + rect.h);
    }

    TEST(UIPreviewOverlayTest, TheAnchorsOfAStretchedAxisSpanTheFractionsOfTheBox) {
        MarksFixture fixture;
        fixture.Update(Output(1000.0f, 600.0f));
        const Rect box = ToOutput(fixture.canvas, fixture.canvas.root->rect);

        const auto mark = UIPreviewOverlay::AnchorsOf(fixture.canvas, *fixture.bar);

        ASSERT_TRUE(mark.has_value());
        ExpectRect(mark->anchors, {box.x + 0.1f * box.w, box.y + box.h, 0.8f * box.w, 0.0f});
        const Rect rect = ToOutput(fixture.canvas, fixture.bar->rect);
        ExpectPoint(mark->pivot, rect.x + 0.5f * rect.w, rect.y + rect.h);
    }

    TEST(UIPreviewOverlayTest, TheAnchorBoxOfANestedElementIsItsParentAndOfARootChildThatIgnoresTheSafeAreaTheCanvas) {
        MarksFixture fixture;
        auto group = AddBox(*fixture.canvas.root, "Group", 50.0f, 300.0f, 200.0f, 100.0f);
        auto nested = AddBox(*group, "Nested", 10.0f, 10.0f, 40.0f, 20.0f);
        auto full = AddBox(*fixture.canvas.root, "Full", 0.0f, 0.0f, 50.0f, 50.0f);
        full->ignoreSafeArea = true;
        fixture.Update(NotchOutput(390.0f, 844.0f));

        const auto nestedMark = UIPreviewOverlay::AnchorsOf(fixture.canvas, *nested);
        ASSERT_TRUE(nestedMark.has_value());
        ExpectRect(nestedMark->box, ToOutput(fixture.canvas, group->rect));

        const auto fullMark = UIPreviewOverlay::AnchorsOf(fixture.canvas, *full);
        ASSERT_TRUE(fullMark.has_value());
        ExpectRect(fullMark->box, ToOutput(fixture.canvas, fixture.canvas.GetResolved().bounds));
        EXPECT_NE(fullMark->box.y, UIPreviewOverlay::ContentBounds(fixture.canvas.GetResolved()).y);
    }

    TEST(UIPreviewOverlayTest, TheAnchorBoxOfAnElementThatNamesAPublishedRegionIsTheRegion) {
        MarksFixture fixture;
        auto named = AddBox(*fixture.canvas.root, "Named", 0.0f, 0.0f, 20.0f, 20.0f);
        named->style.rect.region = "Board"_intern;
        auto unpublished = AddBox(*fixture.canvas.root, "Unpublished", 0.0f, 0.0f, 20.0f, 20.0f);
        unpublished->style.rect.region = "Nowhere"_intern;
        fixture.canvas.regions.Set("Board"_intern, {100.0f, 120.0f, 300.0f, 200.0f});
        fixture.Update(Output(1000.0f, 600.0f));

        const auto mark = UIPreviewOverlay::AnchorsOf(fixture.canvas, *named);
        ASSERT_TRUE(mark.has_value());
        ExpectRect(mark->box, ToOutput(fixture.canvas, {100.0f, 120.0f, 300.0f, 200.0f}));

        const auto fallback = UIPreviewOverlay::AnchorsOf(fixture.canvas, *unpublished);
        ASSERT_TRUE(fallback.has_value());
        ExpectRect(fallback->box, ToOutput(fixture.canvas, fixture.canvas.root->rect));
    }

    TEST(UIPreviewOverlayTest, AnElementWithNoAnchorsToShowGivesNone) {
        MarksFixture fixture;
        fixture.Update(Output(1000.0f, 600.0f));
        fixture.popup->open = true;

        EXPECT_TRUE(UIPreviewOverlay::AnchorsOf(fixture.canvas, *fixture.stack).has_value());    // its parent is the root
        EXPECT_FALSE(UIPreviewOverlay::AnchorsOf(fixture.canvas, *fixture.item).has_value());    // a stack child has none
        EXPECT_FALSE(UIPreviewOverlay::AnchorsOf(fixture.canvas, *fixture.option).has_value());  // a popup places its children itself
        EXPECT_FALSE(UIPreviewOverlay::AnchorsOf(fixture.canvas, *fixture.hidden).has_value());

        auto stranger = New<UIElement>();
        EXPECT_FALSE(UIPreviewOverlay::AnchorsOf(fixture.canvas, *stranger).has_value());

        UICanvasLayout noLayout = MakeCanvas();
        auto box = AddBox(*noLayout.root, "Box", 0.0f, 0.0f, 10.0f, 10.0f);
        EXPECT_FALSE(UIPreviewOverlay::AnchorsOf(noLayout, *box).has_value());
    }

    TEST(UIPreviewOverlayTest, TheAnchorsOfTheChildrenOfAStackRootAreNone) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.root->layout = UILayoutKind::Stack;
        auto child = AddBox(*canvas.root, "Child", 0.0f, 0.0f, 50.0f, 50.0f);
        canvas.Update(Output(400.0f, 600.0f));

        EXPECT_FALSE(UIPreviewOverlay::AnchorsOf(canvas, *child).has_value());
    }

    TEST(UIPreviewOverlayTest, TheDesignRectIsTheReferenceSizeCenteredInTheBounds) {
        UICanvasLayout canvas = MakeCanvas(CanvasScaleMode::ScaleWithScreen);
        canvas.params.referenceWidth = 1280.0f;
        canvas.params.referenceHeight = 720.0f;
        canvas.params.minScale = 0.0f;
        canvas.params.maxScale = 0.0f;
        for (const float match : {0.0f, 1.0f}) {
            canvas.params.match = match;
            canvas.Update(Output(390.0f, 844.0f));
            const Rect bounds = UIPreviewOverlay::CanvasBounds(canvas.GetResolved());

            const auto design = UIPreviewOverlay::DesignRect(canvas);

            ASSERT_TRUE(design.has_value());
            const float scale = canvas.GetResolved().scale;
            EXPECT_NEAR(design->w, 1280.0f * scale, kTolerance);
            EXPECT_NEAR(design->h, 720.0f * scale, kTolerance);
            EXPECT_NEAR(design->x + design->w * 0.5f, bounds.x + bounds.w * 0.5f, kTolerance);
            EXPECT_NEAR(design->y + design->h * 0.5f, bounds.y + bounds.h * 0.5f, kTolerance);
            if (match == 0.0f) {
                EXPECT_NEAR(design->w, bounds.w, kTolerance);
            } else {
                EXPECT_NEAR(design->h, bounds.h, kTolerance);
            }
        }
    }

    TEST(UIPreviewOverlayTest, TheDesignRectIsNoneForOtherScaleModesSpacesAndACanvasWithNoLayout) {
        UICanvasLayout constant = MakeCanvas(CanvasScaleMode::ConstantPixelSize);
        constant.Update(Output(400.0f, 600.0f));
        EXPECT_FALSE(UIPreviewOverlay::DesignRect(constant).has_value());

        UICanvasLayout physical = MakeCanvas(CanvasScaleMode::ConstantPhysicalSize);
        physical.Update(Output(400.0f, 600.0f));
        EXPECT_FALSE(UIPreviewOverlay::DesignRect(physical).has_value());

        UICanvasLayout sceneView = MakeCanvas(CanvasScaleMode::ScaleWithScreen);
        sceneView.params.space = CanvasSpace::SceneView;
        UILayoutContext context = Output(400.0f, 600.0f);
        context.sceneViewWidth = 400.0f;
        context.sceneViewHeight = 300.0f;
        sceneView.Update(context);
        EXPECT_FALSE(UIPreviewOverlay::DesignRect(sceneView).has_value());

        UICanvasLayout noLayout = MakeCanvas(CanvasScaleMode::ScaleWithScreen);
        EXPECT_FALSE(UIPreviewOverlay::DesignRect(noLayout).has_value());
    }

    TEST(UIPreviewOverlayTest, FindingMarksAreTheIssuesOfTheValidatorOneForOneThroughTheTransform) {
        FindingsFixture fixture;
        for (const CanvasScaleMode mode : {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen}) {
            fixture.canvas.params.scaleMode = mode;
            fixture.canvas.Update(fixture.context);
            const auto issues = fixture.canvas.Validate({});
            ASSERT_GE(issues.size(), 5u);

            const auto marks = UIPreviewOverlay::FindingMarks(fixture.canvas, issues);

            ASSERT_EQ(marks.size(), issues.size());
            for (size_t i = 0; i < issues.size(); ++i) {
                EXPECT_EQ(marks[i].kind, issues[i].kind);
                EXPECT_EQ(marks[i].element, issues[i].element);
                EXPECT_EQ(marks[i].other, issues[i].other);
                ExpectRect(marks[i].rect, ToOutput(fixture.canvas, issues[i].rect));
                ASSERT_NE(marks[i].source, nullptr);
                EXPECT_EQ(marks[i].source, issues[i].source);
                EXPECT_EQ(marks[i].source, fixture.canvas.root->FindByName(marks[i].element));
            }
        }
    }

    TEST(UIPreviewOverlayTest, AnOutsideCanvasFindingIsNotClippedToTheOutput) {
        FindingsFixture fixture;
        fixture.canvas.Update(fixture.context);
        const auto issues = fixture.canvas.Validate({});

        const auto marks = UIPreviewOverlay::FindingMarks(fixture.canvas, issues);

        const auto out = std::find_if(marks.begin(), marks.end(), [](const FindingMark& mark) { return mark.kind == UILayoutIssueKind::OutsideCanvas; });
        ASSERT_NE(out, marks.end());
        ExpectRect(out->rect, {950.0f, 140.0f, 100.0f, 50.0f});  // 40 below the top: the content starts under the notch
    }

    TEST(UIPreviewOverlayTest, FindingTextNamesTheKindTheElementTheOtherAndTheSize) {
        UILayoutIssue issue;
        issue.kind = UILayoutIssueKind::TouchTargetTooSmall;
        issue.element = "Button"_intern;
        issue.rect = {0.0f, 0.0f, 48.0f, 24.5f};
        String text = "left over";

        UIPreviewOverlay::FindingText(issue, text);
        EXPECT_EQ(text, "TouchTargetTooSmall: Button (48 x 24.5)");

        issue.kind = UILayoutIssueKind::OutsideRegion;
        issue.other = "table.plate"_intern;
        UIPreviewOverlay::FindingText(issue, text);
        EXPECT_EQ(text, "OutsideRegion: Button / table.plate (48 x 24.5)");
    }

    TEST(UIPreviewOverlayTest, TheFindingsCacheValidatesOnceUntilTheLayoutOrTheMinimumChanges) {
        FindingsFixture fixture;
        UIFindingsCache cache;
        EXPECT_TRUE(cache.Get(fixture.canvas, 32.0f).empty());  // no layout yet
        EXPECT_EQ(cache.GetValidationRuns(), 0u);
        fixture.canvas.Update(fixture.context);

        const size_t count = cache.Get(fixture.canvas, 32.0f).size();
        EXPECT_GE(count, 6u);
        EXPECT_EQ(cache.GetValidationRuns(), 1u);
        EXPECT_EQ(cache.Get(fixture.canvas, 32.0f).size(), count);
        fixture.canvas.Update(fixture.context);  // nothing changed: no layout
        EXPECT_EQ(cache.Get(fixture.canvas, 32.0f).size(), count);
        EXPECT_EQ(cache.GetValidationRuns(), 1u);

        fixture.canvas.Invalidate();
        fixture.canvas.Update(fixture.context);
        EXPECT_EQ(cache.Get(fixture.canvas, 32.0f).size(), count);
        EXPECT_EQ(cache.GetValidationRuns(), 2u);

        UILayoutContext other = fixture.context;
        other.outputHeight = 600.0f;
        fixture.canvas.Update(other);
        cache.Get(fixture.canvas, 32.0f);
        EXPECT_EQ(cache.GetValidationRuns(), 3u);

        cache.Get(fixture.canvas, 1.0f);  // a smaller minimum: the small button is fine now
        EXPECT_EQ(cache.GetValidationRuns(), 4u);
        EXPECT_EQ(cache.Get(fixture.canvas, 1.0f).size(), count - 1);
    }

    TEST(UIPreviewOverlayTest, TheFindingsCacheValidatesAgainForAnotherCanvasAndAfterAReset) {
        FindingsFixture first;
        FindingsFixture second;
        first.canvas.Update(first.context);
        second.canvas.Update(second.context);
        UIFindingsCache cache;

        cache.Get(first.canvas, 32.0f);
        cache.Get(second.canvas, 32.0f);
        EXPECT_EQ(cache.GetValidationRuns(), 2u);
        const auto& issues = cache.Get(second.canvas, 32.0f);
        EXPECT_EQ(cache.GetValidationRuns(), 2u);
        for (const UILayoutIssue& issue : issues) {
            EXPECT_EQ(issue.source, second.canvas.root->FindByName(issue.element));
        }

        cache.Reset();
        cache.Get(second.canvas, 32.0f);
        EXPECT_EQ(cache.GetValidationRuns(), 3u);
    }

}  // namespace BECore::Tests
