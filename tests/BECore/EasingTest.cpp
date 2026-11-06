#include <BECore/Animation/Easing.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-6f;
        constexpr float kLooseTolerance = 1e-5f;
        constexpr int kSteps = 1000;

        float At(int step) {
            return static_cast<float>(step) / static_cast<float>(kSteps);
        }

        bool Contains(std::initializer_list<EaseType> types, EaseType type) {
            return std::find(types.begin(), types.end(), type) != types.end();
        }

        void ExpectMonotone(EaseType type) {
            float previous = Ease(type, 0.0f);
            for (int step = 1; step <= kSteps; ++step) {
                const float value = Ease(type, At(step));
                EXPECT_GE(value, previous - kTolerance) << EnumUtils<EaseType>::ToString(type).data() << " falls at step " << step;
                previous = value;
            }
        }

        // Out(t) == 1 - In(1 - t)
        void ExpectMirrored(EaseType in, EaseType out) {
            for (int step = 0; step <= kSteps; ++step) {
                const float t = At(step);
                EXPECT_NEAR(Ease(out, t), 1.0f - Ease(in, 1.0f - t), kLooseTolerance) << EnumUtils<EaseType>::ToString(out).data() << " at " << t;
            }
        }

        // f(1 - t) == 1 - f(t)
        void ExpectPointSymmetric(EaseType type) {
            for (int step = 0; step <= kSteps; ++step) {
                const float t = At(step);
                EXPECT_NEAR(Ease(type, 1.0f - t), 1.0f - Ease(type, t), kTolerance) << EnumUtils<EaseType>::ToString(type).data() << " at " << t;
            }
        }

        void ExpectInsideUnitRange(EaseType type) {
            for (int step = 0; step <= kSteps; ++step) {
                const float value = Ease(type, At(step));
                EXPECT_GE(value, -kTolerance) << EnumUtils<EaseType>::ToString(type).data() << " at step " << step;
                EXPECT_LE(value, 1.0f + kLooseTolerance) << EnumUtils<EaseType>::ToString(type).data() << " at step " << step;
            }
        }

    }  // namespace

    TEST(EasingTest, EndpointsAreExactForEveryType) {
        for (const EaseType type : EnumUtils<EaseType>::Values()) {
            EXPECT_EQ(Ease(type, 0.0f), 0.0f) << EnumUtils<EaseType>::ToString(type).data();
            EXPECT_EQ(Ease(type, 1.0f), 1.0f) << EnumUtils<EaseType>::ToString(type).data();
        }
    }

    TEST(EasingTest, TIsClamped) {
        for (const EaseType type : EnumUtils<EaseType>::Values()) {
            const char* name = EnumUtils<EaseType>::ToString(type).data();
            EXPECT_EQ(Ease(type, -0.5f), 0.0f) << name;
            EXPECT_EQ(Ease(type, -std::numeric_limits<float>::infinity()), 0.0f) << name;
            EXPECT_EQ(Ease(type, 1.5f), 1.0f) << name;
            EXPECT_EQ(Ease(type, std::numeric_limits<float>::infinity()), 1.0f) << name;
            EXPECT_EQ(Ease(type, std::numeric_limits<float>::quiet_NaN()), 0.0f) << name;
        }
    }

    TEST(EasingTest, LinearIsTheIdentity) {
        for (int step = 0; step <= kSteps; ++step) {
            EXPECT_EQ(Ease(EaseType::Linear, At(step)), At(step));
        }
    }

    TEST(EasingTest, PinnedValues) {
        EXPECT_FLOAT_EQ(Ease(EaseType::QuadIn, 0.5f), 0.25f);
        EXPECT_FLOAT_EQ(Ease(EaseType::QuadOut, 0.5f), 0.75f);
        EXPECT_FLOAT_EQ(Ease(EaseType::CubicIn, 0.5f), 0.125f);
        EXPECT_FLOAT_EQ(Ease(EaseType::CubicOut, 0.5f), 0.875f);
        EXPECT_NEAR(Ease(EaseType::SineInOut, 0.5f), 0.5f, kTolerance);
        EXPECT_NEAR(Ease(EaseType::SineIn, 0.5f), 1.0f - std::sqrt(0.5f), kTolerance);
        EXPECT_NEAR(Ease(EaseType::SineOut, 0.5f), std::sqrt(0.5f), kTolerance);
        // BounceOut: the first parabola, and the point where the second bounce touches the floor of 0.75
        EXPECT_NEAR(Ease(EaseType::BounceOut, 0.25f), 7.5625f * 0.25f * 0.25f, kTolerance);
        EXPECT_NEAR(Ease(EaseType::BounceOut, 1.5f / 2.75f), 0.75f, kTolerance);
        // BackIn dips to about -0.1 and BackOut overshoots to about 1.1
        EXPECT_NEAR(Ease(EaseType::BackIn, 0.5f), 1.70158f * 0.125f + 0.125f - 1.70158f * 0.25f, kTolerance);
    }

    TEST(EasingTest, InOutIsHalfAtHalfAndPointSymmetric) {
        for (const EaseType type : {EaseType::QuadInOut, EaseType::CubicInOut, EaseType::SineInOut, EaseType::BackInOut}) {
            EXPECT_NEAR(Ease(type, 0.5f), 0.5f, kTolerance) << EnumUtils<EaseType>::ToString(type).data();
            ExpectPointSymmetric(type);
        }
    }

    TEST(EasingTest, OutIsTheMirrorOfIn) {
        ExpectMirrored(EaseType::QuadIn, EaseType::QuadOut);
        ExpectMirrored(EaseType::CubicIn, EaseType::CubicOut);
        ExpectMirrored(EaseType::SineIn, EaseType::SineOut);
        ExpectMirrored(EaseType::BackIn, EaseType::BackOut);
    }

    TEST(EasingTest, MonotoneCurvesNeverFall) {
        for (const EaseType type : {EaseType::Linear, EaseType::QuadIn, EaseType::QuadOut, EaseType::QuadInOut, EaseType::CubicIn, EaseType::CubicOut, EaseType::CubicInOut, EaseType::SineIn,
                                    EaseType::SineOut, EaseType::SineInOut}) {
            ExpectMonotone(type);
        }
    }

    TEST(EasingTest, BackLeavesTheUnitRangeOnPurpose) {
        float lowest = 0.0f;
        float highest = 1.0f;
        for (int step = 0; step <= kSteps; ++step) {
            lowest = std::min(lowest, Ease(EaseType::BackIn, At(step)));
            highest = std::max(highest, Ease(EaseType::BackOut, At(step)));
        }
        EXPECT_LT(lowest, -0.05f);
        EXPECT_GT(highest, 1.05f);
    }

    TEST(EasingTest, ElasticOutCrossesOneAndEndsAtOne) {
        float highest = 0.0f;
        for (int step = 0; step <= kSteps; ++step) {
            highest = std::max(highest, Ease(EaseType::ElasticOut, At(step)));
        }
        EXPECT_GT(highest, 1.0f);
        EXPECT_EQ(Ease(EaseType::ElasticOut, 1.0f), 1.0f);
        EXPECT_NEAR(Ease(EaseType::ElasticOut, 0.999f), 1.0f, 0.01f);
    }

    TEST(EasingTest, TheOtherCurvesStayInsideZeroToOne) {
        for (const EaseType type : EnumUtils<EaseType>::Values()) {
            if (Contains({EaseType::BackIn, EaseType::BackOut, EaseType::BackInOut, EaseType::ElasticOut}, type)) {
                continue;
            }
            ExpectInsideUnitRange(type);
        }
    }

    TEST(EasingTest, EveryTypeHasAName) {
        for (const EaseType type : EnumUtils<EaseType>::Values()) {
            EXPECT_FALSE(EnumUtils<EaseType>::ToString(type).empty());
        }
        EXPECT_EQ(EnumUtils<EaseType>::Count(), 15u);
    }

}  // namespace BECore::Tests
