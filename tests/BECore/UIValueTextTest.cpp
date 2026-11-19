#include <BECore/UI/UIValueText.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    // The number helpers that UIProgressBar and UISliderComponent share

    TEST(UIValueTextTest, ParseFiniteFloatRefusesWhatIsNotAFiniteNumber) {
        EXPECT_FLOAT_EQ(Require(UIText::ParseFiniteFloat("12.5")), 12.5f);
        EXPECT_FLOAT_EQ(Require(UIText::ParseFiniteFloat("  -3 ")), -3.0f);
        for (const char* text : {"", " ", "abc", "1x", "nan", "NaN", "inf", "-inf", "infinity", "1e99999"}) {
            EXPECT_FALSE(UIText::ParseFiniteFloat(text).has_value()) << '"' << text << '"';
        }
    }

    TEST(UIValueTextTest, FractionInRangeClampsAndNeverDividesByZero) {
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.0f, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.5f, 0.0f, 1.0f), 0.5f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(1.0f, 0.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(25.0f, 0.0f, 100.0f), 0.25f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(-25.0f, -50.0f, 50.0f), 0.25f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(-7.0f, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(7.0f, 0.0f, 1.0f), 1.0f);

        EXPECT_FLOAT_EQ(UIText::FractionInRange(5.0f, 5.0f, 5.0f), 0.0f) << "an empty range";
        EXPECT_FLOAT_EQ(UIText::FractionInRange(5.0f, 10.0f, 0.0f), 0.0f) << "a reversed range";
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FLOAT_EQ(UIText::FractionInRange(nan, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(inf, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.5f, nan, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.5f, 0.0f, inf), 0.0f);
        const float big = std::numeric_limits<float>::max();
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.0f, -big, big), 0.5f) << "the span of two large floats does not overflow";
    }

    TEST(UIValueTextTest, ValueInRangeIsTheClampedNumberOrTheMinimum) {
        EXPECT_FLOAT_EQ(UIText::ValueInRange("0.3", 0.0f, 1.0f), 0.3f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("-4", 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("4", 0.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("abc", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("nan", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("inf", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("5", 8.0f, 2.0f), 8.0f) << "a reversed range is its minimum";
        EXPECT_FLOAT_EQ(UIText::ValueInRange("5", 3.0f, 3.0f), 3.0f);
    }

}  // namespace BECore::Tests
