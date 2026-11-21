// The helpers of the generated writer (Control/JsonWriteReflect.h): every text they write is compared with `JsonSerializer` + `DumpJson` of the same
// value, the DOM path they replace. The types are written by hand here (the generated ones are in the tests of the generator).

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/JsonWriteReflectTypes.h>
#include <BECore/Math/Vec2.h>
#include <Common/AllocCheck.h>
#include <Common/Require.h>
#include <Generated/JsonWriteReflectTypes.gen.hpp>
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

    // =========================================================================
    // The generated WriteJson (JsonWriteReflectTypes.h) against the DOM path
    // =========================================================================

    namespace {

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
        using JsonWriteTestData::JsonWriteMode;
        using JsonWriteTestData::JsonWriteNested;
        using JsonWriteTestData::JsonWriteOneValue;
        using JsonWriteTestData::JsonWriteOneValueList;
        using JsonWriteTestData::JsonWritePtrBase;
        using JsonWriteTestData::JsonWritePtrEarly;
        using JsonWriteTestData::JsonWritePtrHolder;
        using JsonWriteTestData::JsonWritePtrLate;
        using JsonWriteTestData::JsonWritePtrListHolder;
        using JsonWriteTestData::JsonWritePtrPlain;
        using JsonWriteTestData::JsonWritePtrPlainChild;
        using JsonWriteTestData::JsonWritePtrSame;
        using JsonWriteTestData::JsonWriteScalars;

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
            const auto parsed = ParseJson(text);
            T result;
            if (!parsed.has_value()) {
                ADD_FAILURE() << "does not parse: " << text.c_str();
                return result;
            }
            JsonDeserializer reader(*parsed);
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

    TEST(JsonWriteReflectTest, GeneratedScalarsEqualTheDomPath) {
        ExpectSameAsDom(JsonWriteScalars{});
        ExpectSameAsDom(MakeScalars());
        ExpectSameAsDom(MakeHostileScalars());
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

    TEST(JsonWriteReflectTest, GeneratedNestedShapesEqualTheDomPath) {
        ExpectSameAsDom(JsonWriteNested{});
        ExpectSameAsDom(MakeNested());
        JsonWriteNested emptied = MakeNested();
        emptied.maybeCount.reset();
        emptied.maybeInner.reset();
        emptied.maybeText.reset();
        emptied.objects.clear();
        emptied.fixedNumbers.clear();
        ExpectSameAsDom(emptied);
    }

    TEST(JsonWriteReflectTest, GeneratedEmptyOptionalsLeaveTheirMembersOutAndEmptyObjectsStay) {
        const String text = Written(JsonWriteNested{}, JsonWriter::kCompact);
        EXPECT_EQ(text.find("maybeCount"), String::npos);
        EXPECT_EQ(text.find("maybeInner"), String::npos);
        EXPECT_NE(text.find("\"empty\":{}"), String::npos);
        EXPECT_NE(text.find("\"none\":[]"), String::npos);
    }

    TEST(JsonWriteReflectTest, GeneratedElementOfOneValueCollapsesLikeTheDomPath) {
        JsonWriteOneValueList value;
        value.ones.resize(2);
        value.ones[0].value = 1;
        value.ones[1].value = -2;
        value.single.value = 7;
        ExpectSameAsDom(value);
        ExpectSameAsDom(JsonWriteOneValueList{});
        EXPECT_NE(Written(value, JsonWriter::kCompact).find("\"ones\":[1,-2]"), String::npos);
    }

    TEST(JsonWriteReflectTest, GeneratedBridgedFieldsEqualTheDomPath) {
        ExpectSameAsDom(JsonWriteBridged{});
        ExpectSameAsDom(MakeGeneratedBridged());
    }

    TEST(JsonWriteReflectTest, GeneratedChildInterleavesTheKeysOfItsParent) {
        ExpectSameAsDom(JsonWriteChild{});
        ExpectSameAsDom(MakeChild());
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
        ExpectSameAsDom(JsonWriteChainBase{});
        ExpectSameAsDom(JsonWriteChainMid{});
        ExpectSameAsDom(JsonWriteChainLeaf{});
        ExpectSameAsDom(MakeLeaf());
        JsonWriteChainMid mid;
        mid.e = "mid only";
        mid.maybe = 1;
        ExpectSameAsDom(mid);
        JsonWriteChainLeaf bare = MakeLeaf();
        bare.maybe.reset();
        ExpectSameAsDom(bare);
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
        ExpectSameAsDom(JsonWriteChildOfEmpty{});
        ExpectSameAsDom(JsonWriteChainEmptyMid{});
        JsonWriteChainEmptyLeaf leaf;
        leaf.SetHidden(3);
        ExpectSameAsDom(leaf);
        EXPECT_NE(Written(leaf, JsonWriter::kCompact).find("\"a\":8"), String::npos);
    }

    TEST(JsonWriteReflectTest, ChildKeysThatAllSortBeforeTheParentsKeysComeFirst) {
        JsonWriteChildBefore value;
        value.name = "n";
        value.count = 2;
        ExpectSameAsDom(value);
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
        JsonWritePtrHolder MakeHolder(IntrusivePtrNonAtomic<T> factory) {
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
        ExpectSameAsDom(holder);
        EXPECT_NE(Written(holder, JsonWriter::kCompact).find("\"factory\":{\"alpha\":4,\"label\":\"lbl\",\"type\":\"JsonWritePtrEarly\"}"), String::npos);

        auto late = New<JsonWritePtrLate>();
        late->zeta = 5;
        holder = MakeHolder(late);
        ExpectSameAsDom(holder);
        EXPECT_NE(Written(holder, JsonWriter::kCompact).find("\"factory\":{\"label\":\"lbl\",\"type\":\"JsonWritePtrLate\",\"zeta\":5}"), String::npos);
    }

    TEST(JsonWriteReflectTest, AMemberNamedTypeReplacesTheClassNameAsOnTheDomPath) {
        auto same = New<JsonWritePtrSame>();
        same->type = "mine";
        JsonWritePtrHolder holder = MakeHolder(same);
        ExpectSameAsDom(holder);
        const String text = Written(holder, JsonWriter::kCompact);
        EXPECT_NE(text.find("\"type\":\"mine\""), String::npos);
        EXPECT_EQ(text.find("JsonWritePtrSame"), String::npos);
    }

    TEST(JsonWriteReflectTest, ANullPointerWritesNoMemberAndAPlainPointerWritesItsDynamicObject) {
        JsonWritePtrHolder holder;
        ExpectSameAsDom(holder);
        EXPECT_EQ(Written(holder, JsonWriter::kCompact), "{}");

        auto child = New<JsonWritePtrPlainChild>();
        child->id = 3;
        child->name = "n";
        child->extra = 8;
        child->_count = 9;
        holder.plain = child;
        ExpectSameAsDom(holder);
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
        ExpectSameAsDom(holder);
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

    TEST(JsonWriteReflectTest, GeneratedEventEqualsTheDomPath) {
        JsonWriteEvent event;
        event.text = "event \"text\"";
        event.id = -3;
        event.weight = kAwkwardFloat;
        ExpectSameAsDom(event);
        event.optionalId = 8;
        ExpectSameAsDom(event);
        ExpectSameAsDom(JsonWriteEvent{});
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

}  // namespace BECore::Tests
