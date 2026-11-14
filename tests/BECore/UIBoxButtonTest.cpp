#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStateLook.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The draw of a styled element with a clicker in each state: the box, the bar of a selected one, and a segmented group marking its chosen segment. The look of each state as numbers
     * (what the fill, the border, the shadow, the glow and the gradient end become) is `UIStateLookTest`; the clicker's reading of the pointer is `UIInputClickerTest`.
     */

    namespace {

        constexpr Rect kRect{10.0f, 20.0f, 100.0f, 40.0f};
        constexpr Color kFill{100, 110, 120, 255};
        constexpr Color kEnd{60, 80, 150, 255};
        constexpr Color kBorder{200, 150, 100, 255};
        constexpr Color kText{250, 240, 230, 255};

        enum class State : uint8_t { Normal, Hover, Pressed, Selected, Disabled };

        /// An element with a clicker and a text of the colour `kText` (the colour of the selection), in @p state; its box has every field set when @p styled
        IntrusivePtr<UIElement> MakeElement(State state, bool styled = true) {
            auto element = New<UIElement>();
            element->rect = kRect;
            element->color = kFill;
            element->AddComponent<UITextComponent>().textColor = kText;
            auto& clicker = element->AddComponent<UIInputClicker>();
            if (styled) {
                element->box.cornerRadius = UICorners::Uniform(8.0f);
                element->box.borderWidth = 2.0f;
                element->box.borderColor = kBorder;
                element->box.gradientDirection = UIGradientDirection::Vertical;
                element->box.gradientColor = kEnd;
                element->box.shadow = {0.0f, 4.0f, 12.0f, 0.0f, Color{0, 0, 0, 160}};
                element->box.highlight = Color{255, 255, 255, 80};
            }
            switch (state) {
                case State::Normal:
                    break;
                case State::Hover:
                    element->hovered = true;
                    break;
                case State::Pressed:
                    element->hovered = true;
                    element->pressed = true;
                    break;
                case State::Selected:
                    element->selected = true;
                    break;
                case State::Disabled:
                    clicker.disabled = true;
                    break;
            }
            return element;
        }

        constexpr State kAllStates[] = {State::Normal, State::Hover, State::Pressed, State::Selected, State::Disabled};

        void ExpectColor(const Color& actual, const Color& expected, const char* what) {
            EXPECT_TRUE(ColorsEqual(actual, expected)) << what << ": got " << int(actual.r) << "," << int(actual.g) << "," << int(actual.b) << "," << int(actual.a) << " expected " << int(expected.r)
                                                       << "," << int(expected.g) << "," << int(expected.b) << "," << int(expected.a);
        }

        Vector<DrawCall> Draw(UIElement& element) {
            RecordingRenderer renderer;
            element.DrawSelf(renderer);
            return renderer.calls;
        }

        /// The bar of a selected element: the one Rect call in the bottom strip of the button
        Optional<Rect> BarOf(const Vector<DrawCall>& calls) {
            for (const DrawCall& call : calls) {
                if (call.kind == DrawKind::Rect && call.rect.h <= UIStateLook::kSelectedBarHeight + 0.01f) {
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

    // ---- drawing -------------------------------------------------------------------------------------------------------------

    TEST(UIBoxButtonTest, AStyledElementDrawsOneGeometryCallInEveryState) {
        for (const State state : kAllStates) {
            const auto calls = Draw(*MakeElement(state));
            size_t geometry = 0;
            for (const DrawCall& call : calls) {
                geometry += call.kind == DrawKind::Geometry ? 1u : 0u;
            }
            EXPECT_EQ(geometry, 1u) << "state " << static_cast<int>(state);
            EXPECT_EQ(calls.size(), state == State::Selected ? 2u : 1u) << "state " << static_cast<int>(state) << ": the box, and the bar of a selected one";
        }
    }

    TEST(UIBoxButtonTest, ADisabledElementPaintsNothingOutsideItsRectButTheFeather) {
        const float feather = 2.0f;
        const auto outside = [&](const DrawCall& call) {
            return call.rect.x < kRect.x - feather || call.rect.y < kRect.y - feather || call.rect.x + call.rect.w > kRect.x + kRect.w + feather ||
                   call.rect.y + call.rect.h > kRect.y + kRect.h + feather;
        };

        const auto normal = Draw(*MakeElement(State::Normal));
        ASSERT_EQ(normal.size(), 1u);
        EXPECT_TRUE(outside(normal[0])) << "the shadow reaches beyond the rect";

        const auto disabled = Draw(*MakeElement(State::Disabled));
        ASSERT_EQ(disabled.size(), 1u);
        EXPECT_FALSE(outside(disabled[0])) << "no shadow and no glow when disabled";

        auto glowing = MakeElement(State::Disabled);
        glowing->box.shadow = {};
        glowing->box.glow = {0.0f, 0.0f, 8.0f, 2.0f, Color{255, 255, 0, 255}};
        const auto glowOff = Draw(*glowing);
        ASSERT_EQ(glowOff.size(), 1u);
        EXPECT_FALSE(outside(glowOff[0])) << "a glow is dropped too";
        glowing->GetComponent<UIInputClicker>()->disabled = false;
        const auto glowOn = Draw(*glowing);
        ASSERT_EQ(glowOn.size(), 1u);
        EXPECT_TRUE(outside(glowOn[0]));
    }

    TEST(UIBoxButtonTest, ADefaultStyleElementDrawsTheCallsItDidBefore) {
        const Color expected[] = {kFill, ShiftColor(kFill, kHoverBrighten), ShiftColor(kFill, -kPressDarken), ShiftColor(kFill, 56), WithAlpha(kFill, kDisabledAlpha)};
        for (size_t i = 0; i < std::size(kAllStates); ++i) {
            const auto button = MakeElement(kAllStates[i], false);
            button->selectedColor = Color{};
            EXPECT_FALSE(button->box.HasEffect());
            EXPECT_TRUE(button->GetStateLook().box == button->box) << "a default box is left alone in state " << i;
            const auto calls = Draw(*button);
            ASSERT_GE(calls.size(), 1u);
            EXPECT_EQ(calls[0].kind, DrawKind::Rect);
            ExpectColor(calls[0].color, expected[i], "fill");
            EXPECT_FLOAT_EQ(calls[0].rect.x, kRect.x);
            EXPECT_FLOAT_EQ(calls[0].rect.w, kRect.w);
            ExpectColor(button->GetStateLook().fill, expected[i], "the fill of the state look");
            if (kAllStates[i] == State::Selected) {
                ASSERT_EQ(calls.size(), 2u);
                EXPECT_EQ(calls[1].kind, DrawKind::Rect);
                EXPECT_FLOAT_EQ(calls[1].rect.x, kRect.x) << "the bar spans the full width of a square element";
                EXPECT_FLOAT_EQ(calls[1].rect.w, kRect.w);
                EXPECT_FLOAT_EQ(calls[1].rect.h, UIStateLook::kSelectedBarHeight);
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
            auto button = MakeElement(State::Selected);
            button->box.cornerRadius = UICorners::Uniform(radius);
            const auto bar = BarOf(Draw(*button));
            if (!bar) {
                // Only a shape whose straight bottom edge is gone (a pill as wide as it is high) may draw no bar
                EXPECT_GE(std::min(radius, kRect.h / 2.0f) * 2.0f, kRect.w - 0.01f) << "radius " << radius;
                continue;
            }
            EXPECT_FLOAT_EQ(bar->h, UIStateLook::kSelectedBarHeight);
            EXPECT_FLOAT_EQ(bar->y + bar->h, kRect.y + kRect.h) << "radius " << radius << ": on the bottom edge";
            for (const float x : {bar->x, bar->x + bar->w}) {
                for (const float y : {bar->y, bar->y + bar->h}) {
                    EXPECT_TRUE(InsideRounded(kRect, radius, x, y, 0.01f)) << "radius " << radius << " corner " << x << "," << y;
                }
            }
        }
    }

    TEST(UIBoxButtonTest, TheSelectedBarIsInsetByTheBottomRadiiOnly) {
        auto button = MakeElement(State::Selected);
        button->box.cornerRadius = {20.0f, 20.0f, 6.0f, 10.0f};
        const auto bar = BarOf(Draw(*button));
        ASSERT_TRUE(bar);
        EXPECT_FLOAT_EQ(bar->x, kRect.x + 10.0f);
        EXPECT_FLOAT_EQ(bar->x + bar->w, kRect.x + kRect.w - 6.0f);
    }

    TEST(UIBoxButtonTest, ADisabledSelectedElementKeepsAFadedBar) {
        auto button = MakeElement(State::Disabled);
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
        Vector<IntrusivePtr<UIElement>> segments;
        for (const char* value : {"one", "two", "three"}) {
            auto segment = MakeElement(State::Normal);
            segment->GetComponent<UIInputClicker>()->value = PoolString::Intern(value);
            group->children.push_back(segment);
            segments.push_back(segment);
        }

        CoreManager::GetUIVars().Set("boxButtonChoice"_intern, "two");
        group->SyncChildren();

        EXPECT_FALSE(segments[0]->selected);
        EXPECT_TRUE(segments[1]->selected);
        EXPECT_FALSE(segments[2]->selected);
        ExpectColor(segments[1]->GetStateLook().box.borderColor, kText, "the chosen segment's border");
        ExpectColor(segments[0]->GetStateLook().box.borderColor, kBorder, "another segment's border");

        group->disabled = true;
        group->SyncChildren();
        EXPECT_TRUE(segments[0]->groupDisabled);
        EXPECT_EQ(segments[0]->GetStateLook().box.shadow.color.a, 0);
    }

}  // namespace BECore::Tests
