#include <BECore/UI/UILayout.h>
#include <BECore/UI/UIRectEdit.h>
#include <BECore/UI/UITreeLayout.h>
#include <BECore/UI/UIVariant.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-2f;

        void ExpectRect(const Rect& rect, const Rect& expected) {
            EXPECT_NEAR(rect.x, expected.x, kTolerance);
            EXPECT_NEAR(rect.y, expected.y, kTolerance);
            EXPECT_NEAR(rect.w, expected.w, kTolerance);
            EXPECT_NEAR(rect.h, expected.h, kTolerance);
        }

        /// The anchors a grid of specs walks through: the ten shorthands and edge anchors (a stretched axis beside a point, both stretched, a pivot of its own)
        struct AnchorCase {
            UIAnchor anchor = UIAnchor::TopLeft;
            Optional<Vec2> min;
            Optional<Vec2> max;
            Optional<Vec2> pivot;
        };

        std::vector<AnchorCase> AnchorCases() {
            std::vector<AnchorCase> cases;
            for (const UIAnchor anchor : {UIAnchor::TopLeft, UIAnchor::Top, UIAnchor::TopRight, UIAnchor::Left, UIAnchor::Center, UIAnchor::Right, UIAnchor::BottomLeft, UIAnchor::Bottom,
                                          UIAnchor::BottomRight, UIAnchor::Stretch}) {
                cases.push_back({anchor, {}, {}, {}});
            }
            cases.push_back({UIAnchor::TopLeft, Vec2{0.1f, 1.0f}, Vec2{0.9f, 1.0f}, Vec2{0.5f, 1.0f}});    // a bar on the bottom edge
            cases.push_back({UIAnchor::TopLeft, Vec2{0.65f, 0.0f}, Vec2{1.0f, 1.0f}, Vec2{1.0f, 0.5f}});   // a side panel
            cases.push_back({UIAnchor::TopLeft, Vec2{0.2f, 0.3f}, Vec2{0.2f, 0.3f}, Vec2{0.25f, 0.75f}});  // a point with a pivot of its own
            cases.push_back({UIAnchor::Stretch, Vec2{0.25f, 0.0f}, Vec2{0.75f, 0.0f}, {}});                // Stretch with a point axis turned on
            return cases;
        }

        const Rect kParents[] = {{100.0f, 50.0f, 400.0f, 200.0f}, {0.0f, 0.0f, 1280.0f, 720.0f}, {37.5f, 12.25f, 333.0f, 111.0f}, {-20.0f, -10.0f, 90.0f, 640.0f}};

        UIRectSpec MakeSpec(const AnchorCase& anchors, UIUnit sizeUnit, UIUnit offsetUnit) {
            UIRectSpec spec;
            spec.anchor = anchors.anchor;
            spec.anchorMin = anchors.min;
            spec.anchorMax = anchors.max;
            spec.pivot = anchors.pivot;
            spec.width = {17.0f, sizeUnit};
            spec.height = {9.0f, sizeUnit};
            spec.x = {3.0f, offsetUnit};
            spec.y = {-2.0f, offsetUnit};
            spec.margin = {4.0f, 5.0f, 6.0f, 7.0f};
            return spec;
        }

        std::vector<Rect> WantedRects(const Rect& parent) {
            std::vector<Rect> rects;
            for (const float fx : {0.0f, 0.13f, 0.5f, 0.9f}) {
                for (const float fy : {0.0f, 0.4f, 0.75f}) {
                    for (const float fw : {0.05f, 0.3f, 0.6f}) {
                        for (const float fh : {0.1f, 0.5f}) {
                            rects.emplace_back(parent.x + parent.w * fx, parent.y + parent.h * fy, parent.w * fw, parent.h * fh);
                        }
                    }
                }
            }
            return rects;
        }

    }  // namespace

    // ---- the inverse -----------------------------------------------------------------------------------------------------

    /// Over specs (point and stretched axes, units and percent, a margin) and parents (a region is one): the result lays out at the wanted rect
    TEST(UIRectEditTest, TheResultLaysOutAtTheWantedRect) {
        size_t checked = 0;
        for (const AnchorCase& anchors : AnchorCases()) {
            for (const UIUnit sizeUnit : {UIUnit::Pixels, UIUnit::Percent}) {
                for (const UIUnit offsetUnit : {UIUnit::Pixels, UIUnit::Percent}) {
                    const UIRectSpec spec = MakeSpec(anchors, sizeUnit, offsetUnit);
                    for (const Rect& parent : kParents) {
                        for (const Rect& wanted : WantedRects(parent)) {
                            const UIRectEditResult result = UIRectEdit::SpecForRect(parent, spec, wanted);
                            EXPECT_TRUE(result.exact);
                            ExpectRect(UILayout::ResolveRect(parent, result.spec), wanted);
                            ++checked;
                        }
                    }
                }
            }
        }
        EXPECT_GT(checked, 10000u);
    }

    TEST(UIRectEditTest, OnlyNumbersChangeAndEveryUnitStays) {
        UIRectSpec spec = MakeSpec({UIAnchor::TopLeft, Vec2{0.1f, 1.0f}, Vec2{0.9f, 1.0f}, Vec2{0.5f, 1.0f}}, UIUnit::Percent, UIUnit::Percent);
        spec.minHeight = UILength::Px(5.0f);
        spec.maxHeight = UILength::Percent(90.0f);
        spec.region = "table"_intern;
        const Rect parent{0.0f, 0.0f, 1000.0f, 500.0f};

        const UIRectEditResult result = UIRectEdit::SpecForRect(parent, spec, {200.0f, 400.0f, 600.0f, 50.0f});

        EXPECT_TRUE(result.exact);
        EXPECT_EQ(result.spec.anchor, spec.anchor);
        EXPECT_EQ(result.spec.anchorMin, spec.anchorMin);
        EXPECT_EQ(result.spec.anchorMax, spec.anchorMax);
        EXPECT_EQ(result.spec.pivot, spec.pivot);
        EXPECT_EQ(result.spec.minHeight, spec.minHeight);
        EXPECT_EQ(result.spec.maxHeight, spec.maxHeight);
        EXPECT_EQ(result.spec.region, spec.region);
        EXPECT_EQ(result.spec.height.unit, UIUnit::Percent);
        EXPECT_EQ(result.spec.y.unit, UIUnit::Percent);
        EXPECT_NEAR(result.spec.height.value, 10.0f, 1e-3f);  // 50 of 500
        // The stretched X axis took the margins, and the point-only fields of that axis stayed as authored
        EXPECT_EQ(result.spec.width, spec.width);
        EXPECT_EQ(result.spec.x, spec.x);
        EXPECT_NEAR(result.spec.margin.left, 100.0f, 1e-3f);   // from 0.1 of 1000 to the left edge 200
        EXPECT_NEAR(result.spec.margin.right, 100.0f, 1e-3f);  // the right edge 800 to 0.9 of 1000
        EXPECT_EQ(result.spec.margin.top, spec.margin.top);    // Y is a point axis: its margins are ignored and stay
    }

    TEST(UIRectEditTest, AnAxisThatAlreadyLaysOutThereIsLeftAlone) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::BottomRight;
        spec.width = UILength::Percent(100.0f / 3.0f);
        spec.height = UILength::Px(40.0f);
        spec.x = UILength::Percent(-5.0f);
        const Rect parent{0.0f, 0.0f, 600.0f, 300.0f};
        Rect wanted = UILayout::ResolveRect(parent, spec);
        wanted.y -= 25.0f;  // only Y moves

        const UIRectEditResult result = UIRectEdit::SpecForRect(parent, spec, wanted);

        EXPECT_TRUE(result.exact);
        EXPECT_EQ(result.spec.width, spec.width);
        EXPECT_EQ(result.spec.height, spec.height);
        EXPECT_EQ(result.spec.x, spec.x);
        EXPECT_NEAR(result.spec.y.value, -25.0f, 1e-3f);

        EXPECT_EQ(UIRectEdit::SpecForRect(parent, spec, UILayout::ResolveRect(parent, spec)).spec, spec);
    }

    TEST(UIRectEditTest, ASizeTheLimitsForbidIsReportedAndNeverSilentlyWrong) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Center;
        spec.width = UILength::Px(80.0f);
        spec.height = UILength::Px(40.0f);
        spec.maxWidth = UILength::Px(100.0f);
        spec.minHeight = UILength::Px(30.0f);
        const Rect parent{0.0f, 0.0f, 400.0f, 200.0f};

        const UIRectEditResult fits = UIRectEdit::SpecForRect(parent, spec, {10.0f, 20.0f, 90.0f, 35.0f});
        EXPECT_TRUE(fits.exact);
        ExpectRect(UILayout::ResolveRect(parent, fits.spec), {10.0f, 20.0f, 90.0f, 35.0f});

        const UIRectEditResult tooWide = UIRectEdit::SpecForRect(parent, spec, {10.0f, 20.0f, 150.0f, 35.0f});
        EXPECT_FALSE(tooWide.exact);
        const Rect laidOut = UILayout::ResolveRect(parent, tooWide.spec);
        EXPECT_NEAR(laidOut.w, 100.0f, kTolerance);  // the nearest the limit allows
        EXPECT_NEAR(laidOut.x, 10.0f, kTolerance);
        EXPECT_NEAR(laidOut.h, 35.0f, kTolerance);

        EXPECT_FALSE(UIRectEdit::SpecForRect(parent, spec, {10.0f, 20.0f, 90.0f, 10.0f}).exact);  // lower than the minimum
    }

    TEST(UIRectEditTest, AStretchedAxisWithAMaximumKeepsItsLimitAndItsPlace) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.maxWidth = UILength::Px(300.0f);
        const Rect parent{0.0f, 0.0f, 1000.0f, 400.0f};
        const Rect current = UILayout::ResolveRect(parent, spec);  // centered, 300 wide

        const UIRectEditResult same = UIRectEdit::SpecForRect(parent, spec, current);
        EXPECT_EQ(same.spec, spec);

        const UIRectEditResult moved = UIRectEdit::SpecForRect(parent, spec, {100.0f, 0.0f, 250.0f, 400.0f});
        EXPECT_TRUE(moved.exact);
        ExpectRect(UILayout::ResolveRect(parent, moved.spec), {100.0f, 0.0f, 250.0f, 400.0f});

        EXPECT_FALSE(UIRectEdit::SpecForRect(parent, spec, {100.0f, 0.0f, 500.0f, 400.0f}).exact);
    }

    TEST(UIRectEditTest, TheAspectRatioIsHeldOrReported) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::TopLeft;
        spec.width = UILength::Px(100.0f);
        spec.height = UILength::Px(50.0f);
        spec.aspectRatio = 2.0f;
        const Rect parent{0.0f, 0.0f, 400.0f, 200.0f};

        const UIRectEditResult fits = UIRectEdit::SpecForRect(parent, spec, {30.0f, 40.0f, 120.0f, 60.0f});
        EXPECT_TRUE(fits.exact);
        ExpectRect(UILayout::ResolveRect(parent, fits.spec), {30.0f, 40.0f, 120.0f, 60.0f});

        const UIRectEditResult wrongShape = UIRectEdit::SpecForRect(parent, spec, {30.0f, 40.0f, 120.0f, 100.0f});
        EXPECT_FALSE(wrongShape.exact);
        const Rect laidOut = UILayout::ResolveRect(parent, wrongShape.spec);
        EXPECT_NEAR(laidOut.w / laidOut.h, 2.0f, 1e-3f);  // the spec still lays out a legal rect
    }

    TEST(UIRectEditTest, AZeroParentGivesNoNaNAndKeepsAPercentItCannotSet) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Center;
        spec.width = UILength::Percent(50.0f);
        spec.height = UILength::Px(10.0f);
        spec.x = UILength::Percent(5.0f);

        for (const Rect& parent : {Rect{0.0f, 0.0f, 0.0f, 0.0f}, Rect{10.0f, 10.0f, 0.0f, 100.0f}}) {
            const UIRectEditResult result = UIRectEdit::SpecForRect(parent, spec, {10.0f, 10.0f, 20.0f, 30.0f});
            EXPECT_TRUE(std::isfinite(result.spec.width.value));
            EXPECT_TRUE(std::isfinite(result.spec.x.value));
            EXPECT_EQ(result.spec.width.unit, UIUnit::Percent);
            EXPECT_EQ(result.spec.width.value, 50.0f);  // a percent of nothing keeps its number
            EXPECT_EQ(result.spec.x.value, 5.0f);
            const Rect laidOut = UILayout::ResolveRect(parent, result.spec);
            EXPECT_TRUE(std::isfinite(laidOut.x) && std::isfinite(laidOut.w));
        }

        // A stretched axis of a zero parent: margins are units and need no extent
        UIRectSpec stretched;
        stretched.anchor = UIAnchor::Stretch;
        const UIRectEditResult result = UIRectEdit::SpecForRect({0.0f, 0.0f, 0.0f, 0.0f}, stretched, {5.0f, 5.0f, 20.0f, 20.0f});
        EXPECT_TRUE(std::isfinite(result.spec.margin.left) && std::isfinite(result.spec.margin.right));
    }

    TEST(UIRectEditTest, ANotFiniteWantedRectIsNeverExactAndDoesNotCrash) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Center;
        spec.width = UILength::Px(10.0f);
        spec.height = UILength::Px(10.0f);
        const Rect wanted{std::numeric_limits<float>::quiet_NaN(), 0.0f, 10.0f, 10.0f};

        EXPECT_FALSE(UIRectEdit::SpecForRect({0.0f, 0.0f, 100.0f, 100.0f}, spec, wanted).exact);
    }

    /// The box of an element that names a region is the region: the tree lays out the edited spec where the inverse was asked to put it
    TEST(UIRectEditTest, TheBoxOfAnElementInARegionIsTheRegion) {
        const PoolString region = "table.plate"_intern;
        const Rect regionRect{100.0f, 200.0f, 300.0f, 100.0f};
        UIRegionSet regions;
        regions.Set(region, regionRect);
        auto root = New<UIElement>();
        auto bar = root->AddChild("Bar"_intern);
        bar->style.rect.anchorMin = Vec2{0.1f, 1.0f};
        bar->style.rect.anchorMax = Vec2{0.9f, 1.0f};
        bar->style.rect.pivot = Vec2{0.5f, 1.0f};
        bar->style.rect.height = UILength::Percent(20.0f);
        bar->style.rect.region = region;
        const Rect canvas{0.0f, 0.0f, 1280.0f, 720.0f};
        const Rect wanted{150.0f, 230.0f, 200.0f, 40.0f};

        const UIRectEditResult result = UIRectEdit::SpecForRect(regionRect, bar->style.rect, wanted);
        bar->style.rect = result.spec;
        UIVariants::Apply(*root, {}, canvas);
        UITreeLayout::Run(*root, canvas, canvas, regions);

        EXPECT_TRUE(result.exact);
        ExpectRect(bar->rect, wanted);
    }

    // ---- the unit switch and the region choices ----------------------------------------------------------------------------

    TEST(UIRectEditTest, ConvertUnitKeepsTheLaidOutNumberBothWays) {
        const Optional<UILength> percent = UIRectEdit::ConvertUnit(UILength::Px(100.0f), UIUnit::Percent, 400.0f);
        ASSERT_TRUE(percent.has_value());
        EXPECT_EQ(percent->unit, UIUnit::Percent);
        EXPECT_NEAR(percent->value, 25.0f, 1e-4f);

        const Optional<UILength> back = UIRectEdit::ConvertUnit(*percent, UIUnit::Pixels, 400.0f);
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(back->unit, UIUnit::Pixels);
        EXPECT_NEAR(back->value, 100.0f, 1e-3f);

        EXPECT_EQ(UIRectEdit::ConvertUnit(UILength::Px(7.0f), UIUnit::Pixels, 400.0f), UILength::Px(7.0f));
        const Optional<UILength> negative = UIRectEdit::ConvertUnit(UILength::Px(-20.0f), UIUnit::Percent, 200.0f);
        ASSERT_TRUE(negative.has_value());
        EXPECT_NEAR(negative->value, -10.0f, 1e-4f);
    }

    TEST(UIRectEditTest, ConvertUnitAgainstAZeroExtent) {
        EXPECT_FALSE(UIRectEdit::ConvertUnit(UILength::Px(50.0f), UIUnit::Percent, 0.0f).has_value());  // the number would be lost
        const Optional<UILength> toPixels = UIRectEdit::ConvertUnit(UILength::Percent(50.0f), UIUnit::Pixels, 0.0f);
        ASSERT_TRUE(toPixels.has_value());
        EXPECT_EQ(toPixels->value, 0.0f);  // a percent of nothing is nothing, in any unit
    }

    TEST(UIRectEditTest, RegionChoicesAreThePublishedNamesAndTheCurrentOne) {
        UIRegionSet regions;
        regions.Set("table"_intern, {0.0f, 0.0f, 10.0f, 10.0f});
        regions.Set("table.plate.1"_intern, {1.0f, 1.0f, 2.0f, 2.0f});

        const Vector<PoolString> published = UIRectEdit::RegionChoices(regions, "table.plate.1"_intern);
        ASSERT_EQ(published.size(), 2u);
        EXPECT_EQ(published[0], "table"_intern);
        EXPECT_EQ(published[1], "table.plate.1"_intern);

        const Vector<PoolString> withMissing = UIRectEdit::RegionChoices(regions, "gone"_intern);
        ASSERT_EQ(withMissing.size(), 3u);
        EXPECT_EQ(withMissing[2], "gone"_intern);

        EXPECT_EQ(UIRectEdit::RegionChoices(regions, {}).size(), 2u);
        EXPECT_TRUE(UIRectEdit::RegionChoices({}, {}).empty());
    }

}  // namespace BECore::Tests
