// WriteDoubleText (Control/JsonDoubleText.h): the text of a double is a contract of every control answer, replay file and room message.

#include <BECore/Control/JsonDoubleText.h>
#include <cfloat>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String TextOf(double value) {
            Array<char, kMaxDoubleTextBytes> buffer{};
            return String(buffer.data(), WriteDoubleText(buffer.data(), value));
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

}  // namespace BECore::Tests
