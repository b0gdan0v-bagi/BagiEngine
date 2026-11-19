#include <BECore/Scene/SceneView.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UILayoutHud.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;
        constexpr float kLandscapeWidth = 1280.0f;
        constexpr float kLandscapeHeight = 720.0f;

        /// A scene of 1280 x 720 with a 720 x 1280 view for the tall outputs
        Vector<SceneViewVariant> PortraitVariants() {
            SceneViewVariant portrait;
            portrait.name = "Portrait"_intern;
            portrait.when.orientation = UIOrientation::Portrait;
            portrait.width = kLandscapeHeight;
            portrait.height = kLandscapeWidth;
            return {portrait};
        }

        UIMatrixViews ViewsOf(const Vector<SceneViewVariant>& variants) {
            return {kLandscapeWidth, kLandscapeHeight, {variants.data(), variants.size()}};
        }

        /// A SceneView canvas with a band 1000 units wide (a layout made for the landscape view only)
        UICanvasLayout BandCanvas(bool extend) {
            UICanvasLayout canvas;
            canvas.params.space = CanvasSpace::SceneView;
            canvas.params.extendToOutput = extend;
            auto band = canvas.root->AddChild("Band"_intern);
            band->style.rect.x = UILength::Px(100.0f);
            band->style.rect.y = UILength::Px(300.0f);
            band->style.rect.width = UILength::Px(1000.0f);
            band->style.rect.height = UILength::Px(120.0f);
            return canvas;
        }

        /// The same canvas with the variant that narrows the band for the portrait view
        UICanvasLayout BandCanvasWithPortraitLayout(bool extend) {
            UICanvasLayout canvas = BandCanvas(extend);
            UIVariant portrait;
            portrait.when.orientation = UIOrientation::Portrait;
            UIElementOverride narrow;
            narrow.element = "Band"_intern;
            narrow.width = UILength::Px(500.0f);
            portrait.overrides.push_back(narrow);
            canvas.variants.push_back(portrait);
            return canvas;
        }

        /// An extended canvas with a bar row that only a tall output has room for: shown by a variant on very tall outputs (`maxAspect`: the view alone is 0.5625), hidden otherwise
        UICanvasLayout BarRowCanvas(bool hideWhereThereIsNoBar) {
            UICanvasLayout canvas = BandCanvasWithPortraitLayout(true);
            auto row = canvas.root->AddChild("Bar"_intern);
            row->style.rect.anchor = UIAnchor::Bottom;
            row->style.rect.y = UILength::Px(-20.0f);
            row->style.rect.width = UILength::Px(300.0f);
            row->style.rect.height = UILength::Px(100.0f);
            if (hideWhereThereIsNoBar) {
                row->style.visible = false;
                UIVariant tall;
                tall.when.maxAspect = 0.5f;
                UIElementOverride show;
                show.element = "Bar"_intern;
                show.visible = true;
                tall.overrides.push_back(show);
                canvas.variants.push_back(tall);
            }
            return canvas;
        }

        UILayoutContext BaseView() {
            UILayoutContext context;
            context.sceneViewWidth = kLandscapeWidth;
            context.sceneViewHeight = kLandscapeHeight;
            return context;
        }

        bool AnyLabelEndsWith(const Vector<UIMatrixFailure>& failures, const char* tail) {
            const StringView want(tail);
            for (const UIMatrixFailure& failure : failures) {
                const StringView label(failure.label.data(), failure.label.size());
                if (label.size() >= want.size() && label.substr(label.size() - want.size()) == want) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(UILayoutMatrixTest, ACanvasWithAPortraitLayoutPassesWithTheViewVariants) {
        const auto variants = PortraitVariants();
        UICanvasLayout canvas = BandCanvasWithPortraitLayout(false);

        const auto failures = ValidateOnMatrix(canvas, {}, BaseView(), ViewsOf(variants));

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST(UILayoutMatrixTest, ACanvasLaidOutOnlyForTheLandscapeViewFailsOnAPortraitOutputAndTheLabelNamesTheView) {
        const auto variants = PortraitVariants();
        UICanvasLayout canvas = BandCanvas(false);

        const auto failures = ValidateOnMatrix(canvas, {}, BaseView(), ViewsOf(variants));

        ASSERT_FALSE(failures.empty());
        EXPECT_TRUE(AnyLabelEndsWith(failures, "view Portrait"));
        EXPECT_FALSE(AnyLabelEndsWith(failures, "view default")) << "the landscape view holds the band";
        for (const UIMatrixFailure& failure : failures) {
            EXPECT_NE(failure.label.find("view Portrait"), String::npos) << failure.label.c_str();
            ASSERT_FALSE(failure.issues.empty());
            EXPECT_EQ(failure.issues[0].kind, UILayoutIssueKind::OutsideCanvas);
        }
    }

    TEST(UILayoutMatrixTest, WithoutAViewDeclarationTheMatrixKeepsTheViewOfTheBaseContext) {
        UICanvasLayout canvas = BandCanvas(false);

        const auto failures = ValidateOnMatrix(canvas, {}, BaseView());

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        // And a declaration with no variants is the default view in every case
        const Vector<SceneViewVariant> none;
        EXPECT_TRUE(ValidateOnMatrix(canvas, {}, {}, ViewsOf(none)).empty());
    }

    TEST(UILayoutMatrixTest, AnExtendedCanvasRunsOnTheMatrixWithTheVariantsToo) {
        const auto variants = PortraitVariants();
        UICanvasLayout good = BandCanvasWithPortraitLayout(true);
        // A row on the bar below the view: anchored to the bottom of the whole output
        auto row = good.root->AddChild("Row"_intern);
        row->style.rect.anchor = UIAnchor::Bottom;
        row->style.rect.y = UILength::Px(-20.0f);
        row->style.rect.width = UILength::Px(300.0f);
        row->style.rect.height = UILength::Px(100.0f);

        const auto failures = ValidateOnMatrix(good, {}, BaseView(), ViewsOf(variants));
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();

        UICanvasLayout broken = BandCanvas(true);
        const auto brokenFailures = ValidateOnMatrix(broken, {}, BaseView(), ViewsOf(variants));
        ASSERT_FALSE(brokenFailures.empty());
        EXPECT_TRUE(AnyLabelEndsWith(brokenFailures, "view Portrait"));
    }

    TEST(UILayoutMatrixTest, ABarRowKeptOffTheViewPassesOnTheMatrixWhenAVariantHidesItWhereThereIsNoBar) {
        const auto variants = PortraitVariants();
        UILayoutValidatorOptions options;
        options.keepOffView.push_back("Bar"_intern);
        UICanvasLayout good = BarRowCanvas(true);

        const auto failures = ValidateOnMatrix(good, options, BaseView(), ViewsOf(variants));
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();

        UICanvasLayout broken = BarRowCanvas(false);
        const auto brokenFailures = ValidateOnMatrix(broken, options, BaseView(), ViewsOf(variants));
        ASSERT_FALSE(brokenFailures.empty());
        for (const UIMatrixFailure& failure : brokenFailures) {
            ASSERT_FALSE(failure.issues.empty());
            EXPECT_EQ(failure.issues[0].kind, UILayoutIssueKind::OverlapsSceneView) << failure.label.c_str();
            EXPECT_EQ(failure.issues[0].element, "Bar"_intern);
        }
        EXPECT_TRUE(AnyLabelEndsWith(brokenFailures, "view default"));
        EXPECT_TRUE(AnyLabelEndsWith(brokenFailures, "view Portrait"));

        // Nothing is kept off the view unless the caller names it
        UICanvasLayout unnamed = BarRowCanvas(false);
        EXPECT_TRUE(ValidateOnMatrix(unnamed, {}, BaseView(), ViewsOf(variants)).empty());
    }

    TEST(UILayoutMatrixTest, AnUnnamedVariantIsLabelledByItsIndex) {
        auto variants = PortraitVariants();
        variants[0].name = PoolString();
        UICanvasLayout canvas = BandCanvas(false);

        const auto failures = ValidateOnMatrix(canvas, {}, BaseView(), ViewsOf(variants));

        ASSERT_FALSE(failures.empty());
        EXPECT_TRUE(AnyLabelEndsWith(failures, "view #0"));
    }

    namespace {

        /// The region a game derives from the bounds of each case: the left third of the canvas, its whole height
        UIMatrixRegions LeftThirdAs(const char* name) {
            return [name](UIRegionSet& regions, const UICanvasResolved& resolved) {
                const Rect& bounds = resolved.bounds;
                regions.Set(PoolString::Intern(name), {bounds.x, bounds.y, bounds.w / 3.0f, bounds.h});
            };
        }

        /// A Screen canvas with one element of the given size (units or percent) at the top left of the region "side"
        UICanvasLayout RegionCanvas(UILength width, UILength height) {
            UICanvasLayout canvas;
            auto element = canvas.root->AddChild("Side"_intern);
            element->style.rect.region = "side"_intern;
            element->style.rect.width = width;
            element->style.rect.height = height;
            return canvas;
        }

        constexpr size_t kScreenRunsPerCase = 6;  // three scale modes, two user scales

    }  // namespace

    TEST(UILayoutMatrixTest, AnElementAnchoredByPercentInARegionTheGamePublishesIsCleanOnTheWholeMatrix) {
        UICanvasLayout canvas = RegionCanvas(UILength::Percent(80.0f), UILength::Percent(50.0f));
        canvas.regions.Set("kept"_intern, {1.0f, 2.0f, 3.0f, 4.0f});

        const auto failures = ValidateOnMatrix(canvas, {}, {}, {}, LeftThirdAs("side"));

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        EXPECT_EQ(canvas.regions.Size(), 1u);  // the canvas gets its own set back
        ASSERT_NE(canvas.regions.Find("kept"_intern), nullptr);
        EXPECT_FLOAT_EQ(canvas.regions.Find("kept"_intern)->h, 4.0f);
    }

    TEST(UILayoutMatrixTest, ARegionNameThePublisherDoesNotSetFailsEveryCaseWithMissingRegion) {
        UICanvasLayout canvas = RegionCanvas(UILength::Percent(80.0f), UILength::Percent(50.0f));

        const auto failures = ValidateOnMatrix(canvas, {}, {}, {}, LeftThirdAs("other"));

        EXPECT_EQ(failures.size(), UILayoutMatrix().size() * kScreenRunsPerCase);
        for (const UIMatrixFailure& failure : failures) {
            ASSERT_EQ(failure.issues.size(), 1u) << failure.label.c_str();
            EXPECT_EQ(failure.issues[0].kind, UILayoutIssueKind::MissingRegion);
            EXPECT_EQ(failure.issues[0].other, "side"_intern);
        }
    }

    TEST(UILayoutMatrixTest, AFixedSizeElementWiderThanItsRegionFailsWithOutsideRegionOnTheSmallCases) {
        UICanvasLayout canvas = RegionCanvas(UILength::Px(400.0f), UILength::Px(40.0f));

        const auto failures = ValidateOnMatrix(canvas, {}, {}, {}, LeftThirdAs("side"));

        ASSERT_FALSE(failures.empty());
        EXPECT_LT(failures.size(), UILayoutMatrix().size() * kScreenRunsPerCase);  // the wide cases have room for it
        for (const UIMatrixFailure& failure : failures) {
            // A canvas narrower than the element also gets OutsideCanvas; the region is what every failing case has in common
            const bool outsideRegion = eastl::any_of(failure.issues.begin(), failure.issues.end(), [](const UILayoutIssue& issue) { return issue.kind == UILayoutIssueKind::OutsideRegion; });
            EXPECT_TRUE(outsideRegion) << failure.label.c_str();
        }
    }

    TEST(UILayoutMatrixTest, WithNoPublisherTheCanvasKeepsItsOwnRegionsInEveryCase) {
        UICanvasLayout canvas = RegionCanvas(UILength::Percent(100.0f), UILength::Percent(100.0f));
        canvas.regions.Set("side"_intern, {0.0f, 0.0f, 100.0f, 100.0f});

        const auto failures = ValidateOnMatrix(canvas);

        // The region is 100 units in a canvas of at least a few hundred: the element fills it and is inside it everywhere
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    // ---- the size matrix --------------------------------------------------------------------------------------------------

    TEST(UILayoutMatrixTest, TheMatrixCoversEveryAspectSizeAndSafeArea) {
        const auto& cases = UILayoutMatrix();

        for (const char* aspect : {"16:9 ", "16:10 ", "3:2 ", "4:3 ", "1:1 ", "21:9 ", "32:9 ", "9:16 ", "9:19.5 ", "3:4 "}) {
            for (const char* safe : {"no insets", "notch", "side insets"}) {
                const bool found = std::any_of(cases.begin(), cases.end(), [&](const UIMatrixCase& c) { return c.label.find(aspect) == 0 && c.label.find(safe) != String::npos; });
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

    TEST(UILayoutMatrixTest, TheHudPassesEveryCase) {
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
            auto canvas = MakeUnique<UICanvasLayout>();
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
