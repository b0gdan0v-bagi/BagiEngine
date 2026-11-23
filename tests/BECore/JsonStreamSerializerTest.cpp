// JsonStreamSerializer (Control/JsonStreamSerializer.h): the same documents as JsonSerializer, written straight into a JsonWriter in the order of the writes.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/FactoryInitializeTest.h>
#include <BECore/FuzzProbe.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/TypeRegistryTest.h>
#include <Common/AllocCheck.h>
#include <Common/Require.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace RegistryTestData;

    namespace {

        /// What the body writes into a stream serializer, closed
        template <typename Body>
        String StreamText(Body&& body) {
            String text;
            JsonWriter writer(text);
            JsonStreamSerializer serializer(writer);
            body(serializer);
            serializer.Finish();
            EXPECT_TRUE(writer.IsComplete());
            return text;
        }

        template <typename Body>
        String DomText(Body&& body) {
            JsonSerializer serializer;
            body(serializer);
            return DumpJson(serializer.Take());
        }

        /// The documents are the same value; the key order is the stream's own
        void ExpectSameDocument(const String& stream, const String& dom) {
            const auto streamed = ParseJson(stream);
            const auto built = ParseJson(dom);
            ASSERT_TRUE(streamed.has_value()) << stream.c_str();
            ASSERT_TRUE(built.has_value()) << dom.c_str();
            EXPECT_TRUE(*streamed == *built) << stream.c_str() << "\nagainst\n" << dom.c_str();
        }

        template <typename T>
        void ExpectSavedAsDom(const T& value) {
            const auto body = [&value](auto& serializer) { Ser::Save(serializer, value, "k"); };
            ExpectSameDocument(StreamText(body), DomText(body));
        }

        template <typename T>
        void ExpectObjectAsDom(const T& object) {
            const auto body = [&object](auto& serializer) { object.Serialize(serializer); };
            ExpectSameDocument(StreamText(body), DomText(body));
        }

    }  // namespace

    TEST(JsonStreamSerializerTest, ScalarsOfEveryWidthAreTheDomsNumbers) {
        ExpectSavedAsDom(int8_t{-5});
        ExpectSavedAsDom(uint8_t{200});
        ExpectSavedAsDom(int16_t{-1600});
        ExpectSavedAsDom(uint16_t{60000});
        ExpectSavedAsDom(int32_t{-320000});
        ExpectSavedAsDom(uint32_t{4000000000u});
        ExpectSavedAsDom(std::numeric_limits<int64_t>::min());
        ExpectSavedAsDom(std::numeric_limits<uint64_t>::max());
        ExpectSavedAsDom(true);
        ExpectSavedAsDom(0.1f);
        ExpectSavedAsDom(16777216.0f);
        ExpectSavedAsDom(-2.25);
        ExpectSavedAsDom(1.0f / 3.0f);
        ExpectSavedAsDom(std::numeric_limits<float>::quiet_NaN());
        ExpectSavedAsDom(-std::numeric_limits<double>::infinity());
    }

    TEST(JsonStreamSerializerTest, AFloatIsTheShortestTextThatReadsBack) {
        EXPECT_EQ(StreamText([](auto& serializer) { serializer.Write("k", 0.1f); }), R"({"k":0.1})");
        EXPECT_EQ(StreamText([](auto& serializer) { serializer.Write("k", std::numeric_limits<float>::infinity()); }), R"({"k":null})");
    }

    TEST(JsonStreamSerializerTest, StringsAreEscapedAndInvalidUtf8IsReplaced) {
        ExpectSavedAsDom(String("a \"quoted\" \\ text\n"));
        ExpectSavedAsDom(PoolString{"pooled"_intern});
        ExpectSavedAsDom(RegMode::Auto);
        ExpectSavedAsDom(String());
        EXPECT_EQ(StreamText([](auto& serializer) {
                      serializer.Write("k", StringView("a\xff"
                                                       "b"));
                  }),
                  "{\"k\":\"a\xEF\xBF\xBD"
                  "b\"}");
    }

    TEST(JsonStreamSerializerTest, WritingTextNeverInterns) {
        constexpr StringView kNeverInterned = "json_stream_serializer_never_interned_text";

        const String text = StreamText([&](auto& serializer) {
            serializer.Write("view", kNeverInterned);
            serializer.WriteAttribute("attribute", kNeverInterned);
        });

        EXPECT_FALSE(PoolString::Find(kNeverInterned).has_value());
        EXPECT_FALSE(text.empty());
    }

    TEST(JsonStreamSerializerTest, ContainersAreTheDomsArrays) {
        ExpectSavedAsDom(Vector<int32_t>{1, 2, 3});
        ExpectSavedAsDom(Vector<int32_t>{});
        ExpectSavedAsDom(Vector<String>{"a", "b"});
        ExpectSavedAsDom(Array<int32_t, 2>{7, 8});
        ExpectSavedAsDom(Optional<int32_t>(5));
        ExpectSavedAsDom(Optional<int32_t>());
        ExpectSavedAsDom(Pair<int32_t, String>(1, "s"));
        ExpectSavedAsDom(Variant<int32_t, String>(7));
        ExpectSavedAsDom(Variant<std::monostate, int32_t>());
        ExpectSavedAsDom(Vector<Variant<int32_t, String>>{7, String("s")});

        HashMap<String, int32_t> table;
        table["b"] = 2;
        table["a"] = 1;
        ExpectSavedAsDom(table);
        Map<int32_t, String> ordered;
        ordered[2] = "x";
        ordered[1] = "y";
        ExpectSavedAsDom(ordered);
    }

    TEST(JsonStreamSerializerTest, ObjectsAndPointersAreTheDomsObjects) {
        RegInner inner;
        inner.value = 3;
        inner.label = "l";
        IntrusivePtr<RegNode> node = New<RegNode>();
        node->id = 9;

        ExpectSavedAsDom(inner);
        ExpectSavedAsDom(node);
        ExpectSavedAsDom(IntrusivePtr<RegNode>());
        ExpectSavedAsDom(Vector<RegInner>{inner, RegInner{}});
    }

    TEST(JsonStreamSerializerTest, EveryKindOfAClassIsTheDomsDocument) {
        RegAllKinds all;
        all.flag = true;
        all.tiny = -1;
        all.count = 3;
        all.ratio = 0.1f;
        all.text = "t";
        all.name = "n"_intern;
        all.mode = RegMode::On;
        all.items = {1};
        all.table["k"] = 2;
        all.maybe = 4;
        all.choice = String("c");

        ExpectObjectAsDom(all);
        ExpectObjectAsDom(Fuzz::MakeFilledProbe());
    }

    TEST(JsonStreamSerializerTest, AFactoryElementCarriesItsTypeAndEnabled) {
        NoHookProbeList list;
        for (const int32_t value : {4, 5}) {
            IntrusivePtr<NoHookProbeImpl> item = New<NoHookProbeImpl>();
            item->value = value;
            list.items.push_back(item);
        }
        list.items.push_back({});

        ExpectObjectAsDom(list);
    }

    TEST(JsonStreamSerializerTest, MembersComeOutInTheOrderTheyWereWritten) {
        const String text = StreamText([](auto& serializer) {
            serializer.Write("zebra", int32_t{1});
            ASSERT_TRUE(serializer.BeginObject("middle"));
            serializer.Write("b", int32_t{2});
            serializer.Write("a", int32_t{3});
            serializer.EndObject();
            serializer.Write("apple", int32_t{4});
        });

        EXPECT_EQ(text, R"({"zebra":1,"middle":{"b":2,"a":3},"apple":4})");
    }

    TEST(JsonStreamSerializerTest, AnElementWithOnlyAScalarValueIsThatScalar) {
        const String text = StreamText([](auto& serializer) {
            size_t count = 5;
            ASSERT_TRUE(serializer.BeginArray("list", "item", count));
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.WriteAttribute("value", int32_t{1});
            serializer.EndArrayElement();
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.WriteAttribute("value", int32_t{2});
            serializer.WriteAttribute("other", int32_t{3});
            serializer.EndArrayElement();
            ASSERT_TRUE(serializer.BeginArrayElement());
            ASSERT_TRUE(serializer.BeginObject("value"));
            serializer.WriteAttribute("x", int32_t{4});
            serializer.EndObject();
            serializer.EndArrayElement();
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.WriteAttribute("other", int32_t{5});
            serializer.WriteAttribute("value", int32_t{6});
            serializer.EndArrayElement();
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.EndArrayElement();
            serializer.EndArray();
        });

        EXPECT_EQ(text, R"({"list":[1,{"value":2,"other":3},{"value":{"x":4}},{"other":5,"value":6},{}]})");
    }

    TEST(JsonStreamSerializerTest, ANameWrittenTwiceIsWrittenOnce) {
        const String text = StreamText([](auto& serializer) {
            serializer.Write("a", int32_t{1});
            serializer.Write("a", int32_t{2});
            ASSERT_TRUE(serializer.BeginObject("o"));
            serializer.Write("x", int32_t{1});
            serializer.EndObject();
            EXPECT_FALSE(serializer.BeginObject("o"));
            size_t count = 0;
            EXPECT_FALSE(serializer.BeginArray("a", "item", count));
            serializer.Write("b", int32_t{3});
        });

        EXPECT_EQ(text, R"({"a":1,"o":{"x":1},"b":3})");
    }

    TEST(JsonStreamSerializerTest, AHeldBackValueIsReplacedByALaterOne) {
        const String text = StreamText([](auto& serializer) {
            size_t count = 1;
            ASSERT_TRUE(serializer.BeginArray("list", "item", count));
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.WriteAttribute("value", int32_t{1});
            serializer.WriteAttribute("value", int32_t{2});
            serializer.EndArrayElement();
            serializer.EndArray();
        });

        EXPECT_EQ(text, R"({"list":[2]})");
    }

    TEST(JsonStreamSerializerTest, TheSameNameInTwoObjectsIsNotARepeat) {
        const String text = StreamText([](auto& serializer) {
            ASSERT_TRUE(serializer.BeginObject("first"));
            serializer.Write("x", int32_t{1});
            serializer.EndObject();
            ASSERT_TRUE(serializer.BeginObject("second"));
            serializer.Write("x", int32_t{2});
            serializer.EndObject();
            serializer.Write("x", int32_t{3});
        });

        EXPECT_EQ(text, R"({"first":{"x":1},"second":{"x":2},"x":3})");
    }

    TEST(JsonStreamSerializerTest, CallsThatDoNotFitAreIgnored) {
        const String text = StreamText([](auto& serializer) {
            size_t count = 0;

            serializer.EndObject();
            serializer.EndArray();
            serializer.EndArrayElement();
            EXPECT_FALSE(serializer.BeginArrayElement());
            serializer.Write("a", int32_t{1});

            ASSERT_TRUE(serializer.BeginArray("list", "item", count));
            serializer.Write("ignored", int32_t{2});
            serializer.WriteAttribute("ignored", true);
            EXPECT_FALSE(serializer.BeginObject("ignored"));
            EXPECT_FALSE(serializer.BeginArray("ignored", "item", count));
            serializer.EndObject();
            serializer.EndArrayElement();
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.EndArray();
            serializer.EndArrayElement();
            serializer.EndArray();
            serializer.EndObject();
            serializer.Write("b", int32_t{3});
        });

        EXPECT_EQ(text, R"({"a":1,"list":[{}],"b":3})");
    }

    TEST(JsonStreamSerializerTest, FinishClosesWhatIsStillOpenAndLaterCallsAreIgnored) {
        String text;
        JsonWriter writer(text);
        JsonStreamSerializer serializer(writer);
        size_t count = 1;

        ASSERT_TRUE(serializer.BeginArray("list", "item", count));
        ASSERT_TRUE(serializer.BeginArrayElement());
        serializer.WriteAttribute("value", int32_t{1});
        EXPECT_FALSE(serializer.BeginArrayElement());
        serializer.Finish();
        serializer.Write("late", int32_t{2});
        EXPECT_FALSE(serializer.BeginObject("late"));
        serializer.Finish();

        EXPECT_EQ(text, R"({"list":[1]})");
        EXPECT_TRUE(writer.IsComplete());
    }

    TEST(JsonStreamSerializerTest, NestingPastTheWritersDepthAnswersFalseAndNeverAborts) {
        String text;
        JsonWriter writer(text);
        JsonStreamSerializer serializer(writer);
        size_t opened = 0;

        while (serializer.BeginObject("o")) {
            ++opened;
            ASSERT_LE(opened, JsonWriter::kMaxDepth);
        }
        serializer.Finish();

        EXPECT_EQ(opened, JsonWriter::kMaxDepth - 1);
        EXPECT_TRUE(writer.IsComplete());
        EXPECT_TRUE(ParseJson(text).has_value());
    }

    TEST(JsonStreamSerializerTest, AnElementNearTheDepthLimitStillFits) {
        String text;
        JsonWriter writer(text);
        JsonStreamSerializer serializer(writer);
        size_t count = 0;

        while (writer.GetDepth() + 2 < JsonWriter::kMaxDepth) {
            ASSERT_TRUE(serializer.BeginObject("o"));
        }
        ASSERT_TRUE(serializer.BeginArray("list", "item", count));
        ASSERT_TRUE(serializer.BeginArrayElement());
        EXPECT_FALSE(serializer.BeginObject("deep"));
        serializer.EndArrayElement();
        serializer.Finish();

        EXPECT_TRUE(writer.IsComplete());
        EXPECT_TRUE(ParseJson(text).has_value());
    }

    TEST(JsonStreamSerializerTest, ASecondDocumentOnAReusedStringAllocatesNothing) {
        RegInner inner;
        inner.value = 3;
        inner.label = "l";
        const Vector<RegInner> list{inner, RegInner{}};
        const Vector<int32_t> numbers{1, 2};
        String text;
        JsonWriter first(text);
        JsonStreamSerializer serializer(first);
        Ser::Save(serializer, list, "k");
        Ser::Save(serializer, numbers, "n");
        serializer.Finish();
        const String expected = text;

        text.clear();
        ExpectNoAllocations([&] {
            JsonWriter second(text);
            serializer.Restart(second);
            Ser::Save(serializer, list, "k");
            Ser::Save(serializer, numbers, "n");
            serializer.Finish();
        });

        EXPECT_EQ(text, expected);
    }

}  // namespace BECore::Tests
