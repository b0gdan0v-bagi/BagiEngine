// JsonSerializer (Control/JsonSerializer.h): the JSON text of every shape the data accessors write.
// The generated code of the test types is included in TypeRegistryTest.cpp and FuzzProbeAccess.cpp; here the types are used through their headers.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/FactoryInitializeTest.h>
#include <BECore/FuzzProbe.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/TypeRegistryTest.h>
#include <Common/Require.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace RegistryTestData;

    namespace {

        /// The text of `Ser::Save(value, "k")` as the root object
        template <typename T>
        String SavedText(const T& value) {
            JsonSerializer serializer;
            Ser::Save(serializer, value, "k");
            return DumpJson(serializer.Take());
        }

        template <typename T>
        void ExpectSaved(const T& value, StringView expected) {
            EXPECT_EQ(SavedText(value), String(expected.data(), expected.size()));
        }

        template <typename T>
        String ObjectText(const T& object) {
            JsonSerializer serializer;
            object.Serialize(serializer);
            return DumpJson(serializer.Take());
        }

    }  // namespace

    TEST(JsonSerializerTest, IntegersAreExactJsonIntegers) {
        ExpectSaved(int8_t{-5}, R"({"k":-5})");
        ExpectSaved(uint8_t{200}, R"({"k":200})");
        ExpectSaved(int16_t{-1600}, R"({"k":-1600})");
        ExpectSaved(uint16_t{60000}, R"({"k":60000})");
        ExpectSaved(int32_t{-320000}, R"({"k":-320000})");
        ExpectSaved(uint32_t{4000000000u}, R"({"k":4000000000})");
        ExpectSaved(std::numeric_limits<int64_t>::min(), R"({"k":-9223372036854775808})");
        ExpectSaved(std::numeric_limits<int64_t>::max(), R"({"k":9223372036854775807})");
        ExpectSaved(std::numeric_limits<uint64_t>::max(), R"({"k":18446744073709551615})");
    }

    TEST(JsonSerializerTest, FloatsAreTheShortestTextThatReadsBack) {
        ExpectSaved(0.1f, R"({"k":0.1})");
        ExpectSaved(1.5f, R"({"k":1.5})");
        ExpectSaved(16777216.0f, R"({"k":16777216.0})");
        ExpectSaved(0.1, R"({"k":0.1})");
        ExpectSaved(-2.25, R"({"k":-2.25})");

        const float third = 1.0f / 3.0f;
        const auto parsed = ParseJson(SavedText(third));
        ASSERT_TRUE(parsed.has_value());
        const Json* member = FindJsonMember(*parsed, "k");
        ASSERT_NE(member, nullptr);
        EXPECT_EQ(static_cast<float>(member->get<double>()), third);
    }

    TEST(JsonSerializerTest, ANumberWithoutAJsonFormIsNull) {
        ExpectSaved(std::numeric_limits<float>::quiet_NaN(), R"({"k":null})");
        ExpectSaved(std::numeric_limits<float>::infinity(), R"({"k":null})");
        ExpectSaved(-std::numeric_limits<double>::infinity(), R"({"k":null})");
        ExpectSaved(std::numeric_limits<double>::quiet_NaN(), R"({"k":null})");
    }

    TEST(JsonSerializerTest, BoolsAreJsonBools) {
        ExpectSaved(true, R"({"k":true})");
        ExpectSaved(false, R"({"k":false})");
    }

    TEST(JsonSerializerTest, StringsAreJsonStrings) {
        ExpectSaved(String("a \"quoted\" \\ text\n"), R"({"k":"a \"quoted\" \\ text\n"})");
        ExpectSaved(FixedString<8, false>("fixed"), R"({"k":"fixed"})");
        ExpectSaved(PoolString{"pooled"_intern}, R"({"k":"pooled"})");
        ExpectSaved(RegMode::Auto, R"({"k":"Auto"})");
        ExpectSaved(String(), R"({"k":""})");
    }

    TEST(JsonSerializerTest, InvalidUtf8IsReplacedNotFatal) {
        const String broken("a\xff"
                            "b");

        ExpectSaved(broken, "{\"k\":\"a\xEF\xBF\xBD"
                            "b\"}");
    }

    TEST(JsonSerializerTest, WritingTextNeverInterns) {
        constexpr StringView kNeverInterned = "json_serializer_never_interned_text";
        JsonSerializer serializer;

        serializer.Write("view", kNeverInterned);
        serializer.WriteAttribute("attribute", kNeverInterned);
        serializer.Write("name", "json_serializer_interned_by_the_test"_intern);

        EXPECT_FALSE(PoolString::Find(kNeverInterned).has_value());
    }

    TEST(JsonSerializerTest, SequencesAreArrays) {
        ExpectSaved(Vector<int32_t>{1, 2, 3}, R"({"k":[1,2,3]})");
        ExpectSaved(Vector<int32_t>{}, R"({"k":[]})");
        ExpectSaved(Vector<String>{"a", "b"}, R"({"k":["a","b"]})");
        ExpectSaved(SmallVector<int32_t, 4>{4, 5}, R"({"k":[4,5]})");
        ExpectSaved(FixedVector<int32_t, 3>{6}, R"({"k":[6]})");
        ExpectSaved(Array<int32_t, 2>{7, 8}, R"({"k":[7,8]})");
        ExpectSaved(Vector<bool>{}, R"({"k":[]})");
    }

    TEST(JsonSerializerTest, AnArrayOfObjectsKeepsTheObjects) {
        RegInner first;
        first.value = 1;
        first.label = "x";
        RegInner second;
        Vector<RegInner> list{first, second};

        ExpectSaved(list, R"({"k":[{"label":"x","value":1},{"label":"","value":0}]})");
    }

    TEST(JsonSerializerTest, AnElementWithOnlyAScalarValueIsThatScalar) {
        JsonSerializer serializer;
        size_t count = 2;
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
        serializer.EndArray();

        EXPECT_EQ(DumpJson(serializer.Take()), R"({"list":[1,{"other":3,"value":2},{"value":{"x":4}}]})");
    }

    TEST(JsonSerializerTest, MapsAreArraysOfEntries) {
        HashMap<String, int32_t> table;
        table["b"] = 2;
        table["a"] = 1;
        Map<int32_t, String> ordered;
        ordered[2] = "x";
        ordered[1] = "y";

        ExpectSaved(table, R"({"k":[{"key":"a","value":1},{"key":"b","value":2}]})");
        ExpectSaved(ordered, R"({"k":[{"key":1,"value":"y"},{"key":2,"value":"x"}]})");
    }

    TEST(JsonSerializerTest, OptionalPairAndVariantShapes) {
        ExpectSaved(Optional<int32_t>(5), R"({"k":5})");
        ExpectSaved(Optional<int32_t>(), R"({})");
        ExpectSaved(Pair<int32_t, String>(1, "s"), R"({"k":{"first":1,"second":"s"}})");
        ExpectSaved(Variant<int32_t, String>(7), R"({"k":{"index":0,"value":7}})");
        ExpectSaved(Variant<int32_t, String>(String("s")), R"({"k":{"index":1,"value":"s"}})");
        ExpectSaved(Variant<std::monostate, int32_t>(), R"({"k":{"index":0}})");
        ExpectSaved(Vector<Variant<int32_t, String>>{7, String("s")}, R"({"k":[{"index":0,"value":7},{"index":1,"value":"s"}]})");
    }

    TEST(JsonSerializerTest, ANestedObjectAndAPointerAreObjects) {
        RegInner inner;
        inner.value = 3;
        inner.label = "l";
        IntrusivePtr<RegNode> node = New<RegNode>();
        node->id = 9;

        ExpectSaved(inner, R"({"k":{"label":"l","value":3}})");
        ExpectSaved(node, R"({"k":{"id":9}})");
        ExpectSaved(IntrusivePtr<RegNode>(), R"({})");
    }

    TEST(JsonSerializerTest, EveryKindOfAClassSavesUnderItsSerializedName) {
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

        const auto parsed = ParseJson(ObjectText(all));
        ASSERT_TRUE(parsed.has_value());

        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "flag"))), "true");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "tiny"))), "-1");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "ratio"))), "0.1");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "text"))), R"("t")");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "fixedText"))), R"("f")");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "name"))), R"("n")");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "mode"))), R"("On")");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "items"))), "[1]");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "smallItems"))), "[]");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "pairOfInts"))), "[0,0]");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "table"))), R"([{"key":"k","value":2}])");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "maybe"))), "4");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "choice"))), R"({"index":1,"value":"c"})");
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "inner"))), R"({"label":"","value":0})");
        EXPECT_EQ(FindJsonMember(*parsed, "node"), nullptr);
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*parsed, "entry"))), R"({"first":0,"second":""})");
    }

    TEST(JsonSerializerTest, TheProbeStateSavesEveryFieldContentHas) {
        const FuzzProbeData::ProbeState state = Fuzz::MakeFilledProbe();

        EXPECT_EQ(ObjectText(state), R"({"f32":1.5,"f64":-2.25,"flag":true,"i16":-1600,"i32":-320000,"i64":-6400000000000,"i8":-8,)"
                                     R"("leaf":{"id":7,"label":"nested","weight":0.5},)"
                                     R"("leaves":[{"id":0,"label":"first","weight":0.0},{"id":1,"label":"other","weight":0.25},{"id":2,"label":"other","weight":0.5}],)"
                                     R"("mode":"On","name":"Hero","numbers":[10,-20,30],"text":"hello",)"
                                     R"("tree":{"children":[{"children":[],"id":2},{"children":[{"children":[],"id":4}],"id":3}],"id":1},)"
                                     R"("u16":60000,"u32":4000000000,"u64":18000000000000000000,"u8":200,"words":["one","two"]})");
    }

    TEST(JsonSerializerTest, AFactoryElementCarriesItsTypeAndEnabled) {
        NoHookProbeList list;
        for (const int32_t value : {4, 5}) {
            IntrusivePtr<NoHookProbeImpl> item = New<NoHookProbeImpl>();
            item->value = value;
            list.items.push_back(item);
        }
        list.items.push_back({});

        EXPECT_EQ(ObjectText(list), R"({"items":[{"enabled":true,"type":"NoHookProbeImpl","value":4},{"enabled":true,"type":"NoHookProbeImpl","value":5}]})");
    }

    TEST(JsonSerializerTest, TakeGivesTheRootAndStartsAgain) {
        JsonSerializer serializer;
        serializer.Write("a", int32_t{1});

        EXPECT_EQ(DumpJson(serializer.Take()), R"({"a":1})");
        EXPECT_EQ(DumpJson(serializer.Take()), "{}");

        serializer.Write("b", int32_t{2});
        EXPECT_EQ(DumpJson(serializer.Take()), R"({"b":2})");
    }

    TEST(JsonSerializerTest, AWriteAgainReplacesAndAnObjectIsReopened) {
        JsonSerializer serializer;

        serializer.Write("a", int32_t{1});
        serializer.Write("a", int32_t{2});
        ASSERT_TRUE(serializer.BeginObject("o"));
        serializer.Write("x", int32_t{1});
        serializer.EndObject();
        ASSERT_TRUE(serializer.BeginObject("o"));
        serializer.Write("y", int32_t{2});
        serializer.EndObject();

        EXPECT_EQ(DumpJson(serializer.Take()), R"({"a":2,"o":{"x":1,"y":2}})");
    }

    TEST(JsonSerializerTest, CallsOutOfOrderAreIgnored) {
        JsonSerializer serializer;
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

        EXPECT_EQ(DumpJson(serializer.Take()), R"({"a":1,"b":3,"list":[{}]})");
    }

}  // namespace BECore::Tests
