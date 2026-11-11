#include <BECore/UI/UIAnchorPresets.h>
#include <BECore/UI/UILayout.h>
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

        const Rect kParents[] = {{100.0f, 50.0f, 400.0f, 200.0f}, {0.0f, 0.0f, 1280.0f, 720.0f}, {37.5f, 12.25f, 333.0f, 111.0f}};

        std::vector<UIRectSpec> Specs() {
            std::vector<UIRectSpec> specs;
            for (const UIAnchor anchor : {UIAnchor::TopLeft, UIAnchor::Center, UIAnchor::BottomRight, UIAnchor::Stretch, UIAnchor::Top}) {
                for (const UIUnit unit : {UIUnit::Pixels, UIUnit::Percent}) {
                    UIRectSpec spec;
                    spec.anchor = anchor;
                    spec.width = {20.0f, unit};
                    spec.height = {12.0f, unit};
                    spec.x = {-3.0f, unit};
                    spec.y = {2.0f, unit};
                    spec.margin = {4.0f, 5.0f, 6.0f, 7.0f};
                    specs.push_back(spec);
                }
            }
            UIRectSpec bar;  // stretched on X, a point on Y, a pivot of its own
            bar.anchorMin = Vec2{0.1f, 1.0f};
            bar.anchorMax = Vec2{0.9f, 1.0f};
            bar.pivot = Vec2{0.5f, 1.0f};
            bar.height = UILength::Px(30.0f);
            bar.margin = {10.0f, 0.0f, 10.0f, 0.0f};
            specs.push_back(bar);
            return specs;
        }

    }  // namespace

    // ---- the grid -----------------------------------------------------------------------------------------------------------

    TEST(UIAnchorPresetsTest, TheGridIsUnitysFourByFour) {
        const auto all = UIAnchorPresets::All();
        ASSERT_EQ(all.size(), 16u);
        for (size_t i = 0; i < all.size(); ++i) {
            EXPECT_EQ(all[i].row, i / 4);
            EXPECT_EQ(all[i].column, i % 4);
            EXPECT_EQ(all[i], UIAnchorPresets::At(i / 4, i % 4));
            EXPECT_NE(std::string(UIAnchorPresets::Name(all[i])), "");
        }
        EXPECT_EQ(UIAnchorPresets::At(0, 0).anchorMin, (Vec2{0.0f, 0.0f}));  // top left
        EXPECT_EQ(UIAnchorPresets::At(2, 2).anchorMax, (Vec2{1.0f, 1.0f}));  // bottom right
        EXPECT_EQ(UIAnchorPresets::At(1, 1).pivot, (Vec2{0.5f, 0.5f}));      // middle center
        const UIAnchorPreset& stretchTop = UIAnchorPresets::At(0, UIAnchorPresets::kStretch);
        EXPECT_EQ(stretchTop.anchorMin, (Vec2{0.0f, 0.0f}));
        EXPECT_EQ(stretchTop.anchorMax, (Vec2{1.0f, 0.0f}));
        EXPECT_EQ(stretchTop.pivot, (Vec2{0.5f, 0.0f}));
        const UIAnchorPreset& stretchAll = UIAnchorPresets::At(UIAnchorPresets::kStretch, UIAnchorPresets::kStretch);
        EXPECT_EQ(stretchAll.anchorMin, (Vec2{0.0f, 0.0f}));
        EXPECT_EQ(stretchAll.anchorMax, (Vec2{1.0f, 1.0f}));
        EXPECT_EQ(stretchAll.pivot, (Vec2{0.5f, 0.5f}));
    }

    TEST(UIAnchorPresetsTest, EveryShorthandMatchesItsCell) {
        const struct {
            UIAnchor anchor;
            size_t row;
            size_t column;
        } cases[] = {{UIAnchor::TopLeft, 0, 0}, {UIAnchor::Top, 0, 1},        {UIAnchor::TopRight, 0, 2}, {UIAnchor::Left, 1, 0},        {UIAnchor::Center, 1, 1},
                     {UIAnchor::Right, 1, 2},   {UIAnchor::BottomLeft, 2, 0}, {UIAnchor::Bottom, 2, 1},   {UIAnchor::BottomRight, 2, 2}, {UIAnchor::Stretch, 3, 3}};
        for (const auto& item : cases) {
            UIRectSpec spec;
            spec.anchor = item.anchor;
            const UIAnchorPreset* match = UIAnchorPresets::Match(spec);
            ASSERT_NE(match, nullptr) << static_cast<int>(item.anchor);
            EXPECT_EQ(*match, UIAnchorPresets::At(item.row, item.column));
        }
    }

    TEST(UIAnchorPresetsTest, MatchReadsTheEffectiveAnchorsAndNamesNoPresetForCustomOnes) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{0.0f, 1.0f};
        spec.anchorMax = Vec2{1.0f, 1.0f};
        spec.pivot = Vec2{0.5f, 1.0f};
        const UIAnchorPreset* match = UIAnchorPresets::Match(spec);
        ASSERT_NE(match, nullptr);
        EXPECT_EQ(*match, UIAnchorPresets::At(2, UIAnchorPresets::kStretch));  // stretch bottom

        spec.pivot = Vec2{0.25f, 1.0f};
        EXPECT_EQ(UIAnchorPresets::Match(spec), nullptr);

        UIRectSpec lone;
        lone.anchor = UIAnchor::Stretch;
        lone.anchorMin = Vec2{0.5f, 0.0f};
        EXPECT_EQ(UIAnchorPresets::Match(lone), nullptr);
    }

    TEST(UIAnchorPresetsTest, SimplifyWritesTheShorthandOnlyWhenOneFits) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{1.0f, 1.0f};
        spec.anchorMax = Vec2{1.0f, 1.0f};
        spec.pivot = Vec2{1.0f, 1.0f};
        UIAnchorPresets::Simplify(spec);
        EXPECT_EQ(spec.anchor, UIAnchor::BottomRight);
        EXPECT_FALSE(spec.anchorMin.has_value() || spec.anchorMax.has_value() || spec.pivot.has_value());

        UIRectSpec edge;
        edge.anchorMin = Vec2{0.0f, 1.0f};
        edge.anchorMax = Vec2{1.0f, 1.0f};
        edge.pivot = Vec2{0.5f, 1.0f};
        UIAnchorPresets::Simplify(edge);
        EXPECT_TRUE(edge.anchorMin.has_value());  // stretch bottom is not one of the ten
    }

    // ---- applying -----------------------------------------------------------------------------------------------------------

    /// Every preset, with and without Shift (the pivot), on a grid of specs and parents: the element stays where it was
    TEST(UIAnchorPresetsTest, ApplyingAPresetKeepsTheRectOnScreen) {
        for (const UIRectSpec& spec : Specs()) {
            for (const Rect& parent : kParents) {
                const Rect before = UILayout::ResolveRect(parent, spec);
                for (const UIAnchorPreset& preset : UIAnchorPresets::All()) {
                    for (const bool setPivot : {false, true}) {
                        const UIRectEditResult result = UIAnchorPresets::Apply(parent, spec, preset, {setPivot, false});
                        EXPECT_TRUE(result.exact) << UIAnchorPresets::Name(preset);
                        ExpectRect(UILayout::ResolveRect(parent, result.spec), before);
                        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(result.spec);
                        EXPECT_EQ(anchors.min, preset.anchorMin);
                        EXPECT_EQ(anchors.max, preset.anchorMax);
                        if (setPivot) {
                            EXPECT_EQ(anchors.pivot, preset.pivot);
                        } else {
                            EXPECT_EQ(anchors.pivot, UILayout::EffectiveAnchors(spec).pivot);
                        }
                    }
                }
            }
        }
    }

    TEST(UIAnchorPresetsTest, ApplyingAShorthandPresetWritesTheShorthand) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::TopLeft;
        spec.width = UILength::Px(50.0f);
        spec.height = UILength::Px(20.0f);
        spec.x = UILength::Px(10.0f);
        const Rect parent{0.0f, 0.0f, 400.0f, 200.0f};

        const UIRectSpec bottomRight = UIAnchorPresets::Apply(parent, spec, UIAnchorPresets::At(2, 2), {true, false}).spec;

        EXPECT_EQ(bottomRight.anchor, UIAnchor::BottomRight);
        EXPECT_FALSE(bottomRight.anchorMin.has_value() || bottomRight.anchorMax.has_value() || bottomRight.pivot.has_value());
        EXPECT_NEAR(bottomRight.x.value, 10.0f - 350.0f, 1e-3f);  // the left edge 10 is 350 left of where the pivot 1 sits (400 minus the width 50)
        ExpectRect(UILayout::ResolveRect(parent, bottomRight), {10.0f, 0.0f, 50.0f, 20.0f});
    }

    TEST(UIAnchorPresetsTest, ApplyingKeepsTheUnitsAndTheSizeOfAnAxisThatStaysAPoint) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::TopLeft;
        spec.width = UILength::Percent(25.0f);
        spec.height = UILength::Px(30.0f);
        spec.x = UILength::Percent(10.0f);
        const Rect parent{0.0f, 0.0f, 400.0f, 200.0f};

        const UIRectSpec result = UIAnchorPresets::Apply(parent, spec, UIAnchorPresets::At(1, 1), {true, false}).spec;  // middle center

        EXPECT_EQ(result.width, spec.width);  // the same rect on X: the width is untouched
        EXPECT_EQ(result.height, spec.height);
        EXPECT_EQ(result.x.unit, UIUnit::Percent);
        EXPECT_EQ(result.y.unit, UIUnit::Pixels);
        EXPECT_EQ(result.anchor, UIAnchor::Center);
    }

    TEST(UIAnchorPresetsTest, StretchingAPointElementTurnsItsSpaceIntoMargins) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::TopLeft;
        spec.width = UILength::Px(100.0f);
        spec.height = UILength::Px(50.0f);
        spec.x = UILength::Px(20.0f);
        spec.y = UILength::Px(10.0f);
        const Rect parent{0.0f, 0.0f, 400.0f, 200.0f};

        const UIRectSpec result = UIAnchorPresets::Apply(parent, spec, UIAnchorPresets::At(UIAnchorPresets::kStretch, UIAnchorPresets::kStretch), {true, false}).spec;

        EXPECT_EQ(result.anchor, UIAnchor::Stretch);
        EXPECT_NEAR(result.margin.left, 20.0f, 1e-3f);
        EXPECT_NEAR(result.margin.right, 400.0f - 120.0f, 1e-3f);
        EXPECT_NEAR(result.margin.top, 10.0f, 1e-3f);
        EXPECT_NEAR(result.margin.bottom, 200.0f - 60.0f, 1e-3f);
        EXPECT_EQ(result.width, spec.width);  // ignored while stretched, and waiting for the way back
        EXPECT_EQ(result.x, spec.x);
    }

    TEST(UIAnchorPresetsTest, AltPutsTheElementOnTheAnchors) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::TopLeft;
        spec.width = UILength::Px(100.0f);
        spec.height = UILength::Px(50.0f);
        spec.x = UILength::Px(20.0f);
        spec.y = UILength::Px(10.0f);
        const Rect parent{0.0f, 0.0f, 400.0f, 200.0f};

        const UIRectSpec center = UIAnchorPresets::Apply(parent, spec, UIAnchorPresets::At(1, 1), {true, true}).spec;
        ExpectRect(UILayout::ResolveRect(parent, center), {150.0f, 75.0f, 100.0f, 50.0f});  // centered, the size stays
        EXPECT_EQ(center.x.value, 0.0f);
        EXPECT_EQ(center.y.value, 0.0f);

        const UIRectSpec fill = UIAnchorPresets::Apply(parent, spec, UIAnchorPresets::At(UIAnchorPresets::kStretch, UIAnchorPresets::kStretch), {true, true}).spec;
        ExpectRect(UILayout::ResolveRect(parent, fill), parent);  // the margins are zero: it fills the parent
        EXPECT_EQ(fill.margin, UIEdges{});

        // Alt without Shift keeps the pivot: the old pivot (top left of the element) lands on the new anchor point
        const UIRectSpec keepPivot = UIAnchorPresets::Apply(parent, spec, UIAnchorPresets::At(2, 2), {false, true}).spec;
        ExpectRect(UILayout::ResolveRect(parent, keepPivot), {400.0f, 200.0f, 100.0f, 50.0f});
    }

    TEST(UIAnchorPresetsTest, AParentOfZeroDividesNothing) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Center;
        spec.width = UILength::Percent(50.0f);
        spec.height = UILength::Percent(50.0f);
        spec.x = UILength::Percent(5.0f);

        for (const Rect& parent : {Rect{0.0f, 0.0f, 0.0f, 0.0f}, Rect{3.0f, 4.0f, 0.0f, 200.0f}}) {
            for (const UIAnchorPreset& preset : UIAnchorPresets::All()) {
                for (const bool setPosition : {false, true}) {
                    const UIRectEditResult result = UIAnchorPresets::Apply(parent, spec, preset, {true, setPosition});
                    const Rect laidOut = UILayout::ResolveRect(parent, result.spec);
                    EXPECT_TRUE(std::isfinite(laidOut.x) && std::isfinite(laidOut.y) && std::isfinite(laidOut.w) && std::isfinite(laidOut.h));
                    EXPECT_TRUE(std::isfinite(result.spec.x.value) && std::isfinite(result.spec.width.value) && std::isfinite(result.spec.margin.left));
                }
            }
        }
    }

}  // namespace BECore::Tests
