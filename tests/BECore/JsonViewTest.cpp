// JsonView (Control/JsonView.h): the checked reads over a JsonTape. The numbers JsonWriter writes must read back as the value it was given.

#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <bit>
#include <cfloat>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <random>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kSeed = 0x5EEDB0B5ULL;
        constexpr size_t kRandomNumbers = 20000;

        /// A tape over @p text that stays alive with the fixture
        struct Doc {
            explicit Doc(StringView source) : text(source.data(), source.size()) {
                parsed = tape.Parse(text).has_value();
            }

            JsonView Root() const {
                return JsonView(tape, 0);
            }

            String text;
            JsonTape tape;
            bool parsed = false;
        };

        template <typename TWrite>
        String Written(TWrite&& write) {
            String out;
            JsonWriter writer(out, JsonWriter::kCompact);
            write(writer);
            return out;
        }

        String Shown(StringView text) {
            return String(text.data(), text.size());
        }

        String Decoded(const JsonView& view) {
            String out;
            EXPECT_TRUE(view.ReadText(out));
            return out;
        }

    }  // namespace

    TEST(JsonViewTest, AnInvalidViewReadsAsNothing) {
        const JsonView none;
        EXPECT_FALSE(none.IsValid());
        EXPECT_FALSE(none.IsObject());
        EXPECT_FALSE(none.IsNull());
        EXPECT_EQ(none.Size(), 0u);
        EXPECT_TRUE(none.Raw().empty());
        EXPECT_FALSE(none.Find("a").has_value());
        EXPECT_FALSE(none.AsBool().has_value());
        EXPECT_FALSE(none.AsInt64().has_value());
        EXPECT_FALSE(none.AsDouble().has_value());
        EXPECT_FALSE(none.Text().has_value());
        for ([[maybe_unused]] const JsonView item : none.Items()) {
            ADD_FAILURE();
        }

        const JsonTape empty;
        const JsonView over(empty, 0);
        EXPECT_FALSE(over.IsValid());
        Doc doc("[1]");
        EXPECT_FALSE(JsonView(doc.tape, 5).IsValid());
    }

    TEST(JsonViewTest, EveryAccessorAnswersOnlyItsOwnKind) {
        const Doc doc(R"([null,true,-12,1.5,"s",[1],{"a":1}])");
        ASSERT_TRUE(doc.parsed);
        Vector<JsonView> items;
        for (const JsonView item : doc.Root().Items()) {
            items.push_back(item);
        }
        ASSERT_EQ(items.size(), 7u);
        const JsonView& null = items[0];
        const JsonView& boolean = items[1];
        const JsonView& integer = items[2];
        const JsonView& number = items[3];
        const JsonView& string = items[4];
        const JsonView& array = items[5];
        const JsonView& object = items[6];

        EXPECT_TRUE(null.IsNull());
        EXPECT_EQ(boolean.AsBool(), true);
        EXPECT_TRUE(integer.IsInteger() && integer.IsNumber());
        EXPECT_TRUE(number.IsNumber() && !number.IsInteger());
        EXPECT_TRUE(string.IsString());
        EXPECT_TRUE(array.IsArray());
        EXPECT_TRUE(object.IsObject());

        for (const JsonView& other : {null, integer, number, string, array, object}) {
            EXPECT_FALSE(other.AsBool().has_value());
        }
        for (const JsonView& other : {null, boolean, number, string, array, object}) {
            EXPECT_FALSE(other.AsInt64().has_value());
            EXPECT_FALSE(other.AsUint64().has_value());
        }
        for (const JsonView& other : {null, boolean, string, array, object}) {
            EXPECT_FALSE(other.AsDouble().has_value());
        }
        for (const JsonView& other : {null, boolean, integer, number, array, object}) {
            EXPECT_FALSE(other.Text().has_value());
            String untouched = "keep";
            EXPECT_FALSE(other.ReadText(untouched));
            EXPECT_EQ(untouched, String("keep"));
        }
        EXPECT_EQ(integer.AsInt64(), -12);
        EXPECT_EQ(integer.AsDouble(), -12.0);
        EXPECT_EQ(number.AsDouble(), 1.5);
        EXPECT_EQ(array.Size(), 1u);
        EXPECT_EQ(object.Size(), 1u);
        EXPECT_EQ(string.Size(), 0u);
        EXPECT_FALSE(array.Find("a").has_value());
        EXPECT_FALSE(object.Items().begin() != object.Items().end());
        EXPECT_FALSE(array.Members().begin() != array.Members().end());
    }

    TEST(JsonViewTest, IntegersAtTheEdgesOf64Bits) {
        struct Case {
            StringView text;
            Optional<int64_t> asInt;
            Optional<uint64_t> asUint;
        };
        const Array<Case, 11> cases = {{
            {"-9223372036854775808", std::numeric_limits<int64_t>::min(), std::nullopt},
            {"-9223372036854775809", std::nullopt, std::nullopt},
            {"9223372036854775807", std::numeric_limits<int64_t>::max(), uint64_t{9223372036854775807ULL}},
            {"9223372036854775808", std::nullopt, uint64_t{9223372036854775808ULL}},
            {"18446744073709551615", std::nullopt, std::numeric_limits<uint64_t>::max()},
            {"18446744073709551616", std::nullopt, std::nullopt},
            {"-0", int64_t{0}, uint64_t{0}},
            {"0", int64_t{0}, uint64_t{0}},
            {"-1", int64_t{-1}, std::nullopt},
            {"1e2", std::nullopt, std::nullopt},
            {"1.0", std::nullopt, std::nullopt},
        }};
        for (const Case& c : cases) {
            const Doc doc(c.text);
            ASSERT_TRUE(doc.parsed) << Shown(c.text);
            EXPECT_EQ(doc.Root().AsInt64(), c.asInt) << Shown(c.text);
            EXPECT_EQ(doc.Root().AsUint64(), c.asUint) << Shown(c.text);
        }
    }

    TEST(JsonViewTest, DoublesBeyondTheRangeAreInfiniteOrZeroNotAnError) {
        EXPECT_EQ(Doc("1e400").Root().AsDouble(), std::numeric_limits<double>::infinity());
        EXPECT_EQ(Doc("-1e400").Root().AsDouble(), -std::numeric_limits<double>::infinity());
        EXPECT_EQ(Doc("1e-400").Root().AsDouble(), 0.0);
        const Optional<double> negativeZero = Doc("-0.0").Root().AsDouble();
        ASSERT_TRUE(negativeZero.has_value());
        EXPECT_TRUE(std::signbit(*negativeZero));
        EXPECT_EQ(Doc("1E2").Root().AsDouble(), 100.0);
        // An integer of 40 digits is a number of the double range
        EXPECT_EQ(Doc("1000000000000000000000000000000000000000").Root().AsDouble(), 1e39);
    }

    TEST(JsonViewTest, DoublesReadBackWhatJsonWriterWrote) {
        std::mt19937_64 random(kSeed);
        Vector<double> values = {0.0, -0.0, 1.0, 0.1, 1.0 / 3.0, 5e-324, 2.2250738585072014e-308, DBL_MAX, -DBL_MAX, 1e21, 123456789012345678.0, 9007199254740993.0};
        for (size_t n = 0; n < kRandomNumbers; ++n) {
            const double value = std::bit_cast<double>(random());
            if (std::isfinite(value)) {
                values.push_back(value);
            }
        }
        for (const double value : values) {
            const String text = Written([&](JsonWriter& w) { w.Value(value); });
            const Doc doc(text);
            ASSERT_TRUE(doc.parsed) << text;
            const Optional<double> read = doc.Root().AsDouble();
            ASSERT_TRUE(read.has_value()) << text;
            EXPECT_EQ(std::bit_cast<uint64_t>(*read), std::bit_cast<uint64_t>(value)) << text;
        }
    }

    TEST(JsonViewTest, FloatsReadBackWhatJsonWriterWrote) {
        std::mt19937_64 random(kSeed);
        for (size_t n = 0; n < kRandomNumbers; ++n) {
            const float value = std::bit_cast<float>(static_cast<uint32_t>(random()));
            if (!std::isfinite(value)) {
                continue;
            }
            const String text = Written([&](JsonWriter& w) { w.Value(value); });
            const Doc doc(text);
            ASSERT_TRUE(doc.parsed) << text;
            const Optional<double> read = doc.Root().AsDouble();
            ASSERT_TRUE(read.has_value()) << text;
            EXPECT_EQ(std::bit_cast<uint32_t>(static_cast<float>(*read)), std::bit_cast<uint32_t>(value)) << text;
        }
    }

    TEST(JsonViewTest, TextIsAViewWithoutEscapesAndDecodedWithThem) {
        const Doc doc(R"(["plain","a\nb\"\\\/\u00e9\ud83d\ude00\u0000z","","\u0041"])");
        ASSERT_TRUE(doc.parsed);
        Vector<JsonView> items;
        for (const JsonView item : doc.Root().Items()) {
            items.push_back(item);
        }
        ASSERT_EQ(items.size(), 4u);

        EXPECT_EQ(items[0].Text(), StringView("plain"));
        EXPECT_EQ(Decoded(items[0]), String("plain"));
        EXPECT_FALSE(items[1].Text().has_value());
        const String expected("a\nb\"\\/\xC3\xA9\xF0\x9F\x98\x80\0z", 14);
        EXPECT_EQ(Decoded(items[1]), expected);
        EXPECT_EQ(items[2].Text(), StringView(""));
        EXPECT_FALSE(items[3].Text().has_value());
        EXPECT_EQ(Decoded(items[3]), String("A"));
    }

    TEST(JsonViewTest, ReadTextReusesTheCapacityOfTheTarget) {
        const Doc doc(R"(["short","with \u00e9 escape"])");
        ASSERT_TRUE(doc.parsed);
        String out;
        out.reserve(64);
        const char* const before = out.data();
        for (const JsonView item : doc.Root().Items()) {
            ASSERT_TRUE(item.ReadText(out));
        }
        EXPECT_EQ(out.data(), before);
        EXPECT_EQ(out, String("with \xC3\xA9 escape"));
    }

    TEST(JsonViewTest, RawIsTheTextAsWritten) {
        const Doc doc(R"( {"k": [1, "s\n", {"x":null}] } )");
        ASSERT_TRUE(doc.parsed);
        EXPECT_EQ(doc.Root().Raw(), StringView(R"({"k": [1, "s\n", {"x":null}] })"));
        const Optional<JsonView> array = doc.Root().Find("k");
        ASSERT_TRUE(array.has_value());
        EXPECT_EQ(array->Raw(), StringView(R"([1, "s\n", {"x":null}])"));
        Vector<StringView> raws;
        for (const JsonView item : array->Items()) {
            raws.push_back(item.Raw());
        }
        ASSERT_EQ(raws.size(), 3u);
        EXPECT_EQ(raws[0], StringView("1"));
        EXPECT_EQ(raws[1], StringView(R"("s\n")"));
        EXPECT_EQ(raws[2], StringView(R"({"x":null})"));
    }

    TEST(JsonViewTest, FindDoesNotDependOnTheOrderOfKeys) {
        const Doc doc(R"({"c":3,"a":1,"b":{"a":9}})");
        ASSERT_TRUE(doc.parsed);
        for (const StringView key : {StringView("a"), StringView("b"), StringView("c")}) {
            EXPECT_TRUE(doc.Root().Find(key).has_value()) << Shown(key);
        }
        EXPECT_EQ(Require(doc.Root().Find("a")).AsInt64(), 1);
        EXPECT_FALSE(doc.Root().Find("d").has_value());
        EXPECT_FALSE(doc.Root().Find("").has_value());
        // A nested key is not a member of the outer object
        const JsonView inner = Require(doc.Root().Find("b"));
        EXPECT_EQ(Require(inner.Find("a")).AsInt64(), 9);
    }

    TEST(JsonViewTest, FindFromACursorWrapsOnceAndMovesPastTheMatch) {
        const Doc doc(R"({"a":1,"b":[1,2,3],"c":3})");
        ASSERT_TRUE(doc.parsed);
        uint32_t cursor = 0;
        EXPECT_EQ(Require(doc.Root().Find("a", cursor)).AsInt64(), 1);
        // The cursor skipped the array of "b" by its subtree, not by one token
        EXPECT_EQ(Require(doc.Root().Find("c", cursor)).AsInt64(), 3);
        // After the last member the cursor is at the end: the next search starts again from the first
        EXPECT_EQ(Require(doc.Root().Find("a", cursor)).AsInt64(), 1);
        EXPECT_EQ(Require(doc.Root().Find("c", cursor)).AsInt64(), 3);
        const uint32_t kept = cursor;
        EXPECT_FALSE(doc.Root().Find("missing", cursor).has_value());
        EXPECT_EQ(cursor, kept);
        uint32_t wild = 7777;
        EXPECT_EQ(Require(doc.Root().Find("b", wild)).Size(), 3u);
    }

    TEST(JsonViewTest, AKeyWithEscapesMatchesItsDecodedForm) {
        const Doc doc(R"({"na\u006de":1,"q\"uote":2,"\u00e9":3})");
        ASSERT_TRUE(doc.parsed);
        EXPECT_EQ(Require(doc.Root().Find("name")).AsInt64(), 1);
        EXPECT_EQ(Require(doc.Root().Find("q\"uote")).AsInt64(), 2);
        EXPECT_EQ(Require(doc.Root().Find("\xC3\xA9")).AsInt64(), 3);
        EXPECT_FALSE(doc.Root().Find("na\\u006de").has_value());
        EXPECT_FALSE(doc.Root().Find("nam").has_value());
        EXPECT_FALSE(doc.Root().Find("namee").has_value());
    }

    TEST(JsonViewTest, TheFirstOfTwoEqualKeysWins) {
        const Doc doc(R"({"a":1,"a":2})");
        ASSERT_TRUE(doc.parsed);
        EXPECT_EQ(Require(doc.Root().Find("a")).AsInt64(), 1);
        EXPECT_EQ(doc.Root().Size(), 2u);
    }

    TEST(JsonViewTest, ItemsAndMembersWalkInTheOrderOfTheText) {
        const Doc doc(R"({"z":[1,[2,3],4],"a":{"x":1},"m":5})");
        ASSERT_TRUE(doc.parsed);
        Vector<String> keys;
        for (const JsonMember member : doc.Root().Members()) {
            keys.push_back(Decoded(member.key));
        }
        ASSERT_EQ(keys.size(), 3u);
        EXPECT_EQ(keys[0], String("z"));
        EXPECT_EQ(keys[1], String("a"));
        EXPECT_EQ(keys[2], String("m"));

        const JsonView list = Require(doc.Root().Find("z"));
        Vector<StringView> items;
        for (const JsonView item : list.Items()) {
            items.push_back(item.Raw());
        }
        ASSERT_EQ(items.size(), 3u);
        EXPECT_EQ(items[1], StringView("[2,3]"));
        EXPECT_EQ(items[2], StringView("4"));

        const Doc empty("[{},[]]");
        for (const JsonView item : empty.Root().Items()) {
            EXPECT_EQ(item.Size(), 0u);
            EXPECT_FALSE(item.Members().begin() != item.Members().end());
            EXPECT_FALSE(item.Items().begin() != item.Items().end());
        }
    }

    TEST(JsonViewTest, TextEqualsComparesTheDecodedText) {
        const Doc doc(R"(["a\u0000b","x"])");
        ASSERT_TRUE(doc.parsed);
        Vector<JsonView> items;
        for (const JsonView item : doc.Root().Items()) {
            items.push_back(item);
        }
        ASSERT_EQ(items.size(), 2u);
        EXPECT_TRUE(items[0].TextEquals(StringView("a\0b", 3)));
        EXPECT_FALSE(items[0].TextEquals("a"));
        EXPECT_FALSE(items[0].TextEquals(StringView("a\0b\0", 4)));
        EXPECT_TRUE(items[1].TextEquals("x"));
        EXPECT_FALSE(doc.Root().TextEquals("x"));
    }

}  // namespace BECore::Tests
