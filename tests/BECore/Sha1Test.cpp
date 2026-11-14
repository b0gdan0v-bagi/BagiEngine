#include <RemoteControl/Sha1.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        std::string Hex(const Sha1Digest& digest) {
            constexpr std::string_view kDigits = "0123456789abcdef";
            std::string text;
            for (const uint8_t byte : digest) {
                text.push_back(kDigits[byte >> 4]);
                text.push_back(kDigits[byte & 0x0F]);
            }
            return text;
        }

        std::string Sha1Hex(std::string_view text) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
            return Hex(Sha1(Span<const uint8_t>(bytes, text.size())));
        }

        std::string Sha1OfRepeated(char letter, size_t count) {
            return Sha1Hex(std::string(count, letter));
        }

    }  // namespace

    // Vectors of FIPS 180 and the well-known pangram
    TEST(Sha1Test, EmptyInput) {
        EXPECT_EQ(Sha1Hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    }

    TEST(Sha1Test, Abc) {
        EXPECT_EQ(Sha1Hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    }

    TEST(Sha1Test, TwoBlockMessageOfFips180) {
        EXPECT_EQ(Sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    }

    TEST(Sha1Test, Pangram) {
        EXPECT_EQ(Sha1Hex("The quick brown fox jumps over the lazy dog"), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
    }

    TEST(Sha1Test, OneMillionLetters) {
        EXPECT_EQ(Sha1OfRepeated('a', 1000000), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    }

    // The padding needs a second block from 56 bytes of the last block on; the lengths around 64 and 128 are the other edges
    TEST(Sha1Test, LengthsAroundTheBlockBoundaries) {
        EXPECT_EQ(Sha1OfRepeated('a', 1), "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");
        EXPECT_EQ(Sha1OfRepeated('a', 54), "b05d71c64979cb95fa74a33cdb31a40d258ae02e");
        EXPECT_EQ(Sha1OfRepeated('a', 55), "c1c8bbdc22796e28c0e15163d20899b65621d65a");
        EXPECT_EQ(Sha1OfRepeated('a', 56), "c2db330f6083854c99d4b5bfb6e8f29f201be699");
        EXPECT_EQ(Sha1OfRepeated('a', 57), "f08f24908d682555111be7ff6f004e78283d989a");
        EXPECT_EQ(Sha1OfRepeated('a', 63), "03f09f5b158a7a8cdad920bddc29b81c18a551f5");
        EXPECT_EQ(Sha1OfRepeated('a', 64), "0098ba824b5c16427bd7a1122a5a442a25ec644d");
        EXPECT_EQ(Sha1OfRepeated('a', 65), "11655326c708d70319be2610e8a57d9a5b959d3b");
        EXPECT_EQ(Sha1OfRepeated('a', 119), "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56");
        EXPECT_EQ(Sha1OfRepeated('a', 120), "f34c1488385346a55709ba056ddd08280dd4c6d6");
        EXPECT_EQ(Sha1OfRepeated('a', 127), "89d95fa32ed44a7c610b7ee38517ddf57e0bb975");
        EXPECT_EQ(Sha1OfRepeated('a', 128), "ad5b3fdbcb526778c2839d2f151ea753995e26a0");
    }

}  // namespace BECore::Tests
