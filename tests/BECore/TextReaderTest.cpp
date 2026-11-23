#include <BECore/File/TextReader.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        Vector<String> AllLines(StringView text) {
            Vector<String> lines;
            TextReader reader(text);
            while (const auto line = reader.ReadLine()) {
                lines.push_back(String(*line));
            }
            return lines;
        }

    }  // namespace

    TEST(TextReaderTest, LinesEndAtNewlineAndDropTheCarriageReturnBeforeIt) {
        EXPECT_EQ(AllLines("a\nb\n"), (Vector<String>{"a", "b"}));
        EXPECT_EQ(AllLines("a\r\nb\r\n"), (Vector<String>{"a", "b"}));
    }

    TEST(TextReaderTest, LastLineWithoutNewlineIsALineAndItsCarriageReturnStays) {
        EXPECT_EQ(AllLines("a\nb"), (Vector<String>{"a", "b"}));
        EXPECT_EQ(AllLines("a\r"), (Vector<String>{"a\r"}));
    }

    TEST(TextReaderTest, EmptyLinesAreLinesAndEmptyTextHasNone) {
        EXPECT_EQ(AllLines("a\n\n\nb\n"), (Vector<String>{"a", "", "", "b"}));
        EXPECT_EQ(AllLines("\r\n"), (Vector<String>{""}));
        EXPECT_TRUE(AllLines("").empty());
    }

    TEST(TextReaderTest, TokensSkipMixedWhiteSpaceAndEndWithNothing) {
        TextReader reader(" \t alpha\r\n\v beta\f\n  gamma  ");

        EXPECT_EQ(reader.ReadToken(), "alpha");
        EXPECT_EQ(reader.ReadToken(), "beta");
        EXPECT_EQ(reader.ReadToken(), "gamma");
        EXPECT_FALSE(reader.AtEnd());
        EXPECT_FALSE(reader.ReadToken().has_value());
        EXPECT_EQ(reader.Rest(), "  ");
    }

    TEST(TextReaderTest, LinesAndTokensShareOnePosition) {
        TextReader reader("head 1 2\nsecond line\n");

        EXPECT_EQ(reader.ReadToken(), "head");
        EXPECT_EQ(reader.ReadLine(), " 1 2");
        EXPECT_EQ(reader.ReadLine(), "second line");
        EXPECT_TRUE(reader.AtEnd());
        EXPECT_FALSE(reader.ReadLine().has_value());
    }

    TEST(TextReaderTest, IntegersReadValidAndNegativeTokens) {
        TextReader reader("12 -7  0\n42");

        EXPECT_EQ(reader.ReadInt<int>(), 12);
        EXPECT_EQ(reader.ReadInt<int>(), -7);
        EXPECT_EQ(reader.ReadInt<int64_t>(), 0);
        EXPECT_EQ(reader.ReadInt<uint8_t>(), 42u);
        EXPECT_FALSE(reader.ReadInt<int>().has_value());
    }

    TEST(TextReaderTest, BadOrOutOfRangeTokenGivesNothingAndKeepsThePosition) {
        TextReader reader("300 12abc -1 99999999999 word");

        EXPECT_FALSE(reader.ReadInt<uint8_t>().has_value());
        EXPECT_EQ(reader.Rest(), "300 12abc -1 99999999999 word");
        EXPECT_EQ(reader.ReadInt<int>(), 300);
        EXPECT_FALSE(reader.ReadInt<int>().has_value());
        EXPECT_EQ(reader.ReadToken(), "12abc");
        EXPECT_FALSE(reader.ReadInt<uint32_t>().has_value());
        EXPECT_EQ(reader.ReadToken(), "-1");
        EXPECT_FALSE(reader.ReadInt<int32_t>().has_value());
        EXPECT_EQ(reader.ReadToken(), "99999999999");
        EXPECT_FALSE(reader.ReadInt<int>().has_value());
        EXPECT_EQ(reader.Rest(), " word");
    }

    TEST(TextReaderTest, FloatsReadDecimalAndExponentForms) {
        TextReader reader("1.5 -0.25 3e2 1e999 .5x");

        EXPECT_FLOAT_EQ(reader.ReadFloat<float>().value_or(0.0f), 1.5f);
        EXPECT_DOUBLE_EQ(reader.ReadFloat<double>().value_or(0.0), -0.25);
        EXPECT_DOUBLE_EQ(reader.Read<double>().value_or(0.0), 300.0);
        EXPECT_FALSE(reader.ReadFloat<double>().has_value());
        EXPECT_EQ(reader.ReadToken(), "1e999");
        EXPECT_FALSE(reader.ReadFloat<float>().has_value());
    }

    TEST(TextReaderTest, AnIntegerReaderRefusesAFractionAndAFloatReaderTakesAnInteger) {
        TextReader reader("2.5 7");

        EXPECT_FALSE(reader.ReadInt<int>().has_value());
        EXPECT_FLOAT_EQ(reader.ReadFloat<float>().value_or(0.0f), 2.5f);
        EXPECT_FLOAT_EQ(reader.ReadFloat<float>().value_or(0.0f), 7.0f);
    }

}  // namespace BECore::Tests
