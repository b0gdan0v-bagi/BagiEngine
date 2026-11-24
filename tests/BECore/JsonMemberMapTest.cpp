// JsonMemberMap (Control/JsonMemberMap.h): the bytes of Write() are the contract: the members in byte order, compact.

#include <BECore/Control/JsonMemberMap.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Written(const JsonMemberMap& map, int indent = JsonWriter::kCompact) {
            String out;
            JsonWriter writer(out, indent);
            map.Write(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

    }  // namespace

    TEST(JsonMemberMapTest, SetReplacesAValueAndKeepsTheKeysSorted) {
        JsonMemberMap map;

        map.Set("b", "1");
        map.Set("a", R"("x")");
        map.Set("c", "[1,2]");
        map.Set("b", "2");

        EXPECT_EQ(map.Size(), 3u);
        EXPECT_EQ(Written(map), R"({"a":"x","b":2,"c":[1,2]})");
        ASSERT_NE(map.Find("b"), nullptr);
        EXPECT_EQ(*map.Find("b"), "2");
        EXPECT_EQ(map.Find("d"), nullptr);
    }

    TEST(JsonMemberMapTest, AnEmptyMapIsAnEmptyObject) {
        const JsonMemberMap map;

        EXPECT_TRUE(map.Empty());
        EXPECT_EQ(Written(map), "{}");
        EXPECT_EQ(Written(map, 2), "{}");
    }

    TEST(JsonMemberMapTest, AValueThatIsNotJsonIsWrittenAsNull) {
        JsonMemberMap map;

        map.Set("bad", "{oops");
        map.Set("empty", "");

        EXPECT_EQ(Written(map), R"({"bad":null,"empty":null})");
    }

    TEST(JsonMemberMapTest, AssignKeepsTheMembersThatAreNotKnownWithTheirNestedValues) {
        const JsonDoc doc(R"({"known":1,"zeta":{"y":[1,{"b":1,"a":2}],"x":"s"},"alpha":true,"also":2,"alpha":false})");
        JsonMemberMap map;
        map.Set("stale", "1");

        map.Assign(doc.Root(), {"known", "also"});

        EXPECT_EQ(map.Size(), 2u);
        EXPECT_FALSE(map.Contains("stale"));
        EXPECT_FALSE(map.Contains("known"));
        EXPECT_EQ(Written(map), R"({"alpha":true,"zeta":{"x":"s","y":[1,{"a":2,"b":1}]}})");
    }

    TEST(JsonMemberMapTest, AssignOfAViewThatIsNotAnObjectLeavesTheMapEmpty) {
        JsonMemberMap map;
        map.Set("a", "1");

        map.Assign(JsonDoc("[1]"), {});
        EXPECT_TRUE(map.Empty());

        map.Set("a", "1");
        map.Assign(JsonView(), {});
        EXPECT_TRUE(map.Empty());
    }

    TEST(JsonMemberMapTest, AKeyWithAnEscapeIsTheKeyItStandsFor) {
        const JsonDoc doc(R"({"a\"b":1,"eA":2})");
        JsonMemberMap map;

        map.Assign(doc.Root(), {"eA"});

        EXPECT_TRUE(map.Contains("a\"b"));
        EXPECT_EQ(Written(map), R"({"a\"b":1})");
    }

    TEST(JsonMemberMapTest, WriteIsWhatTheDomDumpedForTheSameMembers) {
        const char* const text = R"({"version":3,"fontScale":1.1,"x":{"b":[1,{"d":null,"c":"é"}],"a":{}},"list":[],"name":"line\n\"q\""})";
        const JsonDoc doc(text);
        JsonMemberMap map;
        map.Assign(doc.Root(), {});

        EXPECT_EQ(Written(map), R"({"fontScale":1.1,"list":[],"name":"line\n\"q\"","version":3,"x":{"a":{},"b":[1,{"c":"é","d":null}]}})");
        EXPECT_EQ(Written(map, 2), "{\n  \"fontScale\": 1.1,\n  \"list\": [],\n  \"name\": \"line\\n\\\"q\\\"\",\n  \"version\": 3,\n  \"x\": {\n    \"a\": {},\n    \"b\": [\n      1,\n      {\n     "
                                   "   \"c\": \"é\",\n        \"d\": null\n      }\n    ]\n  }\n}");
    }

    TEST(JsonMemberMapTest, SetWithStoresWhatTheCallbackWrites) {
        JsonMemberMap map;

        map.SetWith("v", [](JsonWriter& writer) { writer.Value(0.75f); });
        map.SetWith("list", [](JsonWriter& writer) {
            writer.BeginArray();
            writer.Value("a");
            writer.Value(true);
            writer.EndArray();
        });

        EXPECT_EQ(Written(map), R"({"list":["a",true],"v":0.75})");
    }

    TEST(JsonMemberMapTest, SetWithOfAnIncompleteWriteStoresNothing) {
        JsonMemberMap map;
        map.Set("v", "1");

        map.SetWith("v", [](JsonWriter& writer) { writer.BeginArray(); });
        map.SetWith("w", [](JsonWriter&) {});

        EXPECT_EQ(Written(map), R"({"v":1})");
    }

    TEST(JsonMemberMapTest, EraseRemovesTheKeyAndIgnoresAMissingOne) {
        JsonMemberMap map;
        map.Set("a", "1");
        map.Set("b", "2");

        map.Erase("a");
        map.Erase("nope");

        EXPECT_EQ(Written(map), R"({"b":2})");
        map.Clear();
        EXPECT_TRUE(map.Empty());
    }

    TEST(JsonMemberMapTest, ACopyIsIndependent) {
        JsonMemberMap map;
        map.Set("a", "1");

        JsonMemberMap copy = map;
        copy.Set("a", "2");
        copy.Set("b", "3");
        const JsonMemberMap moved = eastl::move(copy);

        EXPECT_EQ(Written(map), R"({"a":1})");
        EXPECT_EQ(Written(moved), R"({"a":2,"b":3})");
    }

}  // namespace BECore::Tests
