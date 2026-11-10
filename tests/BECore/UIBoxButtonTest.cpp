#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UISegmented.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr Rect kRect{10.0f, 20.0f, 100.0f, 40.0f};
        constexpr Color kFill{100, 110, 120, 255};
        constexpr Color kEnd{60, 80, 150, 255};
        constexpr Color kBorder{200, 150, 100, 255};
        constexpr Color kText{250, 240, 230, 255};

        enum class State : uint8_t { Normal, Hover, Pressed, Selected, Disabled };

        /// A button with every box field set, in @p state
        IntrusivePtr<UIButton> MakeStyledButton(State state) {
            auto button = New<UIButton>();
            button->rect = kRect;
            button->color = kFill;
            button->textColor = kText;
            button->box.cornerRadius = UICorners::Uniform(8.0f);
            button->box.borderWidth = 2.0f;
            button->box.borderColor = kBorder;
            button->box.gradientDirection = UIGradientDirection::Vertical;
            button->box.gradientColor = kEnd;
            button->box.shadow = {0.0f, 4.0f, 12.0f, 0.0f, Color{0, 0, 0, 160}};
            button->box.highlight = Color{255, 255, 255, 80};
            switch (state) {
                case State::Normal:
                    break;
                case State::Hover:
                    button->hovered = true;
                    break;
                case State::Pressed:
                    button->hovered = true;
                    button->pressed = true;
                    break;
                case State::Selected:
                    button->selected = true;
                    break;
                case State::Disabled:
                    button->disabled = true;
                    break;
            }
            return button;
        }

        constexpr State kAllStates[] = {State::Normal, State::Hover, State::Pressed, State::Selected, State::Disabled};

        void ExpectColor(const Color& actual, const Color& expected, const char* what) {
            EXPECT_TRUE(ColorsEqual(actual, expected)) << what << ": got " << int(actual.r) << "," << int(actual.g) << "," << int(actual.b) << "," << int(actual.a) << " expected " << int(expected.r)
                                                       << "," << int(expected.g) << "," << int(expected.b) << "," << int(expected.a);
        }

        eastl::vector<DrawCall> Draw(UIButton& button) {
            RecordingRenderer renderer;
            button.DrawSelf(renderer);
            return renderer.calls;
        }

        /// The bar of a selected button: the one Rect call in the bottom strip of the button
        std::optional<Rect> BarOf(const eastl::vector<DrawCall>& calls) {
            for (const DrawCall& call : calls) {
                if (call.kind == DrawKind::Rect && call.rect.h <= UIButton::kSelectedBarHeight + 0.01f) {
                    return call.rect;
                }
            }
            return std::nullopt;
        }

        /// Whether (x, y) is in the rounded rect of @p rect with the uniform radius @p radius (clamped as the builder does), @p epsilon units of slack
        bool InsideRounded(const Rect& rect, float radius, float x, float y, float epsilon) {
            const float r = std::min({radius, rect.w / 2.0f, rect.h / 2.0f});
            const float cx = std::clamp(x, rect.x + r, rect.x + rect.w - r);
            const float cy = std::clamp(y, rect.y + r, rect.y + rect.h - r);
            const float dx = x - cx;
            const float dy = y - cy;
            return dx * dx + dy * dy <= (r + epsilon) * (r + epsilon);
        }

    }  // namespace

    // ---- the effective box of each state -------------------------------------------------------------------------------------

    TEST(UIBoxButtonTest, ANormalButtonKeepsItsBoxAndFills) {
        const auto button = MakeStyledButton(State::Normal);
        const auto look = button->GetCurrentBox();
        EXPECT_TRUE(look.box == button->box);
        ExpectColor(look.fill, kFill, "fill");
        ExpectColor(look.gradientEnd, kEnd, "gradient end");
    }

    TEST(UIBoxButtonTest, HoverLightensBothEndsAndTheBorder) {
        const auto look = MakeStyledButton(State::Hover)->GetCurrentBox();
        ExpectColor(look.fill, ShiftColor(kFill, kHoverBrighten), "fill");
        ExpectColor(look.gradientEnd, ShiftColor(kEnd, kHoverBrighten), "gradient end");
        ExpectColor(look.box.borderColor, ShiftColor(kBorder, kHoverBrighten), "border");
        EXPECT_TRUE(look.box.shadow == MakeStyledButton(State::Normal)->box.shadow) << "hover leaves the shadow";
        ExpectColor(look.box.highlight, Color{255, 255, 255, 80}, "highlight");
    }

    TEST(UIBoxButtonTest, PressDarkensMovesTheShadowAndDropsTheHighlight) {
        const auto look = MakeStyledButton(State::Pressed)->GetCurrentBox();
        ExpectColor(look.fill, ShiftColor(kFill, -kPressDarken), "fill");
        ExpectColor(look.gradientEnd, ShiftColor(kEnd, -kPressDarken), "gradient end");
        ExpectColor(look.box.borderColor, ShiftColor(kBorder, -kPressDarken), "border");
        EXPECT_FLOAT_EQ(look.box.shadow.offsetY, 4.0f * UIButton::kPressShadowOffsetScale);
        EXPECT_FLOAT_EQ(look.box.shadow.offsetY, 2.0f) << "half the offset, so the shadow is nearer";
        EXPECT_FLOAT_EQ(look.box.shadow.blur, 12.0f * UIButton::kPressShadowBlurScale);
        EXPECT_FLOAT_EQ(look.box.shadow.blur, 8.0f) << "a third less blur";
        EXPECT_EQ(look.box.highlight.a, 0) << "the highlight is dropped";
    }

    TEST(UIBoxButtonTest, APressThatMovedOffTheButtonIsNotShownAsPressed) {
        auto button = MakeStyledButton(State::Pressed);
        button->hovered = false;
        const auto look = button->GetCurrentBox();
        EXPECT_TRUE(look.box == button->box);
        ExpectColor(look.fill, kFill, "fill");
    }

    TEST(UIBoxButtonTest, SelectedBrightensRaisesTheBorderAndAddsAGlow) {
        const auto look = MakeStyledButton(State::Selected)->GetCurrentBox();
        ExpectColor(look.fill, ShiftColor(kFill, 56), "fill: the selected brighten of the plain button");
        ExpectColor(look.gradientEnd, ShiftColor(kEnd, 56), "gradient end");
        ExpectColor(look.box.borderColor, kText, "border");
        EXPECT_FLOAT_EQ(look.box.borderWidth, 2.0f) << "a border that is wider than the least keeps its width";
        ExpectColor(look.box.glow.color, WithAlpha(kText, UIButton::kSelectedGlowAlpha), "glow");
        EXPECT_FLOAT_EQ(look.box.glow.blur, UIButton::kSelectedGlowBlur);
    }

    TEST(UIBoxButtonTest, SelectedGivesABorderlessButtonAOneUnitBorderAndKeepsAnAuthoredGlow) {
        auto button = MakeStyledButton(State::Selected);
        button->box.borderWidth = 0.0f;
        button->box.glow = {1.0f, 2.0f, 3.0f, 4.0f, Color{9, 8, 7, 6}};
        const auto look = button->GetCurrentBox();
        EXPECT_FLOAT_EQ(look.box.borderWidth, UIButton::kSelectedBorderMin);
        EXPECT_TRUE(look.box.glow == button->box.glow) << "a glow the style has is not replaced";
    }

    TEST(UIBoxButtonTest, DisabledDropsShadowAndGlowAndHalvesTheAlphas) {
        auto button = MakeStyledButton(State::Disabled);
        button->box.glow = {0.0f, 0.0f, 5.0f, 0.0f, Color{9, 8, 7, 200}};
        const auto look = button->GetCurrentBox();
        EXPECT_EQ(look.box.shadow.color.a, 0);
        EXPECT_EQ(look.box.glow.color.a, 0);
        ExpectColor(look.fill, WithAlpha(kFill, kDisabledAlpha), "fill");
        ExpectColor(look.gradientEnd, WithAlpha(kEnd, kDisabledAlpha), "gradient end");
        ExpectColor(look.box.borderColor, WithAlpha(kBorder, kDisabledAlpha), "border");
        ExpectColor(look.box.highlight, WithAlpha(Color{255, 255, 255, 80}, kDisabledAlpha), "highlight");
    }

    TEST(UIBoxButtonTest, ADisabledGroupCountsAsDisabled) {
        auto button = MakeStyledButton(State::Normal);
        button->groupDisabled = true;
        const auto look = button->GetCurrentBox();
        EXPECT_EQ(look.box.shadow.color.a, 0);
        ExpectColor(look.fill, WithAlpha(kFill, kDisabledAlpha), "fill");
    }

    TEST(UIBoxButtonTest, DisabledWinsOverSelectedForTheFillButKeepsTheRaisedBorderFaded) {
        auto button = MakeStyledButton(State::Disabled);
        button->selected = true;
        const auto look = button->GetCurrentBox();
        ExpectColor(look.fill, WithAlpha(kFill, kDisabledAlpha), "fill");
        ExpectColor(look.box.borderColor, WithAlpha(kText, kDisabledAlpha), "border");
        EXPECT_EQ(look.box.glow.color.a, 0);
    }

    TEST(UIBoxButtonTest, AGradientKeepsTwoDistinctEndsInEveryState) {
        for (const State state : kAllStates) {
            const auto look = MakeStyledButton(state)->GetCurrentBox();
            EXPECT_FALSE(ColorsEqual(look.fill, look.gradientEnd)) << "state " << static_cast<int>(state);
            EXPECT_TRUE(ColorsEqual(look.gradientEnd, look.box.gradientColor)) << "the look says one end, state " << static_cast<int>(state);
            EXPECT_EQ(look.box.gradientDirection, UIGradientDirection::Vertical);
        }
    }

    // ---- an explicit colour of a state ---------------------------------------------------------------------------------------

    TEST(UIBoxButtonTest, AnExplicitStateColorReplacesTheStartAndTheEndKeepsItsOffset) {
        // The end is (-40, -30, +30, 0) from the start; the end follows the new start by the same offset
        const Color explicitColor{150, 160, 170, 255};
        const Color moved{110, 130, 200, 255};

        auto hover = MakeStyledButton(State::Hover);
        hover->hoverColor = explicitColor;
        auto look = hover->GetCurrentBox();
        ExpectColor(look.fill, explicitColor, "hover fill");
        ExpectColor(look.gradientEnd, moved, "hover end");

        auto pressed = MakeStyledButton(State::Pressed);
        pressed->pressedColor = explicitColor;
        look = pressed->GetCurrentBox();
        ExpectColor(look.fill, explicitColor, "pressed fill");
        ExpectColor(look.gradientEnd, moved, "pressed end");

        auto selected = MakeStyledButton(State::Selected);
        selected->selectedColor = explicitColor;
        look = selected->GetCurrentBox();
        ExpectColor(look.fill, explicitColor, "selected fill");
        ExpectColor(look.gradientEnd, moved, "selected end");

        auto disabled = MakeStyledButton(State::Disabled);
        disabled->disabledColor = explicitColor;
        look = disabled->GetCurrentBox();
        ExpectColor(look.fill, explicitColor, "disabled fill");
        ExpectColor(look.gradientEnd, moved, "disabled end");
    }

    TEST(UIBoxButtonTest, TheEndOffsetIsClamped) {
        auto button = MakeStyledButton(State::Hover);
        button->hoverColor = Color{250, 250, 250, 255};  // +150 on every channel: the end 210, 230, 255 (clamped)
        const auto look = button->GetCurrentBox();
        ExpectColor(look.gradientEnd, Color{210, 220, 255, 255}, "end");
    }

    // ---- drawing -------------------------------------------------------------------------------------------------------------

    TEST(UIBoxButtonTest, AStyledButtonDrawsOneGeometryCallInEveryState) {
        for (const State state : kAllStates) {
            const auto calls = Draw(*MakeStyledButton(state));
            size_t geometry = 0;
            for (const DrawCall& call : calls) {
                geometry += call.kind == DrawKind::Geometry ? 1u : 0u;
            }
            EXPECT_EQ(geometry, 1u) << "state " << static_cast<int>(state);
            EXPECT_EQ(calls.size(), state == State::Selected ? 2u : 1u) << "state " << static_cast<int>(state) << ": the box, and the bar of a selected one";
        }
    }

    TEST(UIBoxButtonTest, ADisabledButtonPaintsNothingOutsideItsRectButTheFeather) {
        const float feather = 2.0f;
        const auto outside = [&](const DrawCall& call) {
            return call.rect.x < kRect.x - feather || call.rect.y < kRect.y - feather || call.rect.x + call.rect.w > kRect.x + kRect.w + feather ||
                   call.rect.y + call.rect.h > kRect.y + kRect.h + feather;
        };

        const auto normal = Draw(*MakeStyledButton(State::Normal));
        ASSERT_EQ(normal.size(), 1u);
        EXPECT_TRUE(outside(normal[0])) << "the shadow reaches beyond the rect";

        const auto disabled = Draw(*MakeStyledButton(State::Disabled));
        ASSERT_EQ(disabled.size(), 1u);
        EXPECT_FALSE(outside(disabled[0])) << "no shadow and no glow when disabled";

        auto glowing = MakeStyledButton(State::Disabled);
        glowing->box.shadow = {};
        glowing->box.glow = {0.0f, 0.0f, 8.0f, 2.0f, Color{255, 255, 0, 255}};
        const auto glowOff = Draw(*glowing);
        ASSERT_EQ(glowOff.size(), 1u);
        EXPECT_FALSE(outside(glowOff[0])) << "a glow is dropped too";
        glowing->disabled = false;
        const auto glowOn = Draw(*glowing);
        ASSERT_EQ(glowOn.size(), 1u);
        EXPECT_TRUE(outside(glowOn[0]));
    }

    TEST(UIBoxButtonTest, ADefaultStyleButtonDrawsTheCallsItDidBefore) {
        const auto make = [](State state) {
            auto button = New<UIButton>();
            button->rect = kRect;
            button->color = kFill;
            button->textColor = kText;
            button->selectedColor = Color{};
            switch (state) {
                case State::Hover:
                    button->hovered = true;
                    break;
                case State::Pressed:
                    button->hovered = true;
                    button->pressed = true;
                    break;
                case State::Selected:
                    button->selected = true;
                    break;
                case State::Disabled:
                    button->disabled = true;
                    break;
                case State::Normal:
                    break;
            }
            return button;
        };

        const Color expected[] = {kFill, ShiftColor(kFill, kHoverBrighten), ShiftColor(kFill, -kPressDarken), ShiftColor(kFill, 56), WithAlpha(kFill, kDisabledAlpha)};
        for (size_t i = 0; i < std::size(kAllStates); ++i) {
            const auto button = make(kAllStates[i]);
            EXPECT_FALSE(button->box.HasEffect());
            EXPECT_TRUE(button->GetCurrentBox().box == button->box) << "a default box is left alone in state " << i;
            const auto calls = Draw(*button);
            ASSERT_GE(calls.size(), 1u);
            EXPECT_EQ(calls[0].kind, DrawKind::Rect);
            ExpectColor(calls[0].color, expected[i], "fill");
            EXPECT_FLOAT_EQ(calls[0].rect.x, kRect.x);
            EXPECT_FLOAT_EQ(calls[0].rect.w, kRect.w);
            ExpectColor(button->GetCurrentFill(), expected[i], "GetCurrentFill");
            if (kAllStates[i] == State::Selected) {
                ASSERT_EQ(calls.size(), 2u);
                EXPECT_EQ(calls[1].kind, DrawKind::Rect);
                EXPECT_FLOAT_EQ(calls[1].rect.x, kRect.x) << "the bar spans the full width of a square button";
                EXPECT_FLOAT_EQ(calls[1].rect.w, kRect.w);
                EXPECT_FLOAT_EQ(calls[1].rect.h, UIButton::kSelectedBarHeight);
                ExpectColor(calls[1].color, kText, "bar");
            } else {
                EXPECT_EQ(calls.size(), 1u);
            }
        }
    }

    // ---- the selected bar ----------------------------------------------------------------------------------------------------

    TEST(UIBoxButtonTest, TheSelectedBarLiesInsideTheRoundedShape) {
        // Radius 0, a common one, one larger than half the height (clamped to 20) and one larger than half the width
        for (const float radius : {0.0f, 8.0f, 20.0f, 33.0f, 400.0f}) {
            auto button = MakeStyledButton(State::Selected);
            button->box.cornerRadius = UICorners::Uniform(radius);
            const auto bar = BarOf(Draw(*button));
            if (!bar) {
                // Only a shape whose straight bottom edge is gone (a pill as wide as it is high) may draw no bar
                EXPECT_GE(std::min(radius, kRect.h / 2.0f) * 2.0f, kRect.w - 0.01f) << "radius " << radius;
                continue;
            }
            EXPECT_FLOAT_EQ(bar->h, UIButton::kSelectedBarHeight);
            EXPECT_FLOAT_EQ(bar->y + bar->h, kRect.y + kRect.h) << "radius " << radius << ": on the bottom edge";
            for (const float x : {bar->x, bar->x + bar->w}) {
                for (const float y : {bar->y, bar->y + bar->h}) {
                    EXPECT_TRUE(InsideRounded(kRect, radius, x, y, 0.01f)) << "radius " << radius << " corner " << x << "," << y;
                }
            }
        }
    }

    TEST(UIBoxButtonTest, TheSelectedBarIsInsetByTheBottomRadiiOnly) {
        auto button = MakeStyledButton(State::Selected);
        button->box.cornerRadius = {20.0f, 20.0f, 6.0f, 10.0f};
        const auto bar = BarOf(Draw(*button));
        ASSERT_TRUE(bar);
        EXPECT_FLOAT_EQ(bar->x, kRect.x + 10.0f);
        EXPECT_FLOAT_EQ(bar->x + bar->w, kRect.x + kRect.w - 6.0f);
    }

    TEST(UIBoxButtonTest, ADisabledSelectedButtonKeepsAFadedBar) {
        auto button = MakeStyledButton(State::Disabled);
        button->selected = true;
        const auto calls = Draw(*button);
        const auto bar = BarOf(calls);
        ASSERT_TRUE(bar);
        for (const DrawCall& call : calls) {
            if (call.kind == DrawKind::Rect) {
                ExpectColor(call.color, WithAlpha(kText, kDisabledAlpha), "bar");
            }
        }
    }

    // ---- UISegmented ---------------------------------------------------------------------------------------------------------

    class UIBoxButtonSegmentedTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
        }
    };

    TEST_F(UIBoxButtonSegmentedTest, SegmentedStillMarksOneStyledSegmentSelected) {
        auto group = New<UISegmented>();
        group->bind = PoolString::Intern("boxButtonChoice");
        eastl::vector<IntrusivePtr<UIButton>> segments;
        for (const char* value : {"one", "two", "three"}) {
            auto segment = MakeStyledButton(State::Normal);
            segment->value = PoolString::Intern(value);
            group->children.push_back(segment);
            segments.push_back(segment);
        }

        CoreManager::GetUIVars().Set("boxButtonChoice"_intern, "two");
        group->SyncChildren();

        EXPECT_FALSE(segments[0]->selected);
        EXPECT_TRUE(segments[1]->selected);
        EXPECT_FALSE(segments[2]->selected);
        ExpectColor(segments[1]->GetCurrentBox().box.borderColor, kText, "the chosen segment's border");
        ExpectColor(segments[0]->GetCurrentBox().box.borderColor, kBorder, "another segment's border");

        group->disabled = true;
        group->SyncChildren();
        EXPECT_TRUE(segments[0]->groupDisabled);
        EXPECT_EQ(segments[0]->GetCurrentBox().box.shadow.color.a, 0);
    }

}  // namespace BECore::Tests
