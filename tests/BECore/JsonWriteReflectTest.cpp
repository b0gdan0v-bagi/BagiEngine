// The helpers of the generated writer (Control/JsonWriteReflect.h): the text they write is pinned as a literal, keys in byte order. The types are written by hand here (the generated ones are in the
// tests of the generator).

#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/JsonWriteReflectTypes.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Reflection/TextView.h>
#include <Common/AllocCheck.h>
#include <Common/JsonDoc.h>
#include <Common/Require.h>
#include <Common/ScratchScopeFixture.h>
#include <Common/SerializedText.h>
#include <Generated/JsonWriteReflectTypes.gen.hpp>
#include <cfloat>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        CORE_ENUM(WriteMode, uint8_t, Off, On, Automatic)

        constexpr Array<int, 2> kIndents = {JsonWriter::kCompact, 2};

        template <typename T>
        String Written(const T& object, int indent) {
            String out;
            JsonWriter writer(out, indent);
            object.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        /// The text of @p text copied into a writer of @p indent: the layouts of one document differ in white space only
        String Relaid(StringView text, int indent) {
            const JsonDoc doc(text);
            String out;
            JsonWriter writer(out, indent);
            WriteJson(writer, doc.Root());
            return out;
        }

        /// The compact text of @p object is @p expected, and the indented one is the same document laid out again
        template <typename T>
        void ExpectWrites(const T& object, StringView expected) {
            const String compact = Written(object, JsonWriter::kCompact);
            ASSERT_EQ(compact, String(expected.data(), expected.size()));
            for (const int indent : kIndents) {
                ASSERT_EQ(Written(object, indent), Relaid(expected, indent)) << "indent " << indent;
            }
        }

        /// A class of one field, written the way the generator writes it, so any field type can be checked alone
        template <typename T>
        struct Field {
            T value{};

            static size_t GetStaticFieldCount() {
                return 1;
            }
            void Serialize(ISerializer& s) const {
                Ser::Save(s, value, "field");
            }
            void Deserialize(IDeserializer& /*d*/) {}  // only for the DataAccessor of a nested object, which asks for both
            void WriteJson(JsonWriter& w) const {
                w.BeginObject();
                JsonWrite::SaveField(w, "\"field\":", "field", value);
                w.EndObject();
            }
        };

        template <typename T>
        void ExpectFieldWrites(const T& value, StringView expected) {
            ExpectWrites(Field<T>{value}, expected);
        }

        /// A string field written as a `String`, a `FixedString` and a `PoolString` is one text
        void ExpectStringField(const String& text, StringView expected) {
            ExpectFieldWrites<String>(text, expected);
            ExpectFieldWrites<FixedString<8>>(FixedString<8>(text.data(), text.size()), expected);
            ExpectFieldWrites<PoolString>(PoolString::Intern(StringView(text.data(), text.size())), expected);
        }

        struct Inner {
            int32_t count = 0;
            String name;

            static size_t GetStaticFieldCount() {
                return 2;
            }
            void Serialize(ISerializer& s) const {
                Ser::Save(s, count, "count");
                Ser::Save(s, name, "name");
            }
            void Deserialize(IDeserializer& /*d*/) {}
            void WriteJson(JsonWriter& w) const {
                w.BeginObject();
                JsonWrite::SaveField(w, "\"count\":", "count", count);
                JsonWrite::SaveField(w, "\"name\":", "name", name);
                w.EndObject();
            }
        };

        struct Empty {
            static size_t GetStaticFieldCount() {
                return 0;
            }
            void Serialize(ISerializer& /*s*/) const {}
            void Deserialize(IDeserializer& /*d*/) {}
            void WriteJson(JsonWriter& w) const {
                w.BeginObject();
                w.EndObject();
            }
        };

        /// An element of this class collapses to a scalar
        struct OneValue {
            int32_t value = 0;

            static size_t GetStaticFieldCount() {
                return 1;
            }
            void Serialize(ISerializer& s) const {
                Ser::Save(s, value, "value");
            }
            void Deserialize(IDeserializer& /*d*/) {}
            void WriteJson(JsonWriter& w) const {
                w.BeginObject();
                JsonWrite::SaveField(w, "\"value\":", "value", value);
                w.EndObject();
            }
        };

        /// Every native shape in one class, the keys in sorted order
        struct Native {
            bool active = false;
            Array<int32_t, 3> array{};
            int32_t count = 0;
            FixedVector<int32_t, 4> fixedList;
            Inner inner;
            Vector<int32_t> list;
            WriteMode mode = WriteMode::Off;
            String name;
            Vector<Inner> objects;
            Optional<int32_t> optional;
            PoolString pool;
            float ratio = 0.0f;
            double scale = 0.0;
            FixedString<16> text;

            static size_t GetStaticFieldCount() {
                return 14;
            }
            void Serialize(ISerializer& s) const {
                Ser::Save(s, active, "active");
                Ser::Save(s, array, "array");
                Ser::Save(s, count, "count");
                Ser::Save(s, fixedList, "fixedList");
                Ser::Save(s, inner, "inner");
                Ser::Save(s, list, "list");
                Ser::Save(s, mode, "mode");
                Ser::Save(s, name, "name");
                Ser::Save(s, objects, "objects");
                Ser::Save(s, optional, "optional");
                Ser::Save(s, pool, "pool");
                Ser::Save(s, ratio, "ratio");
                Ser::Save(s, scale, "scale");
                Ser::Save(s, text, "text");
            }
            void Deserialize(IDeserializer& /*d*/) {}
            void WriteJson(JsonWriter& w) const {
                w.BeginObject();
                JsonWrite::SaveField(w, "\"active\":", "active", active);
                JsonWrite::SaveField(w, "\"array\":", "array", array);
                JsonWrite::SaveField(w, "\"count\":", "count", count);
                JsonWrite::SaveField(w, "\"fixedList\":", "fixedList", fixedList);
                JsonWrite::SaveField(w, "\"inner\":", "inner", inner);
                JsonWrite::SaveField(w, "\"list\":", "list", list);
                JsonWrite::SaveField(w, "\"mode\":", "mode", mode);
                JsonWrite::SaveField(w, "\"name\":", "name", name);
                JsonWrite::SaveField(w, "\"objects\":", "objects", objects);
                JsonWrite::SaveField(w, "\"optional\":", "optional", optional);
                JsonWrite::SaveField(w, "\"pool\":", "pool", pool);
                JsonWrite::SaveField(w, "\"ratio\":", "ratio", ratio);
                JsonWrite::SaveField(w, "\"scale\":", "scale", scale);
                JsonWrite::SaveField(w, "\"text\":", "text", text);
                w.EndObject();
            }
        };

        /// Pairs, variants, maps and Math values next to the one shape that stays bridged (`ones`)
        struct Bridged {
            Variant<std::monostate, int32_t, String> choice;
            HashMap<String, int32_t> map;
            Optional<Vec2> maybePoint;
            Vector<OneValue> ones;
            Pair<int32_t, String> pair;
            Vector<Vec2> points;

            static size_t GetStaticFieldCount() {
                return 6;
            }
            void Serialize(ISerializer& s) const {
                Ser::Save(s, choice, "choice");
                Ser::Save(s, map, "map");
                Ser::Save(s, maybePoint, "maybePoint");
                Ser::Save(s, ones, "ones");
                Ser::Save(s, pair, "pair");
                Ser::Save(s, points, "points");
            }
            void Deserialize(IDeserializer& /*d*/) {}
            void WriteJson(JsonWriter& w) const {
                w.BeginObject();
                JsonWrite::SaveField(w, "\"choice\":", "choice", choice);
                JsonWrite::SaveField(w, "\"map\":", "map", map);
                JsonWrite::SaveField(w, "\"maybePoint\":", "maybePoint", maybePoint);
                JsonWrite::SaveField(w, "\"ones\":", "ones", ones);
                JsonWrite::SaveField(w, "\"pair\":", "pair", pair);
                JsonWrite::SaveField(w, "\"points\":", "points", points);
                w.EndObject();
            }
        };

        /// Every byte value once, in order: the control characters, the quote and the backslash, DEL and bytes that are no UTF-8
        String EveryByte() {
            String text;
            for (int byte = 1; byte < 256; ++byte) {
                text.push_back(static_cast<char>(byte));
            }
            return text;
        }

        Inner MakeInner(int32_t count, StringView name) {
            Inner inner;
            inner.count = count;
            inner.name = String(name.data(), name.size());
            return inner;
        }

        Native MakeNative() {
            Native value;
            value.active = true;
            value.array = {-1, 0, 1};
            value.count = -7;
            value.fixedList = {4, 3, 2};
            value.inner = MakeInner(5, "five");
            value.list = {1, 2, 3, 4, 5};
            value.mode = WriteMode::Automatic;
            value.name = "caf\xC3\xA9 \"quoted\"\n";
            value.objects = {MakeInner(1, "one"), MakeInner(2, "")};
            value.optional = 12;
            value.pool = "pooled"_intern;
            value.ratio = 0.1f;
            value.scale = 1e300;
            value.text = "fixed";
            return value;
        }

        Bridged MakeBridged() {
            Bridged value;
            value.choice = String("text");
            value.map = {{"b", 2}, {"a", 1}};
            value.maybePoint = Vec2{1.5f, -2.0f};
            value.ones = {OneValue{3}, OneValue{4}};
            value.pair = {9, "nine"};
            value.points = {Vec2{0.1f, 0.2f}, Vec2{3.0f, 4.0f}};
            return value;
        }

    }  // namespace

    TEST(JsonWriteReflectTest, IntegersOfEveryWidthKeepTheirLimits) {
        ExpectFieldWrites<int8_t>(std::numeric_limits<int8_t>::min(), R"({"field":-128})");
        ExpectFieldWrites<int8_t>(std::numeric_limits<int8_t>::max(), R"({"field":127})");
        ExpectFieldWrites<uint8_t>(std::numeric_limits<uint8_t>::max(), R"({"field":255})");
        ExpectFieldWrites<int16_t>(std::numeric_limits<int16_t>::min(), R"({"field":-32768})");
        ExpectFieldWrites<uint16_t>(std::numeric_limits<uint16_t>::max(), R"({"field":65535})");
        ExpectFieldWrites<int32_t>(std::numeric_limits<int32_t>::min(), R"({"field":-2147483648})");
        ExpectFieldWrites<uint32_t>(std::numeric_limits<uint32_t>::max(), R"({"field":4294967295})");
        ExpectFieldWrites<int64_t>(std::numeric_limits<int64_t>::min(), R"({"field":-9223372036854775808})");
        ExpectFieldWrites<int64_t>(std::numeric_limits<int64_t>::max(), R"({"field":9223372036854775807})");
        ExpectFieldWrites<uint64_t>(std::numeric_limits<uint64_t>::max(), R"({"field":18446744073709551615})");
        ExpectFieldWrites<int32_t>(0, R"({"field":0})");
        ExpectFieldWrites<bool>(true, R"({"field":true})");
        ExpectFieldWrites<bool>(false, R"({"field":false})");
    }

    TEST(JsonWriteReflectTest, FloatsWriteTheShortestTextAndNonFiniteIsNull) {
        ExpectFieldWrites<float>(0.0f, R"({"field":0.0})");
        ExpectFieldWrites<float>(-0.0f, R"({"field":-0.0})");
        ExpectFieldWrites<float>(0.1f, R"({"field":0.1})");
        ExpectFieldWrites<float>(1.0f, R"({"field":1.0})");
        ExpectFieldWrites<float>(-2.5f, R"({"field":-2.5})");
        ExpectFieldWrites<float>(16777216.0f, R"({"field":16777216.0})");
        ExpectFieldWrites<float>(FLT_MIN, R"({"field":1.1754944e-38})");
        ExpectFieldWrites<float>(FLT_MAX, R"({"field":3.4028235e+38})");
        ExpectFieldWrites<float>(std::numeric_limits<float>::denorm_min(), R"({"field":1e-45})");
        ExpectFieldWrites<float>(std::numeric_limits<float>::quiet_NaN(), R"({"field":null})");
        ExpectFieldWrites<float>(std::numeric_limits<float>::infinity(), R"({"field":null})");
        ExpectFieldWrites<float>(-std::numeric_limits<float>::infinity(), R"({"field":null})");
        ExpectFieldWrites<double>(0.0, R"({"field":0.0})");
        ExpectFieldWrites<double>(-0.0, R"({"field":-0.0})");
        ExpectFieldWrites<double>(0.1, R"({"field":0.1})");
        ExpectFieldWrites<double>(1.0, R"({"field":1.0})");
        ExpectFieldWrites<double>(3.0, R"({"field":3.0})");
        ExpectFieldWrites<double>(1e300, R"({"field":1e+300})");
        ExpectFieldWrites<double>(1e-300, R"({"field":1e-300})");
        ExpectFieldWrites<double>(123456789012345678.0, R"({"field":1.2345678901234568e+17})");
        ExpectFieldWrites<double>(DBL_MIN, R"({"field":2.2250738585072014e-308})");
        ExpectFieldWrites<double>(DBL_MAX, R"({"field":1.7976931348623157e+308})");
        ExpectFieldWrites<double>(std::numeric_limits<double>::denorm_min(), R"({"field":5e-324})");
        ExpectFieldWrites<double>(std::numeric_limits<double>::quiet_NaN(), R"({"field":null})");
        ExpectFieldWrites<double>(std::numeric_limits<double>::infinity(), R"({"field":null})");
        ExpectFieldWrites<double>(-std::numeric_limits<double>::infinity(), R"({"field":null})");
    }

    TEST(JsonWriteReflectTest, StringsEscapeAndReplaceInvalidUtf8) {
        const String everyByte = EveryByte();
        ExpectStringField(String(), R"({"field":""})");
        ExpectStringField(String("plain"), R"({"field":"plain"})");
        ExpectStringField(String("a\"b\\c\n\t\r\b\f/"), R"({"field":"a\"b\\c\n\t\r\b\f/"})");
        ExpectStringField(String("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"), R"({"field":"é€😀"})");
        ExpectStringField(String("\xC3\x28\xE2\x82\xF0\x9F\xFF"), R"({"field":"�(���"})");
        ExpectStringField(
            everyByte,
            R"({"field":"\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n)"
            R"(\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f !\"#$%&'()*+,)"
            R"(-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~��������������������������������������������������������������������������������������������������������������������������������"})");
    }

    TEST(JsonWriteReflectTest, AnEnumIsItsName) {
        ExpectFieldWrites<WriteMode>(WriteMode::Off, R"({"field":"Off"})");
        ExpectFieldWrites<WriteMode>(WriteMode::On, R"({"field":"On"})");
        ExpectFieldWrites<WriteMode>(WriteMode::Automatic, R"({"field":"Automatic"})");
    }

    TEST(JsonWriteReflectTest, ANestedObjectIsAMemberObjectEvenWhenEmpty) {
        ExpectFieldWrites<Inner>(MakeInner(3, "x"), R"({"field":{"count":3,"name":"x"}})");
        ExpectFieldWrites<Empty>(Empty{}, R"({"field":{}})");
    }

    TEST(JsonWriteReflectTest, AnEmptyOptionalWritesNoMemberAndASetOneWritesItsValue) {
        ExpectFieldWrites<Optional<int32_t>>(std::nullopt, R"({})");
        ExpectFieldWrites<Optional<int32_t>>(0, R"({"field":0})");
        ExpectFieldWrites<Optional<String>>(String("set"), R"({"field":"set"})");
        ExpectFieldWrites<Optional<Inner>>(MakeInner(1, "a"), R"({"field":{"count":1,"name":"a"}})");
        ExpectFieldWrites<Optional<Optional<int32_t>>>(Optional<int32_t>(5), R"({"field":5})");
        ExpectFieldWrites<Optional<Vector<Inner>>>(Vector<Inner>{MakeInner(1, "a"), MakeInner(2, "b")}, R"({"field":[{"count":1,"name":"a"},{"count":2,"name":"b"}]})");
        ExpectFieldWrites<Optional<Vec2>>(std::nullopt, R"({})");
        ExpectFieldWrites<Optional<Vec2>>(Vec2{1.0f, 2.0f}, R"({"field":{"x":1.0,"y":2.0}})");
    }

    TEST(JsonWriteReflectTest, SequencesOfScalarsAreArraysOfTheScalar) {
        ExpectFieldWrites<Vector<int32_t>>({1, -2, 3}, R"({"field":[1,-2,3]})");
        ExpectFieldWrites<Vector<int32_t>>({}, R"({"field":[]})");
        ExpectFieldWrites<Vector<bool>>({true, false}, R"({"field":[true,false]})");
        ExpectFieldWrites<Vector<double>>({0.1, 1e300, std::numeric_limits<double>::infinity()}, R"({"field":[0.1,1e+300,null]})");
        ExpectFieldWrites<Vector<float>>({0.1f, std::numeric_limits<float>::quiet_NaN()}, R"({"field":[0.1,null]})");
        ExpectFieldWrites<Vector<String>>({String("a"), String(), String("\xFF\"")}, R"({"field":["a","","�\""]})");
        ExpectFieldWrites<Vector<PoolString>>({"one"_intern, "two"_intern}, R"({"field":["one","two"]})");
        ExpectFieldWrites<FixedVector<int32_t, 4>>({1, 2, 3, 4}, R"({"field":[1,2,3,4]})");
        ExpectFieldWrites<FixedVector<int32_t, 4>>({}, R"({"field":[]})");
        ExpectFieldWrites<SmallVector<String, 2>>({String("x"), String("y"), String("z")}, R"({"field":["x","y","z"]})");
        ExpectFieldWrites<Array<int64_t, 3>>({std::numeric_limits<int64_t>::min(), 0, std::numeric_limits<int64_t>::max()}, R"({"field":[-9223372036854775808,0,9223372036854775807]})");
        ExpectFieldWrites<Array<uint8_t, 0>>({}, R"({"field":[]})");
    }

    TEST(JsonWriteReflectTest, SequencesOfObjectsWithTwoFieldsAreArraysOfObjects) {
        ExpectFieldWrites<Vector<Inner>>({MakeInner(1, "a"), MakeInner(2, "\xFF")}, R"({"field":[{"count":1,"name":"a"},{"count":2,"name":"�"}]})");
        ExpectFieldWrites<Vector<Inner>>({}, R"({"field":[]})");
        ExpectFieldWrites<FixedVector<Inner, 3>>({MakeInner(7, "s")}, R"({"field":[{"count":7,"name":"s"}]})");
        ExpectFieldWrites<Array<Inner, 2>>({MakeInner(1, "a"), MakeInner(2, "b")}, R"({"field":[{"count":1,"name":"a"},{"count":2,"name":"b"}]})");
    }

    TEST(JsonWriteReflectTest, AnElementThatHoldsOnlyValueCollapsesToTheScalarAndIsPinned) {
        ExpectFieldWrites<Vector<OneValue>>({OneValue{1}, OneValue{-2}}, R"({"field":[1,-2]})");
        ExpectFieldWrites<Vector<OneValue>>({}, R"({"field":[]})");
        ExpectFieldWrites<Array<OneValue, 2>>({OneValue{3}, OneValue{4}}, R"({"field":[3,4]})");
        EXPECT_EQ(Written(Field<Vector<OneValue>>{{OneValue{1}, OneValue{2}}}, JsonWriter::kCompact), "{\"field\":[1,2]}");
        // An element of no fields is an empty object on both paths
        ExpectFieldWrites<Vector<Empty>>({Empty{}, Empty{}}, R"({"field":[{},{}]})");
    }

    TEST(JsonWriteReflectTest, TypesThatStayOnTheBridgeWriteTheSameBytes) {
        ExpectFieldWrites<HashMap<String, int32_t>>({{"b", 2}, {"a", 1}, {"c", 3}}, R"({"field":[{"key":"a","value":1},{"key":"b","value":2},{"key":"c","value":3}]})");
        ExpectFieldWrites<HashMap<String, int32_t>>({}, R"({"field":[]})");
        ExpectFieldWrites<Variant<std::monostate, int32_t, String>>(std::monostate{}, R"({"field":{"index":0}})");
        ExpectFieldWrites<Variant<std::monostate, int32_t, String>>(int32_t{4}, R"({"field":{"index":1,"value":4}})");
        ExpectFieldWrites<Variant<std::monostate, int32_t, String>>(String("v"), R"({"field":{"index":2,"value":"v"}})");
        ExpectFieldWrites<Pair<int32_t, String>>({1, "one"}, R"({"field":{"first":1,"second":"one"}})");
        ExpectFieldWrites<Vec2>(Vec2{0.1f, -3.5f}, R"({"field":{"x":0.1,"y":-3.5}})");
        ExpectFieldWrites<Vector<Vec2>>({Vec2{1.0f, 2.0f}, Vec2{0.1f, 0.2f}}, R"({"field":[{"x":1.0,"y":2.0},{"x":0.1,"y":0.2}]})");
        ExpectFieldWrites<Vector<Vec2>>({}, R"({"field":[]})");
    }

    TEST(JsonWriteReflectTest, AClassWithEveryNativeShapeIsWrittenInSortedOrder) {
        ExpectWrites(Native{}, R"({"active":false,"array":[0,0,0],"count":0,"fixedList":[],"inner":{"count":0,"name":""},"list":[],"mode":"Off","name":"","objects":[],"pool":"",)"
                               R"("ratio":0.0,"scale":0.0,"text":""})");
        ExpectWrites(MakeNative(), R"({"active":true,"array":[-1,0,1],"count":-7,"fixedList":[4,3,2],"inner":{"count":5,"name":"five"},"list":[1,2,3,4,5],"mode":"Automatic",)"
                                   R"("name":"café \"quoted\"\n","objects":[{"count":1,"name":"one"},{"count":2,"name":""}],"optional":12,"pool":"pooled","ratio":0.1,"scale":1e+300,)"
                                   R"("text":"fixed"})");
        Native empty = MakeNative();
        empty.optional.reset();
        empty.list.clear();
        empty.objects.clear();
        empty.fixedList.clear();
        ExpectWrites(empty, R"({"active":true,"array":[-1,0,1],"count":-7,"fixedList":[],"inner":{"count":5,"name":"five"},"list":[],"mode":"Automatic","name":"café \"quoted\"\n",)"
                            R"("objects":[],"pool":"pooled","ratio":0.1,"scale":1e+300,"text":"fixed"})");
    }

    TEST(JsonWriteReflectTest, AClassWithEveryBridgedShapeIsWrittenInSortedOrder) {
        ExpectWrites(Bridged{}, R"({"choice":{"index":0},"map":[],"ones":[],"pair":{"first":0,"second":""},"points":[]})");
        ExpectWrites(MakeBridged(), R"({"choice":{"index":2,"value":"text"},"map":[{"key":"a","value":1},{"key":"b","value":2}],"maybePoint":{"x":1.5,"y":-2.0},"ones":[3,4],)"
                                    R"("pair":{"first":9,"second":"nine"},"points":[{"x":0.1,"y":0.2},{"x":3.0,"y":4.0}]})");
    }

    TEST(JsonWriteReflectTest, TheWholeObjectBridgeIsPinned) {
        const Native native = MakeNative();
        for (const int indent : kIndents) {
            String out;
            JsonWriter writer(out, indent);
            JsonWrite::WriteSerializedObject(writer, [&](ISerializer& s) { native.Serialize(s); });
            EXPECT_TRUE(writer.IsComplete());
            EXPECT_EQ(out, Written(native, indent));
        }
    }

    TEST(JsonWriteReflectTest, TheBridgeWritesNestedValuesInByteOrderWithoutADom) {
        // Written in reverse order on purpose: the stream serializer keeps the order of the writes, the bridge sorts every object
        const Inner inner{7, "in"};
        const Vector<Inner> items = {Inner{1, "a"}, Inner{2, "b"}};
        const Vector<int32_t> ids = {3, 1, 2};
        const float weight = 0.1f;
        const auto save = [&](ISerializer& s) {
            Ser::Save(s, weight, "weight");
            Ser::Save(s, items, "items");
            Ser::Save(s, inner, "inner");
            Ser::Save(s, ids, "ids");
        };

        String whole;
        JsonWriter wholeWriter(whole);
        JsonWrite::WriteSerializedObject(wholeWriter, save);
        EXPECT_TRUE(wholeWriter.IsComplete());
        EXPECT_EQ(whole, R"({"ids":[3,1,2],"inner":{"count":7,"name":"in"},"items":[{"count":1,"name":"a"},{"count":2,"name":"b"}],"weight":0.1})");

        String member;
        JsonWriter memberWriter(member);
        memberWriter.BeginObject();
        JsonWrite::WriteSerializedMember(memberWriter, "items", save);
        memberWriter.EndObject();
        EXPECT_TRUE(memberWriter.IsComplete());
        EXPECT_EQ(member, R"({"items":[{"count":1,"name":"a"},{"count":2,"name":"b"}]})");
    }

    TEST(JsonWriteReflectTest, TheBridgeKeepsTheFirstOfTwoWritesOfOneName) {
        String out;
        JsonWriter writer(out);
        JsonWrite::WriteSerializedObject(writer, [](ISerializer& s) {
            Ser::Save(s, int32_t{1}, "twice");
            Ser::Save(s, int32_t{2}, "twice");
        });
        EXPECT_EQ(out, R"({"twice":1})");
    }

    TEST(JsonWriteReflectTest, AMemberThatSavesNothingIsNotWritten) {
        String out;
        JsonWriter writer(out);
        writer.BeginObject();
        JsonWrite::WriteSerializedMember(writer, "absent", [](ISerializer& /*s*/) {});
        writer.EndObject();
        EXPECT_TRUE(writer.IsComplete());
        EXPECT_EQ(out, "{}");
    }

    TEST(JsonWriteReflectTest, TheSecondWriteOfTheNativeShapesAllocatesNothing) {
        const Native value = MakeNative();
        String out;
        out.reserve(4096);
        {
            JsonWriter writer(out);
            value.WriteJson(writer);
        }
        const String first = out;
        const char* const data = out.data();
        const size_t capacity = out.capacity();
        out.clear();

        Tests::ExpectNoAllocations([&] {
            JsonWriter writer(out);
            value.WriteJson(writer);
        });

        EXPECT_EQ(out, first);
        EXPECT_EQ(out.data(), data);
        EXPECT_EQ(out.capacity(), capacity);
    }

    // =========================================================================
    // The generated WriteJson (JsonWriteReflectTypes.h) pinned as literals
    // =========================================================================

    namespace {

        using JsonWriteTestData::ArenaItem;
        using JsonWriteTestData::ArenaTwin;
        using JsonWriteTestData::HeapItem;
        using JsonWriteTestData::HeapTwin;
        using JsonWriteTestData::JsonWriteBridged;
        using JsonWriteTestData::JsonWriteChainBase;
        using JsonWriteTestData::JsonWriteChainEmptyLeaf;
        using JsonWriteTestData::JsonWriteChainEmptyMid;
        using JsonWriteTestData::JsonWriteChainLeaf;
        using JsonWriteTestData::JsonWriteChainMid;
        using JsonWriteTestData::JsonWriteChild;
        using JsonWriteTestData::JsonWriteChildBefore;
        using JsonWriteTestData::JsonWriteChildOfEmpty;
        using JsonWriteTestData::JsonWriteEmpty;
        using JsonWriteTestData::JsonWriteEvent;
        using JsonWriteTestData::JsonWriteInner;
        using JsonWriteTestData::JsonWriteKeyed;
        using JsonWriteTestData::JsonWriteMaps;
        using JsonWriteTestData::JsonWriteMathFields;
        using JsonWriteTestData::JsonWriteMode;
        using JsonWriteTestData::JsonWriteNested;
        using JsonWriteTestData::JsonWriteOneValue;
        using JsonWriteTestData::JsonWriteOneValueList;
        using JsonWriteTestData::JsonWritePairsAndVariants;
        using JsonWriteTestData::JsonWritePtrBase;
        using JsonWriteTestData::JsonWritePtrEarly;
        using JsonWriteTestData::JsonWritePtrHolder;
        using JsonWriteTestData::JsonWritePtrLate;
        using JsonWriteTestData::JsonWritePtrListHolder;
        using JsonWriteTestData::JsonWritePtrPlain;
        using JsonWriteTestData::JsonWritePtrPlainChild;
        using JsonWriteTestData::JsonWritePtrSame;
        using JsonWriteTestData::JsonWriteScalars;
        using JsonWriteTestData::ViewHeapTwin;
        using JsonWriteTestData::ViewTwin;

        constexpr float kAwkwardFloat = 0.1f;

        JsonWriteInner MakeGeneratedInner(int32_t count, StringView name) {
            JsonWriteInner inner;
            inner.count = count;
            inner.name = String(name.data(), name.size());
            return inner;
        }

        /// Limits and awkward text; every number is finite so that the text reads back
        JsonWriteScalars MakeScalars() {
            JsonWriteScalars value;
            value._unsigned64 = std::numeric_limits<uint64_t>::max();
            value.tiny = std::numeric_limits<int8_t>::min();
            value.byte = std::numeric_limits<uint8_t>::max();
            value.shortValue = std::numeric_limits<int16_t>::min();
            value.unsignedShort = std::numeric_limits<uint16_t>::max();
            value.count = std::numeric_limits<int32_t>::min();
            value.unsigned32 = std::numeric_limits<uint32_t>::max();
            value.signed64 = std::numeric_limits<int64_t>::min();
            value.active = true;
            value.ratio = kAwkwardFloat;
            value.scale = 1e300;
            value.name = "caf\xC3\xA9 \"quoted\" \\ \t\n/";
            value.fixedName = "fixed";
            value.pool = "pooled"_intern;
            value.mode = JsonWriteMode::Automatic;
            return value;
        }

        /// Control characters, bytes that are no UTF-8 and the numbers JSON has no text for
        JsonWriteScalars MakeHostileScalars() {
            JsonWriteScalars value = MakeScalars();
            value.name = EveryByte();
            value.ratio = std::numeric_limits<float>::quiet_NaN();
            value.scale = std::numeric_limits<double>::infinity();
            return value;
        }

        JsonWriteNested MakeNested() {
            JsonWriteNested value;
            value.objects = {MakeGeneratedInner(1, "a"), MakeGeneratedInner(-2, "b\n")};
            value.inner = MakeGeneratedInner(5, "five");
            value.maybeCount = 0;
            value.maybeInner = MakeGeneratedInner(9, "nine");
            value.maybeText = String("set");
            value.numbers = {1, 2, 3, 4, 5};
            value.reals = {0.1, 2.5, -1e-9};
            value.words = {String("x"), String(), String("quote\"")};
            value.fixedNumbers = {4, 3, 2};
            value.fixedObjects = {MakeGeneratedInner(7, "s")};
            value.triple = {-1, 0, 1};
            return value;
        }

        JsonWriteBridged MakeGeneratedBridged() {
            JsonWriteBridged value;
            value.choice = String("text");
            value.table = {{"b", 2}, {"a", 1}};
            value.pair = {3, "three"};
            value.points = {Vec2{1.0f, 2.0f}, Vec2{0.1f, 0.2f}};
            value.maybePoint = Vec2{3.0f, 4.0f};
            return value;
        }

        JsonWriteMathFields MakeMath() {
            JsonWriteMathFields value;
            value.vec2 = Vec2{0.1f, -2.5f};
            value.vec3 = Vec3{1.0f, 2.0f, 3.0f};
            value.vec4 = Vec4{1.0f, 2.0f, 3.0f, 4.0f};
            value.ivec2 = IVec2{-3, 4};
            value.quat = Quat{0.5f, 0.5f, 0.5f, 0.5f};
            value.color = Color{255, 0, 7, 128};
            value.rect = Rect{1.0f, 2.0f, 3.5f, 4.0f};
            value.points = {Vec2{0.1f, 0.2f}, Vec2{1e20f, -1e-20f}};
            value.colors = {Color{1, 2, 3, 4}, Color{255, 255, 255, 255}};
            value.rects = {Rect{0.0f, 0.0f, 1.0f, 1.0f}};
            value.maybeQuat = Quat{0.0f, 0.0f, 1.0f, 0.0f};
            return value;
        }

        JsonWriteMaps MakeMaps() {
            JsonWriteMaps value;
            value.objects = {{"b", MakeGeneratedInner(2, "two")}, {"a", MakeGeneratedInner(1, "one")}, {"c", MakeGeneratedInner(3, "")}};
            value.numbers = {{"zeta", 1}, {"alpha", 2}, {"mid", 3}, {"", 4}};
            value.names = {{10, "ten"}, {-1, "minus"}, {2, "two"}};
            value.fixedTable = {{"y", 1.5}, {"x", 0.1}};
            value.ordered = {{"q", MakeGeneratedInner(5, "q")}, {"p", MakeGeneratedInner(6, "p")}};
            value.flags = {{3, true}, {1, false}};
            JsonWriteKeyed own;
            own.key = "own";
            own.zed = 9;
            value.keyed = {{"map key", own}, {"other", JsonWriteKeyed{}}};
            value.empties = {{"e", JsonWriteEmpty{}}};
            return value;
        }

        JsonWriteMaps MakeBigMap() {
            JsonWriteMaps value;
            for (int32_t i = 0; i < 40; ++i) {
                value.numbers.emplace(Format("k{}", (i * 7) % 40), i);
                value.names.emplace((i * 13) % 40 - 20, Format("n{}", i));
            }
            return value;
        }

        JsonWritePairsAndVariants MakePairsAndVariants() {
            JsonWritePairsAndVariants value;
            value.numberAndText = {7, "seven"};
            value.objectAndPoint = {MakeGeneratedInner(1, "in"), Vec2{0.1f, 0.2f}};
            value.maybeAndList = {Optional<int32_t>{}, Vector<int32_t>{1, 2}};
            value.choice = MakeGeneratedInner(4, "inner");
            value.shapes = Vector<int32_t>{5, 6};
            value.number = 2.5;
            value.maybePair = Pair<int32_t, int32_t>{1, 2};
            return value;
        }

        JsonWriteChild MakeChild() {
            JsonWriteChild value;
            static_cast<JsonWriteScalars&>(value) = MakeScalars();
            value.label = "child";
            value.extra = 42;
            return value;
        }

        JsonWriteChainLeaf MakeLeaf() {
            JsonWriteChainLeaf value;
            value.SetHidden(50);
            value.JsonWriteChainMid::same = "mid";
            value.e = "e";
            value.b = "b";
            value.maybe = 12;
            value.m = {1, 2};
            value.z = {MakeGeneratedInner(1, "z"), MakeGeneratedInner(2, "y")};
            value._0first = true;
            return value;
        }

        /// The text parsed back and read into a new object: what the writer wrote is what a reader takes
        template <typename T>
        T ReadBack(const T& original) {
            const String text = Written(original, JsonWriter::kCompact);
            T result;
            JsonDeserializer reader;
            if (!reader.Load(text)) {
                ADD_FAILURE() << "does not parse: " << text.c_str();
                return result;
            }
            result.Deserialize(reader);
            EXPECT_FALSE(reader.HasErrors()) << text.c_str();
            return result;
        }

        /// An object that reads back writes the same text again (no operator== on the test types)
        template <typename T>
        void ExpectRoundTrip(const T& original) {
            EXPECT_EQ(Written(ReadBack(original), JsonWriter::kCompact), Written(original, JsonWriter::kCompact));
        }

        /// The second write into a String that kept its capacity moves nothing and allocates nothing
        template <typename T>
        void ExpectSecondWriteAllocatesNothing(const T& value) {
            String out;
            out.reserve(4096);
            {
                JsonWriter writer(out);
                value.WriteJson(writer);
            }
            const String first = out;
            const char* const data = out.data();
            const size_t capacity = out.capacity();
            out.clear();

            Tests::ExpectNoAllocations([&] {
                JsonWriter writer(out);
                value.WriteJson(writer);
            });

            EXPECT_EQ(out, first);
            EXPECT_EQ(out.data(), data);
            EXPECT_EQ(out.capacity(), capacity);
        }

    }  // namespace

    TEST(JsonWriteReflectTest, GeneratedScalarsArePinned) {
        ExpectWrites(JsonWriteScalars{}, R"({"active":false,"byte":0,"count":0,"fixedName":"","mode":"Off","name":"","pool":"","ratio":0.0,"scale":0.0,"shortValue":0,"signed64":0,"tiny":0,)"
                                         R"("unsigned32":0,"unsigned64":0,"unsignedShort":0})");
        ExpectWrites(MakeScalars(), R"({"active":true,"byte":255,"count":-2147483648,"fixedName":"fixed","mode":"Automatic","name":"café \"quoted\" \\ \t\n/","pool":"pooled","ratio":0.1,)"
                                    R"("scale":1e+300,"shortValue":-32768,"signed64":-9223372036854775808,"tiny":-128,"unsigned32":4294967295,"unsigned64":18446744073709551615,)"
                                    R"("unsignedShort":65535})");
        ExpectWrites(
            MakeHostileScalars(),
            R"({"active":true,"byte":255,"count":-2147483648,"fixedName":"fixed","mode":"Automatic",)"
            R"("name":"\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f !\"#$%&'()*+,)"
            R"(-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~��������������������������������������������������������������������������������������������������������������������������������",)"
            R"("pool":"pooled","ratio":null,"scale":null,"shortValue":-32768,"signed64":-9223372036854775808,"tiny":-128,"unsigned32":4294967295,)"
            R"("unsigned64":18446744073709551615,"unsignedShort":65535})");
    }

    TEST(JsonWriteReflectTest, GeneratedKeysAreSortedAndWithoutTheLeadingUnderscore) {
        const String text = Written(JsonWriteScalars{}, JsonWriter::kCompact);
        EXPECT_EQ(text.find("_unsigned64"), String::npos);
        const size_t active = text.find("\"active\"");
        const size_t unsigned64 = text.find("\"unsigned64\"");
        ASSERT_NE(active, String::npos);
        ASSERT_NE(unsigned64, String::npos);
        EXPECT_LT(active, unsigned64);
    }

    TEST(JsonWriteReflectTest, GeneratedNestedShapesArePinned) {
        ExpectWrites(JsonWriteNested{}, R"({"empty":{},"fixedNumbers":[],"fixedObjects":[],"inner":{"count":0,"name":""},"none":[],"numbers":[],"objects":[],"reals":[],"triple":[0,0,0],)"
                                        R"("words":[]})");
        ExpectWrites(MakeNested(), R"({"empty":{},"fixedNumbers":[4,3,2],"fixedObjects":[{"count":7,"name":"s"}],"inner":{"count":5,"name":"five"},"maybeCount":0,"maybeInner":{"count":9,)"
                                   R"("name":"nine"},"maybeText":"set","none":[],"numbers":[1,2,3,4,5],"objects":[{"count":1,"name":"a"},{"count":-2,"name":"b\n"}],"reals":[0.1,2.5,)"
                                   R"(-1e-09],"triple":[-1,0,1],"words":["x","","quote\""]})");
        JsonWriteNested emptied = MakeNested();
        emptied.maybeCount.reset();
        emptied.maybeInner.reset();
        emptied.maybeText.reset();
        emptied.objects.clear();
        emptied.fixedNumbers.clear();
        ExpectWrites(emptied, R"({"empty":{},"fixedNumbers":[],"fixedObjects":[{"count":7,"name":"s"}],"inner":{"count":5,"name":"five"},"none":[],"numbers":[1,2,3,4,5],"objects":[],)"
                              R"("reals":[0.1,2.5,-1e-09],"triple":[-1,0,1],"words":["x","","quote\""]})");
    }

    TEST(JsonWriteReflectTest, GeneratedEmptyOptionalsLeaveTheirMembersOutAndEmptyObjectsStay) {
        const String text = Written(JsonWriteNested{}, JsonWriter::kCompact);
        EXPECT_EQ(text.find("maybeCount"), String::npos);
        EXPECT_EQ(text.find("maybeInner"), String::npos);
        EXPECT_NE(text.find("\"empty\":{}"), String::npos);
        EXPECT_NE(text.find("\"none\":[]"), String::npos);
    }

    TEST(JsonWriteReflectTest, GeneratedElementOfOneValueCollapsesAndIsPinned) {
        JsonWriteOneValueList value;
        value.ones.resize(2);
        value.ones[0].value = 1;
        value.ones[1].value = -2;
        value.single.value = 7;
        ExpectWrites(value, R"({"ones":[1,-2],"single":{"value":7}})");
        ExpectWrites(JsonWriteOneValueList{}, R"({"ones":[],"single":{"value":0}})");
        EXPECT_NE(Written(value, JsonWriter::kCompact).find("\"ones\":[1,-2]"), String::npos);
    }

    TEST(JsonWriteReflectTest, GeneratedBridgedFieldsArePinned) {
        ExpectWrites(JsonWriteBridged{}, R"({"choice":{"index":0},"pair":{"first":0,"second":""},"points":[],"table":[]})");
        ExpectWrites(MakeGeneratedBridged(), R"({"choice":{"index":2,"value":"text"},"maybePoint":{"x":3.0,"y":4.0},"pair":{"first":3,"second":"three"},"points":[{"x":1.0,"y":2.0},{"x":0.1,)"
                                             R"("y":0.2}],"table":[{"key":"a","value":1},{"key":"b","value":2}]})");
    }

    TEST(JsonWriteReflectTest, MathTypesAndSequencesOfThemArePinned) {
        ExpectWrites(JsonWriteMathFields{},
                     R"({"color":{"a":0,"b":0,"g":0,"r":0},"colors":[],"ivec2":{"x":0,"y":0},"points":[],"quat":{"w":1.0,"x":0.0,"y":0.0,"z":0.0},)"
                     R"("rect":{"h":0.0,"w":0.0,"x":0.0,"y":0.0},"rects":[],"vec2":{"x":0.0,"y":0.0},"vec3":{"x":0.0,"y":0.0,"z":0.0},"vec4":{"w":0.0,"x":0.0,"y":0.0,"z":0.0}})");
        ExpectWrites(MakeMath(), R"({"color":{"a":128,"b":7,"g":0,"r":255},"colors":[{"a":4,"b":3,"g":2,"r":1},{"a":255,"b":255,"g":255,"r":255}],"ivec2":{"x":-3,"y":4},)"
                                 R"("maybeQuat":{"w":0.0,"x":0.0,"y":0.0,"z":1.0},"points":[{"x":0.1,"y":0.2},{"x":1e+20,"y":-1e-20}],"quat":{"w":0.5,"x":0.5,"y":0.5,"z":0.5},)"
                                 R"("rect":{"h":4.0,"w":3.5,"x":1.0,"y":2.0},"rects":[{"h":1.0,"w":1.0,"x":0.0,"y":0.0}],"vec2":{"x":0.1,"y":-2.5},"vec3":{"x":1.0,"y":2.0,"z":3.0},)"
                                 R"("vec4":{"w":4.0,"x":1.0,"y":2.0,"z":3.0}})");
        const String text = Written(MakeMath(), JsonWriter::kCompact);
        EXPECT_NE(text.find("\"color\":{\"a\":128,\"b\":7,\"g\":0,\"r\":255}"), String::npos);
        EXPECT_NE(text.find("\"quat\":{\"w\":0.5,\"x\":0.5,\"y\":0.5,\"z\":0.5}"), String::npos);
        EXPECT_NE(text.find("\"rect\":{\"h\":4.0,\"w\":3.5,\"x\":1.0,\"y\":2.0}"), String::npos);
    }

    TEST(JsonWriteReflectTest, MapsOfEveryNativeShapeArePinned) {
        ExpectWrites(JsonWriteMaps{}, R"({"empties":[],"fixedTable":[],"flags":[],"keyed":[],"names":[],"none":[],"numbers":[],"objects":[],"ordered":[]})");
        ExpectWrites(MakeMaps(), R"({"empties":[{"key":"e"}],"fixedTable":[{"key":"x","value":0.1},{"key":"y","value":1.5}],"flags":[{"key":1,"value":false},{"key":3,"value":true}],)"
                                 R"("keyed":[{"key":"own","zed":9},{"key":"","zed":0}],"names":[{"key":-1,"value":"minus"},{"key":2,"value":"two"},{"key":10,"value":"ten"}],"none":[],)"
                                 R"("numbers":[{"key":"","value":4},{"key":"alpha","value":2},{"key":"mid","value":3},{"key":"zeta","value":1}],"objects":[{"count":1,"key":"a",)"
                                 R"("name":"one"},{"count":2,"key":"b","name":"two"},{"count":3,"key":"c","name":""}],"ordered":[{"count":6,"key":"p","name":"p"},{"count":5,"key":"q",)"
                                 R"("name":"q"}]})");
        ExpectWrites(MakeBigMap(), R"({"empties":[],"fixedTable":[],"flags":[],"keyed":[],"names":[{"key":-20,"value":"n0"},{"key":-19,"value":"n37"},{"key":-18,"value":"n34"},{"key":-17,)"
                                   R"("value":"n31"},{"key":-16,"value":"n28"},{"key":-15,"value":"n25"},{"key":-14,"value":"n22"},{"key":-13,"value":"n19"},{"key":-12,"value":"n16"},)"
                                   R"({"key":-11,"value":"n13"},{"key":-10,"value":"n10"},{"key":-9,"value":"n7"},{"key":-8,"value":"n4"},{"key":-7,"value":"n1"},{"key":-6,"value":"n38"},)"
                                   R"({"key":-5,"value":"n35"},{"key":-4,"value":"n32"},{"key":-3,"value":"n29"},{"key":-2,"value":"n26"},{"key":-1,"value":"n23"},{"key":0,"value":"n20"},)"
                                   R"({"key":1,"value":"n17"},{"key":2,"value":"n14"},{"key":3,"value":"n11"},{"key":4,"value":"n8"},{"key":5,"value":"n5"},{"key":6,"value":"n2"},{"key":7,)"
                                   R"("value":"n39"},{"key":8,"value":"n36"},{"key":9,"value":"n33"},{"key":10,"value":"n30"},{"key":11,"value":"n27"},{"key":12,"value":"n24"},{"key":13,)"
                                   R"("value":"n21"},{"key":14,"value":"n18"},{"key":15,"value":"n15"},{"key":16,"value":"n12"},{"key":17,"value":"n9"},{"key":18,"value":"n6"},{"key":19,)"
                                   R"("value":"n3"}],"none":[],"numbers":[{"key":"k0","value":0},{"key":"k1","value":23},{"key":"k10","value":30},{"key":"k11","value":13},{"key":"k12",)"
                                   R"("value":36},{"key":"k13","value":19},{"key":"k14","value":2},{"key":"k15","value":25},{"key":"k16","value":8},{"key":"k17","value":31},{"key":"k18",)"
                                   R"("value":14},{"key":"k19","value":37},{"key":"k2","value":6},{"key":"k20","value":20},{"key":"k21","value":3},{"key":"k22","value":26},{"key":"k23",)"
                                   R"("value":9},{"key":"k24","value":32},{"key":"k25","value":15},{"key":"k26","value":38},{"key":"k27","value":21},{"key":"k28","value":4},{"key":"k29",)"
                                   R"("value":27},{"key":"k3","value":29},{"key":"k30","value":10},{"key":"k31","value":33},{"key":"k32","value":16},{"key":"k33","value":39},{"key":"k34",)"
                                   R"("value":22},{"key":"k35","value":5},{"key":"k36","value":28},{"key":"k37","value":11},{"key":"k38","value":34},{"key":"k39","value":17},{"key":"k4",)"
                                   R"("value":12},{"key":"k5","value":35},{"key":"k6","value":18},{"key":"k7","value":1},{"key":"k8","value":24},{"key":"k9","value":7}],"objects":[],)"
                                   R"("ordered":[]})");
    }

    TEST(JsonWriteReflectTest, AMapEntryWhoseValueHasAMemberNamedKeyKeepsTheValuesMember) {
        const String text = Written(MakeMaps(), JsonWriter::kCompact);
        EXPECT_NE(text.find("\"keyed\":[{\"key\":\"own\",\"zed\":9},{\"key\":\"\",\"zed\":0}]"), String::npos);
    }

    TEST(JsonWriteReflectTest, PairsAndVariantsArePinned) {
        ExpectWrites(JsonWritePairsAndVariants{}, R"({"choice":{"index":0},"emptyChoice":{"index":0},"maybeAndList":{"second":[]},"number":{"index":0,"value":false},"numberAndText":{"first":0,)"
                                                  R"("second":""},"objectAndPoint":{"first":{"count":0,"name":""},"second":{"x":0.0,"y":0.0}},"shapes":{"index":0,"value":{"x":0.0,"y":0.0}}})");
        ExpectWrites(MakePairsAndVariants(), R"({"choice":{"index":3,"value":{"count":4,"name":"inner"}},"emptyChoice":{"index":0},"maybeAndList":{"second":[1,2]},"maybePair":{"first":1,"second":2},)"
                                             R"("number":{"index":1,"value":2.5},"numberAndText":{"first":7,"second":"seven"},"objectAndPoint":{"first":{"count":1,"name":"in"},"second":{"x":0.1,)"
                                             R"("y":0.2}},"shapes":{"index":1,"value":[5,6]}})");
        JsonWritePairsAndVariants other = MakePairsAndVariants();
        other.choice = String("text");
        other.emptyChoice = 3;
        other.shapes = Pair<int32_t, int32_t>{8, 9};
        other.maybeAndList = {Optional<int32_t>{4}, Vector<int32_t>{}};
        other.number = true;
        other.maybePair.reset();
        ExpectWrites(other, R"({"choice":{"index":2,"value":"text"},"emptyChoice":{"index":1,"value":3},"maybeAndList":{"first":4,"second":[]},"number":{"index":0,"value":true},)"
                            R"("numberAndText":{"first":7,"second":"seven"},"objectAndPoint":{"first":{"count":1,"name":"in"},"second":{"x":0.1,"y":0.2}},"shapes":{"index":2,)"
                            R"("value":{"first":8,"second":9}}})");
        other.choice = std::monostate{};
        other.shapes = Vec2{1.0f, 2.0f};
        ExpectWrites(other, R"({"choice":{"index":0},"emptyChoice":{"index":1,"value":3},"maybeAndList":{"first":4,"second":[]},"number":{"index":0,"value":true},)"
                            R"("numberAndText":{"first":7,"second":"seven"},"objectAndPoint":{"first":{"count":1,"name":"in"},"second":{"x":0.1,"y":0.2}},"shapes":{"index":0,)"
                            R"("value":{"x":1.0,"y":2.0}}})");
    }

    TEST(JsonWriteReflectTest, MathPairAndVariantFieldsAreWrittenWithoutAllocations) {
        ExpectSecondWriteAllocatesNothing(MakeMath());
        ExpectSecondWriteAllocatesNothing(MakePairsAndVariants());
        ExpectSecondWriteAllocatesNothing(MakeMaps());
    }

    TEST(JsonWriteReflectTest, GeneratedChildInterleavesTheKeysOfItsParent) {
        ExpectWrites(JsonWriteChild{}, R"({"active":false,"byte":0,"count":0,"extra":0,"fixedName":"","label":"","mode":"Off","name":"","pool":"","ratio":0.0,"scale":0.0,"shortValue":0,)"
                                       R"("signed64":0,"tiny":0,"unsigned32":0,"unsigned64":0,"unsignedShort":0})");
        ExpectWrites(MakeChild(), R"({"active":true,"byte":255,"count":-2147483648,"extra":42,"fixedName":"fixed","label":"child","mode":"Automatic","name":"café \"quoted\" \\ \t\n/",)"
                                  R"("pool":"pooled","ratio":0.1,"scale":1e+300,"shortValue":-32768,"signed64":-9223372036854775808,"tiny":-128,"unsigned32":4294967295,)"
                                  R"("unsigned64":18446744073709551615,"unsignedShort":65535})");
        const String text = Written(MakeChild(), JsonWriter::kCompact);
        const size_t extra = text.find("\"extra\"");
        const size_t label = text.find("\"label\"");
        const size_t active = text.find("\"active\"");
        ASSERT_NE(extra, String::npos);
        ASSERT_NE(label, String::npos);
        ASSERT_NE(active, String::npos);
        EXPECT_LT(active, extra);
        EXPECT_LT(extra, label);
    }

    TEST(JsonWriteReflectTest, AChainOfThreeMergesTheKeysOfAllItsLevels) {
        ExpectWrites(JsonWriteChainBase{}, R"({"a":3,"g":4,"hidden":5,"m":2,"same":1})");
        ExpectWrites(JsonWriteChainMid{}, R"({"a":3,"b":"","e":"","g":4,"hidden":5,"m":2,"same":""})");
        ExpectWrites(JsonWriteChainLeaf{}, R"({"0first":false,"a":3,"b":"","c":6,"e":"","g":4,"hidden":5,"m":[],"same":"","z":[]})");
        ExpectWrites(MakeLeaf(), R"({"0first":true,"a":3,"b":"b","c":6,"e":"e","g":4,"hidden":50,"m":[1,2],"maybe":12,"same":"mid","z":[{"count":1,"name":"z"},{"count":2,"name":"y"}]})");
        JsonWriteChainMid mid;
        mid.e = "mid only";
        mid.maybe = 1;
        ExpectWrites(mid, R"({"a":3,"b":"","e":"mid only","g":4,"hidden":5,"m":2,"maybe":1,"same":""})");
        JsonWriteChainLeaf bare = MakeLeaf();
        bare.maybe.reset();
        ExpectWrites(bare, R"({"0first":true,"a":3,"b":"b","c":6,"e":"e","g":4,"hidden":50,"m":[1,2],"same":"mid","z":[{"count":1,"name":"z"},{"count":2,"name":"y"}]})");
    }

    TEST(JsonWriteReflectTest, TheMostDerivedLevelHoldsAKeyThatSeveralLevelsHave) {
        const String text = Written(MakeLeaf(), JsonWriter::kCompact);
        EXPECT_NE(text.find("\"m\":[1,2]"), String::npos);
        EXPECT_NE(text.find("\"same\":\"mid\""), String::npos);
        EXPECT_EQ(text.find("\"m\":2"), String::npos);
        EXPECT_EQ(text.find("\"same\":1"), String::npos);
        EXPECT_NE(text.find("\"hidden\":50"), String::npos);
    }

    TEST(JsonWriteReflectTest, AParentWithNoFieldsAndAMiddleLevelWithNoFieldsDoNotBreakTheMerge) {
        ExpectWrites(JsonWriteChildOfEmpty{}, R"({"value1":10,"value2":9})");
        ExpectWrites(JsonWriteChainEmptyMid{}, R"({"a":3,"g":4,"hidden":5,"m":2,"same":1})");
        JsonWriteChainEmptyLeaf leaf;
        leaf.SetHidden(3);
        ExpectWrites(leaf, R"({"a":8,"f":7,"g":4,"hidden":3,"m":2,"same":1})");
        EXPECT_NE(Written(leaf, JsonWriter::kCompact).find("\"a\":8"), String::npos);
    }

    TEST(JsonWriteReflectTest, ChildKeysThatAllSortBeforeTheParentsKeysComeFirst) {
        JsonWriteChildBefore value;
        value.name = "n";
        value.count = 2;
        ExpectWrites(value, R"({"aaa":11,"count":2,"name":"n"})");
        EXPECT_EQ(Written(value, JsonWriter::kCompact), "{\"aaa\":11,\"count\":2,\"name\":\"n\"}");
    }

    TEST(JsonWriteReflectTest, AWriteThroughABaseReferenceOfAChainWritesTheMostDerivedMembers) {
        const JsonWriteChainLeaf leaf = MakeLeaf();
        const JsonWriteChainBase& base = leaf;
        String viaBase;
        {
            JsonWriter writer(viaBase);
            base.WriteJson(writer);
        }
        EXPECT_EQ(viaBase, Written(leaf, JsonWriter::kCompact));
    }

    namespace {

        template <typename T>
        JsonWritePtrHolder MakeHolder(IntrusivePtr<T> factory) {
            JsonWritePtrHolder value;
            factory->label = "lbl";
            value.factory = factory;
            return value;
        }

    }  // namespace

    TEST(JsonWriteReflectTest, AFactoryPointerWritesTheTypeBeforeAndAfterTheKeysOfItsObject) {
        auto early = New<JsonWritePtrEarly>();
        early->alpha = 4;
        JsonWritePtrHolder holder = MakeHolder(early);
        ExpectWrites(holder, R"({"factory":{"alpha":4,"label":"lbl","type":"JsonWritePtrEarly"}})");
        EXPECT_NE(Written(holder, JsonWriter::kCompact).find("\"factory\":{\"alpha\":4,\"label\":\"lbl\",\"type\":\"JsonWritePtrEarly\"}"), String::npos);

        auto late = New<JsonWritePtrLate>();
        late->zeta = 5;
        holder = MakeHolder(late);
        ExpectWrites(holder, R"({"factory":{"label":"lbl","type":"JsonWritePtrLate","zeta":5}})");
        EXPECT_NE(Written(holder, JsonWriter::kCompact).find("\"factory\":{\"label\":\"lbl\",\"type\":\"JsonWritePtrLate\",\"zeta\":5}"), String::npos);
    }

    TEST(JsonWriteReflectTest, AMemberNamedTypeReplacesTheClassNameAndIsPinned) {
        auto same = New<JsonWritePtrSame>();
        same->type = "mine";
        JsonWritePtrHolder holder = MakeHolder(same);
        ExpectWrites(holder, R"({"factory":{"label":"lbl","type":"mine"}})");
        const String text = Written(holder, JsonWriter::kCompact);
        EXPECT_NE(text.find("\"type\":\"mine\""), String::npos);
        EXPECT_EQ(text.find("JsonWritePtrSame"), String::npos);
    }

    TEST(JsonWriteReflectTest, ANullPointerWritesNoMemberAndAPlainPointerWritesItsDynamicObject) {
        JsonWritePtrHolder holder;
        ExpectWrites(holder, R"({})");
        EXPECT_EQ(Written(holder, JsonWriter::kCompact), "{}");

        auto child = New<JsonWritePtrPlainChild>();
        child->id = 3;
        child->name = "n";
        child->extra = 8;
        child->_count = 9;
        holder.plain = child;
        ExpectWrites(holder, R"({"plain":{"count":9,"extra":8,"id":3,"name":"n"}})");
        const String text = Written(holder, JsonWriter::kCompact);
        EXPECT_NE(text.find("\"plain\":{\"count\":9,\"extra\":8,\"id\":3,\"name\":\"n\"}"), String::npos);
    }

    TEST(JsonWriteReflectTest, ASequenceOfPointersStaysOnTheBridgeWithTheSameBytes) {
        JsonWritePtrListHolder holder;
        auto early = New<JsonWritePtrEarly>();
        early->alpha = 1;
        holder.list.push_back(early);
        holder.list.emplace_back();
        holder.list.push_back(New<JsonWritePtrLate>());
        ExpectWrites(holder, R"({"list":[{"alpha":1,"enabled":true,"label":"","type":"JsonWritePtrEarly"},{"enabled":true,"label":"","type":"JsonWritePtrLate","zeta":0}]})");
        EXPECT_NE(Written(holder, JsonWriter::kCompact).find("\"enabled\":true"), String::npos);
    }

    TEST(JsonWriteReflectTest, ThePointerFieldsAreWrittenWithoutAllocations) {
        JsonWritePtrHolder holder;
        auto early = New<JsonWritePtrEarly>();
        early->alpha = 4;
        early->label = "x";
        holder.factory = early;
        auto child = New<JsonWritePtrPlainChild>();
        child->name = "n";
        holder.plain = child;
        ExpectSecondWriteAllocatesNothing(holder);
    }

    TEST(JsonWriteReflectTest, GeneratedEventIsPinned) {
        JsonWriteEvent event;
        event.text = "event \"text\"";
        event.id = -3;
        event.weight = kAwkwardFloat;
        ExpectWrites(event, R"({"id":-3,"text":"event \"text\"","weight":0.1})");
        event.optionalId = 8;
        ExpectWrites(event, R"({"id":-3,"optionalId":8,"text":"event \"text\"","weight":0.1})");
        ExpectWrites(JsonWriteEvent{}, R"({"id":0,"text":"","weight":0.0})");
    }

    TEST(JsonWriteReflectTest, GeneratedWriteOfAClassWithNoFieldsIsAnEmptyObject) {
        EXPECT_EQ(Written(JsonWriteEmpty{}, JsonWriter::kCompact), "{}");
    }

    TEST(JsonWriteReflectTest, GeneratedTextReadsBackIntoTheSameObject) {
        ExpectRoundTrip(MakeScalars());
        ExpectRoundTrip(MakeNested());
        ExpectRoundTrip(MakeGeneratedBridged());
        ExpectRoundTrip(MakeChild());
        const JsonWriteScalars read = ReadBack(MakeScalars());
        EXPECT_EQ(read._unsigned64, std::numeric_limits<uint64_t>::max());
        EXPECT_EQ(read.name, MakeScalars().name);
        EXPECT_EQ(read.ratio, kAwkwardFloat);
        EXPECT_EQ(read.mode, JsonWriteMode::Automatic);
    }

    TEST(JsonWriteReflectTest, GeneratedWriteThroughABaseWritesTheDerivedFields) {
        const JsonWriteChild child = MakeChild();
        const JsonWriteScalars& base = child;
        String viaBase;
        {
            JsonWriter writer(viaBase);
            base.WriteJson(writer);
        }
        EXPECT_EQ(viaBase, Written(child, JsonWriter::kCompact));
        EXPECT_NE(viaBase.find("\"label\":\"child\""), String::npos);
    }

    TEST(JsonWriteReflectTest, GeneratedWriteAppendsToTextThatIsAlreadyThere) {
        String out = "prefix:";
        JsonWriter writer(out);
        JsonWriteEvent event;
        event.id = 1;
        event.WriteJson(writer);
        EXPECT_TRUE(writer.IsComplete());
        EXPECT_EQ(out, Format("prefix:{}", Written(event, JsonWriter::kCompact)));
    }

    TEST(JsonWriteReflectTest, TheSecondGeneratedWriteOfTheNativeTypesAllocatesNothing) {
        ExpectSecondWriteAllocatesNothing(MakeScalars());
        ExpectSecondWriteAllocatesNothing(MakeNested());
        ExpectSecondWriteAllocatesNothing(MakeLeaf());
        ExpectSecondWriteAllocatesNothing(MakeChild());
        JsonWriteEvent event;
        event.text = "event";
        event.id = 4;
        event.optionalId = 2;
        ExpectSecondWriteAllocatesNothing(event);
    }

    // =========================================================================
    // The strings and lists of the scratch arena (ArenaString, ArenaVector) next to their heap twins
    // =========================================================================

    namespace {

        constexpr StringView kLongText = "a text well past the small-string buffer of twenty-three bytes";
        constexpr StringView kLongWord = "another word that is longer than the inline buffer";

        ArenaString ArenaText(StringView text) {
            return ArenaString(text.data(), text.size());
        }

        /// The same content in both shapes; every text is over the small-string limit so it takes a block
        ArenaTwin MakeArenaTwin() {
            ArenaTwin value;
            value.text = ArenaText(kLongText);
            value.maybeText = ArenaText(kLongWord);
            value.words.reserve(2);
            value.words.push_back(ArenaText(kLongText));
            value.words.push_back(ArenaText("q\"uote\n"));
            value.numbers.push_back(7);
            value.numbers.push_back(4000000000U);
            value.items.reserve(2);
            for (int32_t index = 0; index < 2; ++index) {
                ArenaItem& item = value.items.emplace_back();
                item.label = ArenaText(kLongWord);
                item.rank = index;
            }
            value.maybeWords.emplace();
            value.maybeWords->push_back(ArenaText(kLongWord));
            value.pools.reserve(2);
            value.pools.push_back("ArenaPoolOne"_intern);
            value.pools.push_back("ArenaPoolTwo"_intern);
            return value;
        }

        HeapTwin MakeHeapTwin() {
            HeapTwin value;
            value.text = String(kLongText.data(), kLongText.size());
            value.maybeText = String(kLongWord.data(), kLongWord.size());
            value.words = {String(kLongText.data(), kLongText.size()), "q\"uote\n"};
            value.numbers = {7, 4000000000U};
            for (int32_t index = 0; index < 2; ++index) {
                HeapItem& item = value.items.emplace_back();
                item.label = String(kLongWord.data(), kLongWord.size());
                item.rank = index;
            }
            value.maybeWords = Vector<String>{String(kLongWord.data(), kLongWord.size())};
            value.pools = {"ArenaPoolOne"_intern, "ArenaPoolTwo"_intern};
            return value;
        }

    }  // namespace

    class JsonWriteArenaTest : public ScratchScopeFixture {};

    TEST_F(JsonWriteArenaTest, TheArenaShapeWritesTheBytesOfItsHeapTwin) {
        const ArenaTwin arena = MakeArenaTwin();
        const HeapTwin heap = MakeHeapTwin();
        for (const int indent : kIndents) {
            EXPECT_EQ(Written(arena, indent), Written(heap, indent)) << "indent " << indent;
        }
    }

    TEST_F(JsonWriteArenaTest, TheArenaShapeSerializesTheBytesOfItsHeapTwin) {
        const ArenaTwin arena = MakeArenaTwin();
        const HeapTwin heap = MakeHeapTwin();
        EXPECT_EQ(SerializedText(arena), SerializedText(heap));
    }

    TEST_F(JsonWriteArenaTest, TheArenaShapeWrittenByTheGeneratedWriterIsPinned) {
        ExpectWrites(MakeArenaTwin(), R"({"items":[{"label":"another word that is longer than the inline buffer","rank":0},{"label":"another word that is longer than the inline buffer",)"
                                      R"("rank":1}],"maybeText":"another word that is longer than the inline buffer","maybeWords":["another word that is longer than the inline buffer"],)"
                                      R"("numbers":[7,4000000000],"pools":["ArenaPoolOne","ArenaPoolTwo"],"text":"a text well past the small-string buffer of twenty-three bytes",)"
                                      R"("words":["a text well past the small-string buffer of twenty-three bytes","q\"uote\n"]})");
        ExpectWrites(ArenaTwin{}, R"({"items":[],"numbers":[],"pools":[],"text":"","words":[]})");
    }

    TEST_F(JsonWriteArenaTest, TheArenaShapeReadsBackIntoTheSameObject) {
        const ArenaTwin original = MakeArenaTwin();
        const ArenaTwin read = ReadBack(original);
        EXPECT_EQ(read.text, original.text);
        ASSERT_TRUE(read.maybeText.has_value());
        EXPECT_EQ(*read.maybeText, *original.maybeText);
        EXPECT_EQ(read.words, original.words);
        EXPECT_EQ(read.numbers, original.numbers);
        ASSERT_EQ(read.items.size(), original.items.size());
        EXPECT_EQ(read.items[1].label, original.items[1].label);
        EXPECT_EQ(read.items[1].rank, 1);
        ASSERT_TRUE(read.maybeWords.has_value());
        EXPECT_EQ(*read.maybeWords, *original.maybeWords);
        EXPECT_EQ(read.pools, original.pools);
        ExpectRoundTrip(original);
    }

    TEST_F(JsonWriteArenaTest, AnEmptyOptionalOfTheArenaShapeLeavesItsMemberOut) {
        const ArenaTwin empty;
        const String text = Written(empty, JsonWriter::kCompact);
        EXPECT_EQ(text.find("maybeText"), String::npos);
        EXPECT_EQ(text.find("maybeWords"), String::npos);
        EXPECT_EQ(text, Written(HeapTwin{}, JsonWriter::kCompact));
    }

    TEST_F(JsonWriteArenaTest, TheSecondWriteOfTheArenaShapeAllocatesNothing) {
        ExpectSecondWriteAllocatesNothing(MakeArenaTwin());
    }

    // =========================================================================
    // Views of text kept elsewhere (TextView) next to their heap twin
    // =========================================================================

    namespace {

        constexpr StringView kViewedText = "a text kept elsewhere, longer than the small-string buffer";
        constexpr StringView kViewedWord = "q\"uote\n";

        ViewTwin MakeViewTwin() {
            ViewTwin value;
            value.text = TextView(kViewedText);
            value.maybeText = TextView(kViewedWord);
            value.words.reserve(2);
            value.words.push_back(TextView(kViewedText));
            value.words.push_back(TextView(kViewedWord));
            value.maybeWords.emplace();
            value.maybeWords->push_back(TextView(kViewedText));
            return value;
        }

        ViewHeapTwin MakeViewHeapTwin() {
            ViewHeapTwin value;
            value.text = String(kViewedText.data(), kViewedText.size());
            value.maybeText = String(kViewedWord.data(), kViewedWord.size());
            value.words = {String(kViewedText.data(), kViewedText.size()), String(kViewedWord.data(), kViewedWord.size())};
            value.maybeWords = Vector<String>{String(kViewedText.data(), kViewedText.size())};
            return value;
        }

    }  // namespace

    class JsonWriteViewTest : public ScratchScopeFixture {};

    TEST_F(JsonWriteViewTest, TheViewShapeWritesTheBytesOfItsHeapTwin) {
        const ViewTwin views = MakeViewTwin();
        const ViewHeapTwin heap = MakeViewHeapTwin();
        for (const int indent : kIndents) {
            EXPECT_EQ(Written(views, indent), Written(heap, indent)) << "indent " << indent;
        }
    }

    TEST_F(JsonWriteViewTest, AnEmptyViewIsAnEmptyStringAndAnEmptyOptionalLeavesItsMemberOut) {
        const ViewTwin empty;
        const String text = Written(empty, JsonWriter::kCompact);
        EXPECT_EQ(text.find("maybeText"), String::npos);
        EXPECT_EQ(text.find("maybeWords"), String::npos);
        EXPECT_EQ(text, Written(ViewHeapTwin{}, JsonWriter::kCompact));
    }

    TEST_F(JsonWriteViewTest, TheViewShapeSerializesTheBytesOfItsHeapTwin) {
        const ViewTwin views = MakeViewTwin();
        const ViewHeapTwin heap = MakeViewHeapTwin();
        EXPECT_EQ(SerializedText(views), SerializedText(heap));
    }

    TEST_F(JsonWriteViewTest, TheViewShapeWrittenByTheGeneratedWriterIsPinned) {
        ExpectWrites(MakeViewTwin(), R"({"maybeText":"q\"uote\n","maybeWords":["a text kept elsewhere, longer than the small-string buffer"],"text":"a text kept elsewhere,)"
                                     R"( longer than the small-string buffer","words":["a text kept elsewhere, longer than the small-string buffer","q\"uote\n"]})");
        ExpectWrites(ViewTwin{}, R"({"text":"","words":[]})");
    }

    TEST_F(JsonWriteViewTest, ReadingLeavesTheViewsAlone) {
        const ViewTwin original = MakeViewTwin();
        const ViewTwin read = ReadBack(original);
        EXPECT_TRUE(read.text.View().empty());
        EXPECT_FALSE(read.maybeText.has_value());
    }

    TEST_F(JsonWriteViewTest, TheSecondWriteOfTheViewShapeAllocatesNothing) {
        ExpectSecondWriteAllocatesNothing(MakeViewTwin());
    }

}  // namespace BECore::Tests
