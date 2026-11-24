// WriteDoubleText (Control/JsonDoubleText.h): the text of a double is a contract of every control answer, replay file and room message.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDoubleText.h>
#include <bit>
#include <cfloat>
#include <cmath>
#include <gtest/gtest.h>
#include <random>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kSeed = 0xD0B1E7E57ULL;
        constexpr size_t kRandomBitPatterns = 100000;

        String TextOf(double value) {
            Array<char, kMaxDoubleTextBytes> buffer{};
            return String(buffer.data(), WriteDoubleText(buffer.data(), value));
        }

        /// What the library the routine was ported from writes
        String ReferenceTextOf(double value) {
            const std::string text = Json(value).dump();  //-NoStd: the reference is nlohmann's own dump
            return String(text.c_str(), text.size());
        }

        struct Pin {
            double value;
            const char* text;
        };

    }  // namespace

    TEST(JsonDoubleTextTest, LiteralPins) {
        const Pin pins[] = {
            {0.0, "0.0"},
            {-0.0, "-0.0"},
            {1.0, "1.0"},
            {-1.5, "-1.5"},
            {0.1, "0.1"},
            {0.30000000000000004, "0.30000000000000004"},
            {1e-4, "0.0001"},
            {1e-5, "1e-05"},
            {1e-7, "1e-07"},
            {1e14, "100000000000000.0"},
            {1e15, "1e+15"},
            {1e21, "1e+21"},
            {123456789012345680.0, "1.2345678901234568e+17"},
            {9007199254740992.0, "9.007199254740992e+15"},
            {9007199254740991.0, "9.007199254740991e+15"},
            {9007199254740993.0, "9.007199254740992e+15"},
            {5e-324, "5e-324"},
            {DBL_MAX, "1.7976931348623157e+308"},
            {-DBL_MAX, "-1.7976931348623157e+308"},
            {DBL_MIN, "2.2250738585072014e-308"},
        };
        for (const Pin& pin : pins) {
            EXPECT_EQ(TextOf(pin.value), String(pin.text)) << pin.text;
        }
    }

    TEST(JsonDoubleTextTest, EveryPowerOfTenAndTwoMatchesTheLibrary) {
        for (int exponent = -323; exponent <= 308; ++exponent) {
            const double value = std::pow(10.0, exponent);
            ASSERT_EQ(TextOf(value), ReferenceTextOf(value)) << "10^" << exponent;
        }
        for (int exponent = -1074; exponent <= 1023; ++exponent) {
            const double value = std::ldexp(1.0, exponent);
            ASSERT_EQ(TextOf(value), ReferenceTextOf(value)) << "2^" << exponent;
            ASSERT_EQ(TextOf(-value), ReferenceTextOf(-value)) << "-2^" << exponent;
        }
    }

    TEST(JsonDoubleTextTest, RandomBitPatternsMatchTheLibrary) {
        std::mt19937_64 random(kSeed);
        size_t checked = 0;
        for (size_t n = 0; n < kRandomBitPatterns; ++n) {
            const double value = std::bit_cast<double>(random());
            if (!std::isfinite(value)) {
                continue;
            }
            ++checked;
            ASSERT_EQ(TextOf(value), ReferenceTextOf(value)) << "case " << n;
        }
        EXPECT_GT(checked, kRandomBitPatterns * 9 / 10);
    }

}  // namespace BECore::Tests
