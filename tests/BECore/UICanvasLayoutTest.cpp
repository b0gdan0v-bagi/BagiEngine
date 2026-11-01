#include <BECore/UI/UICanvasLayout.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
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

        IntrusivePtr<UIElement> AddBox(UIElement& parent, const char* name, float x, float y, float width, float height) {
            auto box = parent.AddChild(PoolString::Intern(name));
            box->style.rect.x = x;
            box->style.rect.y = y;
            box->style.rect.width = UILength::Px(width);
            box->style.rect.height = UILength::Px(height);
            return box;
        }

        /// The HUD of the plan: a panel top left with a score and a Reset button. The button is 48 units high and the canvas never scales below 0.7, so on
        /// a near-square window (where ScaleWithScreen goes down to 0.5) it is still the 32 window units that the validator asks for.
        void BuildHud(UICanvasLayout& canvas) {
            canvas.params.useSafeArea = true;
            canvas.params.minScale = 0.7f;
            auto hud = AddBox(*canvas.root, "Hud", 16.0f, 16.0f, 220.0f, 96.0f);
            hud->blocksInput = true;
            hud->color = Color{0, 0, 0, 160};
            AddBox(*hud, "Score", 12.0f, 10.0f, 120.0f, 28.0f);
            auto reset = AddBox(*hud, "Reset", -12.0f, -10.0f, 96.0f, 48.0f);
            reset->style.rect.anchor = UIAnchor::BottomRight;
            reset->interactive = true;
        }

    }  // namespace

    // ---- the cache --------------------------------------------------------------------------------------------------------

    TEST(UICanvasLayoutTest, TheFirstUpdateLaysOutAndAnUnchangedContextDoesNothing) {
        UICanvasLayout canvas;
        BuildHud(canvas);

        EXPECT_FALSE(canvas.HasLayout());
        EXPECT_TRUE(canvas.Update(Output(1280.0f, 720.0f)));
        EXPECT_TRUE(canvas.HasLayout());
        EXPECT_FALSE(canvas.Update(Output(1280.0f, 720.0f)));
        EXPECT_FALSE(canvas.Update(Output(1280.0f, 720.0f)));

        EXPECT_EQ(canvas.GetLayoutRuns(), 1u);
        EXPECT_EQ(canvas.GetVariantRuns(), 1u);
    }

    TEST(UICanvasLayoutTest, AChangedContextOrChangedParamsLayOutAgain) {
        UICanvasLayout canvas;
        BuildHud(canvas);
        canvas.Update(Output(1280.0f, 720.0f));

        EXPECT_TRUE(canvas.Update(Output(1920.0f, 1080.0f)));
        canvas.params.match = 0.0f;
        EXPECT_TRUE(canvas.Update(Output(1920.0f, 1080.0f)));
        EXPECT_FALSE(canvas.Update(Output(1920.0f, 1080.0f)));
        EXPECT_EQ(canvas.GetLayoutRuns(), 3u);
    }

    TEST(UICanvasLayoutTest, VariantsAreChosenAgainOnlyWhenTheBoundsChange) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        BuildHud(canvas);
        canvas.Update(Output(1000.0f, 500.0f));
        ASSERT_EQ(canvas.GetVariantRuns(), 1u);

        // A new safe area moves the content but not the bounds: the tree is laid out again, the variants are not chosen again
        UILayoutContext notch = Output(1000.0f, 500.0f);
        notch.safeArea = {0.0f, 40.0f, 1000.0f, 460.0f};
        EXPECT_TRUE(canvas.Update(notch));
        EXPECT_EQ(canvas.GetLayoutRuns(), 2u);
        EXPECT_EQ(canvas.GetVariantRuns(), 1u);

        // The user scale changes the scale and so the bounds
        UILayoutContext bigger = notch;
        bigger.userScale = 1.5f;
        canvas.Update(bigger);
        EXPECT_EQ(canvas.GetVariantRuns(), 2u);

        // A new output size with the same bounds (the scale follows it) does not choose again either
        UILayoutContext sameBounds = bigger;
        canvas.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
        canvas.params.minScale = 0.0f;
        canvas.params.maxScale = 0.0f;
        sameBounds.userScale = 1.0f;
        sameBounds.outputWidth = 1280.0f;  // ScaleWithScreen at the reference size: bounds 1280 x 720
        sameBounds.outputHeight = 720.0f;
        canvas.Update(sameBounds);
        const uint32_t runsBefore = canvas.GetVariantRuns();
        sameBounds.outputWidth = 2560.0f;
        sameBounds.outputHeight = 1440.0f;
        canvas.Update(sameBounds);
        EXPECT_EQ(canvas.GetVariantRuns(), runsBefore);
        ExpectRect(canvas.GetResolved().bounds, 0.0f, 0.0f, 1280.0f, 720.0f);
    }

    TEST(UICanvasLayoutTest, InvalidateMakesTheNextUpdateChooseAndLayOutAgain) {
        UICanvasLayout canvas;
        BuildHud(canvas);
        canvas.Update(Output(1280.0f, 720.0f));

        auto late = AddBox(*canvas.root, "Late", 300.0f, 300.0f, 50.0f, 50.0f);
        EXPECT_FALSE(canvas.Update(Output(1280.0f, 720.0f)));  // an edit is not seen by itself
        canvas.Invalidate();
        EXPECT_TRUE(canvas.Update(Output(1280.0f, 720.0f)));

        ExpectRect(late->rect, 300.0f, 300.0f, 50.0f, 50.0f);
        EXPECT_EQ(canvas.GetVariantRuns(), 2u);
        EXPECT_EQ(canvas.GetLayoutRuns(), 2u);
    }

    // ---- the pipeline -----------------------------------------------------------------------------------------------------

    TEST(UICanvasLayoutTest, TheContentIsLaidOutInTheSafeBoundsWhenTheCanvasUsesThem) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        canvas.params.useSafeArea = true;
        auto corner = AddBox(*canvas.root, "Corner", 0.0f, 0.0f, 10.0f, 10.0f);
        UILayoutContext context = Output(1000.0f, 500.0f);
        context.safeArea = {50.0f, 40.0f, 900.0f, 420.0f};

        canvas.Update(context);

        ExpectRect(canvas.root->rect, 50.0f, 40.0f, 900.0f, 420.0f);
        ExpectRect(corner->rect, 50.0f, 40.0f, 10.0f, 10.0f);
        ExpectRect(canvas.GetResolved().bounds, 0.0f, 0.0f, 1000.0f, 500.0f);
    }

    TEST(UICanvasLayoutTest, APortraitVariantShapesTheScreenForATallCanvas) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
        BuildHud(canvas);
        UIVariant portrait;
        portrait.when.orientation = UIOrientation::Portrait;
        UIElementOverride hud;
        hud.element = "Hud"_intern;
        hud.anchor = UIAnchor::Top;
        hud.x = 0.0f;
        hud.width = UILength::Percent(90.0f);
        portrait.overrides.push_back(hud);
        canvas.variants.push_back(portrait);

        canvas.Update(Output(1280.0f, 720.0f));
        ExpectRect(canvas.root->FindByName("Hud"_intern)->rect, 16.0f, 16.0f, 220.0f, 96.0f);

        canvas.Update(Output(720.0f, 1280.0f));
        ExpectRect(canvas.root->FindByName("Hud"_intern)->rect, 36.0f, 16.0f, 648.0f, 96.0f);

        canvas.Update(Output(1280.0f, 720.0f));  // and back
        ExpectRect(canvas.root->FindByName("Hud"_intern)->rect, 16.0f, 16.0f, 220.0f, 96.0f);
    }

    TEST(UICanvasLayoutTest, OutputPixelsMapToCanvasUnitsThroughTheLastLayout) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
        canvas.Update(Output(2560.0f, 1440.0f));  // scale 2

        const auto [x, y] = canvas.OutputToCanvas(400.0f, 100.0f);

        EXPECT_NEAR(x, 200.0f, kTolerance);
        EXPECT_NEAR(y, 50.0f, kTolerance);
    }

    // ---- hit testing ------------------------------------------------------------------------------------------------------

    TEST(UICanvasLayoutTest, ABeforeTheFirstUpdateNothingIsHit) {
        UICanvasLayout canvas;
        BuildHud(canvas);

        EXPECT_EQ(canvas.HitTest(20.0f, 20.0f), nullptr);
    }

    TEST(UICanvasLayoutTest, TheTopmostHitTestableElementIsFound) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        auto below = AddBox(*canvas.root, "Below", 100.0f, 100.0f, 200.0f, 200.0f);
        below->blocksInput = true;
        auto above = AddBox(*canvas.root, "Above", 150.0f, 150.0f, 200.0f, 200.0f);  // a later sibling is drawn on top
        above->blocksInput = true;
        auto decoration = AddBox(*canvas.root, "Decoration", 100.0f, 100.0f, 400.0f, 400.0f);  // not hit-testable: clicks go through
        auto child = AddBox(*below, "Child", 10.0f, 10.0f, 50.0f, 50.0f);                      // a child is drawn over its parent
        child->interactive = true;
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_EQ(canvas.HitTest(120.0f, 120.0f), child.Get());
        EXPECT_EQ(canvas.HitTest(200.0f, 200.0f), above.Get());  // both rects, the later wins
        EXPECT_EQ(canvas.HitTest(110.0f, 280.0f), below.Get());
        EXPECT_EQ(canvas.HitTest(400.0f, 400.0f), nullptr);  // only the decoration is there
        EXPECT_EQ(canvas.HitTest(50.0f, 50.0f), nullptr);
        EXPECT_NE(canvas.HitTest(100.0f, 100.0f), decoration.Get());
    }

    TEST(UICanvasLayoutTest, TheEdgesBelongToTheElementOnTheirLeftAndTopOnly) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        auto box = AddBox(*canvas.root, "Box", 100.0f, 100.0f, 50.0f, 50.0f);
        box->blocksInput = true;
        canvas.Update(Output(1000.0f, 500.0f));

        EXPECT_EQ(canvas.HitTest(100.0f, 100.0f), box.Get());
        EXPECT_EQ(canvas.HitTest(149.9f, 149.9f), box.Get());
        EXPECT_EQ(canvas.HitTest(150.0f, 120.0f), nullptr);
        EXPECT_EQ(canvas.HitTest(99.9f, 120.0f), nullptr);
    }

    TEST(UICanvasLayoutTest, HiddenElementsAreNotHit) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        auto panel = AddBox(*canvas.root, "Panel", 0.0f, 0.0f, 300.0f, 300.0f);
        panel->blocksInput = true;
        auto button = AddBox(*panel, "Button", 10.0f, 10.0f, 100.0f, 50.0f);
        button->interactive = true;
        canvas.Update(Output(1000.0f, 500.0f));
        ASSERT_EQ(canvas.HitTest(20.0f, 20.0f), button.Get());

        button->style.visible = false;
        canvas.Invalidate();
        canvas.Update(Output(1000.0f, 500.0f));
        EXPECT_EQ(canvas.HitTest(20.0f, 20.0f), panel.Get());  // the panel under it still blocks

        panel->style.visible = false;
        canvas.Invalidate();
        canvas.Update(Output(1000.0f, 500.0f));
        EXPECT_EQ(canvas.HitTest(20.0f, 20.0f), nullptr);
    }

    // ---- the size matrix --------------------------------------------------------------------------------------------------

    TEST(UILayoutMatrixTest, TheMatrixCoversEveryAspectSizeAndSafeAreaOfThePlan) {
        const auto& cases = UILayoutMatrix();

        for (const char* aspect : {"16:9 ", "16:10 ", "3:2 ", "4:3 ", "1:1 ", "21:9 ", "32:9 ", "9:16 ", "9:19.5 ", "3:4 "}) {
            for (const char* safe : {"no insets", "notch", "side insets"}) {
                const bool found = std::any_of(cases.begin(), cases.end(), [&](const UIMatrixCase& c) { return c.label.find(aspect) == 0 && c.label.find(safe) != eastl::string::npos; });
                EXPECT_TRUE(found) << aspect << " " << safe;
            }
        }

        float smallestShortSide = 1e9f;
        float largestLongSide = 0.0f;
        bool hasFourK = false;
        for (const UIMatrixCase& c : cases) {
            smallestShortSide = std::min({smallestShortSide, c.context.outputWidth, c.context.outputHeight});
            largestLongSide = std::max({largestLongSide, c.context.outputWidth, c.context.outputHeight});
            hasFourK |= c.context.outputWidth == 3840.0f && c.context.outputHeight == 2160.0f;
            EXPECT_GT(c.context.safeArea.w, 0.0f) << c.label.c_str();
        }
        EXPECT_FLOAT_EQ(smallestShortSide, 480.0f);
        EXPECT_FLOAT_EQ(largestLongSide, 3840.0f);
        EXPECT_TRUE(hasFourK);
    }

    TEST(UILayoutMatrixTest, EveryLabelIsUnique) {
        const auto& cases = UILayoutMatrix();
        for (size_t i = 0; i < cases.size(); ++i) {
            for (size_t j = i + 1; j < cases.size(); ++j) {
                ASSERT_NE(cases[i].label, cases[j].label);
            }
        }
    }

    TEST(UILayoutMatrixTest, ASafeAreaStaysInsideTheOutput) {
        for (const UIMatrixCase& c : UILayoutMatrix()) {
            const Rect& safe = c.context.safeArea;
            EXPECT_GE(safe.x, 0.0f) << c.label.c_str();
            EXPECT_GE(safe.y, 0.0f) << c.label.c_str();
            EXPECT_LE(safe.x + safe.w, c.context.outputWidth + kTolerance) << c.label.c_str();
            EXPECT_LE(safe.y + safe.h, c.context.outputHeight + kTolerance) << c.label.c_str();
        }
    }

    TEST(UILayoutMatrixTest, TheShippedStyleOfHudPassesEveryCase) {
        UICanvasLayout canvas;
        BuildHud(canvas);

        const auto failures = ValidateOnMatrix(canvas);

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST(UILayoutMatrixTest, AScreenThatDoesNotAdaptIsCaughtOnTheSmallAndTallCases) {
        UICanvasLayout canvas;
        auto banner = AddBox(*canvas.root, "Banner", 0.0f, 0.0f, 1000.0f, 80.0f);  // a fixed 1000 units wide: too wide for any phone-like canvas at scale 1
        banner->blocksInput = true;
        auto tiny = AddBox(*canvas.root, "Tiny", 20.0f, 100.0f, 20.0f, 20.0f);
        tiny->interactive = true;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;

        const auto failures = ValidateOnMatrix(canvas);

        ASSERT_FALSE(failures.empty());
        bool bannerCaught = false;
        bool tinyCaught = false;
        for (const UIMatrixFailure& failure : failures) {
            for (const UILayoutIssue& issue : failure.issues) {
                bannerCaught |= issue.kind == UILayoutIssueKind::OutsideCanvas && issue.element == "Banner"_intern;
                tinyCaught |= issue.kind == UILayoutIssueKind::TouchTargetTooSmall && issue.element == "Tiny"_intern;
            }
        }
        EXPECT_TRUE(bannerCaught);
        EXPECT_TRUE(tinyCaught);
        EXPECT_EQ(canvas.params.scaleMode, CanvasScaleMode::ConstantPixelSize);  // the helper gives the params back
    }

    TEST(UILayoutMatrixTest, AVariantThatNarrowsThePanelMakesTheScreenPass) {
        auto build = [](bool withVariant) {
            auto canvas = eastl::make_unique<UICanvasLayout>();
            auto panel = AddBox(*canvas->root, "Panel", 10.0f, 10.0f, 700.0f, 100.0f);
            panel->blocksInput = true;
            if (withVariant) {
                UIVariant narrow;
                narrow.when.maxWidth = 800.0f;
                UIElementOverride shrink;
                shrink.element = "Panel"_intern;
                shrink.width = UILength::Percent(90.0f);
                narrow.overrides.push_back(shrink);
                canvas->variants.push_back(narrow);
            }
            return canvas;
        };

        auto without = build(false);
        auto with = build(true);

        EXPECT_FALSE(ValidateOnMatrix(*without).empty());
        const auto failures = ValidateOnMatrix(*with);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST(UILayoutMatrixTest, ASceneViewCanvasRunsOncePerCaseBecauseTheModeAndUserScaleDoNotApply) {
        UICanvasLayout canvas;
        canvas.params.space = CanvasSpace::SceneView;
        UILayoutContext base;
        base.sceneViewWidth = 840.0f;
        base.sceneViewHeight = 760.0f;
        AddBox(*canvas.root, "Label", 100.0f, 100.0f, 200.0f, 40.0f);

        const auto failures = ValidateOnMatrix(canvas, {}, base);

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        EXPECT_EQ(canvas.GetLayoutRuns(), static_cast<uint32_t>(UILayoutMatrix().size()));
    }

}  // namespace BECore::Tests
