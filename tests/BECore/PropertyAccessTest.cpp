// GetProperty and SetProperty (Reflection/PropertyAccess.h): the value table, the checks of attributes, every container shape.

#include "PropertyAccessTest.h"

#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Generated/PropertyAccessTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace PropertyAccessData;

    namespace {

        constexpr StringView kNeverInterned = "pa_never_interned_lookup_text";

        template <typename T>
        TypedRef Ref(T& object) {
            const TypedRef ref = TypedRef::Of(object);
            EXPECT_TRUE(ref);
            return ref;
        }

        /// The saved text of an object: two equal states give equal text
        String Snapshot(TypedRef ref) {
            XmlSerializer serializer;
            serializer.BeginObject("object");
            ref.type->serialize(ref.object, serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        PropertyValue Read(TypedRef ref, StringView path) {
            const auto value = GetProperty(ref, path);
            EXPECT_TRUE(value.has_value()) << String(path).c_str() << ": " << (value.has_value() ? String{} : value.error().message).c_str();
            return value.has_value() ? *value : PropertyValue{};
        }

        void ExpectReadError(TypedRef ref, StringView path, PropertyErrorCode code) {
            const auto value = GetProperty(ref, path);
            ASSERT_FALSE(value.has_value()) << String(path).c_str();
            EXPECT_EQ(value.error().code, code) << String(path).c_str() << ": " << value.error().message.c_str();
            EXPECT_TRUE(Contains(value.error().message, path)) << value.error().message.c_str();
        }

        void ExpectWritten(TypedRef ref, StringView path, const PropertyValue& value, const PropertyValue& readBack, const SetOptions& options = {}) {
            const auto result = SetProperty(ref, path, value, options);
            ASSERT_TRUE(result.has_value()) << String(path).c_str() << ": " << (result.has_value() ? String{} : result.error().message).c_str();
            EXPECT_EQ(Read(ref, path), readBack) << String(path).c_str();
        }

        /// A refused write names the path and changes nothing
        void ExpectRefused(TypedRef ref, StringView path, const PropertyValue& value, PropertyErrorCode code, const SetOptions& options = {}) {
            const String before = Snapshot(ref);
            const auto result = SetProperty(ref, path, value, options);
            ASSERT_FALSE(result.has_value()) << String(path).c_str();
            EXPECT_EQ(result.error().code, code) << String(path).c_str() << ": " << result.error().message.c_str();
            EXPECT_TRUE(Contains(result.error().message, path)) << result.error().message.c_str();
            EXPECT_EQ(Snapshot(ref), before) << String(path).c_str();
        }

        PaItem MakeItem(int32_t id, StringView name) {
            PaItem item;
            item.id = id;
            item.name = String(name.data(), name.size());
            return item;
        }

        PropertyValue Int(int64_t value) {
            return PropertyValue(std::in_place_type<int64_t>, value);
        }

        PropertyValue Uint(uint64_t value) {
            return PropertyValue(std::in_place_type<uint64_t>, value);
        }

        PropertyValue Num(double value) {
            return PropertyValue(std::in_place_type<double>, value);
        }

        PropertyValue Text(StringView value) {
            return PropertyValue(std::in_place_type<String>, String(value.data(), value.size()));
        }

        PropertyValue Flag(bool value) {
            return PropertyValue(std::in_place_type<bool>, value);
        }

    }  // namespace

    // -------------------------------------------------------------------------
    // The value table: what a read gives and what a write takes, per field type
    // -------------------------------------------------------------------------

    TEST(PropertyAccessTest, ReadGivesTheKindOfTheFieldType) {
        PaScalars scalars;
        scalars.fixedText = "ab";
        scalars.name = "pooled"_intern;
        const TypedRef ref = Ref(scalars);

        EXPECT_EQ(Read(ref, "flag"), Flag(true));
        EXPECT_EQ(Read(ref, "i8"), Int(-8));
        EXPECT_EQ(Read(ref, "i16"), Int(-16));
        EXPECT_EQ(Read(ref, "i32"), Int(-32));
        EXPECT_EQ(Read(ref, "i64"), Int(-64));
        EXPECT_EQ(Read(ref, "u8"), Uint(8));
        EXPECT_EQ(Read(ref, "u16"), Uint(16));
        EXPECT_EQ(Read(ref, "u32"), Uint(32));
        EXPECT_EQ(Read(ref, "u64"), Uint(64));
        EXPECT_EQ(Read(ref, "f32"), Num(0.5));
        EXPECT_EQ(Read(ref, "f64"), Num(0.25));
        EXPECT_EQ(Read(ref, "text"), Text("hello"));
        EXPECT_EQ(Read(ref, "fixedText"), Text("ab"));
        EXPECT_EQ(Read(ref, "name"), Text("pooled"));
        EXPECT_EQ(Read(ref, "shade"), Text("Mid"));
    }

    TEST(PropertyAccessTest, WriteTakesWhatFitsEachFieldType) {
        PaScalars scalars;
        const TypedRef ref = Ref(scalars);

        ExpectWritten(ref, "flag", Flag(false), Flag(false));
        ExpectWritten(ref, "i8", Int(-128), Int(-128));
        ExpectWritten(ref, "i8", Uint(127), Int(127));
        ExpectWritten(ref, "i16", Int(32767), Int(32767));
        ExpectWritten(ref, "i32", Int(std::numeric_limits<int32_t>::min()), Int(std::numeric_limits<int32_t>::min()));
        ExpectWritten(ref, "i64", Int(std::numeric_limits<int64_t>::min()), Int(std::numeric_limits<int64_t>::min()));
        ExpectWritten(ref, "i64", Uint(5), Int(5));
        ExpectWritten(ref, "u8", Int(255), Uint(255));
        ExpectWritten(ref, "u16", Uint(65535), Uint(65535));
        ExpectWritten(ref, "u32", Int(0), Uint(0));
        ExpectWritten(ref, "u64", Uint(std::numeric_limits<uint64_t>::max()), Uint(std::numeric_limits<uint64_t>::max()));
        ExpectWritten(ref, "f32", Num(1.5), Num(1.5));
        ExpectWritten(ref, "f32", Int(2), Num(2.0));
        ExpectWritten(ref, "f32", Uint(3), Num(3.0));
        ExpectWritten(ref, "f64", Num(-1.0e300), Num(-1.0e300));
        ExpectWritten(ref, "f64", Int(-4), Num(-4.0));
        ExpectWritten(ref, "text", Text("a longer text than the fixed one"), Text("a longer text than the fixed one"));
        ExpectWritten(ref, "fixedText", Text("fou"), Text("fou"));
        ExpectWritten(ref, "shade", Text("Light"), Text("Light"));
        ExpectWritten(ref, "shade", Int(0), Text("Dark"));
        ExpectWritten(ref, "shade", Uint(1), Text("Mid"));
        EXPECT_EQ(scalars.shade, PaShade::Mid);
        EXPECT_EQ(scalars.i8, 127);
    }

    TEST(PropertyAccessTest, WriteRefusesTheWrongKindAndTheOutOfRange) {
        PaScalars scalars;
        const TypedRef ref = Ref(scalars);

        // A bool takes only a bool, an integer never a double (even a whole one), a text only a string
        ExpectRefused(ref, "flag", Int(1), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "flag", Text("true"), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "i32", Flag(true), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "i32", Num(3.0), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "i32", Text("3"), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "u8", Num(3.0), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "f32", Text("1.5"), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "f64", Flag(true), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "text", Int(1), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "fixedText", Num(1.0), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "shade", Flag(true), PropertyErrorCode::type_mismatch);
        ExpectRefused(ref, "shade", Num(1.0), PropertyErrorCode::type_mismatch);

        // An integer must fit its own type
        ExpectRefused(ref, "i8", Int(200), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "i8", Int(-129), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "u8", Int(-1), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "u8", Uint(256), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "i16", Int(32768), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "u16", Int(65536), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "i32", Int(int64_t{std::numeric_limits<int32_t>::max()} + 1), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "u32", Uint(uint64_t{std::numeric_limits<uint32_t>::max()} + 1), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "i64", Uint(std::numeric_limits<uint64_t>::max()), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "u64", Int(-1), PropertyErrorCode::out_of_range);

        // A float or a double never takes a non-finite number, a float not one beyond its range
        ExpectRefused(ref, "f32", Num(std::numeric_limits<double>::quiet_NaN()), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "f32", Num(std::numeric_limits<double>::infinity()), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "f32", Num(1.0e300), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "f32", Num(-1.0e300), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "f64", Num(-std::numeric_limits<double>::infinity()), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "f64", Num(std::numeric_limits<double>::quiet_NaN()), PropertyErrorCode::out_of_range);

        // An enum takes one of its names or an index below the count
        ExpectRefused(ref, "shade", Text("Purple"), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "shade", Text(""), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "shade", Int(3), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "shade", Int(-1), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "shade", Uint(99), PropertyErrorCode::out_of_range);
    }

    TEST(PropertyAccessTest, AFixedStringThatWouldCutTheTextKeepsItsOldValue) {
        PaScalars scalars;
        scalars.fixedText = "abc";
        const TypedRef ref = Ref(scalars);

        ExpectRefused(ref, "fixedText", Text("toolong"), PropertyErrorCode::out_of_range);
        EXPECT_EQ(Read(ref, "fixedText"), Text("abc"));
        ExpectWritten(ref, "fixedText", Text("xyz"), Text("xyz"));
        ExpectRefused(ref, "fixedText", Text("xyzw"), PropertyErrorCode::out_of_range);
        EXPECT_EQ(Read(ref, "fixedText"), Text("xyz"));
    }

    TEST(PropertyAccessTest, APoolStringIsWrittenOnlyWhenInterningIsAllowed) {
        PaScalars scalars;
        const TypedRef ref = Ref(scalars);
        const String text = FreshText("pa-interned-only-on-request");

        ExpectRefused(ref, "name", Text(text.c_str()), PropertyErrorCode::interning);
        EXPECT_FALSE(PoolString::Find(StringView(text.data(), text.size())).has_value());
        ExpectRefused(ref, "name", Int(1), PropertyErrorCode::type_mismatch, SetOptions{true});

        SetOptions allow;
        allow.allowInterning = true;
        ExpectWritten(ref, "name", Text(text.c_str()), Text(text.c_str()), allow);
        EXPECT_EQ(scalars.name.ToStringView(), StringView(text.data(), text.size()));
    }

    TEST(PropertyAccessTest, ALookupNeverInterns) {
        PaScalars scalars;
        const TypedRef ref = Ref(scalars);

        ExpectReadError(ref, kNeverInterned, PropertyErrorCode::not_found);
        ExpectRefused(ref, kNeverInterned, Int(1), PropertyErrorCode::not_found);
        const String keyed = Format("table[\"{}\"]", kNeverInterned);
        static_cast<void>(GetProperty(ref, StringView(keyed.data(), keyed.size())));
        EXPECT_FALSE(PoolString::Find(kNeverInterned).has_value());
    }

    // -------------------------------------------------------------------------
    // The attributes: range, readonly, hidden
    // -------------------------------------------------------------------------

    TEST(PropertyAccessTest, RangeOfTheInnermostFieldIsCheckedOnANumericWrite) {
        PaGuarded guarded;
        const TypedRef ref = Ref(guarded);

        ExpectWritten(ref, "opacity", Num(1.0), Num(1.0));
        ExpectWritten(ref, "opacity", Int(0), Num(0.0));
        ExpectRefused(ref, "opacity", Num(1.5), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "opacity", Num(-0.1), PropertyErrorCode::out_of_range);
        ExpectWritten(ref, "ranged", Int(10), Int(10));
        ExpectRefused(ref, "ranged", Int(11), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "ranged", Int(0), PropertyErrorCode::out_of_range);
        // The check is made on the type's own failure first: the wrong kind is not a range error
        ExpectRefused(ref, "ranged", Text("5"), PropertyErrorCode::type_mismatch);
    }

    TEST(PropertyAccessTest, RangeOfAContainerFieldMeansItsNumericElements) {
        PaGuarded guarded;
        guarded._percents = {10, 20};
        const TypedRef ref = Ref(guarded);

        ExpectWritten(ref, "percents[1]", Int(100), Int(100));
        ExpectRefused(ref, "percents[0]", Int(101), PropertyErrorCode::out_of_range);
        ExpectRefused(ref, "percents[0]", Int(-1), PropertyErrorCode::out_of_range);
        EXPECT_EQ(Read(ref, "percents[0]"), Int(10));
    }

    TEST(PropertyAccessTest, ReadOnlyOfAnyFieldOnThePathRefusesAWrite) {
        PaGuarded guarded;
        guarded._lockedList = {1, 2};
        const TypedRef ref = Ref(guarded);

        EXPECT_EQ(Read(ref, "locked"), Int(11));
        ExpectRefused(ref, "locked", Int(1), PropertyErrorCode::read_only);
        // The field above the leaf decides, for a struct and for a container
        ExpectRefused(ref, "frozen.depth", Int(1), PropertyErrorCode::read_only);
        ExpectRefused(ref, "lockedList[0]", Int(5), PropertyErrorCode::read_only);
        EXPECT_EQ(Read(ref, "frozen.depth"), Int(3));
        EXPECT_EQ(Read(ref, "lockedList[1]"), Int(2));
        ExpectWritten(ref, "open.depth", Int(7), Int(7));
    }

    TEST(PropertyAccessTest, AHiddenFieldIsReadButNotWritten) {
        PaGuarded guarded;
        const TypedRef ref = Ref(guarded);

        EXPECT_EQ(Read(ref, "secret"), Int(12));
        ExpectRefused(ref, "secret", Int(1), PropertyErrorCode::hidden);
        // Hidden says more than readonly
        EXPECT_EQ(Read(ref, "sealed"), Int(13));
        ExpectRefused(ref, "sealed", Int(1), PropertyErrorCode::hidden);
    }

    TEST(PropertyAccessTest, AConstRootRefusesEveryWriteAndAllowsReads) {
        PaScalars scalars;
        const PaScalars& constant = scalars;
        const TypedRef ref = TypedRef::Of(constant);
        ASSERT_TRUE(ref);
        EXPECT_TRUE(ref.isConst);

        EXPECT_EQ(Read(ref, "i32"), Int(-32));
        ExpectRefused(ref, "i32", Int(1), PropertyErrorCode::read_only);
        ExpectRefused(ref, "text", Text("x"), PropertyErrorCode::read_only);
    }

    // -------------------------------------------------------------------------
    // Where a path can go: structs, containers, wrappers, parents
    // -------------------------------------------------------------------------

    TEST(PropertyAccessTest, AStructNestedThreeDeepIsReachedByItsFields) {
        PaTop top;
        const TypedRef ref = Ref(top);

        EXPECT_EQ(Read(ref, "top"), Int(1));
        EXPECT_EQ(Read(ref, "middle.level"), Int(2));
        EXPECT_EQ(Read(ref, "middle.core.depth"), Int(3));
        ExpectWritten(ref, "middle.core.depth", Int(30), Int(30));
        ExpectWritten(ref, "middle.core.label", Text("deep"), Text("deep"));
        EXPECT_EQ(top.middle.core.depth, 30);
        EXPECT_EQ(top.middle.level, 2);

        // A field of the wrong place or a member of a value is not found
        ExpectReadError(ref, "core.depth", PropertyErrorCode::not_found);
        ExpectReadError(ref, "middle.depth", PropertyErrorCode::not_found);
        ExpectReadError(ref, "middle.core.depth.more", PropertyErrorCode::not_found);
        ExpectReadError(ref, "middle.core.depth[0]", PropertyErrorCode::not_found);
        ExpectRefused(ref, "middle.core.nothing", Int(1), PropertyErrorCode::not_found);
    }

    TEST(PropertyAccessTest, APathThatEndsAtAStructAListOrAMapIsNotAValue) {
        PaContainers containers;
        containers.items.resize(2);
        containers.table["a"] = 1;
        containers.ordered[3] = "three";
        containers.maybeItem = MakeItem(0, "");
        containers.node = New<PaNode>();
        PaTop top;

        ExpectReadError(Ref(top), "middle", PropertyErrorCode::not_a_value);
        ExpectReadError(Ref(top), "middle.core", PropertyErrorCode::not_a_value);
        const TypedRef ref = Ref(containers);
        ExpectReadError(ref, "items", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "items[1]", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "table", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "ordered", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "entry", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "choice", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "maybeItem", PropertyErrorCode::not_a_value);
        ExpectReadError(ref, "node", PropertyErrorCode::not_a_value);
        ExpectRefused(Ref(top), "middle", Int(1), PropertyErrorCode::not_a_value);
    }

    TEST(PropertyAccessTest, ASequenceElementIsReachedByItsPosition) {
        PaContainers containers;
        containers.items = {MakeItem(1, "one"), MakeItem(2, "two")};
        containers.numbers = {10, 20, 30};
        containers.pairOfInts = {7, 8};
        const TypedRef ref = Ref(containers);

        EXPECT_EQ(Read(ref, "numbers[2]"), Int(30));
        EXPECT_EQ(Read(ref, "items[1].name"), Text("two"));
        EXPECT_EQ(Read(ref, "pairOfInts[1]"), Int(8));
        ExpectWritten(ref, "numbers[0]", Int(-5), Int(-5));
        ExpectWritten(ref, "items[0].name", Text("first"), Text("first"));
        ExpectWritten(ref, "pairOfInts[0]", Int(70), Int(70));

        ExpectReadError(ref, "numbers[3]", PropertyErrorCode::not_found);
        ExpectReadError(ref, "items[2].id", PropertyErrorCode::not_found);
        ExpectReadError(ref, "numbers.size", PropertyErrorCode::not_found);
        // A text key does not name a sequence element
        ExpectReadError(ref, "numbers[\"0\"]", PropertyErrorCode::not_found);
        // A write adds no element
        ExpectRefused(ref, "numbers[3]", Int(1), PropertyErrorCode::not_found);
        EXPECT_EQ(containers.numbers.size(), 3U);
    }

    TEST(PropertyAccessTest, AMapValueIsReachedByItsKey) {
        PaContainers containers;
        containers.table["alpha"] = 1;
        containers.table["beta"] = 2;
        containers.ordered[3] = "three";
        containers.ordered[40] = "forty";
        const TypedRef ref = Ref(containers);

        EXPECT_EQ(Read(ref, "table[\"beta\"]"), Int(2));
        ExpectWritten(ref, "table[\"alpha\"]", Int(10), Int(10));
        ExpectReadError(ref, "table[\"gamma\"]", PropertyErrorCode::not_found);
        // A field name is not a key
        ExpectReadError(ref, "table.alpha", PropertyErrorCode::not_found);

        EXPECT_EQ(Read(ref, "ordered[3]"), Text("three"));
        EXPECT_EQ(Read(ref, "ordered[\"40\"]"), Text("forty"));
        ExpectWritten(ref, "ordered[40]", Text("FORTY"), Text("FORTY"));
        ExpectReadError(ref, "ordered[4]", PropertyErrorCode::not_found);
        ExpectRefused(ref, "table[\"gamma\"]", Int(1), PropertyErrorCode::not_found);
        EXPECT_EQ(containers.table.size(), 2U);
    }

    TEST(PropertyAccessTest, AnOptionalIsAddressedThroughItsValue) {
        PaContainers containers;
        const TypedRef ref = Ref(containers);

        ExpectReadError(ref, "maybe", PropertyErrorCode::not_found);
        const auto empty = GetProperty(ref, "maybe");
        ASSERT_FALSE(empty.has_value());
        EXPECT_TRUE(Contains(empty.error().message, "empty")) << empty.error().message.c_str();
        ExpectRefused(ref, "maybe", Int(1), PropertyErrorCode::not_found);
        ExpectReadError(ref, "maybeItem.id", PropertyErrorCode::not_found);
        EXPECT_FALSE(containers.maybe.has_value());

        containers.maybe = 5;
        containers.maybeItem = MakeItem(4, "four");
        EXPECT_EQ(Read(ref, "maybe"), Int(5));
        ExpectWritten(ref, "maybe", Int(6), Int(6));
        EXPECT_EQ(Read(ref, "maybeItem.name"), Text("four"));
        ExpectWritten(ref, "maybeItem.id", Int(44), Int(44));
        ExpectReadError(ref, "maybeItem", PropertyErrorCode::not_a_value);
    }

    TEST(PropertyAccessTest, AVariantIsAddressedThroughItsActiveAlternative) {
        PaContainers containers;
        const TypedRef ref = Ref(containers);

        containers.choice = 7;
        EXPECT_EQ(Read(ref, "choice.value"), Int(7));
        ExpectWritten(ref, "choice.value", Int(8), Int(8));
        ExpectRefused(ref, "choice.value", Text("text"), PropertyErrorCode::type_mismatch);
        EXPECT_EQ(containers.choice.index(), 0U);

        containers.choice = String("word");
        EXPECT_EQ(Read(ref, "choice.value"), Text("word"));
        ExpectWritten(ref, "choice.value", Text("other"), Text("other"));
        ExpectRefused(ref, "choice.value", Int(1), PropertyErrorCode::type_mismatch);
        ExpectReadError(ref, "choice.index", PropertyErrorCode::not_found);
    }

    TEST(PropertyAccessTest, APairAndAPointerAreAddressedThroughTheirMembers) {
        PaContainers containers;
        containers.entry = {4, "four"};
        containers.node = New<PaNode>();
        containers.nodes.push_back(New<PaNode>());
        containers.nodes.push_back(New<PaNode>());
        containers.nodes[1]->id = 21;
        const TypedRef ref = Ref(containers);

        EXPECT_EQ(Read(ref, "entry.first"), Int(4));
        ExpectWritten(ref, "entry.second", Text("FOUR"), Text("FOUR"));
        EXPECT_EQ(Read(ref, "node.id"), Int(9));
        ExpectWritten(ref, "node.id", Int(90), Int(90));
        EXPECT_EQ(Read(ref, "nodes[1].id"), Int(21));
        ExpectWritten(ref, "nodes[0].id", Int(22), Int(22));
        ExpectReadError(ref, "nodes[2].id", PropertyErrorCode::not_found);

        // A null pointer has no members
        ExpectReadError(ref, "emptyNode.id", PropertyErrorCode::not_found);
        ExpectRefused(ref, "emptyNode.id", Int(1), PropertyErrorCode::not_found);
    }

    TEST(PropertyAccessTest, AFieldOfAParentIsAddressedLikeAnOwnField) {
        PaDerived derived;
        const TypedRef ref = Ref(derived);

        EXPECT_EQ(Read(ref, "baseValue"), Int(1));
        EXPECT_EQ(Read(ref, "ownValue"), Int(2));
        ExpectWritten(ref, "baseValue", Int(10), Int(10));
        ExpectWritten(ref, "core.depth", Int(11), Int(11));
        EXPECT_EQ(derived.baseValue, 10);
        EXPECT_EQ(derived.core.depth, 11);
    }

    TEST(PropertyAccessTest, AnObjectReachedThroughItsBaseKeepsItsDerivedFields) {
        PaDerived derived;
        PaBase& asBase = derived;
        const TypedRef ref = Ref(asBase);
        EXPECT_EQ(ref.object, static_cast<void*>(&derived));

        EXPECT_EQ(Read(ref, "ownValue"), Int(2));
        ExpectWritten(ref, "ownValue", Int(20), Int(20));
        EXPECT_EQ(derived.ownValue, 20);

        // The base is not at offset 0 here; the generator names a parent only when it is the first base, so the registry is told by hand
        TypeRegistry registry;
        registry.Register(MakeTypeInfo<PaBase, void>());
        registry.Register(MakeTypeInfo<PaShifted, PaBase>());
        PaShifted shifted;
        PaBase& shiftedBase = shifted;
        const TypedRef shiftedRef = TypedRef::Of(shiftedBase, registry);
        ASSERT_TRUE(shiftedRef);
        EXPECT_EQ(shiftedRef.object, static_cast<void*>(&shifted));
        ExpectWritten(shiftedRef, "shiftedValue", Int(31), Int(31));
        EXPECT_EQ(shifted.shiftedValue, 31);
    }

    // -------------------------------------------------------------------------
    // The refusals of the path and of the object
    // -------------------------------------------------------------------------

    TEST(PropertyAccessTest, ABadPathIsRefusedWithItsPositionAndNothingIsChanged) {
        PaScalars scalars;
        const TypedRef ref = Ref(scalars);

        const auto read = GetProperty(ref, "i32..x");
        ASSERT_FALSE(read.has_value());
        EXPECT_EQ(read.error().code, PropertyErrorCode::invalid_path);
        EXPECT_TRUE(Contains(read.error().message, "position")) << read.error().message.c_str();

        const String before = Snapshot(ref);
        const auto written = SetProperty(ref, "", Int(1));
        ASSERT_FALSE(written.has_value());
        EXPECT_EQ(written.error().code, PropertyErrorCode::invalid_path);
        EXPECT_EQ(Snapshot(ref), before);

        const String longPath(PropertyPathLimits::kMaxPathBytes + 1, 'a');
        const auto tooLong = GetProperty(ref, StringView(longPath.data(), longPath.size()));
        ASSERT_FALSE(tooLong.has_value());
        EXPECT_EQ(tooLong.error().code, PropertyErrorCode::too_long);
    }

    TEST(PropertyAccessTest, AnUnknownFieldNamesWhereTheNameWasNotFound) {
        PaTop top;
        const auto missing = GetProperty(Ref(top), "middle.nope");
        ASSERT_FALSE(missing.has_value());
        EXPECT_EQ(missing.error().code, PropertyErrorCode::not_found);
        EXPECT_TRUE(Contains(missing.error().message, "middle.nope")) << missing.error().message.c_str();
        EXPECT_TRUE(Contains(missing.error().message, "'middle' has no member 'nope'")) << missing.error().message.c_str();
    }

    TEST(PropertyAccessTest, AnEmptyReferenceAndATypeWithoutAVisitorAreRefused) {
        const auto empty = GetProperty(TypedRef{}, "x");
        ASSERT_FALSE(empty.has_value());
        EXPECT_EQ(empty.error().code, PropertyErrorCode::not_found);

        PaEvent event;
        const TypedRef ref = Ref(event);
        ExpectReadError(ref, "payload", PropertyErrorCode::no_visitor);
        ExpectRefused(ref, "payload", Int(1), PropertyErrorCode::no_visitor);
    }

    TEST(PropertyAccessTest, TheOverloadOfAParsedPathAgreesWithTheTextOne) {
        PaTop top;
        const TypedRef ref = Ref(top);
        const auto path = ParsePropertyPath("middle.core.depth");
        ASSERT_TRUE(path.has_value());

        const auto value = GetProperty(ref, *path);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, Int(3));
        EXPECT_TRUE(SetProperty(ref, *path, Int(4)).has_value());
        EXPECT_EQ(top.middle.core.depth, 4);
    }

    // -------------------------------------------------------------------------
    // A real engine object
    // -------------------------------------------------------------------------

    TEST(PropertyAccessTest, ACreatedTransformComponentIsReadAndSetByPath) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* info = registry.Find("TransformComponent");
        ASSERT_NE(info, nullptr);
        OwnedObject made = registry.Create(*info);
        ASSERT_TRUE(made);
        const TypedRef ref{made.Get(), info, false};

        EXPECT_EQ(Read(ref, "width"), Num(1.0));
        EXPECT_EQ(Read(ref, "pivotX"), Num(0.5));
        ExpectWritten(ref, "x", Num(2.5), Num(2.5));
        ExpectWritten(ref, "rotation", Int(90), Num(90.0));
        ExpectRefused(ref, "x", Text("far"), PropertyErrorCode::type_mismatch);
        ExpectReadError(ref, "_x", PropertyErrorCode::not_found);

        const auto* transform = static_cast<const TransformComponent*>(made.Get());
        EXPECT_FLOAT_EQ(transform->_x, 2.5f);
        EXPECT_FLOAT_EQ(transform->_rotation, 90.0f);
    }

}  // namespace BECore::Tests
