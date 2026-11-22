// JsonWriter (Control/JsonWriter.h): every text it writes is compared with `nlohmann::json::dump(indent, ' ', false, error_handler_t::replace)` of the same
// document, the DOM path it replaces.

#include <BECore/Assert/AssertEvent.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
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

        /// What the DOM path writes for @p value
        String Reference(const Json& value, int indent) {
            const std::string text = value.dump(indent, ' ', false, Json::error_handler_t::replace);  //-NoStd: the reference is nlohmann's own dump
            return String(text.c_str(), text.size());
        }

        Json StringJson(StringView text) {
            return Json(std::string(text.data(), text.size()));  //-NoStd: a Json string holds std::string
        }

        template <typename TWrite>
        String Written(int indent, TWrite&& write) {
            String out;
            JsonWriter writer(out, indent);
            write(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        void ExpectStringMatches(StringView text) {
            const Json reference = StringJson(text);
            for (const int indent : kIndents) {
                const String written = Written(indent, [&](JsonWriter& w) { w.Value(text); });
                ASSERT_EQ(written, Reference(reference, indent)) << "a string of " << text.size() << " bytes, first byte " << (text.empty() ? 0 : static_cast<uint8_t>(text[0]));
            }
        }

        template <typename T>
        void ExpectNumberMatches(T value) {
            const Json reference(value);
            for (const int indent : kIndents) {
                const String written = Written(indent, [&](JsonWriter& w) { w.Value(value); });
                ASSERT_EQ(written, Reference(reference, indent));
            }
        }

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

        Json NestedJson() {
            Json doc = Json::object();
            doc["a"] = Json::array({1, Json::array(), Json::object()});
            doc["b"] = Json::object({{"c", "x"}, {"d", Json::array({true, nullptr, 1.5})}});
            doc["e"] = Json::object();
            doc["f"] = Json::array();
            return doc;
        }

    }  // namespace

    TEST(JsonWriterTest, EscapesAndControlCharactersMatchDump) {
        ExpectStringMatches("");
        ExpectStringMatches("plain text 123");
        ExpectStringMatches("quote \" backslash \\ slash / tab \t newline \n return \r backspace \b formfeed \f");
        ExpectStringMatches("\x7f DEL stays, \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80 and \xEF\xBF\xBD stay as they are");
        for (int c = 0; c < 0x20; ++c) {
            const char single[] = {static_cast<char>(c), 0};
            ExpectStringMatches(StringView(single, 1));
            ExpectStringMatches(Format("a{}b", StringView(single, 1)));
        }
        String longText;
        for (int i = 0; i < 4000; ++i) {
            longText.append("line \"");
            longText.push_back(static_cast<char>(i % 0x20));
            longText.append("\" \xC3\xA9\n");
        }
        ExpectStringMatches(longText);
    }

    TEST(JsonWriterTest, HexEscapesAreLowerCase) {
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Value(StringView("\x1f\x0b", 2)); }), String("\"\\u001f\\u000b\""));
    }

    TEST(JsonWriterTest, NulBytesAreWrittenNotCut) {
        const String text("a\0b", 3);
        EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(text)); }), String("\"a\\u0000b\""));
    }

    TEST(JsonWriterTest, InvalidUtf8IsReplacedLikeDump) {
        const Vector<String> cases = {
            Bytes({0x80}),                          // lone continuation
            Bytes({0x61, 0x80, 0x62}),              //
            Bytes({0xBF, 0xBF, 0xBF}),              //
            Bytes({0xC0, 0x80}),                    // overlong NUL
            Bytes({0xC1, 0xBF}),                    // overlong
            Bytes({0xE0, 0x80, 0x80}),              // overlong three bytes
            Bytes({0xE0, 0x9F, 0xBF}),              //
            Bytes({0xF0, 0x80, 0x80, 0x80}),        // overlong four bytes
            Bytes({0xF0, 0x8F, 0xBF, 0xBF}),        //
            Bytes({0xED, 0xA0, 0x80}),              // surrogate D800
            Bytes({0xED, 0xBF, 0xBF}),              // surrogate DFFF
            Bytes({0xF4, 0x90, 0x80, 0x80}),        // above U+10FFFF
            Bytes({0xC3}),                          // truncated at the end
            Bytes({0x61, 0xE2, 0x82}),              //
            Bytes({0xF0, 0x9F, 0x98}),              //
            Bytes({0xE2, 0x82, 0x61}),              // sequence cut by an ASCII byte
            Bytes({0xE2, 0xE2, 0x82, 0xAC}),        // a lead inside a sequence starts a new one
            Bytes({0xC3, 0xC3, 0xA9}),              //
            Bytes({0xF8, 0x88, 0x80, 0x80, 0x80}),  // five-byte form
            Bytes({0xFC, 0x84, 0x80, 0x80, 0x80, 0x80}),
        };
        for (const String& text : cases) {
            ExpectStringMatches(text);
        }
        for (int byte = 0xF5; byte <= 0xFF; ++byte) {
            ExpectStringMatches(Bytes({static_cast<uint8_t>(byte)}));
            ExpectStringMatches(Bytes({0x61, static_cast<uint8_t>(byte), 0x62}));
        }
    }

    TEST(JsonWriterTest, EveryByteAndEveryPairOfBytesMatchDump) {
        for (int first = 0; first < 256; ++first) {
            ExpectStringMatches(Bytes({0x61, static_cast<uint8_t>(first), 0x62}));
            for (int second = 0; second < 256; ++second) {
                const String text = Bytes({static_cast<uint8_t>(first), static_cast<uint8_t>(second)});
                const String written = Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(text)); });
                ASSERT_EQ(written, Reference(StringJson(text), JsonWriter::kCompact)) << first << " " << second;
            }
        }
    }

    TEST(JsonWriterTest, RandomByteStringsMatchDump) {
        std::mt19937_64 random(kSeed);
        // The bytes that matter to the decoder, plus a few that do not
        constexpr Array<uint8_t, 24> alphabet = {0x00, 0x22, 0x41, 0x5C, 0x7F, 0x80, 0x8F, 0x90, 0xA0, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xE1, 0xED, 0xEF, 0xF0, 0xF1, 0xF4, 0xF5, 0xFF, 0x0A, 0x1F};
        for (size_t n = 0; n < kRandomStrings; ++n) {
            String text;
            const size_t length = random() % 12;
            for (size_t i = 0; i < length; ++i) {
                text.push_back(static_cast<char>(alphabet[random() % alphabet.size()]));
            }
            const String written = Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(StringView(text)); });
            ASSERT_EQ(written, Reference(StringJson(text), JsonWriter::kCompact)) << "case " << n;
        }
    }

    TEST(JsonWriterTest, KeysAreEscapedLikeStrings) {
        const String key = "k\"\\\n\xff";
        Json doc = Json::object();
        doc[std::string(key.c_str(), key.size())] = 1;  //-NoStd: a Json key is std::string
        for (const int indent : kIndents) {
            EXPECT_EQ(Written(indent,
                              [&](JsonWriter& w) {
                                  w.BeginObject();
                                  w.Member(StringView(key), 1);
                                  w.EndObject();
                              }),
                      Reference(doc, indent));
        }
    }

    TEST(JsonWriterTest, IntegersMatchDumpAtTheLimitsOfEveryWidth) {
        ExpectNumberMatches<int8_t>(std::numeric_limits<int8_t>::min());
        ExpectNumberMatches<int8_t>(std::numeric_limits<int8_t>::max());
        ExpectNumberMatches<uint8_t>(std::numeric_limits<uint8_t>::max());
        ExpectNumberMatches<int16_t>(std::numeric_limits<int16_t>::min());
        ExpectNumberMatches<int16_t>(std::numeric_limits<int16_t>::max());
        ExpectNumberMatches<uint16_t>(std::numeric_limits<uint16_t>::max());
        ExpectNumberMatches<int32_t>(std::numeric_limits<int32_t>::min());
        ExpectNumberMatches<int32_t>(std::numeric_limits<int32_t>::max());
        ExpectNumberMatches<uint32_t>(std::numeric_limits<uint32_t>::max());
        ExpectNumberMatches<int64_t>(std::numeric_limits<int64_t>::min());
        ExpectNumberMatches<int64_t>(std::numeric_limits<int64_t>::max());
        ExpectNumberMatches<uint64_t>(std::numeric_limits<uint64_t>::max());
        ExpectNumberMatches<uint64_t>(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1);
        ExpectNumberMatches<int>(0);
        ExpectNumberMatches<int>(-1);
        ExpectNumberMatches<unsigned>(0);
        ExpectNumberMatches<size_t>(123456789);
    }

    TEST(JsonWriterTest, DoublesMatchDumpForAFixedBattery) {
        const double battery[] = {0.0,
                                  -0.0,
                                  1.0,
                                  -1.0,
                                  0.1,
                                  -0.1,
                                  0.5,
                                  1.5,
                                  100.0,
                                  1e-5,
                                  1e-7,
                                  1e15,
                                  1e16,
                                  1e17,
                                  123456789012345678.0,
                                  9007199254740993.0,
                                  5e-324,
                                  2.2250738585072014e-308,
                                  DBL_MAX,
                                  -DBL_MAX,
                                  DBL_EPSILON,
                                  3.141592653589793,
                                  2.718281828459045,
                                  1.0 / 3.0,
                                  0.30000000000000004,
                                  4503599627370496.0,
                                  1e21,
                                  1e22,
                                  1e23,
                                  123456.789e3,
                                  0.000123456};
        for (const double value : battery) {
            ExpectNumberMatches(value);
        }
        for (int exponent = -1074; exponent <= 1023; exponent += 7) {
            ExpectNumberMatches(std::ldexp(1.0, exponent));
            ExpectNumberMatches(-std::ldexp(1.5, exponent));
        }
    }

    TEST(JsonWriterTest, IntegralDoublesKeepTheirPointZero) {
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Value(3.0); }), String("3.0"));
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Value(-0.0); }), String("-0.0"));
    }

    TEST(JsonWriterTest, NonFiniteIsNull) {
        for (const double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }), String("null"));
            ExpectNumberMatches(value);
        }
        for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); }), String("null"));
        }
    }

    TEST(JsonWriterTest, RandomDoublesMatchDump) {
        std::mt19937_64 random(kSeed);
        size_t checked = 0;
        for (size_t n = 0; n < kRandomDoubles; ++n) {
            const double value =
                (n % 3 == 0) ? std::bit_cast<double>(random())
                             : (n % 3 == 1 ? static_cast<double>(static_cast<int64_t>(random())) / 1024.0 : std::ldexp(static_cast<double>(random() >> 11), static_cast<int>(random() % 80) - 60));
            if (!std::isfinite(value)) {
                continue;
            }
            ++checked;
            const String written = Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); });
            ASSERT_EQ(written, Reference(Json(value), JsonWriter::kCompact)) << "case " << n;
        }
        EXPECT_GT(checked, kRandomDoubles * 9 / 10);
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

    TEST(JsonWriterTest, FloatsEqualWhatJsonSerializerWrites) {
        std::mt19937_64 random(kSeed);
        for (size_t n = 0; n < 20000; ++n) {
            const float value = std::bit_cast<float>(static_cast<uint32_t>(random()));
            JsonSerializer serializer;
            serializer.Write("v", value);
            const Json doc = serializer.Take();
            const Json* member = FindJsonMember(doc, "v");
            ASSERT_NE(member, nullptr);
            const String written = Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(value); });
            ASSERT_EQ(written, Reference(*member, JsonWriter::kCompact)) << "case " << n;
        }
    }

    TEST(JsonWriterTest, ScalarsAtTheRoot) {
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Null(); }), Reference(Json(nullptr), 2));
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Value(true); }), Reference(Json(true), 2));
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Value(false); }), Reference(Json(false), 2));
        EXPECT_EQ(Written(2, [](JsonWriter& w) { w.Value("text"); }), Reference(Json("text"), 2));
        EXPECT_EQ(Written(JsonWriter::kCompact, [](JsonWriter& w) { w.Raw("{\"x\":1}"); }), String("{\"x\":1}"));
    }

    TEST(JsonWriterTest, PoolStringIsWrittenAsText) {
        const PoolString name = "json_writer_pool_text_4c1f"_intern;
        EXPECT_EQ(Written(JsonWriter::kCompact, [&](JsonWriter& w) { w.Value(name); }), String("\"json_writer_pool_text_4c1f\""));
    }

    TEST(JsonWriterTest, NestedContainersMatchDumpInEveryLayout) {
        const Json reference = NestedJson();
        for (const int indent : kIndents) {
            EXPECT_EQ(Written(indent, [](JsonWriter& w) { WriteNested(w); }), Reference(reference, indent)) << "indent " << indent;
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

    TEST(JsonWriterTest, ArraysOfScalarsAndObjectsMatchDump) {
        Json reference = Json::array();
        reference.push_back(Json::object({{"id", 1}, {"name", "a"}}));
        reference.push_back(Json::object({{"id", 2}, {"name", "b"}}));
        reference.push_back("tail");
        for (const int indent : kIndents) {
            const String written = Written(indent, [](JsonWriter& w) {
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
            EXPECT_EQ(written, Reference(reference, indent)) << "indent " << indent;
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
