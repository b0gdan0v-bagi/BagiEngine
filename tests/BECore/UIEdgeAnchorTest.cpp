#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UIRegions.h>
#include <BECore/UI/UITreeLayout.h>
#include <BECore/UI/UIVariant.h>
#include <Common/Printers.h>
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

        const Rect kParent{100.0f, 50.0f, 400.0f, 200.0f};

        UIRectSpec Sized(float width, float height) {
            UIRectSpec spec;
            spec.width = UILength::Px(width);
            spec.height = UILength::Px(height);
            return spec;
        }

        /// The layout rule as it was before anchorMin / anchorMax / pivot existed: ten anchors, pixel or percent sizes and offsets. The reference of the
        /// equivalence test; it must not change with the layout.
        Rect LegacyResolveRect(const Rect& parent, const UIRectSpec& spec) {
            const bool stretch = spec.anchor == UIAnchor::Stretch;
            const Rect area = stretch ? UILayout::Inset(parent, spec.margin) : parent;

            float width = stretch ? area.w : spec.width.Resolve(parent.w);
            float height = stretch ? area.h : spec.height.Resolve(parent.h);
            const auto clamp = [](float extent, float parentExtent, const std::optional<UILength>& minimum, const std::optional<UILength>& maximum) {
                if (maximum) {
                    extent = std::min(extent, maximum->Resolve(parentExtent));
                }
                if (minimum) {
                    extent = std::max(extent, minimum->Resolve(parentExtent));
                }
                return std::max(extent, 0.0f);
            };
            width = clamp(width, parent.w, spec.minWidth, spec.maxWidth);
            height = clamp(height, parent.h, spec.minHeight, spec.maxHeight);
            if (spec.aspectRatio > 0.0f && width > 0.0f && height > 0.0f) {
                const bool boxIsWider = width / height > spec.aspectRatio;
                if ((spec.aspectMode == UIAspectMode::Fit) == boxIsWider) {
                    width = height * spec.aspectRatio;
                } else {
                    height = width / spec.aspectRatio;
                }
            }

            if (stretch) {
                return {area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height};
            }
            const auto [fractionX, fractionY] = UILayout::AnchorFraction(spec.anchor);
            return {parent.x + (parent.w - width) * fractionX + spec.x.Resolve(parent.w), parent.y + (parent.h - height) * fractionY + spec.y.Resolve(parent.h), width, height};
        }

        constexpr UIAnchor kAllAnchors[] = {UIAnchor::TopLeft, UIAnchor::Top,        UIAnchor::TopRight, UIAnchor::Left,        UIAnchor::Center,
                                            UIAnchor::Right,   UIAnchor::BottomLeft, UIAnchor::Bottom,   UIAnchor::BottomRight, UIAnchor::Stretch};

        /// The fields that spell out the shorthand of an anchor
        void SpellOutShorthand(UIRectSpec& spec) {
            if (spec.anchor == UIAnchor::Stretch) {
                spec.anchorMin = Vec2{0.0f, 0.0f};
                spec.anchorMax = Vec2{1.0f, 1.0f};
                spec.pivot = Vec2{0.5f, 0.5f};
                return;
            }
            const auto [fractionX, fractionY] = UILayout::AnchorFraction(spec.anchor);
            spec.anchorMin = spec.anchorMax = spec.pivot = Vec2{fractionX, fractionY};
        }

        void Layout(UIElement& root, const UIRegionSet& regions = {}, const Rect& canvas = {0.0f, 0.0f, 1280.0f, 720.0f}) {
            UIVariants::Apply(root, {}, canvas);
            UITreeLayout::Run(root, canvas, canvas, regions);
        }

    }  // namespace

    // ---- the shorthand is a case of the fields ---------------------------------------------------------------------------

    TEST(UIEdgeAnchorTest, TheShorthandsAreTheFieldsTheySpellOut) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::BottomRight;
        EXPECT_EQ(UILayout::EffectiveAnchors(spec), (UIEffectiveAnchors{{1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}}));
        spec.anchor = UIAnchor::Top;
        EXPECT_EQ(UILayout::EffectiveAnchors(spec), (UIEffectiveAnchors{{0.5f, 0.0f}, {0.5f, 0.0f}, {0.5f, 0.0f}}));
        spec.anchor = UIAnchor::Stretch;
        EXPECT_EQ(UILayout::EffectiveAnchors(spec), (UIEffectiveAnchors{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}}));
    }

    TEST(UIEdgeAnchorTest, EverySetFieldReplacesOnlyItsPartOfTheShorthand) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.anchorMin = Vec2{0.5f, 0.0f};  // a lone min: the max and the pivot stay the shorthand's

        EXPECT_EQ(UILayout::EffectiveAnchors(spec), (UIEffectiveAnchors{{0.5f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}}));
        ExpectRect(UILayout::ResolveRect(kParent, spec), 300.0f, 50.0f, 200.0f, 200.0f);  // x spans 0.5..1 of the parent, y the whole height

        spec.pivot = Vec2{0.0f, 0.0f};
        spec.anchorMax = Vec2{0.75f, 1.0f};
        EXPECT_EQ(UILayout::EffectiveAnchors(spec), (UIEffectiveAnchors{{0.5f, 0.0f}, {0.75f, 1.0f}, {0.0f, 0.0f}}));
    }

    TEST(UIEdgeAnchorTest, MinGreaterThanMaxIsPutInOrderPerAxis) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{0.9f, 0.2f};
        spec.anchorMax = Vec2{0.1f, 0.8f};

        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);

        EXPECT_EQ(anchors.min, (Vec2{0.1f, 0.2f}));
        EXPECT_EQ(anchors.max, (Vec2{0.9f, 0.8f}));
        ExpectRect(UILayout::ResolveRect(kParent, spec), 140.0f, 90.0f, 320.0f, 120.0f);
    }

    TEST(UIEdgeAnchorTest, ANotFiniteFieldCountsAsUnset) {
        UIRectSpec spec = Sized(40.0f, 20.0f);
        spec.anchor = UIAnchor::BottomRight;
        spec.anchorMin = Vec2{std::numeric_limits<float>::quiet_NaN(), 0.5f};
        spec.pivot = Vec2{1.0f, std::numeric_limits<float>::infinity()};

        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);

        EXPECT_EQ(anchors.min, (Vec2{1.0f, 0.5f}));
        EXPECT_EQ(anchors.max, (Vec2{1.0f, 1.0f}));
        EXPECT_EQ(anchors.pivot, (Vec2{1.0f, 1.0f}));
    }

    /// For every anchor, a grid of parents, sizes, offsets, margins, limits and aspect settings: the rect is the one of the old rule, with the new fields
    /// unset and with them set to the shorthand's own values
    TEST(UIEdgeAnchorTest, WithoutNewValuesEveryAnchorLaysOutAsItAlwaysDid) {
        const Rect parents[] = {{0.0f, 0.0f, 1280.0f, 720.0f}, {100.0f, 50.0f, 400.0f, 200.0f}, {-30.0f, 17.5f, 333.0f, 91.0f}, {10.0f, 10.0f, 0.0f, 0.0f}};
        const UILength lengths[] = {UILength::Px(0.0f), UILength::Px(96.0f), UILength::Percent(35.0f), UILength::Percent(150.0f)};
        const UILength offsets[] = {UILength::Px(0.0f), UILength::Px(-12.5f), UILength::Percent(5.0f)};
        const UIEdges margins[] = {{}, {10.0f, 20.0f, 30.0f, 40.0f}, {400.0f, 0.0f, 400.0f, 300.0f}};
        struct Limits {
            std::optional<UILength> minimum;
            std::optional<UILength> maximum;
        };
        const Limits limits[] = {{}, {UILength::Px(50.0f), std::nullopt}, {std::nullopt, UILength::Percent(20.0f)}, {UILength::Px(200.0f), UILength::Px(100.0f)}};
        struct Aspect {
            float ratio;
            UIAspectMode mode;
        };
        const Aspect aspects[] = {{0.0f, UIAspectMode::Fit}, {2.0f, UIAspectMode::Fit}, {0.5f, UIAspectMode::Fill}};

        size_t cases = 0;
        for (const UIAnchor anchor : kAllAnchors) {
            for (const Rect& parent : parents) {
                for (const UILength& width : lengths) {
                    for (const UILength& height : lengths) {
                        for (const UILength& offset : offsets) {
                            for (const UIEdges& margin : margins) {
                                for (const Limits& limit : limits) {
                                    for (const Aspect& aspect : aspects) {
                                        UIRectSpec spec;
                                        spec.anchor = anchor;
                                        spec.x = offset;
                                        spec.y = UILength{-offset.value, offset.unit};
                                        spec.width = width;
                                        spec.height = height;
                                        spec.margin = margin;
                                        spec.minWidth = limit.minimum;
                                        spec.maxWidth = limit.maximum;
                                        spec.minHeight = limit.minimum;
                                        spec.maxHeight = limit.maximum;
                                        spec.aspectRatio = aspect.ratio;
                                        spec.aspectMode = aspect.mode;
                                        const Rect expected = LegacyResolveRect(parent, spec);

                                        const Rect plain = UILayout::ResolveRect(parent, spec);
                                        SpellOutShorthand(spec);
                                        const Rect spelled = UILayout::ResolveRect(parent, spec);

                                        ++cases;
                                        const auto matches = [&](const Rect& actual) { return actual.x == expected.x && actual.y == expected.y && actual.w == expected.w && actual.h == expected.h; };
                                        if (!matches(plain) || !matches(spelled)) {
                                            ADD_FAILURE() << EnumUtils<UIAnchor>::ToString(anchor).data() << " in (" << parent.x << ", " << parent.y << ", " << parent.w << ", " << parent.h
                                                          << ") width " << width.value << " height " << height.value << ": expected (" << expected.x << ", " << expected.y << ", " << expected.w << ", "
                                                          << expected.h << "), got (" << plain.x << ", " << plain.y << ", " << plain.w << ", " << plain.h << ") and spelled out (" << spelled.x << ", "
                                                          << spelled.y << ", " << spelled.w << ", " << spelled.h << ")";
                                            return;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        EXPECT_GT(cases, 10000u);
    }

    // ---- a point and a stretched axis -------------------------------------------------------------------------------------

    TEST(UIEdgeAnchorTest, ABarPinnedByTwoEdgesOnXSitsAtAPointOnY) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Bottom;
        spec.anchorMin = Vec2{0.1f, 1.0f};
        spec.anchorMax = Vec2{0.9f, 1.0f};
        spec.height = UILength::Px(30.0f);
        spec.width = UILength::Px(5.0f);  // a stretched axis ignores its size
        spec.x = UILength::Px(999.0f);    // and its offset
        spec.y = UILength::Px(-10.0f);

        ExpectRect(UILayout::ResolveRect(kParent, spec), 140.0f, 50.0f + 200.0f - 30.0f - 10.0f, 320.0f, 30.0f);
    }

    TEST(UIEdgeAnchorTest, TheMarginCountsOnlyOnAStretchedAxis) {
        UIRectSpec spec = Sized(40.0f, 20.0f);
        spec.anchorMin = Vec2{0.0f, 0.5f};
        spec.anchorMax = Vec2{1.0f, 0.5f};  // stretched on X, a point on Y
        spec.pivot = Vec2{0.5f, 0.5f};
        spec.margin = {10.0f, 100.0f, 30.0f, 100.0f};

        // X: 400 wide span minus 10 + 30; Y: the top and bottom margins are not used by a point axis
        ExpectRect(UILayout::ResolveRect(kParent, spec), 110.0f, 50.0f + 100.0f - 10.0f, 360.0f, 20.0f);
    }

    TEST(UIEdgeAnchorTest, TheMarginIsTakenFromTheSpanBetweenTheAnchorsNotFromTheParent) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{0.25f, 0.0f};
        spec.anchorMax = Vec2{0.75f, 1.0f};
        spec.margin = {5.0f, 0.0f, 15.0f, 0.0f};

        // The span is x 200..400; the margins move its edges in
        ExpectRect(UILayout::ResolveRect(kParent, spec), 205.0f, 50.0f, 180.0f, 200.0f);
    }

    TEST(UIEdgeAnchorTest, ThePivotMovesAPointAnchoredElement) {
        UIRectSpec spec = Sized(40.0f, 20.0f);
        spec.anchor = UIAnchor::Center;
        ExpectRect(UILayout::ResolveRect(kParent, spec), 280.0f, 140.0f, 40.0f, 20.0f);

        spec.pivot = Vec2{0.0f, 0.0f};  // the top left corner of the element on the center of the parent
        ExpectRect(UILayout::ResolveRect(kParent, spec), 300.0f, 150.0f, 40.0f, 20.0f);

        spec.pivot = Vec2{1.0f, 1.0f};
        spec.x = UILength::Percent(10.0f);
        ExpectRect(UILayout::ResolveRect(kParent, spec), 300.0f - 40.0f + 40.0f, 150.0f - 20.0f, 40.0f, 20.0f);
    }

    TEST(UIEdgeAnchorTest, AnchorsAnywhereInTheParentAreFractionsOfIt) {
        UIRectSpec spec = Sized(40.0f, 20.0f);
        spec.anchorMin = spec.anchorMax = Vec2{0.25f, 0.75f};
        spec.pivot = Vec2{0.5f, 0.5f};

        ExpectRect(UILayout::ResolveRect(kParent, spec), 100.0f + 100.0f - 20.0f, 50.0f + 150.0f - 10.0f, 40.0f, 20.0f);
    }

    TEST(UIEdgeAnchorTest, ThePivotPlacesAStretchedElementThatTheLimitsMadeSmallerThanItsSpan) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{0.0f, 0.0f};
        spec.anchorMax = Vec2{1.0f, 0.0f};
        spec.height = UILength::Px(20.0f);
        spec.maxWidth = UILength::Px(100.0f);

        spec.pivot = Vec2{0.0f, 0.0f};
        ExpectRect(UILayout::ResolveRect(kParent, spec), 100.0f, 50.0f, 100.0f, 20.0f);
        spec.pivot = Vec2{0.5f, 0.0f};
        ExpectRect(UILayout::ResolveRect(kParent, spec), 250.0f, 50.0f, 100.0f, 20.0f);
        spec.pivot = Vec2{1.0f, 0.0f};
        ExpectRect(UILayout::ResolveRect(kParent, spec), 400.0f, 50.0f, 100.0f, 20.0f);
    }

    TEST(UIEdgeAnchorTest, TheMinimumBeatsTheMaximumOnAStretchedAxis) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{0.0f, 0.0f};
        spec.anchorMax = Vec2{0.5f, 1.0f};
        spec.minWidth = UILength::Px(250.0f);
        spec.maxWidth = UILength::Px(100.0f);
        spec.pivot = Vec2{0.0f, 0.0f};

        ExpectRect(UILayout::ResolveRect(kParent, spec), 100.0f, 50.0f, 250.0f, 200.0f);
    }

    TEST(UIEdgeAnchorTest, TheAspectRatioFitsAnElementPinnedOnBothAxes) {
        UIRectSpec spec;
        spec.anchorMin = Vec2{0.0f, 0.0f};
        spec.anchorMax = Vec2{1.0f, 1.0f};
        spec.pivot = Vec2{0.5f, 0.5f};
        spec.aspectRatio = 1.0f;

        // Fit: the largest square in the 400 x 200 span, centered by the pivot
        ExpectRect(UILayout::ResolveRect(kParent, spec), 200.0f, 50.0f, 200.0f, 200.0f);

        spec.aspectMode = UIAspectMode::Fill;
        ExpectRect(UILayout::ResolveRect(kParent, spec), 100.0f, -50.0f, 400.0f, 400.0f);
    }

    TEST(UIEdgeAnchorTest, AnEmptyParentOrSpanGivesAnEmptyRectNotNaN) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.anchorMin = Vec2{0.2f, 0.0f};
        spec.anchorMax = Vec2{0.8f, 1.0f};
        spec.aspectRatio = 2.0f;

        for (const Rect& parent : {Rect{10.0f, 20.0f, 0.0f, 0.0f}, Rect{10.0f, 20.0f, 0.0f, 100.0f}, Rect{10.0f, 20.0f, 100.0f, 0.0f}}) {
            const Rect rect = UILayout::ResolveRect(parent, spec);
            EXPECT_TRUE(std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.w) && std::isfinite(rect.h));
            EXPECT_TRUE(rect.IsEmpty());
        }

        spec.margin = {300.0f, 0.0f, 300.0f, 0.0f};  // margins wider than the span
        const Rect rect = UILayout::ResolveRect({0.0f, 0.0f, 400.0f, 200.0f}, spec);
        EXPECT_FLOAT_EQ(rect.w, 0.0f);
        EXPECT_TRUE(std::isfinite(rect.x));
    }

    // ---- inside the tree ----------------------------------------------------------------------------------------------------

    TEST(UIEdgeAnchorTest, AnElementInAPublishedRegionTakesItsAnchorsFromTheRegion) {
        const PoolString region = "table.plate"_intern;
        UIRegionSet regions;
        regions.Set(region, {100.0f, 200.0f, 300.0f, 100.0f});
        auto root = New<UIElement>();
        auto bar = root->AddChild("Bar"_intern);
        bar->style.rect.anchor = UIAnchor::Bottom;
        bar->style.rect.anchorMin = Vec2{0.1f, 1.0f};
        bar->style.rect.anchorMax = Vec2{0.9f, 1.0f};
        bar->style.rect.height = UILength::Px(20.0f);
        bar->style.rect.region = region;

        Layout(*root, regions);

        ExpectRect(bar->rect, 130.0f, 280.0f, 240.0f, 20.0f);
    }

    TEST(UIEdgeAnchorTest, ANestedElementIsLaidOutInItsParentsRect) {
        auto root = New<UIElement>();
        auto panel = root->AddChild("Panel"_intern);
        panel->style.rect.anchorMin = Vec2{0.65f, 0.0f};
        panel->style.rect.anchorMax = Vec2{1.0f, 1.0f};
        panel->style.rect.pivot = Vec2{0.0f, 0.0f};
        auto inner = panel->AddChild("Inner"_intern);
        inner->style.rect.anchor = UIAnchor::Stretch;
        inner->style.rect.margin = {10.0f, 10.0f, 10.0f, 10.0f};

        Layout(*root);

        ExpectRect(panel->rect, 832.0f, 0.0f, 448.0f, 720.0f);
        ExpectRect(inner->rect, 842.0f, 10.0f, 428.0f, 700.0f);
    }

    TEST(UIEdgeAnchorTest, AHiddenChildHasNoRectAndAStackChildIgnoresTheAnchors) {
        auto root = New<UIElement>();
        auto hidden = root->AddChild("Hidden"_intern);
        hidden->style.visible = false;
        hidden->style.rect.anchorMin = Vec2{0.0f, 0.0f};
        hidden->style.rect.anchorMax = Vec2{1.0f, 1.0f};
        auto column = root->AddChild("Column"_intern);
        column->layout = UILayoutKind::Stack;
        column->style.rect.width = UILength::Px(100.0f);
        column->style.rect.height = UILength::Px(100.0f);
        auto child = column->AddChild("Child"_intern);
        child->style.rect.width = UILength::Px(40.0f);
        child->style.rect.height = UILength::Px(20.0f);
        child->style.rect.anchorMin = Vec2{0.0f, 0.0f};
        child->style.rect.anchorMax = Vec2{1.0f, 1.0f};
        child->style.rect.pivot = Vec2{1.0f, 1.0f};
        child->style.rect.x = UILength::Percent(50.0f);

        Layout(*root);

        EXPECT_TRUE(hidden->rect.IsEmpty());
        ExpectRect(child->rect, column->rect.x, column->rect.y, 40.0f, 20.0f);
    }

    // ---- the validator ------------------------------------------------------------------------------------------------------

    namespace {

        UICanvasLayout MakeCanvas() {
            UICanvasLayout canvas;
            canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
            return canvas;
        }

        UILayoutContext Output(float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            return context;
        }

        IntrusivePtr<UIElement> AddBottomBar(UICanvasLayout& canvas) {
            auto bar = canvas.root->AddChild("Bar"_intern);
            bar->style.rect.anchor = UIAnchor::Bottom;
            bar->style.rect.anchorMin = Vec2{0.1f, 1.0f};
            bar->style.rect.anchorMax = Vec2{0.9f, 1.0f};
            bar->style.rect.height = UILength::Px(40.0f);
            return bar;
        }

    }  // namespace

    TEST(UIEdgeAnchorTest, AnElementPinnedByItsEdgesInsideTheCanvasHasNoIssue) {
        UICanvasLayout canvas = MakeCanvas();
        auto bar = AddBottomBar(canvas);
        canvas.Update(Output(1000.0f, 500.0f));

        ExpectRect(bar->rect, 100.0f, 460.0f, 800.0f, 40.0f);
        EXPECT_TRUE(canvas.Validate({}).empty());
    }

    TEST(UIEdgeAnchorTest, AnOffsetThatPushesAnEdgePinnedElementOutOfTheCanvasIsReported) {
        UICanvasLayout canvas = MakeCanvas();
        auto bar = AddBottomBar(canvas);
        bar->style.rect.y = UILength::Percent(2.0f);  // 10 units below the edge of a canvas 500 high
        canvas.Update(Output(1000.0f, 500.0f));

        const auto issues = canvas.Validate({});

        ASSERT_EQ(issues.size(), 1u);
        EXPECT_EQ(issues[0].kind, UILayoutIssueKind::OutsideCanvas);
        EXPECT_EQ(issues[0].element, "Bar"_intern);
    }

}  // namespace BECore::Tests
