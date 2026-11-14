#include "TestRandom.h"

#include <Common/Printers.h>
#include <Rooms/RoomCode.h>
#include <Rooms/RoomLimits.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Always the same words, in order, then the last one again
        RandomFn MakeFixed(eastl::vector<uint64_t> words) {
            auto index = eastl::make_shared<size_t>(0);
            return [words = std::move(words), index]() -> uint64_t {
                const size_t at = *index < words.size() ? (*index)++ : words.size() - 1;
                return words[at];
            };
        }

        bool IsInAlphabet(char c) {
            return RoomLimits::kRoomCodeAlphabet.find(c) != StringView::npos;
        }

    }  // namespace

    static_assert(RoomLimits::kRoomCodeAlphabet.size() == 32);
    static_assert(RoomLimits::kRoomCodeLength == 5);
    static_assert(RoomLimits::kTokenLength == 32);

    TEST(RoomCodeTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST(RoomCodeTest, AlphabetHasNoAmbiguousCharacters) {
        const StringView alphabet = RoomLimits::kRoomCodeAlphabet;
        for (const char c : {'I', 'O', '0', '1', 'i', 'o', 'l'}) {
            EXPECT_EQ(alphabet.find(c), StringView::npos) << c;
        }
        for (const char c : alphabet) {
            EXPECT_TRUE((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9')) << c;
            EXPECT_EQ(alphabet.find(c), alphabet.rfind(c)) << "duplicate " << c;
        }
    }

    TEST(RoomCodeTest, GeneratedCodesHaveTheLengthAndTheAlphabet) {
        const RandomFn random = MakeSequence(1);
        eastl::hash_map<char, int> seen;
        for (int i = 0; i < 3000; ++i) {
            const String code = GenerateRoomCode(random);
            ASSERT_EQ(code.size(), RoomLimits::kRoomCodeLength) << code.c_str();
            for (const char c : code) {
                ASSERT_TRUE(IsInAlphabet(c)) << code.c_str();
                ++seen[c];
            }
        }
        // Every character of the alphabet comes up, so no character is out of reach
        EXPECT_EQ(seen.size(), RoomLimits::kRoomCodeAlphabet.size());
    }

    TEST(RoomCodeTest, GeneratedCodeIsDeterministicForTheSameWord) {
        EXPECT_EQ(GenerateRoomCode(MakeFixed({0})), "AAAAA");
        // Five bits per character, low bits first: 1, 2, 3, 4, 5 are B, C, D, E, F
        const uint64_t word = 1u | (2u << 5) | (3u << 10) | (4u << 15) | (5u << 20);
        EXPECT_EQ(GenerateRoomCode(MakeFixed({word})), "BCDEF");
        EXPECT_EQ(GenerateRoomCode(MakeFixed({word})), GenerateRoomCode(MakeFixed({word})));
        // The bits above the code are ignored; the last character is the top of the alphabet
        EXPECT_EQ(GenerateRoomCode(MakeFixed({~uint64_t{0}})), "99999");
        EXPECT_EQ(GenerateRoomCode(MakeSequence(5)), GenerateRoomCode(MakeSequence(5)));
    }

    TEST(RoomCodeTest, GeneratedCodesDiffer) {
        const RandomFn random = MakeSequence(9);
        EXPECT_NE(GenerateRoomCode(random), GenerateRoomCode(random));
    }

    TEST(RoomCodeTest, NormalizeUpperCasesAValidCode) {
        EXPECT_EQ(NormalizeRoomCode("ABCDE"), "ABCDE");
        EXPECT_EQ(NormalizeRoomCode("abcde"), "ABCDE");
        EXPECT_EQ(NormalizeRoomCode("aBc2z"), "ABC2Z");
        EXPECT_EQ(NormalizeRoomCode("23456"), "23456");
        EXPECT_EQ(NormalizeRoomCode("k7m2x"), "K7M2X");
    }

    TEST(RoomCodeTest, NormalizeRefusesWhatIsNotACode) {
        EXPECT_FALSE(NormalizeRoomCode("").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCD").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCDEF").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCDI").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCDO").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCD0").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCD1").has_value());
        EXPECT_FALSE(NormalizeRoomCode("abcdi").has_value());
        EXPECT_FALSE(NormalizeRoomCode("AB CD").has_value());
        EXPECT_FALSE(NormalizeRoomCode(" ABCD").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABCD-").has_value());
        EXPECT_FALSE(NormalizeRoomCode("ABC\xC3\x89").has_value());
        EXPECT_FALSE(NormalizeRoomCode(StringView("ABC\0E", 5)).has_value());
    }

    TEST(RoomCodeTest, NormalizeNeverFailsOnAnyByte) {
        for (int byte = 0; byte < 256; ++byte) {
            String text = "ABCD";
            text.push_back(static_cast<char>(byte));
            const bool isLowerCase = byte >= 'a' && byte <= 'z';
            const bool expected = IsInAlphabet(static_cast<char>(isLowerCase ? byte - 'a' + 'A' : byte));
            EXPECT_EQ(NormalizeRoomCode(text).has_value(), expected) << byte;
        }
    }

    TEST(RoomCodeTest, GeneratedCodesNormaliseToThemselves) {
        const RandomFn random = MakeSequence(3);
        for (int i = 0; i < 200; ++i) {
            const String code = GenerateRoomCode(random);
            EXPECT_EQ(NormalizeRoomCode(code), code);
        }
    }

    TEST(RoomCodeTest, TokenIs32LowerCaseHexCharacters) {
        const RandomFn random = MakeSequence(11);
        for (int i = 0; i < 100; ++i) {
            const String token = GenerateToken(random);
            EXPECT_EQ(token.size(), RoomLimits::kTokenLength);
            EXPECT_TRUE(IsWellFormedToken(token)) << token.c_str();
        }
    }

    TEST(RoomCodeTest, TokenUsesTwoWordsHighFirst) {
        EXPECT_EQ(GenerateToken(MakeFixed({0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL})), "0123456789abcdeffedcba9876543210");
        EXPECT_EQ(GenerateToken(MakeFixed({0, 0})), "00000000000000000000000000000000");
    }

    TEST(RoomCodeTest, TokensDiffer) {
        const RandomFn random = MakeSequence(21);
        eastl::hash_map<String, int> seen;
        for (int i = 0; i < 500; ++i) {
            ++seen[GenerateToken(random)];
        }
        EXPECT_EQ(seen.size(), 500u);
    }

    TEST(RoomCodeTest, SecureRandomGivesDifferentWords) {
        const RandomFn random = MakeSecureRandom();
        ASSERT_TRUE(static_cast<bool>(random));
        EXPECT_NE(GenerateToken(random), GenerateToken(random));
        const RandomFn other = MakeSecureRandom();
        EXPECT_NE(GenerateToken(random), GenerateToken(other));
    }

    TEST(RoomCodeTest, WellFormedTokenCases) {
        EXPECT_TRUE(IsWellFormedToken("0123456789abcdef0123456789abcdef"));
        EXPECT_FALSE(IsWellFormedToken(""));
        EXPECT_FALSE(IsWellFormedToken("0123456789abcdef0123456789abcde"));
        EXPECT_FALSE(IsWellFormedToken("0123456789abcdef0123456789abcdef0"));
        EXPECT_FALSE(IsWellFormedToken("0123456789ABCDEF0123456789ABCDEF"));
        EXPECT_FALSE(IsWellFormedToken("0123456789abcdef0123456789abcdeg"));
        EXPECT_FALSE(IsWellFormedToken("0123456789abcdef0123456789abcde "));
        EXPECT_FALSE(IsWellFormedToken(StringView("0123456789abcdef0123456789abcd\0e", 32)));
    }

}  // namespace BECore::Tests
