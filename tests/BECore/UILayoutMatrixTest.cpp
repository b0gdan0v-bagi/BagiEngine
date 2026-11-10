#include <BECore/Scene/SceneView.h>
#include <BECore/UI/UICanvasLayout.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kLandscapeWidth = 1280.0f;
        constexpr float kLandscapeHeight = 720.0f;

        /// A scene of 1280 x 720 with a 720 x 1280 view for the tall outputs
        eastl::vector<SceneViewVariant> PortraitVariants() {
            SceneViewVariant portrait;
            portrait.name = "Portrait"_intern;
            portrait.when.orientation = UIOrientation::Portrait;
            portrait.width = kLandscapeHeight;
            portrait.height = kLandscapeWidth;
            return {portrait};
        }

        UIMatrixViews ViewsOf(const eastl::vector<SceneViewVariant>& variants) {
            return {kLandscapeWidth, kLandscapeHeight, {variants.data(), variants.size()}};
        }

        /// A SceneView canvas with a band 1000 units wide (a layout made for the landscape view only)
        UICanvasLayout BandCanvas(bool extend) {
            UICanvasLayout canvas;
            canvas.params.space = CanvasSpace::SceneView;
            canvas.params.extendToOutput = extend;
            auto band = canvas.root->AddChild("Band"_intern);
            band->style.rect.x = 100.0f;
            band->style.rect.y = 300.0f;
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

        UILayoutContext BaseView() {
            UILayoutContext context;
            context.sceneViewWidth = kLandscapeWidth;
            context.sceneViewHeight = kLandscapeHeight;
            return context;
        }

        bool AnyLabelEndsWith(const eastl::vector<UIMatrixFailure>& failures, const char* tail) {
            const eastl::string_view want(tail);
            for (const UIMatrixFailure& failure : failures) {
                const eastl::string_view label(failure.label.data(), failure.label.size());
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
            EXPECT_NE(failure.label.find("view Portrait"), eastl::string::npos) << failure.label.c_str();
            ASSERT_FALSE(failure.issues.empty());
            EXPECT_EQ(failure.issues[0].kind, UILayoutIssueKind::OutsideCanvas);
        }
    }

    TEST(UILayoutMatrixTest, WithoutAViewDeclarationTheMatrixKeepsTheViewOfTheBaseContext) {
        UICanvasLayout canvas = BandCanvas(false);

        const auto failures = ValidateOnMatrix(canvas, {}, BaseView());

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        // And a declaration with no variants is the default view in every case
        const eastl::vector<SceneViewVariant> none;
        EXPECT_TRUE(ValidateOnMatrix(canvas, {}, {}, ViewsOf(none)).empty());
    }

    TEST(UILayoutMatrixTest, AnExtendedCanvasRunsOnTheMatrixWithTheVariantsToo) {
        const auto variants = PortraitVariants();
        UICanvasLayout good = BandCanvasWithPortraitLayout(true);
        // A row on the bar below the view: anchored to the bottom of the whole output
        auto row = good.root->AddChild("Row"_intern);
        row->style.rect.anchor = UIAnchor::Bottom;
        row->style.rect.y = -20.0f;
        row->style.rect.width = UILength::Px(300.0f);
        row->style.rect.height = UILength::Px(100.0f);

        const auto failures = ValidateOnMatrix(good, {}, BaseView(), ViewsOf(variants));
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();

        UICanvasLayout broken = BandCanvas(true);
        const auto brokenFailures = ValidateOnMatrix(broken, {}, BaseView(), ViewsOf(variants));
        ASSERT_FALSE(brokenFailures.empty());
        EXPECT_TRUE(AnyLabelEndsWith(brokenFailures, "view Portrait"));
    }

    TEST(UILayoutMatrixTest, AnUnnamedVariantIsLabelledByItsIndex) {
        auto variants = PortraitVariants();
        variants[0].name = PoolString();
        UICanvasLayout canvas = BandCanvas(false);

        const auto failures = ValidateOnMatrix(canvas, {}, BaseView(), ViewsOf(variants));

        ASSERT_FALSE(failures.empty());
        EXPECT_TRUE(AnyLabelEndsWith(failures, "view #0"));
    }

}  // namespace BECore::Tests
