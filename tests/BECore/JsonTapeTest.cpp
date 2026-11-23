// JsonTape (Control/JsonTape.h): the tokens of a text and the grammar it refuses. The grammar is the one of `ParseJson` (the DOM it replaces),
// so the accepted and refused texts below are checked against it as well.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Printable(StringView text) {
            return String(text.data(), text.size());
        }

        bool Accepts(StringView text) {
            JsonTape tape;
            return tape.Parse(text).has_value();
        }

        /// Both the tape and the DOM refuse @p text, and the tape is empty afterwards
        void ExpectRefused(StringView text) {
            JsonTape tape;
            const auto result = tape.Parse(text);
            EXPECT_FALSE(result.has_value()) << Printable(text);
            EXPECT_TRUE(tape.Empty()) << Printable(text);
            EXPECT_FALSE(ParseJson(text).has_value()) << "the DOM accepts " << Printable(text);
        }

        void ExpectAccepted(StringView text) {
            EXPECT_TRUE(Accepts(text)) << Printable(text);
            EXPECT_TRUE(ParseJson(text).has_value()) << "the DOM refuses " << Printable(text);
        }

        StringView Slice(const JsonTape& tape, size_t index) {
            const JsonToken& token = tape.Tokens()[index];
            return tape.Text().substr(token.offset, token.length);
        }

        String Nested(size_t depth) {
            String text;
            text.append(depth, '[');
            text.append(depth, ']');
            return text;
        }

    }  // namespace

    TEST(JsonTapeTest, ScalarsHaveTheirKindAndTheirText) {
        struct Case {
            StringView text;
            JsonKind kind;
            StringView slice;
        };
        const Array<Case, 11> cases = {{
            {"null", JsonKind::Null, "null"},
            {"true", JsonKind::Bool, "true"},
            {"false", JsonKind::Bool, "false"},
            {"0", JsonKind::Integer, "0"},
            {"-0", JsonKind::Integer, "-0"},
            {"-12", JsonKind::Integer, "-12"},
            {"12345678901234567890123", JsonKind::Integer, "12345678901234567890123"},
            {"1.5", JsonKind::Number, "1.5"},
            {"-2e3", JsonKind::Number, "-2e3"},
            {"1E+2", JsonKind::Number, "1E+2"},
            {"1e999", JsonKind::Number, "1e999"},
        }};
        for (const Case& c : cases) {
            JsonTape tape;
            ASSERT_TRUE(tape.Parse(c.text).has_value()) << Printable(c.text);
            ASSERT_EQ(tape.Size(), 1U) << Printable(c.text);
            EXPECT_EQ(tape.Tokens()[0].kind, c.kind) << Printable(c.text);
            EXPECT_EQ(Slice(tape, 0), c.slice);
            EXPECT_EQ(tape.Tokens()[0].next, 1U);
        }
    }

    TEST(JsonTapeTest, StringContentIsBetweenTheQuotesAndRaw) {
        JsonTape tape;
        const StringView text = R"(  "a\nb"  )";
        ASSERT_TRUE(tape.Parse(text).has_value());
        ASSERT_EQ(tape.Size(), 1U);
        EXPECT_EQ(tape.Tokens()[0].kind, JsonKind::String);
        EXPECT_EQ(Slice(tape, 0), R"(a\nb)");
        EXPECT_EQ(tape.Tokens()[0].flags & JsonToken::kHasEscape, JsonToken::kHasEscape);

        ASSERT_TRUE(tape.Parse(R"("plain")").has_value());
        EXPECT_EQ(tape.Tokens()[0].flags, 0U);
        ASSERT_TRUE(tape.Parse(R"("")").has_value());
        EXPECT_EQ(tape.Tokens()[0].length, 0U);
    }

    TEST(JsonTapeTest, ContainersCoverTheirSpanAndCountTheirChildren) {
        JsonTape tape;
        const StringView text = R"({"a":[1,[2,3],{}],"b":{"c":null},"d":[]})";
        ASSERT_TRUE(tape.Parse(text).has_value());
        const auto& t = tape.Tokens();
        // 0 root, 1 "a", 2 [..], 3 1, 4 [2,3], 5 2, 6 3, 7 {}, 8 "b", 9 {..}, 10 "c", 11 null, 12 "d", 13 []
        ASSERT_EQ(tape.Size(), 14U);
        EXPECT_EQ(t[0].kind, JsonKind::Object);
        EXPECT_EQ(t[0].count, 3U);
        EXPECT_EQ(t[0].next, 14U);
        EXPECT_EQ(Slice(tape, 0), text);

        EXPECT_EQ(t[1].kind, JsonKind::String);
        EXPECT_EQ(t[1].next, 2U) << "a key's next is its value";
        EXPECT_EQ(t[2].kind, JsonKind::Array);
        EXPECT_EQ(t[2].count, 3U);
        EXPECT_EQ(t[2].next, 8U);
        EXPECT_EQ(Slice(tape, 2), R"([1,[2,3],{}])");
        EXPECT_EQ(t[4].count, 2U);
        EXPECT_EQ(t[4].next, 7U);
        EXPECT_EQ(Slice(tape, 4), "[2,3]");
        EXPECT_EQ(t[7].kind, JsonKind::Object);
        EXPECT_EQ(t[7].count, 0U);
        EXPECT_EQ(t[7].next, 8U);
        EXPECT_EQ(Slice(tape, 7), "{}");
        EXPECT_EQ(t[9].count, 1U);
        EXPECT_EQ(t[9].next, 12U);
        EXPECT_EQ(t[13].count, 0U);
        EXPECT_EQ(t[13].next, 14U);
    }

    TEST(JsonTapeTest, EmptyContainersAndWhitespace) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse("[]").has_value());
        EXPECT_EQ(tape.Tokens()[0].count, 0U);
        ASSERT_TRUE(tape.Parse("{}").has_value());
        EXPECT_EQ(tape.Tokens()[0].count, 0U);
        ASSERT_TRUE(tape.Parse(" \t\r\n[ \t\r\n1 \n, \r2\t]\n ").has_value());
        EXPECT_EQ(tape.Tokens()[0].count, 2U);
        ASSERT_TRUE(tape.Parse("{ \"a\" \n : \t1 , \"b\":2 }").has_value());
        EXPECT_EQ(tape.Tokens()[0].count, 2U);
        EXPECT_FALSE(Accepts("\f1")) << "a form feed is no whitespace";
        EXPECT_FALSE(Accepts("\v1"));
    }

    TEST(JsonTapeTest, LegalEscapesAreAccepted) {
        ExpectAccepted(R"("\" \\ \/ \b \f \n \r \t")");
        ExpectAccepted(R"("éé\u0000€")");
        ExpectAccepted(R"("😀")");
        ExpectAccepted("\"\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"");
        ExpectAccepted("\"\x7F\"");
    }

    TEST(JsonTapeTest, IllegalEscapesAreRefused) {
        ExpectRefused(R"("\a")");
        ExpectRefused(R"("\x41")");
        ExpectRefused(R"("\'")");
        ExpectRefused(R"("\u12")");
        ExpectRefused(R"("\u12G4")");
        ExpectRefused(R"("\)");
        ExpectRefused(R"("\u")");
        ExpectRefused(R"("\ud83d")");
        ExpectRefused(R"("\ud83dx")");
        ExpectRefused(R"("\ud83dA")");
        ExpectRefused(R"("\ud83d\ud83d")");
        ExpectRefused(R"("\ude00")");
        ExpectRefused(R"("\ude00\ud83d")");
    }

    TEST(JsonTapeTest, ControlCharactersInAStringAreRefused) {
        ExpectRefused(String("\"a\nb\""));
        ExpectRefused(String("\"a\tb\""));
        ExpectRefused(String("\"a\x01"
                             "b\""));
        const char raw[] = {'"', 'a', '\0', 'b', '"'};
        ExpectRefused(StringView(raw, sizeof(raw)));
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(R"("a\u0000b")").has_value());
        EXPECT_EQ(Slice(tape, 0), R"(a\u0000b)");
    }

    TEST(JsonTapeTest, InvalidUtf8InAStringIsRefused) {
        ExpectRefused("\"\x80\"");              // a continuation byte alone
        ExpectRefused("\"\xC0\x80\"");          // overlong
        ExpectRefused("\"\xC1\xBF\"");          // overlong
        ExpectRefused("\"\xC3\"");              // truncated
        ExpectRefused("\"\xC3\x28\"");          // bad continuation
        ExpectRefused("\"\xE0\x80\x80\"");      // overlong
        ExpectRefused("\"\xED\xA0\x80\"");      // an encoded surrogate
        ExpectRefused("\"\xE2\x82\"");          // truncated
        ExpectRefused("\"\xF0\x80\x80\x80\"");  // overlong
        ExpectRefused("\"\xF4\x90\x80\x80\"");  // past U+10FFFF
        ExpectRefused("\"\xF5\x80\x80\x80\"");
        ExpectRefused("\"\xFF\"");
        ExpectRefused("\"\xC3");  // truncated at the end of the text
        ExpectAccepted("\"\xF4\x8F\xBF\xBF\"");
        ExpectAccepted("\"\xED\x9F\xBF\"");
    }

    TEST(JsonTapeTest, NumberGrammar) {
        for (const StringView text : {"0", "-0", "7", "10", "-10", "0.5", "1.0", "1e5", "1E5", "1e+5", "1e-5", "0e0", "-0.0e-0", "123456789012345678901234567890", "2.5E10"}) {
            ExpectAccepted(text);
        }
        for (const StringView text : {"01", "00", "-01", "1.", ".5", "-", "-.5", "1e", "1e+", "1E-", "+1", "0x10", "1.e5", "1..2", "1e5.5", "--1", "- 1", "1 2", "Infinity", "NaN", "-NaN", "1_0"}) {
            ExpectRefused(text);
        }
    }

    TEST(JsonTapeTest, ANumberBeyondADoubleIsStillANumber) {
        // The DOM refuses it; the tape leaves the judgement to the reader that knows the target (JsonView)
        EXPECT_TRUE(Accepts("1e999"));
        EXPECT_TRUE(Accepts("-1e999"));
    }

    TEST(JsonTapeTest, LiteralsAreExact) {
        for (const StringView text : {"tru", "nulll", "True", "NULL", "fals", "falsee", "nul", "t", "n", "f", "truefalse", "nullnull"}) {
            ExpectRefused(text);
        }
    }

    TEST(JsonTapeTest, StructureErrors) {
        for (const StringView text : {"",
                                      " ",
                                      "\n\t",
                                      "[1,]",
                                      "[,1]",
                                      "[1 2]",
                                      "[1,,2]",
                                      "{\"a\":1,}",
                                      "{,}",
                                      "{\"a\"}",
                                      "{\"a\" 1}",
                                      "{\"a\":}",
                                      "{\"a\":1 \"b\":2}",
                                      "{1:2}",
                                      "{\"a\":1",
                                      "[1",
                                      "[",
                                      "{",
                                      "]",
                                      "}",
                                      "[}",
                                      "{]",
                                      "[1}",
                                      "{\"a\":1]",
                                      "[1]]",
                                      "{}}",
                                      "[] []",
                                      "1 1",
                                      "\"a\" x",
                                      "{'a':1}",
                                      "[1;2]",
                                      "// c\n1",
                                      "/* c */1",
                                      "[1] // c",
                                      "{\"a\":1,\"b\"}",
                                      "\"abc",
                                      "{\"a",
                                      "{\"a\":",
                                      "[\"a\","}) {
            ExpectRefused(text);
        }
    }

    TEST(JsonTapeTest, AByteOrderMarkIsRefused) {
        EXPECT_FALSE(Accepts("\xEF\xBB\xBF"
                             "1"));
    }

    TEST(JsonTapeTest, AnErrorNamesTheOffset) {
        JsonTape tape;
        const auto result = tape.Parse("[1, 2 x]");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().offset, 6U);
        EXPECT_FALSE(result.error().message.empty());

        const auto empty = tape.Parse("");
        ASSERT_FALSE(empty.has_value());
        EXPECT_EQ(empty.error().offset, 0U);

        const auto trailing = tape.Parse("{} x");
        ASSERT_FALSE(trailing.has_value());
        EXPECT_EQ(trailing.error().offset, 3U);
    }

    TEST(JsonTapeTest, NestingIsBoundedByTheLimit) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(Nested(ReaderLimits::kMaxJsonNesting)).has_value());
        EXPECT_EQ(tape.Size(), ReaderLimits::kMaxJsonNesting);
        EXPECT_EQ(tape.Tokens()[0].count, 1U);
        EXPECT_EQ(tape.Tokens()[0].next, ReaderLimits::kMaxJsonNesting);

        const auto past = tape.Parse(Nested(ReaderLimits::kMaxJsonNesting + 1));
        EXPECT_FALSE(past.has_value());
        EXPECT_TRUE(tape.Empty());

        // Deeper than any stack could take as recursion: refused, never a crash
        EXPECT_FALSE(Accepts(Nested(1000000)));
        String objects;
        for (size_t i = 0; i < ReaderLimits::kMaxJsonNesting + 1; ++i) {
            objects.append("{\"a\":");
        }
        EXPECT_FALSE(Accepts(objects));
    }

    TEST(JsonTapeTest, TheTapeIsEmptyAfterAFailureAndReusable) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse("[1,2,3]").has_value());
        EXPECT_EQ(tape.Size(), 4U);
        EXPECT_FALSE(tape.Parse("[1,2,").has_value());
        EXPECT_TRUE(tape.Empty());
        EXPECT_TRUE(tape.Text().empty());
        ASSERT_TRUE(tape.Parse("{\"a\":true}").has_value());
        EXPECT_EQ(tape.Size(), 3U);
        EXPECT_EQ(tape.Text(), "{\"a\":true}");
    }

    TEST(JsonTapeTest, ASecondParseOfTheSameShapeAllocatesNothing) {
        JsonTape tape;
        const StringView first = R"({"id":1,"method":"perform_action","params":{"a":[1,2,3],"b":"x\ny","c":null}})";
        const StringView second = R"({"id":2,"method":"perform_action","params":{"a":[4,5,6],"b":"q\nr","c":null}})";
        ASSERT_TRUE(tape.Parse(first).has_value());
        ExpectNoAllocations([&] { EXPECT_TRUE(tape.Parse(second).has_value()); });
        EXPECT_EQ(tape.Size(), 16U);
    }

    TEST(JsonTapeTest, AFailedParseAllocatesNothingEither) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse("[1,2,3]").has_value());
        ExpectNoAllocations([&] { EXPECT_FALSE(tape.Parse("[1,2,").has_value()); });
    }

}  // namespace BECore::Tests
