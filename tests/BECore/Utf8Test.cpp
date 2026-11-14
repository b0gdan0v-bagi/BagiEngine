#include <BECore/Text/Utf8.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        Vector<char32_t> DecodeAll(StringView text) {
            Vector<char32_t> result;
            size_t pos = 0;
            while (pos < text.size()) {
                result.push_back(Utf8::DecodeNext(text, pos));
            }
            return result;
        }

    }  // namespace

    TEST(Utf8Test, DecodesAscii) {
        const auto result = DecodeAll("Aa 1");

        ASSERT_EQ(result.size(), 4u);
        EXPECT_EQ(result[0], U'A');
        EXPECT_EQ(result[3], U'1');
    }

    TEST(Utf8Test, DecodesCyrillicAndOtherMultiByteSequences) {
        const auto result = DecodeAll("Жяé…\U0001F600");

        ASSERT_EQ(result.size(), 5u);
        EXPECT_EQ(result[0], 0x0416u);   // Ж, two bytes
        EXPECT_EQ(result[1], 0x044Fu);   // я
        EXPECT_EQ(result[2], 0x00E9u);   // é
        EXPECT_EQ(result[3], 0x2026u);   // …, three bytes
        EXPECT_EQ(result[4], 0x1F600u);  // four bytes
    }

    TEST(Utf8Test, DecodeAdvancesPastTheSequence) {
        const StringView text = "Ж!";
        size_t pos = 0;

        EXPECT_EQ(Utf8::DecodeNext(text, pos), 0x0416u);
        EXPECT_EQ(pos, 2u);
        EXPECT_EQ(Utf8::DecodeNext(text, pos), U'!');
        EXPECT_EQ(pos, 3u);
    }

    TEST(Utf8Test, DecodeAtTheEndAnswersZeroAndStays) {
        size_t pos = 3;

        EXPECT_EQ(Utf8::DecodeNext("abc", pos), 0u);
        EXPECT_EQ(pos, 3u);
    }

    TEST(Utf8Test, MalformedBytesGiveTheReplacementOneByteAtATime) {
        // A stray continuation byte, a truncated two-byte sequence, an overlong 'A', a surrogate, past U+10FFFF
        const StringView stray("\x80", 1);
        const StringView truncated("\xD0", 1);
        const StringView overlong("\xC1\x81", 2);
        const StringView surrogate("\xED\xA0\x80", 3);
        const StringView tooBig("\xF4\x90\x80\x80", 4);

        for (const StringView text : {stray, truncated, overlong, surrogate, tooBig}) {
            size_t pos = 0;
            EXPECT_EQ(Utf8::DecodeNext(text, pos), Utf8::kReplacement);
            EXPECT_EQ(pos, 1u);
        }
    }

    TEST(Utf8Test, ADamagedSequenceDoesNotSwallowTheNextCharacter) {
        const StringView text("\xD0z", 2);  // a lead byte followed by an ASCII letter
        const auto result = DecodeAll(text);

        ASSERT_EQ(result.size(), 2u);
        EXPECT_EQ(result[0], Utf8::kReplacement);
        EXPECT_EQ(result[1], U'z');
    }

    TEST(Utf8Test, AppendRoundTripsEveryLength) {
        String encoded;
        for (const char32_t codePoint : {U'A', char32_t{0x00E9}, char32_t{0x0416}, char32_t{0x2026}, char32_t{0x1F600}}) {
            Utf8::Append(encoded, codePoint);
        }

        const auto decoded = DecodeAll(encoded);
        ASSERT_EQ(decoded.size(), 5u);
        EXPECT_EQ(decoded[0], U'A');
        EXPECT_EQ(decoded[4], 0x1F600u);
        EXPECT_EQ(encoded.size(), 1u + 2u + 2u + 3u + 4u);
    }

    TEST(Utf8Test, AppendWritesTheReplacementForANonScalarValue) {
        String encoded;
        Utf8::Append(encoded, 0xD800);
        Utf8::Append(encoded, 0x110000);

        const auto decoded = DecodeAll(encoded);
        ASSERT_EQ(decoded.size(), 2u);
        EXPECT_EQ(decoded[0], Utf8::kReplacement);
        EXPECT_EQ(decoded[1], Utf8::kReplacement);
    }

    TEST(Utf8Test, CountsCodePointsNotBytes) {
        EXPECT_EQ(Utf8::CountCodePoints("Привет"), 6u);
        EXPECT_EQ(Utf8::CountCodePoints(""), 0u);
        EXPECT_EQ(Utf8::CountCodePoints(StringView("a\x80z", 3)), 3u);
    }

}  // namespace BECore::Tests
