#include <BECore/UI/UIStateBlend.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        // Linear and times of powers of two keep every number exact; QuadOut is checked once on its own
        constexpr EaseType kLinear = EaseType::Linear;
        constexpr float kSecond = 1.0f;

        constexpr Color kFill{100, 110, 120, 255};
        constexpr Color kEnd{60, 80, 150, 255};
        constexpr Color kBorder{200, 150, 100, 255};
        constexpr Color kAccent{250, 240, 230, 255};
        constexpr Color kHighlight{255, 255, 255, 80};
        constexpr Color kShadowColor{20, 40, 200, 160};

        constexpr UIStateLook::State kNormal{};
        constexpr UIStateLook::State kHover{true, false, false, false};
        constexpr UIStateLook::State kPressed{true, true, false, false};
        constexpr UIStateLook::State kDisabled{false, false, true, false};

        UIBoxStyle StyledBox() {
            UIBoxStyle box;
            box.cornerRadius = UICorners::Uniform(8.0f);
            box.borderWidth = 2.0f;
            box.borderColor = kBorder;
            box.gradientDirection = UIGradientDirection::Vertical;
            box.gradientColor = kEnd;
            box.shadow = {0.0f, 4.0f, 12.0f, 0.0f, kShadowColor};
            box.highlight = kHighlight;
            return box;
        }

        UIStateLook::Look Resolve(const UIBoxStyle& box, const UIStateLook::State& state) {
            return UIStateLook::ResolveBox(box, kFill, {}, state, kAccent);
        }

        bool SameLook(const UIStateLook::Look& a, const UIStateLook::Look& b) {
            return ColorsEqual(a.fill, b.fill) && ColorsEqual(a.gradientEnd, b.gradientEnd) && a.box == b.box;
        }

        void ExpectColor(const Color& actual, const Color& expected) {
            EXPECT_TRUE(ColorsEqual(actual, expected)) << "got " << int(actual.r) << "," << int(actual.g) << "," << int(actual.b) << "," << int(actual.a) << " expected " << int(expected.r) << ","
                                                       << int(expected.g) << "," << int(expected.b) << "," << int(expected.a);
        }

    }  // namespace

    TEST(UIStateBlendTest, ColorFirstCallSnapsToTheTarget) {
        UIBlend<Color> blend;
        ExpectColor(blend.Update(kFill, 5.0, kSecond, kLinear), kFill);
        EXPECT_FALSE(blend.IsRunning());
    }

    TEST(UIStateBlendTest, ColorHalfWayIsTheMiddle) {
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
        ExpectColor(blend.Update(Color{100, 200, 50, 255}, 10.0, kSecond, kLinear), Color{0, 0, 0, 255});
        EXPECT_TRUE(blend.IsRunning());
        ExpectColor(blend.Update(Color{100, 200, 50, 255}, 10.5, kSecond, kLinear), Color{50, 100, 25, 255});
        ExpectColor(blend.Update(Color{100, 200, 50, 255}, 10.25, kSecond, kLinear), Color{25, 50, 13, 255});
    }

    TEST(UIStateBlendTest, ColorEndsExactlyAtStartPlusDuration) {
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
        (void)blend.Update(Color{100, 200, 50, 255}, 10.0, kSecond, kLinear);
        ExpectColor(blend.Update(Color{100, 200, 50, 255}, 10.9375, kSecond, kLinear), Color{94, 188, 47, 255});
        EXPECT_TRUE(blend.IsRunning());
        ExpectColor(blend.Update(Color{100, 200, 50, 255}, 11.0, kSecond, kLinear), Color{100, 200, 50, 255});
        EXPECT_FALSE(blend.IsRunning());
        ExpectColor(blend.Update(Color{100, 200, 50, 255}, 99.0, kSecond, kLinear), Color{100, 200, 50, 255});
    }

    TEST(UIStateBlendTest, ColorUsesTheEaseOfTheChange) {
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, EaseType::QuadOut);
        (void)blend.Update(Color{200, 200, 200, 255}, 10.0, kSecond, EaseType::QuadOut);
        // QuadOut at 1/2 is 3/4
        ExpectColor(blend.Update(Color{200, 200, 200, 255}, 10.5, kSecond, EaseType::QuadOut), Color{150, 150, 150, 255});
    }

    TEST(UIStateBlendTest, SameTargetChangesNothing) {
        UIBlend<Color> blend;
        (void)blend.Update(kFill, 0.0, kSecond, kLinear);
        for (const double now : {1.0, 2.0, 50.0}) {
            ExpectColor(blend.Update(kFill, now, kSecond, kLinear), kFill);
            EXPECT_FALSE(blend.IsRunning());
        }
    }

    TEST(UIStateBlendTest, SameTargetDuringARunDoesNotRestartIt) {
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
        (void)blend.Update(Color{100, 100, 100, 255}, 10.0, kSecond, kLinear);
        ExpectColor(blend.Update(Color{100, 100, 100, 255}, 10.5, kSecond, kLinear), Color{50, 50, 50, 255});
        ExpectColor(blend.Update(Color{100, 100, 100, 255}, 10.75, kSecond, kLinear), Color{75, 75, 75, 255});
        ExpectColor(blend.Update(Color{100, 100, 100, 255}, 11.0, kSecond, kLinear), Color{100, 100, 100, 255});
    }

    TEST(UIStateBlendTest, RetargetMidBlendStartsFromWhatIsShown) {
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
        (void)blend.Update(Color{100, 100, 100, 255}, 10.0, kSecond, kLinear);
        ASSERT_TRUE(ColorsEqual(blend.Update(Color{100, 100, 100, 255}, 10.5, kSecond, kLinear), Color{50, 50, 50, 255}));

        // a press cuts the hover: no jump at the cut, then it goes to the new target over the whole duration
        ExpectColor(blend.Update(Color{200, 0, 0, 255}, 10.5, kSecond, kLinear), Color{50, 50, 50, 255});
        ExpectColor(blend.Update(Color{200, 0, 0, 255}, 11.0, kSecond, kLinear), Color{125, 25, 25, 255});
        ExpectColor(blend.Update(Color{200, 0, 0, 255}, 11.5, kSecond, kLinear), Color{200, 0, 0, 255});
    }

    TEST(UIStateBlendTest, RetargetAfterTheEndStartsFromTheOldTarget) {
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
        (void)blend.Update(Color{100, 100, 100, 255}, 10.0, kSecond, kLinear);
        (void)blend.Update(Color{100, 100, 100, 255}, 20.0, kSecond, kLinear);
        ExpectColor(blend.Update(Color{0, 0, 0, 255}, 30.0, kSecond, kLinear), Color{100, 100, 100, 255});
        ExpectColor(blend.Update(Color{0, 0, 0, 255}, 30.5, kSecond, kLinear), Color{50, 50, 50, 255});
    }

    TEST(UIStateBlendTest, NoDurationSnapsEvenMidBlend) {
        for (const float duration : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
            UIBlend<Color> blend;
            (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
            ExpectColor(blend.Update(Color{100, 100, 100, 255}, 1.0, duration, kLinear), Color{100, 100, 100, 255});
            EXPECT_FALSE(blend.IsRunning());
        }
        UIBlend<Color> blend;
        (void)blend.Update(Color{0, 0, 0, 255}, 0.0, kSecond, kLinear);
        (void)blend.Update(Color{100, 100, 100, 255}, 10.0, kSecond, kLinear);
        ASSERT_TRUE(blend.IsRunning());
        ExpectColor(blend.Update(Color{100, 100, 100, 255}, 10.25, 0.0f, kLinear), Color{100, 100, 100, 255});
        EXPECT_FALSE(blend.IsRunning());
    }

    TEST(UIStateBlendTest, ATransparentColorTakesTheChannelsOfTheOtherEnd) {
        const Color transparent{};
        const Color target{20, 40, 200, 160};
        ExpectColor(UIStateLook::Lerp({{}, transparent, transparent}, {{}, target, target}, 0.5f).fill, Color{20, 40, 200, 80});
        ExpectColor(UIStateLook::Lerp({{}, target, target}, {{}, transparent, transparent}, 0.5f).fill, Color{20, 40, 200, 80});
    }

    TEST(UIStateLookLerpTest, EndsAreExact) {
        const UIStateLook::Look normal = Resolve(StyledBox(), kNormal);
        const UIStateLook::Look pressed = Resolve(StyledBox(), kPressed);
        EXPECT_TRUE(SameLook(UIStateLook::Lerp(normal, pressed, 0.0f), normal));
        EXPECT_TRUE(SameLook(UIStateLook::Lerp(normal, pressed, 1.0f), pressed));
        EXPECT_TRUE(SameLook(UIStateLook::Lerp(normal, pressed, std::numeric_limits<float>::quiet_NaN()), normal));
    }

    TEST(UIStateLookLerpTest, AnOvershootExtrapolatesAndClampsTheBytes) {
        const UIStateLook::Look normal = Resolve(StyledBox(), kNormal);
        const UIStateLook::Look hover = Resolve(StyledBox(), kHover);
        const UIStateLook::Look past = UIStateLook::Lerp(normal, hover, 2.0f);
        ExpectColor(past.fill, Color{kFill.r + 2 * kHoverBrighten, kFill.g + 2 * kHoverBrighten, kFill.b + 2 * kHoverBrighten, 255});
    }

    TEST(UIStateLookLerpTest, HalfWayOfAHoverBlendsFillAndBorder) {
        const UIStateLook::Look half = UIStateLook::Lerp(Resolve(StyledBox(), kNormal), Resolve(StyledBox(), kHover), 0.5f);
        ExpectColor(half.fill, Color{kFill.r + kHoverBrighten / 2, kFill.g + kHoverBrighten / 2, kFill.b + kHoverBrighten / 2, 255});
        ExpectColor(half.gradientEnd, Color{kEnd.r + kHoverBrighten / 2, kEnd.g + kHoverBrighten / 2, kEnd.b + kHoverBrighten / 2, 255});
        ExpectColor(half.box.borderColor, Color{kBorder.r + kHoverBrighten / 2, kBorder.g + kHoverBrighten / 2, kBorder.b + kHoverBrighten / 2, 255});
        EXPECT_EQ(half.box.borderWidth, 2.0f);
    }

    TEST(UIStateLookLerpTest, HalfWayOfAPressBlendsShadowAndHighlight) {
        const UIStateLook::Look half = UIStateLook::Lerp(Resolve(StyledBox(), kNormal), Resolve(StyledBox(), kPressed), 0.5f);
        // the pressed look keeps half the offset and two thirds of the blur, and has no highlight
        EXPECT_EQ(half.box.shadow.offsetY, 3.0f);
        EXPECT_EQ(half.box.shadow.blur, 10.0f);
        ExpectColor(half.box.shadow.color, kShadowColor);
        ExpectColor(half.box.highlight, Color{255, 255, 255, 40});
    }

    TEST(UIStateLookLerpTest, ShadowThatAppearsFadesInAsItself) {
        UIBoxStyle plain = StyledBox();
        plain.shadow = {};
        const UIStateLook::Look from = Resolve(plain, kNormal);
        const UIStateLook::Look to = Resolve(StyledBox(), kNormal);
        const UIStateLook::Look half = UIStateLook::Lerp(from, to, 0.5f);
        ExpectColor(half.box.shadow.color, Color{20, 40, 200, 80});
        EXPECT_EQ(half.box.shadow.offsetY, 2.0f);
        EXPECT_EQ(half.box.shadow.blur, 6.0f);
    }

    TEST(UIStateLookLerpTest, ShadowThatVanishesFadesOutAsItself) {
        // a disabled control drops its shadow and its glow
        const UIStateLook::Look half = UIStateLook::Lerp(Resolve(StyledBox(), kNormal), Resolve(StyledBox(), kDisabled), 0.5f);
        ExpectColor(half.box.shadow.color, Color{20, 40, 200, 80});
        EXPECT_EQ(half.box.shadow.blur, 6.0f);
        EXPECT_EQ(half.box.shadow.offsetY, 2.0f);
        ExpectColor(half.fill, Color{kFill.r, kFill.g, kFill.b, 191});
    }

    TEST(UIStateLookLerpTest, DiscreteMembersAndTokensComeFromTheTarget) {
        UIBoxStyle from = StyledBox();
        UIBoxStyle to = StyledBox();
        to.cornerRadius = UICorners::Uniform(2.0f);
        to.gradientDirection = UIGradientDirection::Horizontal;
        to.shadowColorToken = "shadow"_intern;
        to.borderColor = UIColor(Color{10, 20, 30, 255});
        to.borderColor.token = "accent"_intern;
        const UIStateLook::Look half = UIStateLook::Lerp(Resolve(from, kNormal), Resolve(to, kNormal), 0.5f);
        EXPECT_EQ(half.box.cornerRadius, UICorners::Uniform(2.0f));
        EXPECT_EQ(half.box.gradientDirection, UIGradientDirection::Horizontal);
        EXPECT_EQ(half.box.shadowColorToken, "shadow"_intern);
        EXPECT_EQ(half.box.borderColor.token, "accent"_intern);
        ExpectColor(half.box.borderColor, Color{105, 85, 65, 255});
    }

    TEST(UIStateBlendTest, LookHalfWayAndExactEnd) {
        UIBlend<UIStateLook::Look> blend;
        const UIStateLook::Look normal = Resolve(StyledBox(), kNormal);
        const UIStateLook::Look hover = Resolve(StyledBox(), kHover);
        EXPECT_TRUE(SameLook(blend.Update(normal, 0.0, kSecond, kLinear), normal));
        EXPECT_TRUE(SameLook(blend.Update(hover, 10.0, kSecond, kLinear), normal));
        EXPECT_TRUE(SameLook(blend.Update(hover, 10.5, kSecond, kLinear), UIStateLook::Lerp(normal, hover, 0.5f)));
        EXPECT_FALSE(SameLook(blend.Shown(), hover));
        EXPECT_TRUE(SameLook(blend.Update(hover, 11.0, kSecond, kLinear), hover));
        EXPECT_FALSE(blend.IsRunning());
    }

    TEST(UIStateBlendTest, LookSameTargetIsANoOp) {
        UIBlend<UIStateLook::Look> blend;
        const UIStateLook::Look normal = Resolve(StyledBox(), kNormal);
        (void)blend.Update(normal, 0.0, kSecond, kLinear);
        for (const double now : {1.0, 2.0}) {
            EXPECT_TRUE(SameLook(blend.Update(normal, now, kSecond, kLinear), normal));
            EXPECT_FALSE(blend.IsRunning());
        }
    }

    TEST(UIStateBlendTest, LookHoverCutByPressIsContinuous) {
        UIBlend<UIStateLook::Look> blend;
        const UIStateLook::Look normal = Resolve(StyledBox(), kNormal);
        const UIStateLook::Look hover = Resolve(StyledBox(), kHover);
        const UIStateLook::Look pressed = Resolve(StyledBox(), kPressed);
        (void)blend.Update(normal, 0.0, kSecond, kLinear);
        (void)blend.Update(hover, 10.0, kSecond, kLinear);
        const UIStateLook::Look midHover = blend.Update(hover, 10.5, kSecond, kLinear);
        const UIStateLook::Look atCut = blend.Update(pressed, 10.5, kSecond, kLinear);
        EXPECT_TRUE(SameLook(atCut, midHover));
        EXPECT_TRUE(SameLook(blend.Update(pressed, 10.5 + 1.0, kSecond, kLinear), pressed));
    }

    TEST(UIStateBlendTest, LookStyledShadowAppearsAndVanishes) {
        UIBoxStyle plain = StyledBox();
        plain.shadow = {};
        UIBlend<UIStateLook::Look> blend;
        const UIStateLook::Look without = Resolve(plain, kNormal);
        const UIStateLook::Look with = Resolve(StyledBox(), kNormal);
        (void)blend.Update(without, 0.0, kSecond, kLinear);
        (void)blend.Update(with, 10.0, kSecond, kLinear);
        const UIStateLook::Look half = blend.Update(with, 10.5, kSecond, kLinear);
        ExpectColor(half.box.shadow.color, Color{20, 40, 200, 80});
        EXPECT_TRUE(SameLook(blend.Update(with, 11.0, kSecond, kLinear), with));
        (void)blend.Update(without, 20.0, kSecond, kLinear);
        ExpectColor(blend.Update(without, 20.5, kSecond, kLinear).box.shadow.color, Color{20, 40, 200, 80});
        EXPECT_TRUE(SameLook(blend.Update(without, 21.0, kSecond, kLinear), without));
    }

}  // namespace BECore::Tests
