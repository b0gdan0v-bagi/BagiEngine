#include <BECore/UI/UICanvasLayout.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// An element that reports what its content needs, the way a label will once it measures its text
        class ContentElement : public UIElement {
        public:
            UIContentFit CheckContent(float /*width*/, float /*height*/, float /*scale*/) const override {
                return fit;
            }

            UIContentFit fit;
        };

        UILayoutContext Output(float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            return context;
        }

        /// A canvas of 1000 x 500 units at scale 1, so the rects of the elements are the rects the test wrote down
        UICanvasLayout MakeCanvas() {
            UICanvasLayout canvas;
            canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
            return canvas;
        }

        IntrusivePtr<UIElement> AddBox(UIElement& parent, const char* name, float x, float y, float width, float height) {
            auto box = parent.AddChild(PoolString::Intern(name));
            box->style.rect.x = x;
            box->style.rect.y = y;
            box->style.rect.width = UILength::Px(width);
            box->style.rect.height = UILength::Px(height);
            return box;
        }

        size_t CountOf(const eastl::vector<UILayoutIssue>& issues, UILayoutIssueKind kind) {
            return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [kind](const UILayoutIssue& issue) { return issue.kind == kind; }));
        }

        /// The tall output of the bar tests: 400 x 600 around a 400 x 300 view, so bars of 150 above and below (scene y -150..0 and 300..450)
        UILayoutContext TallOutputWithBars() {
            UILayoutContext context = Output(400.0f, 600.0f);
            context.sceneViewWidth = 400.0f;
            context.sceneViewHeight = 300.0f;
            return context;
        }

        UICanvasLayout MakeExtendedCanvas() {
            UICanvasLayout canvas;
            canvas.params.space = CanvasSpace::SceneView;
            canvas.params.extendToOutput = true;
            return canvas;
        }

        /// A 200 x 60 row at the bottom of the canvas, @p lift units above its edge (the bar below the view is 150 high)
        IntrusivePtr<UIElement> AddBottomRow(UIElement& parent, const char* name, float lift) {
            auto row = AddBox(parent, name, 0.0f, -lift, 200.0f, 60.0f);
            row->style.rect.anchor = UIAnchor::Bottom;
            return row;
        }

        UILayoutValidatorOptions KeepOff(const char* name) {
            UILayoutValidatorOptions options;
            options.keepOffView.push_back(PoolString::Intern(name));
            return options;
        }

    }  // namespace

    TEST(UILayoutValidatorTest, ACleanLayoutHasNoIssues) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "A", 10.0f, 10.0f, 100.0f, 50.0f);
        auto button = AddBox(*canvas.root, "B", 200.0f, 10.0f, 100.0f, 50.0f);
        button->interactive = true;
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_TRUE(canvas.Validate({}).empty());
    }

    TEST(UILayoutValidatorTest, AnElementOutsideTheCanvasIsReportedWithItsNameAndRect) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Inside", 0.0f, 0.0f, 1000.0f, 500.0f);  // exactly the canvas
        AddBox(*canvas.root, "TooWide", 950.0f, 10.0f, 100.0f, 50.0f);
        AddBox(*canvas.root, "AboveTheTop", 10.0f, -5.0f, 50.0f, 50.0f);
        canvas.Update(Output(1000.0f, 500.0f));

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 2u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OutsideCanvas);
        EXPECT_EQ(issues[0].element, "TooWide"_intern);
        EXPECT_FLOAT_EQ(issues[0].rect.w, 100.0f);
        EXPECT_EQ(issues[1].element, "AboveTheTop"_intern);
    }

    TEST(UILayoutValidatorTest, ARoundingErrorIsNotAnIssue) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Edge", 900.0f, 0.0f, 100.004f, 50.0f);
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_TRUE(canvas.Validate({}).empty());
    }

    TEST(UILayoutValidatorTest, ChildrenAreCheckedAgainstTheCanvasNotOnlyTheirParent) {
        UICanvasLayout canvas = MakeCanvas();
        auto panel = AddBox(*canvas.root, "Panel", 800.0f, 0.0f, 150.0f, 100.0f);
        AddBox(*panel, "Deep", 100.0f, 10.0f, 100.0f, 20.0f);    // x 900..1000 is inside; this one:
        AddBox(*panel, "Deeper", 120.0f, 40.0f, 100.0f, 20.0f);  // 920..1020 sticks out of the canvas
        canvas.Update(Output(1000.0f, 500.0f));

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].element, "Deeper"_intern);
    }

    TEST(UILayoutValidatorTest, TheSafeAreaIsCheckedOnlyWhenTheCanvasUsesIt) {
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {0.0f, 40.0f, 1000.0f, 460.0f};  // a notch of 40 at the top

        UICanvasLayout free = MakeCanvas();
        AddBox(*free.root, "Header", 0.0f, 0.0f, 1000.0f, 30.0f);
        free.Update(context);
        EXPECT_TRUE(free.Validate({}).empty());

        UICanvasLayout safe = MakeCanvas();
        safe.params.useSafeArea = true;
        AddBox(*safe.root, "Header", 0.0f, -20.0f, 1000.0f, 30.0f);  // 20 units above the content: in the notch
        safe.Update(context);
        const auto issues = safe.Validate({});
        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OutsideSafeArea);
    }

    TEST(UILayoutValidatorTest, ABackgroundThatIgnoresTheSafeAreaMayFillTheWholeCanvas) {
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {50.0f, 40.0f, 900.0f, 420.0f};
        UICanvasLayout canvas = MakeCanvas();
        canvas.params.useSafeArea = true;
        auto background = canvas.root->AddChild("Background"_intern);
        background->style.rect.anchor = UIAnchor::Stretch;
        background->ignoreSafeArea = true;
        auto content = canvas.root->AddChild("Content"_intern);
        content->style.rect.anchor = UIAnchor::Stretch;
        canvas.Update(context);

        EXPECT_TRUE(canvas.Validate({}).empty());

        background->ignoreSafeArea = false;  // without the flag the same rect leaves the safe area
        canvas.Invalidate();
        canvas.Update(context);
        // ...but a Stretch element is laid out in the content bounds, so it is inside them again
        EXPECT_TRUE(canvas.Validate({}).empty());
    }

    TEST(UILayoutValidatorTest, IgnoreSafeAreaCoversTheDescendantsOfTheElement) {
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {50.0f, 40.0f, 900.0f, 420.0f};  // a notch of 40 at the top, 50 at the sides
        UICanvasLayout canvas = MakeCanvas();
        canvas.params.useSafeArea = true;
        auto bar = AddBox(*canvas.root, "Bar", 0.0f, 0.0f, 1000.0f, 100.0f);  // from the corner of the whole canvas
        bar->ignoreSafeArea = true;
        auto child = AddBox(*bar, "Child", 10.0f, 5.0f, 300.0f, 30.0f);  // y 5..35: in the notch
        AddBox(*child, "Grandchild", 5.0f, 5.0f, 100.0f, 20.0f);
        auto row = AddBox(*bar, "Row", 400.0f, 5.0f, 300.0f, 30.0f);
        row->layout = UILayoutKind::Stack;
        row->style.stack.direction = UIStackDirection::Horizontal;
        AddBox(*row, "Cell1", 0.0f, 0.0f, 100.0f, 30.0f);
        AddBox(*row, "Cell2", 0.0f, 0.0f, 100.0f, 30.0f);
        canvas.Update(context);

        EXPECT_TRUE(canvas.Validate({}).empty());

        // The same tree without the flag, put on the same absolute place (offsets count from the safe area then): the safe area applies to the root child and everything under it
        bar->ignoreSafeArea = false;
        bar->style.rect.x = -50.0f;
        bar->style.rect.y = -40.0f;
        canvas.Invalidate();
        canvas.Update(context);
        const auto issues = canvas.Validate({});
        EXPECT_EQ(CountOf(issues, UILayoutIssueKind::OutsideSafeArea), 6u);
    }

    TEST(UILayoutValidatorTest, ADescendantOfAnIgnoringElementIsStillCheckedAgainstTheCanvas) {
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {50.0f, 40.0f, 900.0f, 420.0f};
        UICanvasLayout canvas = MakeCanvas();
        canvas.params.useSafeArea = true;
        auto bar = AddBox(*canvas.root, "Bar", 0.0f, 0.0f, 1000.0f, 100.0f);
        bar->ignoreSafeArea = true;
        auto child = AddBox(*bar, "Child", 10.0f, 5.0f, 300.0f, 30.0f);
        AddBox(*child, "Grandchild", 5.0f, -20.0f, 100.0f, 20.0f);  // y -15..5: above the canvas
        canvas.Update(context);

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OutsideCanvas);
        EXPECT_EQ(issues[0].element, "Grandchild"_intern);
    }

    TEST(UILayoutValidatorTest, TheFlagOfADeeperElementMeansNothingAndItsSiblingsKeepTheSafeArea) {
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {50.0f, 40.0f, 900.0f, 420.0f};
        UICanvasLayout canvas = MakeCanvas();
        canvas.params.useSafeArea = true;
        auto panel = AddBox(*canvas.root, "Panel", 0.0f, 0.0f, 500.0f, 200.0f);  // from the corner of the safe area
        auto flagged = AddBox(*panel, "Flagged", 0.0f, -20.0f, 100.0f, 20.0f);   // y 20..40: leaves the safe area (40..)
        flagged->ignoreSafeArea = true;
        AddBox(*panel, "Plain", 200.0f, -20.0f, 100.0f, 20.0f);
        canvas.Update(context);

        const auto issues = canvas.Validate({});

        EXPECT_EQ(CountOf(issues, UILayoutIssueKind::OutsideSafeArea), 2u);
        EXPECT_EQ(CountOf(issues, UILayoutIssueKind::OutsideCanvas), 0u);
    }

    TEST(UILayoutValidatorTest, AnElementInTheBarOfAnExtendedCanvasIsInsideItButOnePastTheOutputIsNot) {
        UILayoutContext context = Output(400.0f, 600.0f);  // a 400 x 300 view: bars of 150 above and below
        context.sceneViewWidth = 400.0f;
        context.sceneViewHeight = 300.0f;
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        canvas.params.extendToOutput = true;
        AddBox(*canvas.root, "InTheBar", 10.0f, 10.0f, 100.0f, 40.0f);       // offsets are from the corner of the bounds: scene y -140, above the view
        AddBox(*canvas.root, "BelowTheView", 10.0f, 550.0f, 100.0f, 40.0f);  // scene y 400..440
        canvas.Update(context);
        EXPECT_TRUE(canvas.Validate({}).empty());

        AddBox(*canvas.root, "PastTheOutput", 10.0f, 580.0f, 100.0f, 40.0f);  // ends at scene y 470, the canvas at 450
        canvas.Invalidate();
        canvas.Update(context);
        const auto issues = canvas.Validate({});
        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OutsideCanvas);
        EXPECT_EQ(issues[0].element, "PastTheOutput"_intern);

        // The same element is outside a canvas that is not extended: its bounds are the view
        UICanvasLayout plain;
        plain.params.space = CanvasSpace::SceneView;
        AddBox(*plain.root, "BelowTheView", 10.0f, 400.0f, 100.0f, 40.0f);
        plain.Update(context);
        EXPECT_EQ(CountOf(plain.Validate({}), UILayoutIssueKind::OutsideCanvas), 1u);
    }

    TEST(UILayoutValidatorTest, TheSafeAreaOfAnExtendedCanvasStillReports) {
        UILayoutContext context = Output(400.0f, 600.0f);
        context.sceneViewWidth = 400.0f;
        context.sceneViewHeight = 300.0f;
        context.safeArea = {0.0f, 50.0f, 400.0f, 550.0f};  // a notch of 50 at the top of the output: scene y from -100
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        canvas.params.extendToOutput = true;
        canvas.params.useSafeArea = true;
        auto inNotch = AddBox(*canvas.root, "InNotch", 10.0f, -20.0f, 100.0f, 40.0f);  // 20 above the content, which starts at -100: scene y -120
        inNotch->style.rect.anchor = UIAnchor::TopLeft;
        auto fine = AddBox(*canvas.root, "Fine", 10.0f, 10.0f, 100.0f, 40.0f);
        fine->style.rect.anchor = UIAnchor::TopLeft;
        canvas.Update(context);

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OutsideSafeArea);
        EXPECT_EQ(issues[0].element, "InNotch"_intern);
    }

    TEST(UILayoutValidatorTest, ARowOnTheBarOfAnExtendedCanvasPassesAndOneReachingIntoTheViewIsReported) {
        UICanvasLayout canvas = MakeExtendedCanvas();
        AddBottomRow(*canvas.root, "Bar", 10.0f);  // scene y 380..440
        canvas.Update(TallOutputWithBars());
        EXPECT_TRUE(canvas.Validate(KeepOff("Bar")).empty());

        UICanvasLayout reaching = MakeExtendedCanvas();
        AddBottomRow(*reaching.root, "Bar", 100.0f);  // scene y 290..350: 10 units over the view, which ends at 300
        reaching.Update(TallOutputWithBars());
        const auto issues = reaching.Validate(KeepOff("Bar"));

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OverlapsSceneView);
        EXPECT_EQ(issues[0].element, "Bar"_intern);
        EXPECT_FLOAT_EQ(issues[0].rect.x, 100.0f);
        EXPECT_FLOAT_EQ(issues[0].rect.y, 290.0f);
        EXPECT_FLOAT_EQ(issues[0].rect.w, 200.0f);
        EXPECT_FLOAT_EQ(issues[0].rect.h, 60.0f);
    }

    TEST(UILayoutValidatorTest, ARowThatOnlyTouchesTheViewIsNotOverlappingIt) {
        UICanvasLayout canvas = MakeExtendedCanvas();
        AddBottomRow(*canvas.root, "Bar", 90.0f);  // scene y 300..360
        canvas.Update(TallOutputWithBars());

        EXPECT_TRUE(canvas.Validate(KeepOff("Bar")).empty());
    }

    TEST(UILayoutValidatorTest, ADescendantOfANamedRootChildIsFoundAndAnUnnamedSiblingIsNotChecked) {
        UICanvasLayout canvas = MakeExtendedCanvas();
        AddBox(*canvas.root, "Board", 0.0f, 150.0f, 400.0f, 300.0f);  // the view itself: not named, not checked
        auto bar = AddBottomRow(*canvas.root, "Bar", 0.0f);           // a plate filling the bar below the view: scene y 300..450
        bar->style.rect.height = UILength::Px(150.0f);
        bar->style.rect.width = UILength::Px(400.0f);
        auto reach = AddBox(*bar, "Reach", 10.0f, -30.0f, 50.0f, 20.0f);  // 30 above the plate: scene y 270..290, in the view
        AddBox(*reach, "Deeper", 0.0f, 0.0f, 10.0f, 10.0f);               // also in the view, but the first one on its path is the one reported
        canvas.Update(TallOutputWithBars());

        const auto issues = canvas.Validate(KeepOff("Bar"));

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OverlapsSceneView);
        EXPECT_EQ(issues[0].element, "Reach"_intern);
    }

    TEST(UILayoutValidatorTest, ARowHiddenByItsStyleIsNotReportedAsOverlappingTheView) {
        UICanvasLayout canvas = MakeExtendedCanvas();
        auto row = AddBottomRow(*canvas.root, "Bar", 100.0f);
        row->style.visible = false;
        canvas.Update(TallOutputWithBars());

        EXPECT_TRUE(canvas.Validate(KeepOff("Bar")).empty());
    }

    TEST(UILayoutValidatorTest, ARowOnACanvasWithNoBarOverlapsTheViewUnlessAVariantHidesIt) {
        const UILayoutContext landscape = [] {
            UILayoutContext context = Output(600.0f, 300.0f);  // the output is the view: no bar, `viewBounds` is `bounds`
            context.sceneViewWidth = 600.0f;
            context.sceneViewHeight = 300.0f;
            return context;
        }();
        UICanvasLayout canvas = MakeExtendedCanvas();
        AddBottomRow(*canvas.root, "Bar", 10.0f);
        canvas.Update(landscape);
        const auto issues = canvas.Validate(KeepOff("Bar"));
        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OverlapsSceneView);

        UIVariant noBar;
        noBar.when.orientation = UIOrientation::Landscape;
        UIElementOverride hide;
        hide.element = "Bar"_intern;
        hide.visible = false;
        noBar.overrides.push_back(hide);
        canvas.variants.push_back(noBar);
        canvas.Invalidate();
        canvas.Update(landscape);
        EXPECT_TRUE(canvas.Validate(KeepOff("Bar")).empty());
    }

    TEST(UILayoutValidatorTest, WithoutNamesInKeepOffViewNothingIsReportedForTheView) {
        UICanvasLayout canvas = MakeExtendedCanvas();
        AddBottomRow(*canvas.root, "Bar", 100.0f);
        canvas.Update(TallOutputWithBars());

        EXPECT_TRUE(canvas.Validate({}).empty());
        EXPECT_TRUE(canvas.Validate(KeepOff("Other")).empty()) << "a name that is not a child of the root keeps nothing off the view";
    }

    TEST(UILayoutValidatorTest, SiblingsOfAStackThatOverlapAreReported) {
        UICanvasLayout canvas = MakeCanvas();
        canvas.root->layout = UILayoutKind::Stack;
        canvas.root->style.stack.spacing = -10.0f;  // pulls the rows into each other
        AddBox(*canvas.root, "Row1", 0.0f, 0.0f, 100.0f, 40.0f);
        AddBox(*canvas.root, "Row2", 0.0f, 0.0f, 100.0f, 40.0f);
        AddBox(*canvas.root, "Row3", 0.0f, 0.0f, 100.0f, 40.0f);
        canvas.Update(Output(1000.0f, 500.0f));

        const auto issues = canvas.Validate({});

        ASSERT_EQ(CountOf(issues, UILayoutIssueKind::SiblingsOverlap), 2u);  // Row1/Row2 and Row2/Row3; Row1 and Row3 only touch
        EXPECT_EQ(issues[0].element, "Row1"_intern);
        EXPECT_EQ(issues[0].other, "Row2"_intern);
    }

    TEST(UILayoutValidatorTest, ARegularStackAndOverlappingAnchoredElementsAreFine) {
        UICanvasLayout stack = MakeCanvas();
        stack.root->layout = UILayoutKind::Stack;
        stack.root->style.stack.spacing = 8.0f;
        AddBox(*stack.root, "Row1", 0.0f, 0.0f, 100.0f, 40.0f);
        AddBox(*stack.root, "Row2", 0.0f, 0.0f, 100.0f, 40.0f);
        stack.Update(Output(1000.0f, 500.0f));
        EXPECT_TRUE(stack.Validate({}).empty());

        UICanvasLayout anchored = MakeCanvas();  // a label on top of a panel is how anchored layouts are used
        AddBox(*anchored.root, "Panel", 10.0f, 10.0f, 200.0f, 100.0f);
        AddBox(*anchored.root, "Label", 20.0f, 20.0f, 100.0f, 20.0f);
        anchored.Update(Output(1000.0f, 500.0f));
        EXPECT_TRUE(anchored.Validate({}).empty());
    }

    TEST(UILayoutValidatorTest, AnInteractiveElementSmallerThanTheMinimumTouchSizeIsReported) {
        UICanvasLayout canvas = MakeCanvas();
        auto small = AddBox(*canvas.root, "Small", 10.0f, 10.0f, 100.0f, 20.0f);
        small->interactive = true;
        auto ok = AddBox(*canvas.root, "Ok", 10.0f, 100.0f, 100.0f, 32.0f);
        ok->interactive = true;
        AddBox(*canvas.root, "Decoration", 10.0f, 200.0f, 5.0f, 5.0f);  // not interactive: any size
        canvas.Update(Output(1000.0f, 500.0f));

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::TouchTargetTooSmall);
        EXPECT_EQ(issues[0].element, "Small"_intern);
    }

    TEST(UILayoutValidatorTest, TheTouchSizeIsWhatTheUserSeesNotTheUnits) {
        UICanvasLayout canvas = MakeCanvas();
        auto button = AddBox(*canvas.root, "Button", 10.0f, 10.0f, 60.0f, 20.0f);
        button->interactive = true;
        UILayoutValidatorOptions options;
        options.minTouchSize = 32.0f;

        canvas.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
        canvas.Update(Output(2560.0f, 1440.0f));  // scale 2: 20 units are 40 px
        EXPECT_TRUE(canvas.Validate(options).empty());

        options.pixelDensity = 2.0f;  // ...but on a display with twice the pixels that is 20 window units again
        EXPECT_EQ(CountOf(canvas.Validate(options), UILayoutIssueKind::TouchTargetTooSmall), 1u);

        options.pixelDensity = 1.0f;
        canvas.Update(Output(640.0f, 360.0f));  // scale 0.5: 10 px
        EXPECT_EQ(CountOf(canvas.Validate(options), UILayoutIssueKind::TouchTargetTooSmall), 1u);
    }

    TEST(UILayoutValidatorTest, AContentThatDoesNotFitAndCannotShrinkIsReported) {
        UICanvasLayout canvas = MakeCanvas();
        auto stuck = New<ContentElement>();
        stuck->name = "Stuck"_intern;
        stuck->fit = {true, false, false};
        auto shrinks = New<ContentElement>();
        shrinks->name = "Shrinks"_intern;
        shrinks->fit = {true, false, true};
        auto fits = New<ContentElement>();
        fits->name = "Fits"_intern;
        fits->fit = {true, true, false};
        for (auto& element : {stuck, shrinks, fits}) {
            element->style.rect.width = UILength::Px(10.0f);
            element->style.rect.height = UILength::Px(10.0f);
            canvas.root->children.push_back(element);
        }
        canvas.Update(Output(1000.0f, 500.0f));

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::TextDoesNotFit);
        EXPECT_EQ(issues[0].element, "Stuck"_intern);
    }

    TEST(UILayoutValidatorTest, HiddenElementsAreSkippedWithTheirSubtree) {
        UICanvasLayout canvas = MakeCanvas();
        auto hidden = AddBox(*canvas.root, "Hidden", 5000.0f, 0.0f, 100.0f, 100.0f);
        hidden->style.visible = false;
        auto inner = AddBox(*hidden, "Inner", 0.0f, 0.0f, 5.0f, 5.0f);
        inner->interactive = true;
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_TRUE(canvas.Validate({}).empty());
    }

    TEST(UILayoutValidatorTest, AnEmptyElementCannotStickOut) {
        UICanvasLayout canvas = MakeCanvas();
        AddBox(*canvas.root, "Nothing", 5000.0f, 5000.0f, 0.0f, 0.0f);
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_TRUE(canvas.Validate({}).empty());
    }

    TEST(UILayoutValidatorTest, AVisibleInteractiveElementWithNoSizeIsATouchTargetThatIsTooSmall) {
        UICanvasLayout canvas = MakeCanvas();
        auto button = AddBox(*canvas.root, "Button", 10.0f, 10.0f, 0.0f, 0.0f);
        button->interactive = true;
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_EQ(CountOf(canvas.Validate({}), UILayoutIssueKind::TouchTargetTooSmall), 1u);
    }

    TEST(UILayoutValidatorTest, CompileTimeChecks) {}

}  // namespace BECore::Tests
