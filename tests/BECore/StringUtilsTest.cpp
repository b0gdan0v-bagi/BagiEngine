#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(StringUtilsTest, SplitKeepsEmptyPartsAndTrailingPart) {
        const auto parts = StringUtils::Split("a,,b,", ',');
        ASSERT_EQ(parts.size(), 4u);
        EXPECT_EQ(parts[0], "a");
        EXPECT_EQ(parts[1], "");
        EXPECT_EQ(parts[2], "b");
        EXPECT_EQ(parts[3], "");
    }

    TEST(StringUtilsTest, SplitOfEmptyIsEmptyAndWithoutDelimiterIsOnePart) {
        EXPECT_TRUE(StringUtils::Split("", ',').empty());
        const auto one = StringUtils::Split("abc", ',');
        ASSERT_EQ(one.size(), 1u);
        EXPECT_EQ(one[0], "abc");
    }

    TEST(StringUtilsTest, GetHashIsFnv1a64) {
        // The generated reflection code relies on these values.
        EXPECT_EQ(StringUtils::GetHash(StringView{""}), 0xcbf29ce484222325ULL);
        EXPECT_EQ(StringUtils::GetHash(StringView{"a"}), 0xaf63dc4c8601ec8cULL);
        EXPECT_EQ(StringUtils::GetHash(StringView{"foobar"}), 0x85944171f73967e8ULL);
        EXPECT_EQ(StringUtils::GetEmptyHash(), 0xcbf29ce484222325ULL);
    }

    TEST(StringUtilsTest, ParseNumberReadsTheWholeTextOrNothing) {
        EXPECT_EQ(StringUtils::ParseNumber<int32_t>("42"), Optional<int32_t>(42));
        EXPECT_EQ(StringUtils::ParseNumber<int32_t>("-7"), Optional<int32_t>(-7));
        EXPECT_EQ(StringUtils::ParseNumber<uint64_t>("18446744073709551615"), Optional<uint64_t>(18446744073709551615ULL));
        EXPECT_EQ(StringUtils::ParseNumber<float>("1.5"), Optional<float>(1.5f));

        EXPECT_FALSE(StringUtils::ParseNumber<int32_t>("").has_value());
        EXPECT_FALSE(StringUtils::ParseNumber<int32_t>("12abc").has_value()) << "the whole text, not a prefix";
        EXPECT_FALSE(StringUtils::ParseNumber<int32_t>(" 12").has_value());
        EXPECT_FALSE(StringUtils::ParseNumber<uint16_t>("65536").has_value()) << "out of range";
        EXPECT_FALSE(StringUtils::ParseNumber<uint32_t>("-1").has_value());
    }

}  // namespace BECore::Tests
