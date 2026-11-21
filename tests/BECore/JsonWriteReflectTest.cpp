// The helpers of the generated writer (Control/JsonWriteReflect.h): every text they write is compared with `JsonSerializer` + `DumpJson` of the same
// value, the DOM path they replace. The types are written by hand here (the generated ones are in the tests of the generator).

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Math/Vec2.h>
#include <Common/AllocCheck.h>
#include <cfloat>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        CORE_ENUM(WriteMode, uint8_t, Off, On, Automatic)

        constexpr Array<int, 2> kIndents = {JsonWriter::kCompact, 2};

        /// The DOM path: what `Serialize` saves into a JsonSerializer, dumped
        template <typename T>
        String Reference(const T& object, int indent) {
            JsonSerializer serializer;
            object.Serialize(serializer);
            return DumpJson(serializer.Take(), indent);
        }

        template <typename T>
        String Written(const T& object, int indent) {
            String out;
            JsonWriter writer(out, indent);
            object.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        template <typename T>
        void ExpectSameAsDom(const T& object) {
            for (const int indent : kIndents) {
                ASSERT_EQ(Written(object, indent), Reference(object, indent)) << "indent " << indent;
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
        void ExpectFieldSameAsDom(const T& value) {
            ExpectSameAsDom(Field<T>{value});
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

        /// An element of this class collapses to a scalar on the DOM path
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

        /// A field of every type that stays on the bridge
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
        ExpectFieldSameAsDom<int8_t>(std::numeric_limits<int8_t>::min());
        ExpectFieldSameAsDom<int8_t>(std::numeric_limits<int8_t>::max());
        ExpectFieldSameAsDom<uint8_t>(std::numeric_limits<uint8_t>::max());
        ExpectFieldSameAsDom<int16_t>(std::numeric_limits<int16_t>::min());
        ExpectFieldSameAsDom<uint16_t>(std::numeric_limits<uint16_t>::max());
        ExpectFieldSameAsDom<int32_t>(std::numeric_limits<int32_t>::min());
        ExpectFieldSameAsDom<uint32_t>(std::numeric_limits<uint32_t>::max());
        ExpectFieldSameAsDom<int64_t>(std::numeric_limits<int64_t>::min());
        ExpectFieldSameAsDom<int64_t>(std::numeric_limits<int64_t>::max());
        ExpectFieldSameAsDom<uint64_t>(std::numeric_limits<uint64_t>::max());
        ExpectFieldSameAsDom<int32_t>(0);
        ExpectFieldSameAsDom<bool>(true);
        ExpectFieldSameAsDom<bool>(false);
    }

    TEST(JsonWriteReflectTest, FloatsWriteTheShortestTextAndNonFiniteIsNull) {
        for (const float value : {0.0f, -0.0f, 0.1f, 1.0f, -2.5f, 16777216.0f, FLT_MIN, FLT_MAX, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            ExpectFieldSameAsDom<float>(value);
        }
        for (const double value : {0.0, -0.0, 0.1, 1.0, 3.0, 1e300, 1e-300, 123456789012345678.0, DBL_MIN, DBL_MAX, std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::quiet_NaN(),
                                   std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            ExpectFieldSameAsDom<double>(value);
        }
    }

    TEST(JsonWriteReflectTest, StringsEscapeAndReplaceInvalidUtf8) {
        const String everyByte = EveryByte();
        for (const String& text : {String(), String("plain"), String("a\"b\\c\n\t\r\b\f/"), String("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"), String("\xC3\x28\xE2\x82\xF0\x9F\xFF"), everyByte}) {
            ExpectFieldSameAsDom<String>(text);
            ExpectFieldSameAsDom<FixedString<8>>(FixedString<8>(text.data(), text.size()));
            ExpectFieldSameAsDom<PoolString>(PoolString::Intern(StringView(text.data(), text.size())));
        }
    }

    TEST(JsonWriteReflectTest, AnEnumIsItsName) {
        for (const WriteMode mode : {WriteMode::Off, WriteMode::On, WriteMode::Automatic}) {
            ExpectFieldSameAsDom<WriteMode>(mode);
        }
    }

    TEST(JsonWriteReflectTest, ANestedObjectIsAMemberObjectEvenWhenEmpty) {
        ExpectFieldSameAsDom<Inner>(MakeInner(3, "x"));
        ExpectFieldSameAsDom<Empty>(Empty{});
    }

    TEST(JsonWriteReflectTest, AnEmptyOptionalWritesNoMemberAndASetOneWritesItsValue) {
        ExpectFieldSameAsDom<Optional<int32_t>>(std::nullopt);
        ExpectFieldSameAsDom<Optional<int32_t>>(0);
        ExpectFieldSameAsDom<Optional<String>>(String("set"));
        ExpectFieldSameAsDom<Optional<Inner>>(MakeInner(1, "a"));
        ExpectFieldSameAsDom<Optional<Optional<int32_t>>>(Optional<int32_t>(5));
        ExpectFieldSameAsDom<Optional<Vector<Inner>>>(Vector<Inner>{MakeInner(1, "a"), MakeInner(2, "b")});
        ExpectFieldSameAsDom<Optional<Vec2>>(std::nullopt);
        ExpectFieldSameAsDom<Optional<Vec2>>(Vec2{1.0f, 2.0f});
    }

    TEST(JsonWriteReflectTest, SequencesOfScalarsAreArraysOfTheScalar) {
        ExpectFieldSameAsDom<Vector<int32_t>>({1, -2, 3});
        ExpectFieldSameAsDom<Vector<int32_t>>({});
        ExpectFieldSameAsDom<Vector<bool>>({true, false});
        ExpectFieldSameAsDom<Vector<double>>({0.1, 1e300, std::numeric_limits<double>::infinity()});
        ExpectFieldSameAsDom<Vector<float>>({0.1f, std::numeric_limits<float>::quiet_NaN()});
        ExpectFieldSameAsDom<Vector<String>>({String("a"), String(), String("\xFF\"")});
        ExpectFieldSameAsDom<Vector<PoolString>>({"one"_intern, "two"_intern});
        ExpectFieldSameAsDom<FixedVector<int32_t, 4>>({1, 2, 3, 4});
        ExpectFieldSameAsDom<FixedVector<int32_t, 4>>({});
        ExpectFieldSameAsDom<SmallVector<String, 2>>({String("x"), String("y"), String("z")});
        ExpectFieldSameAsDom<Array<int64_t, 3>>({std::numeric_limits<int64_t>::min(), 0, std::numeric_limits<int64_t>::max()});
        ExpectFieldSameAsDom<Array<uint8_t, 0>>({});
    }

    TEST(JsonWriteReflectTest, SequencesOfObjectsWithTwoFieldsAreArraysOfObjects) {
        ExpectFieldSameAsDom<Vector<Inner>>({MakeInner(1, "a"), MakeInner(2, "\xFF")});
        ExpectFieldSameAsDom<Vector<Inner>>({});
        ExpectFieldSameAsDom<FixedVector<Inner, 3>>({MakeInner(7, "s")});
        ExpectFieldSameAsDom<Array<Inner, 2>>({MakeInner(1, "a"), MakeInner(2, "b")});
    }

    TEST(JsonWriteReflectTest, AnElementThatHoldsOnlyValueCollapsesToTheScalarLikeTheDomPath) {
        ExpectFieldSameAsDom<Vector<OneValue>>({OneValue{1}, OneValue{-2}});
        ExpectFieldSameAsDom<Vector<OneValue>>({});
        ExpectFieldSameAsDom<Array<OneValue, 2>>({OneValue{3}, OneValue{4}});
        EXPECT_EQ(Written(Field<Vector<OneValue>>{{OneValue{1}, OneValue{2}}}, JsonWriter::kCompact), "{\"field\":[1,2]}");
        // An element of no fields is an empty object on both paths
        ExpectFieldSameAsDom<Vector<Empty>>({Empty{}, Empty{}});
    }

    TEST(JsonWriteReflectTest, TypesThatStayOnTheBridgeWriteTheSameBytes) {
        ExpectFieldSameAsDom<HashMap<String, int32_t>>({{"b", 2}, {"a", 1}, {"c", 3}});
        ExpectFieldSameAsDom<HashMap<String, int32_t>>({});
        ExpectFieldSameAsDom<Variant<std::monostate, int32_t, String>>(std::monostate{});
        ExpectFieldSameAsDom<Variant<std::monostate, int32_t, String>>(int32_t{4});
        ExpectFieldSameAsDom<Variant<std::monostate, int32_t, String>>(String("v"));
        ExpectFieldSameAsDom<Pair<int32_t, String>>({1, "one"});
        ExpectFieldSameAsDom<Vec2>(Vec2{0.1f, -3.5f});
        ExpectFieldSameAsDom<Vector<Vec2>>({Vec2{1.0f, 2.0f}, Vec2{0.1f, 0.2f}});
        ExpectFieldSameAsDom<Vector<Vec2>>({});
    }

    TEST(JsonWriteReflectTest, AClassWithEveryNativeShapeIsWrittenInSortedOrder) {
        ExpectSameAsDom(Native{});
        ExpectSameAsDom(MakeNative());
        Native empty = MakeNative();
        empty.optional.reset();
        empty.list.clear();
        empty.objects.clear();
        empty.fixedList.clear();
        ExpectSameAsDom(empty);
    }

    TEST(JsonWriteReflectTest, AClassWithEveryBridgedShapeIsWrittenInSortedOrder) {
        ExpectSameAsDom(Bridged{});
        ExpectSameAsDom(MakeBridged());
    }

    TEST(JsonWriteReflectTest, TheWholeObjectBridgeEqualsTheDomPath) {
        const Native native = MakeNative();
        for (const int indent : kIndents) {
            String out;
            JsonWriter writer(out, indent);
            JsonWrite::WriteSerializedObject(writer, [&](ISerializer& s) { native.Serialize(s); });
            EXPECT_TRUE(writer.IsComplete());
            EXPECT_EQ(out, Reference(native, indent));
        }
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

}  // namespace BECore::Tests
