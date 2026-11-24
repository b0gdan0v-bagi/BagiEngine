// JsonStreamSerializer (Control/JsonStreamSerializer.h): the JSON text of every shape the data accessors write, in the order of the writes.
// The generated code of the test types is included in TypeRegistryTest.cpp and FuzzProbeAccess.cpp; here the types are used through their headers.

#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/FactoryInitializeTest.h>
#include <BECore/FuzzProbe.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/TypeRegistryTest.h>
#include <Common/AllocCheck.h>
#include <Common/JsonDoc.h>
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

        /// The text of `Ser::Save(value, "k")` as the root object
        template <typename T>
        void ExpectSaved(const T& value, StringView expected) {
            const String text = StreamText([&value](auto& serializer) { Ser::Save(serializer, value, "k"); });
            EXPECT_EQ(text, String(expected.data(), expected.size()));
        }

        template <typename T>
        void ExpectObject(const T& object, StringView expected) {
            const String text = StreamText([&object](auto& serializer) { object.Serialize(serializer); });
            EXPECT_EQ(text, String(expected.data(), expected.size()));
        }

    }  // namespace

    TEST(JsonStreamSerializerTest, IntegersAreExactJsonIntegers) {
        ExpectSaved(int8_t{-5}, R"({"k":-5})");
        ExpectSaved(uint8_t{200}, R"({"k":200})");
        ExpectSaved(int16_t{-1600}, R"({"k":-1600})");
        ExpectSaved(uint16_t{60000}, R"({"k":60000})");
        ExpectSaved(int32_t{-320000}, R"({"k":-320000})");
        ExpectSaved(uint32_t{4000000000u}, R"({"k":4000000000})");
        ExpectSaved(std::numeric_limits<int64_t>::min(), R"({"k":-9223372036854775808})");
        ExpectSaved(std::numeric_limits<int64_t>::max(), R"({"k":9223372036854775807})");
        ExpectSaved(std::numeric_limits<uint64_t>::max(), R"({"k":18446744073709551615})");
        ExpectSaved(true, R"({"k":true})");
        ExpectSaved(false, R"({"k":false})");
    }

    TEST(JsonStreamSerializerTest, AFloatIsTheShortestTextThatReadsBack) {
        ExpectSaved(0.1f, R"({"k":0.1})");
        ExpectSaved(1.5f, R"({"k":1.5})");
        ExpectSaved(16777216.0f, R"({"k":16777216.0})");
        ExpectSaved(1.0f / 3.0f, R"({"k":0.33333334})");
        ExpectSaved(0.1, R"({"k":0.1})");
        ExpectSaved(-2.25, R"({"k":-2.25})");
    }

    TEST(JsonStreamSerializerTest, ANumberWithoutAJsonFormIsNull) {
        ExpectSaved(std::numeric_limits<float>::quiet_NaN(), R"({"k":null})");
        ExpectSaved(std::numeric_limits<float>::infinity(), R"({"k":null})");
        ExpectSaved(-std::numeric_limits<double>::infinity(), R"({"k":null})");
        ExpectSaved(std::numeric_limits<double>::quiet_NaN(), R"({"k":null})");
    }

    TEST(JsonStreamSerializerTest, StringsAreEscapedAndInvalidUtf8IsReplaced) {
        ExpectSaved(String("a \"quoted\" \\ text\n"), R"({"k":"a \"quoted\" \\ text\n"})");
        ExpectSaved(FixedString<8, false>("fixed"), R"({"k":"fixed"})");
        ExpectSaved(PoolString{"pooled"_intern}, R"({"k":"pooled"})");
        ExpectSaved(RegMode::Auto, R"({"k":"Auto"})");
        ExpectSaved(String(), R"({"k":""})");
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

    TEST(JsonStreamSerializerTest, SequencesAreArraysAndMapsAreArraysOfEntries) {
        ExpectSaved(Vector<int32_t>{1, 2, 3}, R"({"k":[1,2,3]})");
        ExpectSaved(Vector<int32_t>{}, R"({"k":[]})");
        ExpectSaved(Vector<String>{"a", "b"}, R"({"k":["a","b"]})");
        ExpectSaved(SmallVector<int32_t, 4>{4, 5}, R"({"k":[4,5]})");
        ExpectSaved(FixedVector<int32_t, 3>{6}, R"({"k":[6]})");
        ExpectSaved(Array<int32_t, 2>{7, 8}, R"({"k":[7,8]})");
        ExpectSaved(Vector<bool>{}, R"({"k":[]})");

        HashMap<String, int32_t> table;
        table["b"] = 2;
        table["a"] = 1;
        ExpectSaved(table, R"({"k":[{"key":"a","value":1},{"key":"b","value":2}]})");
        Map<int32_t, String> ordered;
        ordered[2] = "x";
        ordered[1] = "y";
        ExpectSaved(ordered, R"({"k":[{"key":1,"value":"y"},{"key":2,"value":"x"}]})");
    }

    TEST(JsonStreamSerializerTest, OptionalPairAndVariantShapes) {
        ExpectSaved(Optional<int32_t>(5), R"({"k":5})");
        ExpectSaved(Optional<int32_t>(), R"({})");
        ExpectSaved(Pair<int32_t, String>(1, "s"), R"({"k":{"first":1,"second":"s"}})");
        ExpectSaved(Variant<int32_t, String>(7), R"({"k":{"index":0,"value":7}})");
        ExpectSaved(Variant<int32_t, String>(String("s")), R"({"k":{"index":1,"value":"s"}})");
        ExpectSaved(Variant<std::monostate, int32_t>(), R"({"k":{"index":0}})");
        ExpectSaved(Vector<Variant<int32_t, String>>{7, String("s")}, R"({"k":[{"index":0,"value":7},{"index":1,"value":"s"}]})");
    }

    TEST(JsonStreamSerializerTest, ObjectsAndPointersAreObjects) {
        RegInner inner;
        inner.value = 3;
        inner.label = "l";
        IntrusivePtr<RegNode> node = New<RegNode>();
        node->id = 9;

        ExpectSaved(inner, R"({"k":{"value":3,"label":"l"}})");
        ExpectSaved(node, R"({"k":{"id":9}})");
        ExpectSaved(IntrusivePtr<RegNode>(), R"({})");
        ExpectSaved(Vector<RegInner>{inner, RegInner{}}, R"({"k":[{"value":3,"label":"l"},{"value":0,"label":""}]})");
    }

    TEST(JsonStreamSerializerTest, EveryKindOfAClassSavesUnderItsSerializedName) {
        RegAllKinds all;
        all.flag = true;
        all.tiny = -1;
        all.count = 3;
        all.ratio = 0.1f;
        all.text = "t";
        all.fixedText = "f";
        all.name = "n"_intern;
        all.mode = RegMode::On;
        all.items = {1};
        all.table["k"] = 2;
        all.maybe = 4;
        all.choice = String("c");

        ExpectObject(all, R"({"flag":true,"tiny":-1,"word":0,"count":3,"big":0,"utiny":0,"uword":0,"ucount":0,"ubig":0,"ratio":0.1,"precise":0.0,"text":"t","fixedText":"f",)"
                          R"("name":"n","mode":"On","inner":{"value":0,"label":""},"items":[1],"smallItems":[],"fixedItems":[],"pairOfInts":[0,0],)"
                          R"("table":[{"key":"k","value":2}],"fixedTable":[],"ordered":[],"maybe":4,"entry":{"first":0,"second":""},"choice":{"index":1,"value":"c"},"inners":[]})");
    }

    TEST(JsonStreamSerializerTest, TheProbeStateSavesEveryFieldContentHas) {
        ExpectObject(Fuzz::MakeFilledProbe(),
                     R"({"flag":true,"i8":-8,"u8":200,"i16":-1600,"u16":60000,"i32":-320000,"u32":4000000000,"i64":-6400000000000,"u64":18000000000000000000,"f32":1.5,"f64":-2.25,)"
                     R"("text":"hello","name":"Hero","mode":"On","leaf":{"id":7,"weight":0.5,"label":"nested"},)"
                     R"("leaves":[{"id":0,"weight":0.0,"label":"first"},{"id":1,"weight":0.25,"label":"other"},{"id":2,"weight":0.5,"label":"other"}],)"
                     R"("numbers":[10,-20,30],"words":["one","two"],)"
                     R"("tree":{"id":1,"children":[{"id":2,"children":[]},{"id":3,"children":[{"id":4,"children":[]}]}]}})");
    }

    TEST(JsonStreamSerializerTest, AFactoryElementCarriesItsTypeAndEnabled) {
        NoHookProbeList list;
        for (const int32_t value : {4, 5}) {
            IntrusivePtr<NoHookProbeImpl> item = New<NoHookProbeImpl>();
            item->value = value;
            list.items.push_back(item);
        }
        list.items.push_back({});

        ExpectObject(list, R"({"items":[{"enabled":true,"type":"NoHookProbeImpl","value":4},{"enabled":true,"type":"NoHookProbeImpl","value":5}]})");
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
        EXPECT_TRUE(JsonDoc(text).IsParsed());
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
        EXPECT_TRUE(JsonDoc(text).IsParsed());
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
