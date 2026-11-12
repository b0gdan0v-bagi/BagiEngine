#include <BECore/Control/Json.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        eastl::string_view View(std::string_view text) {
            return eastl::string_view(text.data(), text.size());
        }

        Json SampleObject() {
            auto parsed = ParseJson(R"({"s":"x","i":5,"neg":-5,"big":18446744073709551615,"f":1.5,"b":true,"n":null,"o":{}})");
            return parsed ? std::move(*parsed) : Json();
        }

    }  // namespace

    TEST(JsonTest, ParsesAValidDocument) {
        const auto parsed = ParseJson(R"({"a":[1,2,3],"b":{"c":"d"}})");

        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(parsed->is_object());
        const Json* a = FindJsonMember(*parsed, "a");
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->size(), 3u);
    }

    TEST(JsonTest, ParsesScalarsToo) {
        const auto parsed = ParseJson("5");

        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(parsed->is_number_integer());
    }

    TEST(JsonTest, BadTextIsAParseErrorNotACrash) {
        const std::string deepOpen(1 << 20, '[');
        const std::vector<std::string> bad = {"", " ", "{", "}", "not json", "{\"a\":}", "[1,2", "\xff\xfe", "{\"a\":\"\xff\"}", "nul", "[1,]", deepOpen};

        for (const std::string& text : bad) {
            const auto parsed = ParseJson(View(text));
            ASSERT_FALSE(parsed.has_value()) << "input of " << text.size() << " bytes";
            EXPECT_EQ(parsed.error().code, eastl::string(ControlErrors::kParseError));
            EXPECT_FALSE(parsed.error().message.empty());
        }
    }

    TEST(JsonTest, VeryDeepNestingIsParsedAndFreedWithoutOverflowingTheStack) {
        std::string deep(1 << 20, '[');
        deep.append(1 << 20, ']');

        const auto parsed = ParseJson(View(deep));

        EXPECT_TRUE(parsed.has_value());
    }

    TEST(JsonTest, NestingScanCountsBracketsOutsideStringsOnly) {
        EXPECT_FALSE(IsJsonNestedDeeperThan(View(""), 0));
        EXPECT_FALSE(IsJsonNestedDeeperThan(View("[[[]]]"), 3));
        EXPECT_TRUE(IsJsonNestedDeeperThan(View("[[[]]]"), 2));
        EXPECT_TRUE(IsJsonNestedDeeperThan(View(R"({"a":{"b":[1]}})"), 2));
        EXPECT_FALSE(IsJsonNestedDeeperThan(View(R"({"a":{"b":[1]}})"), 3));
        // Brackets inside a string, an escaped quote inside a string, and siblings do not add up
        EXPECT_FALSE(IsJsonNestedDeeperThan(View(R"({"a":"[[[[[[","b":"\"[[[["})"), 1));
        EXPECT_FALSE(IsJsonNestedDeeperThan(View("[[],[],[],[[]]]"), 3));
        // Closers with nothing open are ignored, not counted below zero
        EXPECT_FALSE(IsJsonNestedDeeperThan(View("]]]]][[["), 3));
        EXPECT_TRUE(IsJsonNestedDeeperThan(View(std::string(1 << 20, '[')), 128));
    }

    TEST(JsonTest, FindsMembersOfObjectsOnly) {
        const Json object = SampleObject();

        EXPECT_NE(FindJsonMember(object, "s"), nullptr);
        EXPECT_NE(FindJsonMember(object, "n"), nullptr);  // a null member exists
        EXPECT_EQ(FindJsonMember(object, "missing"), nullptr);

        EXPECT_EQ(FindJsonMember(Json::array({1, 2}), "s"), nullptr);
        EXPECT_EQ(FindJsonMember(Json("text"), "s"), nullptr);
        EXPECT_EQ(FindJsonMember(Json(), "s"), nullptr);
    }

    TEST(JsonTest, TypedGettersCheckTheType) {
        const Json object = SampleObject();

        EXPECT_EQ(GetJsonString(object, "s").value_or("").c_str(), std::string("x"));
        EXPECT_FALSE(GetJsonString(object, "i"));
        EXPECT_FALSE(GetJsonString(object, "n"));
        EXPECT_FALSE(GetJsonString(object, "missing"));

        EXPECT_EQ(GetJsonInt(object, "i"), 5);
        EXPECT_EQ(GetJsonInt(object, "neg"), -5);
        EXPECT_FALSE(GetJsonInt(object, "f"));  // 1.5 is not an integer
        EXPECT_FALSE(GetJsonInt(object, "s"));  // neither is a string
        EXPECT_FALSE(GetJsonInt(object, "b"));
        EXPECT_FALSE(GetJsonInt(object, "big"));  // does not fit int64_t
        EXPECT_FALSE(GetJsonInt(object, "missing"));

        EXPECT_DOUBLE_EQ(GetJsonNumber(object, "i").value_or(0.0), 5.0);
        EXPECT_DOUBLE_EQ(GetJsonNumber(object, "f").value_or(0.0), 1.5);
        EXPECT_FALSE(GetJsonNumber(object, "s"));
        EXPECT_FALSE(GetJsonNumber(object, "b"));

        EXPECT_EQ(GetJsonBool(object, "b"), true);
        EXPECT_FALSE(GetJsonBool(object, "i"));
        EXPECT_FALSE(GetJsonBool(object, "s"));
    }

    TEST(JsonTest, TypedGettersOnANonObjectGiveNothing) {
        const Json array = Json::array({1, 2, 3});

        EXPECT_FALSE(GetJsonString(array, "s"));
        EXPECT_FALSE(GetJsonInt(array, "i"));
        EXPECT_FALSE(GetJsonNumber(array, "f"));
        EXPECT_FALSE(GetJsonBool(array, "b"));
    }

    TEST(JsonTest, IntegerLimitsOfInt64AreAccepted) {
        auto parsed = ParseJson(R"({"min":-9223372036854775808,"max":9223372036854775807})");

        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonInt(*parsed, "min"), std::numeric_limits<int64_t>::min());
        EXPECT_EQ(GetJsonInt(*parsed, "max"), std::numeric_limits<int64_t>::max());
    }

    TEST(JsonTest, DumpIsCompact) {
        Json object = Json::object();
        object["a"] = 1;
        object["b"] = Json::array({true, nullptr});

        EXPECT_EQ(std::string(DumpJson(object).c_str()), R"({"a":1,"b":[true,null]})");
        EXPECT_EQ(std::string(DumpJson(Json::object()).c_str()), "{}");
    }

    TEST(JsonTest, DumpReplacesInvalidUtf8InsteadOfAborting) {
        const Json text = MakeJsonString("bad \xff byte");

        const eastl::string dumped = DumpJson(text);

        EXPECT_NE(std::string(dumped.c_str()).find("\xef\xbf\xbd"), std::string::npos);  // U+FFFD
    }

    TEST(JsonTest, StringsRoundTripThroughDumpAndParse) {
        const std::string original = "quote \" backslash \\ newline \n tab \t unicode \xc3\xa9 \xe2\x82\xac";

        const eastl::string dumped = DumpJson(MakeJsonString(View(original)));
        const auto parsed = ParseJson(dumped);

        ASSERT_TRUE(parsed.has_value());
        ASSERT_TRUE(parsed->is_string());
        EXPECT_EQ(parsed->get_ref<const std::string&>(), original);
    }

    TEST(JsonTest, StringMembersMayHoldNulCharacters) {
        const std::string withNul("a\0b", 3);
        Json object = Json::object();
        object["s"] = MakeJsonString(View(withNul));

        const auto text = GetJsonString(object, "s");

        ASSERT_TRUE(text.has_value());
        EXPECT_EQ(text->size(), 3u);
    }

}  // namespace BECore::Tests
