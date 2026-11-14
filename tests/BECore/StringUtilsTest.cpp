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

    TEST(StringUtilsTest, StdStringViewOverloadAgrees) {
        constexpr std::string_view text = "BECore";
        EXPECT_EQ(StringUtils::GetHash(text), StringUtils::GetHash(FromStd(text)));
        static_assert(StringUtils::GetHash(std::string_view{"a"}) == 0xaf63dc4c8601ec8cULL);
    }

}  // namespace BECore::Tests
