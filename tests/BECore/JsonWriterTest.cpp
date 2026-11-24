// JsonWriter (Control/JsonWriter.h): the bytes it writes, pinned as literal texts. The exhaustive and the random cases pin a digest of
// everything written.

#include <BECore/Assert/AssertEvent.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
#include <Common/JsonDoc.h>
#include <bit>
#include <cfloat>
#include <gtest/gtest.h>
#include <limits>
#include <random>

namespace BECore::Tests {

    namespace {

        constexpr Array<int, 5> kIndents = {JsonWriter::kCompact, 0, 1, 2, 4};
        constexpr uint64_t kSeed = 0x5EEDB0B5ULL;
        constexpr size_t kRandomDoubles = 100000;
        constexpr size_t kRandomStrings = 20000;
        constexpr size_t kRandomFloats = 20000;

        /// U+FFFD, which stands for each `@` of an expected text (no test text has an `@` of its own)
        constexpr StringView kReplacement = "\xEF\xBF\xBD";

        template <typename TWrite>
        String Written(int indent, TWrite&& write) {
            String out;
            JsonWriter writer(out, indent);
            write(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        String Replaced(StringView pattern) {
            String out;
            for (const char c : pattern) {
                if (c == '@') {
                    out.append(kReplacement.data(), kReplacement.size());
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }

        /// The text as a string value is @p expected in every layout (a scalar has no layout); an `@` of @p expected is U+FFFD
        void ExpectString(StringView text, StringView expected) {
            for (const int indent : kIndents) {
                const String written = Written(indent, [&](JsonWriter& w) { w.Value(text); });
                ASSERT_EQ(written, Replaced(expected)) << "a string of " << text.size() << " bytes, first byte " << (text.empty() ? 0 : static_cast<uint8_t>(text[0]));
            }
        }

        template <typename T>
        void ExpectNumber(T value, StringView expected) {
            for (const int indent : kIndents) {
                const String written = Written(indent, [&](JsonWriter& w) { w.Value(value); });
                ASSERT_EQ(written, String(expected.data(), expected.size()));
            }
        }

        /// FNV-1a over what was written, with a NUL (which no written text holds: it is escaped) between the texts
        class Digest {
        public:
            void Add(const String& text) {
                for (const char c : text) {
                    Mix(static_cast<uint8_t>(c));
                }
                Mix(0);
            }

            [[nodiscard]] uint64_t Value() const {
                return _hash;
            }

        private:
            void Mix(uint8_t byte) {
                _hash = (_hash ^ byte) * 0x100000001B3ULL;
            }

            uint64_t _hash = 0xCBF29CE484222325ULL;
        };

        String Bytes(std::initializer_list<uint8_t> bytes) {
            String text;
            for (const uint8_t byte : bytes) {
                text.push_back(static_cast<char>(byte));
            }
            return text;
        }

        /// {"a":[1,[],{}],"b":{"c":"x","d":[true,null,1.5]},"e":{},"f":[]}: nested empty and filled containers
        template <typename TWriter>
        void WriteNested(TWriter& w) {
            w.BeginObject();
            w.Key("a");
            w.BeginArray();
            w.Value(1);
            w.BeginArray();
            w.EndArray();
            w.BeginObject();
            w.EndObject();
            w.EndArray();
            w.Key("b");
            w.BeginObject();
            w.Member("c", "x");
            w.Key("d");
            w.BeginArray();
            w.Value(true);
            w.Null();
            w.Value(1.5);
            w.EndArray();
            w.EndObject();
            w.Key("e");
            w.BeginObject();
            w.EndObject();
            w.Key("f");
            w.BeginArray();
            w.EndArray();
            w.EndObject();
        }

        /// The text of a control character 0 to 31 inside a string
        constexpr Array<StringView, 32> kControlEscapes = {R"(\u0000)", R"(\u0001)", R"(\u0002)", R"(\u0003)", R"(\u0004)", R"(\u0005)", R"(\u0006)", R"(\u0007)",
                                                           R"(\b)",     R"(\t)",     R"(\n)",     R"(\u000b)", R"(\f)",     R"(\r)",     R"(\u000e)", R"(\u000f)",
                                                           R"(\u0010)", R"(\u0011)", R"(\u0012)", R"(\u0013)", R"(\u0014)", R"(\u0015)", R"(\u0016)", R"(\u0017)",
                                                           R"(\u0018)", R"(\u0019)", R"(\u001a)", R"(\u001b)", R"(\u001c)", R"(\u001d)", R"(\u001e)", R"(\u001f)"};

    }  // namespace

    TEST(JsonWriterTest, EscapesAndControlCharactersAreWrittenAsJsonSays) {
        ExpectString("", R"("")");
        ExpectString("plain text 123", R"("plain text 123")");
        ExpectString("quote \" backslash \\ slash / tab \t newline \n return \r backspace \b formfeed \f", R"("quote \" backslash \\ slash / tab \t newline \n return \r backspace \b formfeed \f")");
        ExpectString("\x7f DEL stays, \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80 and \xEF\xBF\xBD stay as they are",
                     "\"\x7f DEL stays, \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80 and \xEF\xBF\xBD stay as they are\"");
        for (int c = 0; c < 0x20; ++c) {
            const char single[] = {static_cast<char>(c), 0};
            const StringView escape = kControlEscapes[static_cast<size_t>(c)];
            ExpectString(StringView(single, 1), Format("\"{}\"", escape));
            ExpectString(Format("a{}b", StringView(single, 1)), Format("\"a{}b\"", escape));
        }
        String longText;
        String longExpected = "\"";
        for (int i = 0; i < 4000; ++i) {
            longText.append("line \"");
            longText.push_back(static_cast<char>(i % 0x20));
            longText.append("\" \xC3\xA9\n");
            const StringView escape = kControlEscapes[static_cast<size_t>(i % 0x20)];
            longExpected.append("line \\\"");
            longExpected.append(escape.data(), escape.size());
            longExpected.append("\\\" \xC3\xA9\\n");
        }
        longExpected.push_back('"');
        ExpectString(longText, longExpected);
    }

    TEST(JsonWriterTest, HexEscapesAreLowerCase) {
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Value(StringView("\x1f\x0b", 2)); }), String("\"\\u001f\\u000b\""));
    }

    TEST(JsonWriterTest, NulBytesAreWrittenNotCut) {
        const String text("a\0b", 3);
        EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(text)); }), String("\"a\\u0000b\""));
    }

    TEST(JsonWriterTest, InvalidUtf8IsReplacedByOneReplacementCharacterPerByteOfARefusedSequence) {
        struct Case {
            String bytes;
            StringView expected;
        };
        const Vector<Case> cases = {
            {Bytes({0x80}), R"("@")"},                                     // lone continuation
            {Bytes({0x61, 0x80, 0x62}), R"("a@b")"},                       //
            {Bytes({0xBF, 0xBF, 0xBF}), R"("@@@")"},                       //
            {Bytes({0xC0, 0x80}), R"("@@")"},                              // overlong NUL
            {Bytes({0xC1, 0xBF}), R"("@@")"},                              // overlong
            {Bytes({0xE0, 0x80, 0x80}), R"("@@@")"},                       // overlong three bytes
            {Bytes({0xE0, 0x9F, 0xBF}), R"("@@@")"},                       //
            {Bytes({0xF0, 0x80, 0x80, 0x80}), R"("@@@@")"},                // overlong four bytes
            {Bytes({0xF0, 0x8F, 0xBF, 0xBF}), R"("@@@@")"},                //
            {Bytes({0xED, 0xA0, 0x80}), R"("@@@")"},                       // surrogate D800
            {Bytes({0xED, 0xBF, 0xBF}), R"("@@@")"},                       // surrogate DFFF
            {Bytes({0xF4, 0x90, 0x80, 0x80}), R"("@@@@")"},                // above U+10FFFF
            {Bytes({0xC3}), R"("@")"},                                     // truncated at the end
            {Bytes({0x61, 0xE2, 0x82}), R"("a@")"},                        // the truncated sequence is one replacement
            {Bytes({0xF0, 0x9F, 0x98}), R"("@")"},                         //
            {Bytes({0xE2, 0x82, 0x61}), R"("@a")"},                        // sequence cut by an ASCII byte
            {Bytes({0xE2, 0xE2, 0x82, 0xAC}), "\"@\xE2\x82\xAC\""},        // a lead inside a sequence starts a new one
            {Bytes({0xC3, 0xC3, 0xA9}), "\"@\xC3\xA9\""},                  //
            {Bytes({0xF8, 0x88, 0x80, 0x80, 0x80}), R"("@@@@@")"},         // five-byte form
            {Bytes({0xFC, 0x84, 0x80, 0x80, 0x80, 0x80}), R"("@@@@@@")"},  //
        };
        for (const Case& item : cases) {
            ExpectString(item.bytes, item.expected);
        }
        for (int byte = 0xF5; byte <= 0xFF; ++byte) {
            ExpectString(Bytes({static_cast<uint8_t>(byte)}), R"("@")");
            ExpectString(Bytes({0x61, static_cast<uint8_t>(byte), 0x62}), R"("a@b")");
        }
    }

    // Any change of a byte written for any of the inputs below changes its digest
    TEST(JsonWriterTest, EveryByteAndEveryPairOfBytesAreWrittenAsTheDomDumpedThem) {
        Digest digest;
        for (int first = 0; first < 256; ++first) {
            digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(Bytes({0x61, static_cast<uint8_t>(first), 0x62}))); }));
            for (int second = 0; second < 256; ++second) {
                const String text = Bytes({static_cast<uint8_t>(first), static_cast<uint8_t>(second)});
                digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(text)); }));
            }
        }
        EXPECT_EQ(digest.Value(), 0x6D58DE7BE4AD41C3ULL);
    }

    TEST(JsonWriterTest, RandomByteStringsAreWrittenAsTheDomDumpedThem) {
        std::mt19937_64 random(kSeed);
        // The bytes that matter to the decoder, plus a few that do not
        constexpr Array<uint8_t, 24> alphabet = {0x00, 0x22, 0x41, 0x5C, 0x7F, 0x80, 0x8F, 0x90, 0xA0, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xE1, 0xED, 0xEF, 0xF0, 0xF1, 0xF4, 0xF5, 0xFF, 0x0A, 0x1F};
        Digest digest;
        for (size_t n = 0; n < kRandomStrings; ++n) {
            String text;
            const size_t length = random() % 12;
            for (size_t i = 0; i < length; ++i) {
                text.push_back(static_cast<char>(alphabet[random() % alphabet.size()]));
            }
            digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(text)); }));
        }
        EXPECT_EQ(digest.Value(), 0xF01F844E30D4FA4ULL);
    }

    TEST(JsonWriterTest, AStringReadsBackAsItWasWritten) {
        constexpr char kText[] = "quote \" backslash \\ newline \n tab \t unicode \xc3\xa9 \xe2\x82\xac nul \0 end";
        const String original(kText, sizeof(kText) - 1);
        const String text = Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(original.data(), original.size())); });

        const JsonDoc doc(text);
        ASSERT_TRUE(doc.IsParsed());
        String decoded;
        ASSERT_TRUE(doc.Root().ReadText(decoded));
        EXPECT_EQ(decoded, original);
    }

    TEST(JsonWriterTest, KeysAreEscapedLikeStrings) {
        const String key = "k\"\\\n\xff";
        constexpr StringView kExpected[] = {
            "{\"k\\\"\\\\\\n\xEF\xBF\xBD\":1}",        "{\n\"k\\\"\\\\\\n\xEF\xBF\xBD\": 1\n}",     "{\n \"k\\\"\\\\\\n\xEF\xBF\xBD\": 1\n}",
            "{\n  \"k\\\"\\\\\\n\xEF\xBF\xBD\": 1\n}", "{\n    \"k\\\"\\\\\\n\xEF\xBF\xBD\": 1\n}",
        };
        for (size_t i = 0; i < kIndents.size(); ++i) {
            EXPECT_EQ(Written(kIndents[i],
                              [&](JsonWriter& w) {
                                  w.BeginObject();
                                  w.Member(StringView(key), 1);
                                  w.EndObject();
                              }),
                      String(kExpected[i].data(), kExpected[i].size()))
                << "indent " << kIndents[i];
        }
    }

    TEST(JsonWriterTest, IntegersAreExactAtTheLimitsOfEveryWidth) {
        ExpectNumber<int8_t>(std::numeric_limits<int8_t>::min(), "-128");
        ExpectNumber<int8_t>(std::numeric_limits<int8_t>::max(), "127");
        ExpectNumber<uint8_t>(std::numeric_limits<uint8_t>::max(), "255");
        ExpectNumber<int16_t>(std::numeric_limits<int16_t>::min(), "-32768");
        ExpectNumber<int16_t>(std::numeric_limits<int16_t>::max(), "32767");
        ExpectNumber<uint16_t>(std::numeric_limits<uint16_t>::max(), "65535");
        ExpectNumber<int32_t>(std::numeric_limits<int32_t>::min(), "-2147483648");
        ExpectNumber<int32_t>(std::numeric_limits<int32_t>::max(), "2147483647");
        ExpectNumber<uint32_t>(std::numeric_limits<uint32_t>::max(), "4294967295");
        ExpectNumber<int64_t>(std::numeric_limits<int64_t>::min(), "-9223372036854775808");
        ExpectNumber<int64_t>(std::numeric_limits<int64_t>::max(), "9223372036854775807");
        ExpectNumber<uint64_t>(std::numeric_limits<uint64_t>::max(), "18446744073709551615");
        ExpectNumber<uint64_t>(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1, "9223372036854775808");
        ExpectNumber<int>(0, "0");
        ExpectNumber<int>(-1, "-1");
        ExpectNumber<unsigned>(0, "0");
        ExpectNumber<size_t>(123456789, "123456789");
    }

    TEST(JsonWriterTest, DoublesAreWrittenAsTheDomDumpedThem) {
        ExpectNumber(0.0, "0.0");
        ExpectNumber(-0.0, "-0.0");
        ExpectNumber(1.0, "1.0");
        ExpectNumber(-1.0, "-1.0");
        ExpectNumber(0.1, "0.1");
        ExpectNumber(-0.1, "-0.1");
        ExpectNumber(0.5, "0.5");
        ExpectNumber(1.5, "1.5");
        ExpectNumber(100.0, "100.0");
        ExpectNumber(1e-5, "1e-05");
        ExpectNumber(1e-7, "1e-07");
        ExpectNumber(1e15, "1e+15");
        ExpectNumber(1e16, "1e+16");
        ExpectNumber(1e17, "1e+17");
        ExpectNumber(123456789012345678.0, "1.2345678901234568e+17");
        ExpectNumber(9007199254740993.0, "9.007199254740992e+15");
        ExpectNumber(5e-324, "5e-324");
        ExpectNumber(2.2250738585072014e-308, "2.2250738585072014e-308");
        ExpectNumber(DBL_MAX, "1.7976931348623157e+308");
        ExpectNumber(-DBL_MAX, "-1.7976931348623157e+308");
        ExpectNumber(DBL_EPSILON, "2.220446049250313e-16");
        ExpectNumber(3.141592653589793, "3.141592653589793");
        ExpectNumber(2.718281828459045, "2.718281828459045");
        ExpectNumber(1.0 / 3.0, "0.3333333333333333");
        ExpectNumber(0.30000000000000004, "0.30000000000000004");
        ExpectNumber(4503599627370496.0, "4.503599627370496e+15");
        ExpectNumber(1e21, "1e+21");
        ExpectNumber(1e22, "1e+22");
        ExpectNumber(1e23, "9.999999999999999e+22");
        ExpectNumber(123456.789e3, "123456789.0");
        ExpectNumber(0.000123456, "0.000123456");

        Digest digest;
        for (int exponent = -1074; exponent <= 1023; exponent += 7) {
            digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(std::ldexp(1.0, exponent)); }));
            digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(-std::ldexp(1.5, exponent)); }));
        }
        EXPECT_EQ(digest.Value(), 0xAFCA89D41637777BULL);
    }

    TEST(JsonWriterTest, IntegralDoublesKeepTheirPointZero) {
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Value(3.0); }), String("3.0"));
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Value(-0.0); }), String("-0.0"));
    }

    TEST(JsonWriterTest, NonFiniteIsNull) {
        for (const double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }), String("null"));
        }
        for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }), String("null"));
        }
    }

    TEST(JsonWriterTest, RandomDoublesAreWrittenAsTheDomDumpedThem) {
        std::mt19937_64 random(kSeed);
        size_t checked = 0;
        Digest digest;
        for (size_t n = 0; n < kRandomDoubles; ++n) {
            const double value =
                (n % 3 == 0) ? std::bit_cast<double>(random())
                             : (n % 3 == 1 ? static_cast<double>(static_cast<int64_t>(random())) / 1024.0 : std::ldexp(static_cast<double>(random() >> 11), static_cast<int>(random() % 80) - 60));
            if (!std::isfinite(value)) {
                continue;
            }
            ++checked;
            digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }));
        }
        EXPECT_GT(checked, kRandomDoubles * 9 / 10);
        EXPECT_EQ(digest.Value(), 0x84C625E3F3E4681AULL);
    }

    TEST(JsonWriterTest, FloatsAreTheShortestTextThatReadsBackAsTheFloat) {
        const auto writeFloat = [](float value) { return Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }); };
        EXPECT_EQ(writeFloat(0.1f), String("0.1"));
        EXPECT_EQ(writeFloat(1.5f), String("1.5"));
        EXPECT_EQ(writeFloat(16777216.0f), String("16777216.0"));
        EXPECT_EQ(writeFloat(FLT_MAX), String("3.4028235e+38"));
        EXPECT_EQ(writeFloat(-FLT_MAX), String("-3.4028235e+38"));
        EXPECT_EQ(writeFloat(FLT_TRUE_MIN), String("1e-45"));
        EXPECT_EQ(writeFloat(FLT_MIN), String("1.1754944e-38"));
    }

    // The digest was taken from the DOM serializer's dump of the same floats (the text the stream serializer writes through `Value(float)`)
    TEST(JsonWriterTest, RandomFloatsAreWrittenAsTheDomSerializerDumpedThem) {
        std::mt19937_64 random(kSeed);
        Digest digest;
        for (size_t n = 0; n < kRandomFloats; ++n) {
            const float value = std::bit_cast<float>(static_cast<uint32_t>(random()));
            digest.Add(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }));
        }
        EXPECT_EQ(digest.Value(), 0x42799561EE588C70ULL);
    }

    TEST(JsonWriterTest, ScalarsAtTheRoot) {
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Null(); }), String("null"));
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Value(true); }), String("true"));
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Value(false); }), String("false"));
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Value("text"); }), String(R"("text")"));
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Raw("{\"x\":1}"); }), String("{\"x\":1}"));
    }

    TEST(JsonWriterTest, PoolStringIsWrittenAsText) {
        const PoolString name = "json_writer_pool_text_4c1f"_intern;
        EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(name); }), String("\"json_writer_pool_text_4c1f\""));
    }

    TEST(JsonWriterTest, NestedContainersAreWrittenInEveryLayout) {
        constexpr StringView kExpected[] = {
            R"({"a":[1,[],{}],"b":{"c":"x","d":[true,null,1.5]},"e":{},"f":[]})",
            "{\n\"a\": [\n1,\n[],\n{}\n],\n\"b\": {\n\"c\": \"x\",\n\"d\": [\ntrue,\nnull,\n1.5\n]\n},\n\"e\": {},\n\"f\": []\n}",
            "{\n \"a\": [\n  1,\n  [],\n  {}\n ],\n \"b\": {\n  \"c\": \"x\",\n  \"d\": [\n   true,\n   null,\n   1.5\n  ]\n },\n \"e\": {},\n \"f\": []\n}",
            "{\n  \"a\": [\n    1,\n    [],\n    {}\n  ],\n  \"b\": {\n    \"c\": \"x\",\n    \"d\": [\n      true,\n      null,\n      1.5\n    ]\n  },\n  \"e\": {},\n  \"f\": []\n}",
            "{\n    \"a\": [\n        1,\n        [],\n        {}\n    ],\n    \"b\": {\n        \"c\": \"x\",\n        \"d\": [\n            true,\n            null,\n            1.5\n        ]\n   "
            " },\n    \"e\": {},\n    \"f\": []\n}",
        };
        for (size_t i = 0; i < kIndents.size(); ++i) {
            EXPECT_EQ(Written(kIndents[i], [](JsonWriter& w) { WriteNested(w); }), String(kExpected[i].data(), kExpected[i].size())) << "indent " << kIndents[i];
        }
    }

    TEST(JsonWriterTest, EmptyContainersAreBracketsOnly) {
        for (const int indent : kIndents) {
            EXPECT_EQ(Written(indent,
                              [](JsonWriter& w) {
                                  w.BeginObject();
                                  w.EndObject();
                              }),
                      String("{}"));
            EXPECT_EQ(Written(indent,
                              [](JsonWriter& w) {
                                  w.BeginArray();
                                  w.EndArray();
                              }),
                      String("[]"));
        }
    }

    TEST(JsonWriterTest, ArraysOfScalarsAndObjectsAreWrittenInEveryLayout) {
        constexpr StringView kExpected[] = {
            R"([{"id":1,"name":"a"},{"id":2,"name":"b"},"tail"])",
            "[\n{\n\"id\": 1,\n\"name\": \"a\"\n},\n{\n\"id\": 2,\n\"name\": \"b\"\n},\n\"tail\"\n]",
            "[\n {\n  \"id\": 1,\n  \"name\": \"a\"\n },\n {\n  \"id\": 2,\n  \"name\": \"b\"\n },\n \"tail\"\n]",
            "[\n  {\n    \"id\": 1,\n    \"name\": \"a\"\n  },\n  {\n    \"id\": 2,\n    \"name\": \"b\"\n  },\n  \"tail\"\n]",
            "[\n    {\n        \"id\": 1,\n        \"name\": \"a\"\n    },\n    {\n        \"id\": 2,\n        \"name\": \"b\"\n    },\n    \"tail\"\n]",
        };
        for (size_t i = 0; i < kIndents.size(); ++i) {
            const String written = Written(kIndents[i], [](JsonWriter& w) {
                w.BeginArray();
                w.BeginObject();
                w.Member("id", 1);
                w.Member("name", "a");
                w.EndObject();
                w.BeginObject();
                w.Member("id", 2);
                w.Member("name", "b");
                w.EndObject();
                w.Value("tail");
                w.EndArray();
            });
            EXPECT_EQ(written, String(kExpected[i].data(), kExpected[i].size())) << "indent " << kIndents[i];
        }
    }

    TEST(JsonWriterTest, KeyLiteralEqualsKey) {
        for (const int indent : kIndents) {
            const String viaKey = Written(indent, [](JsonWriter& w) {
                w.BeginObject();
                w.Member("a", 1);
                w.Member("b", "x");
                w.EndObject();
            });
            const String viaLiteral = Written(indent, [](JsonWriter& w) {
                w.BeginObject();
                w.KeyLiteral("\"a\":");
                w.Value(1);
                w.KeyLiteral("\"b\":");
                w.Value("x");
                w.EndObject();
            });
            EXPECT_EQ(viaLiteral, viaKey) << "indent " << indent;
        }
    }

    TEST(JsonWriterTest, TheDeepestAllowedNestingIsWritten) {
        constexpr size_t kLevels = JsonWriter::kMaxDepth;
        String out;
        JsonWriter writer(out);
        for (size_t i = 0; i < kLevels; ++i) {
            writer.BeginArray();
        }
        for (size_t i = 0; i < kLevels; ++i) {
            writer.EndArray();
        }
        EXPECT_TRUE(writer.IsComplete());
        EXPECT_EQ(out.size(), kLevels * 2);
    }

    TEST(JsonWriterTest, AppendsAndNeverClears) {
        String out = "prefix:";
        JsonWriter writer(out);
        writer.Value(1);
        EXPECT_EQ(out, String("prefix:1"));
    }

    TEST(JsonWriterTest, CompletenessFollowsTheRootValue) {
        String out;
        JsonWriter writer(out);
        EXPECT_FALSE(writer.IsComplete());
        writer.BeginObject();
        EXPECT_FALSE(writer.IsComplete());
        writer.EndObject();
        EXPECT_TRUE(writer.IsComplete());
    }

    TEST(JsonWriterTest, AFailureIsKeptOnceAndInvalidatesTheWriterWithoutAnExpect) {
        size_t expects = 0;
        AssertBridge::Capture capture([&](const AssertEvent&) { ++expects; });
        String out;
        JsonWriter writer(out);
        EXPECT_EQ(writer.GetFailure(), nullptr);
        writer.BeginObject();
        writer.Fail(MakeControlError("first", "one"));
        writer.Fail(MakeControlError("second", "two"));
        writer.Member("after", 1);  // ignored with no misuse: the failed writer of a value may have left containers open
        writer.EndObject();
        EXPECT_EQ(out, "{");

        EXPECT_FALSE(writer.IsValid());
        EXPECT_FALSE(writer.IsComplete());
        ASSERT_NE(writer.GetFailure(), nullptr);
        EXPECT_EQ(writer.GetFailure()->code, "first");
        EXPECT_EQ(writer.GetFailure()->message, "one");
        EXPECT_EQ(expects, 0u);
    }

    // The engine's own misuse is an EXPECT and never a crash or a write past a buffer; the captured count only proves the EXPECT fired
    TEST(JsonWriterTest, MisuseIsIgnoredAndNeverAborts) {
        size_t expects = 0;
        AssertBridge::Capture capture([&](const AssertEvent&) { ++expects; });
        const auto misuse = [&](auto&& body) {
            String out;
            JsonWriter writer(out);
            body(writer);
            EXPECT_FALSE(writer.IsValid());
            EXPECT_FALSE(writer.IsComplete());
        };

        misuse([](JsonWriter& w) { w.EndObject(); });
        misuse([](JsonWriter& w) { w.EndArray(); });
        misuse([](JsonWriter& w) { w.Key("a"); });
        misuse([](JsonWriter& w) { w.KeyLiteral("\"a\":"); });
        misuse([](JsonWriter& w) {
            w.BeginArray();
            w.Key("a");
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.Value(1);
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.BeginArray();
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.Key("a");
            w.Key("b");
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.Key("a");
            w.EndObject();
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.EndArray();
        });
        misuse([](JsonWriter& w) {
            w.BeginArray();
            w.EndObject();
        });
        misuse([](JsonWriter& w) {
            w.Value(1);
            w.Value(2);
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.KeyLiteral("a:");
        });
        misuse([](JsonWriter& w) {
            w.BeginObject();
            w.KeyLiteral("\"a\"");
        });
        misuse([](JsonWriter& w) { w.Raw(""); });
        misuse([](JsonWriter& w) {
            for (size_t i = 0; i <= JsonWriter::kMaxDepth; ++i) {
                w.BeginArray();
            }
        });
#if ENGINE_ENABLE_EXPECTS
        EXPECT_EQ(expects, 16u);
#endif
    }

    TEST(JsonWriterTest, AMisusedCallWritesNothing) {
        AssertBridge::Capture capture;
        String out;
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Value(1);
        writer.EndObject();
        EXPECT_EQ(out, String("{}"));
    }

    // The writer itself allocates nothing, so a second message of the same shape into a String that kept its capacity does not either. The pointer and the
    // capacity check works without the allocation tracker.
    TEST(JsonWriterTest, SecondMessageAllocatesNothing) {
        const auto message = [](JsonWriter& w) {
            w.BeginObject();
            w.Member("big", std::numeric_limits<uint64_t>::max());
            w.Member("flag", true);
            w.Member("float", 0.1f);
            w.Member("int", -42);
            w.Key("list");
            w.BeginArray();
            for (int i = 0; i < 20; ++i) {
                w.Value(i * 0.25);
            }
            w.BeginObject();
            w.Member("text", "quote \" tab \t \xC3\xA9 broken \xE2\x82");
            w.EndObject();
            w.EndArray();
            w.Key("nothing");
            w.Null();
            w.Member("pi", 3.141592653589793);
            w.EndObject();
        };

        for (const int indent : kIndents) {
            String out;
            {
                JsonWriter first(out, indent);
                message(first);
            }
            const String firstText = out;
            const char* const data = out.data();
            const size_t capacity = out.capacity();
            out.clear();

            ExpectNoAllocations([&] {
                JsonWriter second(out, indent);
                message(second);
            });

            EXPECT_EQ(out, firstText);
            EXPECT_EQ(out.data(), data) << "the String moved: the writer or the String allocated, indent " << indent;
            EXPECT_EQ(out.capacity(), capacity);
        }
    }

}  // namespace BECore::Tests
