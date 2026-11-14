#include <BECore/Utils/Base64.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        std::string Encode(StringView text) {
            const String encoded = Base64Encode(Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
            return std::string(encoded.c_str(), encoded.size());
        }

        std::string Encode(std::initializer_list<uint8_t> bytes) {
            const Vector<uint8_t> data(bytes);
            const String encoded = Base64Encode(Span<const uint8_t>(data.data(), data.size()));
            return std::string(encoded.c_str(), encoded.size());
        }

    }  // namespace

    TEST(Base64Test, Rfc4648TestVectors) {
        EXPECT_EQ(Encode(""), "");
        EXPECT_EQ(Encode("f"), "Zg==");
        EXPECT_EQ(Encode("fo"), "Zm8=");
        EXPECT_EQ(Encode("foo"), "Zm9v");
        EXPECT_EQ(Encode("foob"), "Zm9vYg==");
        EXPECT_EQ(Encode("fooba"), "Zm9vYmE=");
        EXPECT_EQ(Encode("foobar"), "Zm9vYmFy");
    }

    TEST(Base64Test, UsesTheStandardAlphabetForHighBits) {
        EXPECT_EQ(Encode({0xFF, 0xFE, 0xFD}), "//79");  // '/' and '+' come from the top of the alphabet
        EXPECT_EQ(Encode({0xFB, 0xEF, 0xBE}), "++++");
        EXPECT_EQ(Encode({0x00, 0x00, 0x00}), "AAAA");
        EXPECT_EQ(Encode({0xFF}), "/w==");
    }

    TEST(Base64Test, EmptyInputGivesAnEmptyString) {
        const String encoded = Base64Encode(Span<const uint8_t>());

        EXPECT_TRUE(encoded.empty());
    }

    TEST(Base64Test, EncodesAllByteValues) {
        Vector<uint8_t> data;
        for (int value = 0; value < 256; ++value) {
            data.push_back(static_cast<uint8_t>(value));
        }

        const String encoded = Base64Encode(Span<const uint8_t>(data.data(), data.size()));

        EXPECT_EQ(encoded.size(), (256 + 2) / 3 * 4);
        EXPECT_EQ(encoded.substr(0, 8), String("AAECAwQF"));
        EXPECT_EQ(encoded.substr(encoded.size() - 8, 4), String("/P3+"));  // bytes 252, 253, 254
        EXPECT_EQ(encoded.substr(encoded.size() - 4), String("/w=="));     // the last byte, 255, with padding
    }

}  // namespace BECore::Tests
