#include <BECore/Reflection/ReaderLimits.h>
#include <BECore/Reflection/StrictText.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using StrictText::Status;

    namespace {

        template <typename T>
        Status ParseInto(StringView text, T& value) {
            return StrictText::Parse(text, value);
        }

    }  // namespace

    TEST(StrictTextTest, BoolSpellings) {
        for (const StringView text : {"true", "1", " true ", "\t1\n"}) {
            bool value = false;
            EXPECT_EQ(ParseInto(text, value), Status::Ok) << String(text).c_str();
            EXPECT_TRUE(value) << String(text).c_str();
        }
        for (const StringView text : {"false", "0", " false"}) {
            bool value = true;
            EXPECT_EQ(ParseInto(text, value), Status::Ok) << String(text).c_str();
            EXPECT_FALSE(value) << String(text).c_str();
        }
        for (const StringView text : {"", "yes", "True", "2", "tru", "1 0"}) {
            bool value = true;
            EXPECT_EQ(ParseInto(text, value), Status::Malformed) << String(text).c_str();
            EXPECT_TRUE(value) << "a refused text keeps the value: " << String(text).c_str();
        }
    }

    TEST(StrictTextTest, IntegerTakesWholeTextOnly) {
        int32_t value = 7;
        EXPECT_EQ(ParseInto("42", value), Status::Ok);
        EXPECT_EQ(value, 42);
        EXPECT_EQ(ParseInto("  +5  ", value), Status::Ok);
        EXPECT_EQ(value, 5);
        EXPECT_EQ(ParseInto("-9", value), Status::Ok);
        EXPECT_EQ(value, -9);

        value = 7;
        for (const StringView text : {"", " ", "+", "abc", "12abc", "0x10", "1.5", "+-1", "++1", "1 2"}) {
            EXPECT_EQ(ParseInto(text, value), Status::Malformed) << String(text).c_str();
        }
        EXPECT_EQ(value, 7);
    }

    TEST(StrictTextTest, RangeIsTheTargetTypes) {
        uint16_t port = 1;
        EXPECT_EQ(ParseInto("65535", port), Status::Ok);
        EXPECT_EQ(port, 65535);
        EXPECT_EQ(ParseInto("65536", port), Status::OutOfRange);
        EXPECT_EQ(ParseInto("-1", port), Status::Malformed);
        EXPECT_EQ(port, 65535);

        int8_t small = 0;
        EXPECT_EQ(ParseInto("-128", small), Status::Ok);
        EXPECT_EQ(ParseInto("128", small), Status::OutOfRange);
    }

    TEST(StrictTextTest, FloatingPointRefusesNonFinite) {
        float value = 1.0f;
        EXPECT_EQ(ParseInto(" 2.5 ", value), Status::Ok);
        EXPECT_FLOAT_EQ(value, 2.5f);
        EXPECT_EQ(ParseInto("+1e2", value), Status::Ok);
        EXPECT_FLOAT_EQ(value, 100.0f);

        value = 1.0f;
        for (const StringView text : {"nan", "inf", "-inf", "infinity", "abc", "1.5x", ""}) {
            EXPECT_EQ(ParseInto(text, value), Status::Malformed) << String(text).c_str();
        }
        EXPECT_EQ(ParseInto("1e99", value), Status::OutOfRange);
        EXPECT_FLOAT_EQ(value, 1.0f);
    }

    TEST(StrictTextTest, MessageNamesKindAndReason) {
        EXPECT_EQ(StrictText::InvalidValueMessage<bool>("maybe", "maybe", Status::Malformed), "Invalid boolean value 'maybe'");
        EXPECT_EQ(StrictText::InvalidValueMessage<uint16_t>("70000", "70000", Status::OutOfRange), "Invalid integer value '70000' (out of range)");
        EXPECT_EQ(StrictText::InvalidValueMessage<double>("x", "x", Status::Malformed), "Invalid number value 'x'");
    }

    TEST(StrictTextTest, MessageMarksACutValue) {
        const String text(ReaderLimits::kMaxQuotedValueBytes * 3, 'a');
        const StringView shown(text.data(), ReaderLimits::kMaxQuotedValueBytes);
        const String message = StrictText::InvalidValueMessage<int32_t>(StringView(text), shown, Status::Malformed);
        EXPECT_EQ(message, Format("Invalid integer value '{}...'", shown));
    }

}  // namespace BECore::Tests
