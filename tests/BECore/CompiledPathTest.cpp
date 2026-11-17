// Compiled paths (Reflection/CompiledPath.h): the fixed part of a path resolved once, and agreement with GetProperty and the real member.
// The generated code of the test types is included in TypeRegistryTest.cpp and PropertyAccessTest.cpp; here the types are found by name.

#include <BECore/Math/IVec2.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PropertyAccessTest.h>
#include <BECore/Reflection/CompiledPath.h>
#include <BECore/TypeRegistryTest.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace PropertyAccessData;
    using namespace RegistryTestData;

    namespace {

        const TypeInfo& InfoOf(StringView name) {
            const TypeInfo* info = TypeRegistry::GetInstance().Find(name);
            EXPECT_NE(info, nullptr) << String(name).c_str();
            return *info;
        }

        template <typename Object>
        TypedRef RefOf(Object& object, StringView name) {
            return TypedRef{std::addressof(object), &InfoOf(name), false};
        }

        /// The handle finds the very member the compiler names, and the erased read agrees with GetProperty (a leaf) or says it is no value
        template <typename T, typename Object>
        void ExpectSame(Object& object, StringView typeName, StringView path, const T& member) {
            const TypeInfo& info = InfoOf(typeName);
            const auto property = CompileProperty<T>(info, path);
            ASSERT_TRUE(property.has_value()) << String(path).c_str() << ": " << (property.has_value() ? String{} : property.error().message).c_str();
            EXPECT_EQ(&property->Get(&object), &member) << String(path).c_str();
            EXPECT_EQ(&property->Mutable(&object), &member) << String(path).c_str();

            const auto compiled = CompilePath(info, path);
            ASSERT_TRUE(compiled.has_value()) << String(path).c_str();
            EXPECT_TRUE(compiled->IsFixed());
            const TypedRef ref = RefOf(object, typeName);
            const auto erased = ReadCompiled(*compiled, ref);
            const auto byName = GetProperty(ref, path);
            ASSERT_EQ(erased.has_value(), byName.has_value()) << String(path).c_str();
            if (erased.has_value()) {
                EXPECT_EQ(*erased, *byName) << String(path).c_str();
            } else {
                EXPECT_EQ(erased.error().code, byName.error().code) << String(path).c_str();
            }
        }

        template <typename T>
        void ExpectNotFixed(StringView typeName, StringView path, StringView rest) {
            const auto property = CompileProperty<T>(InfoOf(typeName), path);
            ASSERT_FALSE(property.has_value()) << String(path).c_str();
            EXPECT_EQ(property.error().code, PropertyErrorCode::not_fixed) << String(path).c_str();
            const auto compiled = CompilePath(InfoOf(typeName), path);
            ASSERT_TRUE(compiled.has_value()) << String(path).c_str();
            EXPECT_EQ(compiled->rest, String(rest.data(), rest.size())) << String(path).c_str();
        }

    }  // namespace

    TEST(CompiledPathTest, EveryFieldKindOfRegAllKindsAgreesWithTheMemberAndGetProperty) {
        RegAllKinds all;
        all.flag = true;
        all.tiny = -3;
        all.word = -300;
        all.count = -70000;
        all.big = -5000000000;
        all.utiny = 200;
        all.uword = 60000;
        all.ucount = 4000000000U;
        all.ubig = 18000000000000000000ULL;
        all.ratio = 1.5f;
        all.precise = -2.25;
        all.text = "text";
        all.fixedText = "fixed";
        all.name = "pooled"_intern;
        all.mode = RegMode::Auto;
        all.inner.value = 8;
        all.inner.label = "inner";
        all.items = {1, 2};
        all.maybe = 4;
        all.entry = {5, "five"};
        all.choice = String("alt");

        constexpr StringView kType = "RegAllKinds";
        ExpectSame(all, kType, "flag", all.flag);
        ExpectSame(all, kType, "tiny", all.tiny);
        ExpectSame(all, kType, "word", all.word);
        ExpectSame(all, kType, "count", all.count);
        ExpectSame(all, kType, "big", all.big);
        ExpectSame(all, kType, "utiny", all.utiny);
        ExpectSame(all, kType, "uword", all.uword);
        ExpectSame(all, kType, "ucount", all.ucount);
        ExpectSame(all, kType, "ubig", all.ubig);
        ExpectSame(all, kType, "ratio", all.ratio);
        ExpectSame(all, kType, "precise", all.precise);
        ExpectSame(all, kType, "text", all.text);
        ExpectSame(all, kType, "fixedText", all.fixedText);
        ExpectSame(all, kType, "name", all.name);
        ExpectSame(all, kType, "mode", all.mode);
        ExpectSame(all, kType, "inner", all.inner);
        ExpectSame(all, kType, "inner.value", all.inner.value);
        ExpectSame(all, kType, "inner.label", all.inner.label);
        ExpectSame(all, kType, "items", all.items);
        ExpectSame(all, kType, "smallItems", all.smallItems);
        ExpectSame(all, kType, "fixedItems", all.fixedItems);
        ExpectSame(all, kType, "pairOfInts", all.pairOfInts);
        ExpectSame(all, kType, "table", all.table);
        ExpectSame(all, kType, "fixedTable", all.fixedTable);
        ExpectSame(all, kType, "ordered", all.ordered);
        ExpectSame(all, kType, "maybe", all.maybe);
        ExpectSame(all, kType, "entry", all.entry);
        ExpectSame(all, kType, "choice", all.choice);
        ExpectSame(all, kType, "node", all.node);
        ExpectSame(all, kType, "inners", all.inners);
    }

    TEST(CompiledPathTest, TheTypesOfTheAccessTestAgreeToo) {
        PaScalars scalars;
        ExpectSame(scalars, "PaScalars", "i32", scalars.i32);
        ExpectSame(scalars, "PaScalars", "f32", scalars.f32);
        ExpectSame(scalars, "PaScalars", "shade", scalars.shade);
        ExpectSame(scalars, "PaScalars", "text", scalars.text);

        PaTop top;
        ExpectSame(top, "PaTop", "middle.core.depth", top.middle.core.depth);
        ExpectSame(top, "PaTop", "middle.core.label", top.middle.core.label);
        ExpectSame(top, "PaTop", "middle", top.middle);

        PaGuarded guarded;
        ExpectSame(guarded, "PaGuarded", "opacity", guarded._opacity);
        ExpectSame(guarded, "PaGuarded", "frozen.depth", guarded._frozen.depth);
        ExpectSame(guarded, "PaGuarded", "open.label", guarded.open.label);

        PaContainers containers;
        ExpectSame(containers, "PaContainers", "maybeItem", containers.maybeItem);
        ExpectSame(containers, "PaContainers", "emptyNode", containers.emptyNode);
        ExpectSame(containers, "PaContainers", "nodes", containers.nodes);
    }

    TEST(CompiledPathTest, AFieldOfAParentIsFoundAtTheParentsOffset) {
        RegDerived derived;
        ExpectSame(derived, "RegDerived", "baseValue", derived.baseValue);
        ExpectSame(derived, "RegDerived", "ownValue", derived.ownValue);
        ExpectSame(derived, "RegDerived", "nested.value", derived.nested.value);

        PaDerived paDerived;
        ExpectSame(paDerived, "PaDerived", "baseValue", paDerived.baseValue);
        ExpectSame(paDerived, "PaDerived", "core.depth", paDerived.core.depth);
    }

    TEST(CompiledPathTest, ABaseThatIsNotAtOffsetZeroAddsItsOffset) {
        // The generator names a parent only when it is the first base: copy the type's info and name the parent by hand, as TypeRegistryTest does
        RegShifted shifted;
        const RegBase& asBase = shifted;
        const auto distance = reinterpret_cast<const char*>(&asBase) - reinterpret_cast<const char*>(&shifted);
        ASSERT_GT(distance, 0);

        TypeInfo shiftedInfo = InfoOf("RegShifted");
        shiftedInfo.parentHash = InfoOf("RegBase").Hash();
        shiftedInfo.parentOffset = static_cast<size_t>(distance);
        TypeRegistry registry;
        registry.Register(InfoOf("RegBase"));
        registry.Register(shiftedInfo);

        const auto property = CompileProperty<int32_t>(shiftedInfo, "baseValue", registry);
        ASSERT_TRUE(property.has_value());
        EXPECT_EQ(&property->Get(&shifted), &shifted.baseValue);
        EXPECT_EQ(property->Offset(), static_cast<size_t>(distance) + InfoOf("RegBase").fields[0].offset);

        const auto own = CompileProperty<int32_t>(shiftedInfo, "shiftedValue", registry);
        ASSERT_TRUE(own.has_value());
        EXPECT_EQ(&own->Get(&shifted), &shifted.shiftedValue);

        const auto compiled = CompilePath(shiftedInfo, "baseValue", registry);
        ASSERT_TRUE(compiled.has_value());
        shifted.baseValue = 77;
        const TypedRef ref{&shifted, &shiftedInfo, false};
        const auto read = ReadCompiled(*compiled, ref, registry);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, PropertyValue(std::in_place_type<int64_t>, 77));
    }

    TEST(CompiledPathTest, APathCompiledAgainstABaseReadsTheBaseSubobjectOfADerivedObject) {
        RegDerived derived;
        derived.baseValue = 41;
        const RegBase& asBase = derived;

        const auto property = CompileProperty<int32_t>(InfoOf("RegBase"), "baseValue");
        ASSERT_TRUE(property.has_value());
        EXPECT_EQ(property->Get(&asBase), 41);
        EXPECT_EQ(&property->Get(&asBase), &derived.baseValue);

        const auto compiled = CompilePath(InfoOf("RegBase"), "baseValue");
        ASSERT_TRUE(compiled.has_value());
        const auto throughDerived = ReadCompiled(*compiled, RefOf(derived, "RegDerived"));
        ASSERT_TRUE(throughDerived.has_value());
        EXPECT_EQ(*throughDerived, PropertyValue(std::in_place_type<int64_t>, 41));
        const auto throughBase = ReadCompiled(*compiled, TypedRef{const_cast<RegBase*>(&asBase), &InfoOf("RegBase"), false});
        ASSERT_TRUE(throughBase.has_value());
        EXPECT_EQ(*throughBase, *throughDerived);

        // An object of an unrelated type is refused, whatever its layout
        RegInner inner;
        const auto wrong = ReadCompiled(*compiled, RefOf(inner, "RegInner"));
        ASSERT_FALSE(wrong.has_value());
        EXPECT_EQ(wrong.error().code, PropertyErrorCode::type_mismatch);
    }

    TEST(CompiledPathTest, APathBehindAListAMapAnOptionalOrAPointerIsNotFixedButReadable) {
        ExpectNotFixed<int32_t>("PaContainers", "items[1].id", "[1].id");
        ExpectNotFixed<int32_t>("PaContainers", "numbers[2]", "[2]");
        ExpectNotFixed<int32_t>("PaContainers", "table[\"a\"]", "[\"a\"]");
        ExpectNotFixed<int32_t>("PaContainers", "maybeItem.id", ".id");
        ExpectNotFixed<int32_t>("PaContainers", "node.id", ".id");
        ExpectNotFixed<int32_t>("PaContainers", "choice.value", ".value");
        ExpectNotFixed<int32_t>("PaContainers", "entry.first", ".first");

        PaContainers containers;
        containers.items.resize(2);
        containers.items[1].id = 6;
        containers.numbers = {1, 2, 3};
        containers.table["a"] = 9;
        containers.maybeItem = PaItem{};
        containers.maybeItem->id = 12;
        containers.node = New<PaNode>();
        containers.choice = 15;
        containers.entry = {16, "x"};
        const TypedRef ref = RefOf(containers, "PaContainers");

        const StringView paths[] = {"items[1].id", "numbers[2]", "table[\"a\"]", "maybeItem.id", "node.id", "choice.value", "entry.first", "entry.second"};
        for (const StringView path : paths) {
            const auto compiled = CompilePath(InfoOf("PaContainers"), path);
            ASSERT_TRUE(compiled.has_value()) << String(path).c_str();
            EXPECT_FALSE(compiled->IsFixed());
            const auto erased = ReadCompiled(*compiled, ref);
            const auto byName = GetProperty(ref, path);
            ASSERT_TRUE(byName.has_value()) << String(path).c_str();
            ASSERT_TRUE(erased.has_value()) << String(path).c_str() << ": " << (erased.has_value() ? String{} : erased.error().message).c_str();
            EXPECT_EQ(*erased, *byName) << String(path).c_str();
        }

        // What the live object does not have is the answer of the read, not of the compile
        const auto missing = CompilePath(InfoOf("PaContainers"), "items[7].id");
        ASSERT_TRUE(missing.has_value());
        const auto absent = ReadCompiled(*missing, ref);
        ASSERT_FALSE(absent.has_value());
        EXPECT_EQ(absent.error().code, PropertyErrorCode::not_found);
    }

    TEST(CompiledPathTest, AFixedPathInsideAnEmbeddedObjectBehindAListContinuesFromTheEmbeddedObject) {
        // `inner` of RegAllKinds is embedded: its fields are fixed; the list of structs ends the fixed part at the field
        RegAllKinds all;
        all.inners.resize(2);
        all.inners[1].value = 33;
        const auto compiled = CompilePath(InfoOf("RegAllKinds"), "inners[1].value");
        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(compiled->fixedSegments, 1U);
        EXPECT_EQ(compiled->type->kind, TypeKind::List);
        const auto read = ReadCompiled(*compiled, RefOf(all, "RegAllKinds"));
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, PropertyValue(std::in_place_type<int64_t>, 33));
    }

    TEST(CompiledPathTest, TheRefusalsOfTheCompile) {
        const TypeInfo& info = InfoOf("PaScalars");

        const auto unknown = CompilePath(info, "nothing");
        ASSERT_FALSE(unknown.has_value());
        EXPECT_EQ(unknown.error().code, PropertyErrorCode::not_found);
        EXPECT_NE(unknown.error().message.find("unknown field"), String::npos) << unknown.error().message.c_str();

        const auto members = CompilePath(info, "i32.more");
        ASSERT_FALSE(members.has_value());
        EXPECT_EQ(members.error().code, PropertyErrorCode::not_found);

        const auto elements = CompilePath(InfoOf("PaTop"), "middle[0]");
        ASSERT_FALSE(elements.has_value());
        EXPECT_EQ(elements.error().code, PropertyErrorCode::not_found);

        const auto badPath = CompilePath(info, "i32..x");
        ASSERT_FALSE(badPath.has_value());
        EXPECT_EQ(badPath.error().code, PropertyErrorCode::invalid_path);

        const auto wrongType = CompileProperty<int64_t>(info, "i32");
        ASSERT_FALSE(wrongType.has_value());
        EXPECT_EQ(wrongType.error().code, PropertyErrorCode::type_mismatch);
        EXPECT_EQ(CompileProperty<uint32_t>(info, "i32").error().code, PropertyErrorCode::type_mismatch);
        EXPECT_EQ(CompileProperty<String>(info, "i32").error().code, PropertyErrorCode::type_mismatch);
        // A text with another capacity is another type
        using WrongCapacity = FixedString<8, false>;
        using RightCapacity = FixedString<4, false>;
        EXPECT_EQ(CompileProperty<WrongCapacity>(info, "fixedText").error().code, PropertyErrorCode::type_mismatch);
        EXPECT_TRUE(CompileProperty<RightCapacity>(info, "fixedText").has_value());
    }

    TEST(CompiledPathTest, AnOpaqueFieldIsOnlyReadAsItsOwnType) {
        const TypeInfo& info = InfoOf("PaVectors");
        PaVectors vectors;
        ExpectSame<Vec2>(vectors, "PaVectors", "planar", vectors.planar);
        ExpectSame<Vec3>(vectors, "PaVectors", "spatial", vectors.spatial);
        ExpectSame<Color>(vectors, "PaVectors", "tint", vectors.tint);

        const auto wrong = CompileProperty<Vec3>(info, "planar");
        ASSERT_FALSE(wrong.has_value());
        EXPECT_EQ(wrong.error().code, PropertyErrorCode::type_mismatch);
        EXPECT_EQ(CompileProperty<Vec2>(info, "spatial").error().code, PropertyErrorCode::type_mismatch);
        EXPECT_EQ(CompileProperty<Vec4>(info, "spatial").error().code, PropertyErrorCode::type_mismatch);
        EXPECT_EQ(CompileProperty<Color>(info, "planar").error().code, PropertyErrorCode::type_mismatch);
        EXPECT_EQ(CompileProperty<IVec2>(info, "planar").error().code, PropertyErrorCode::type_mismatch);
    }

    TEST(CompiledPathTest, SetStillChecksReadOnlyHiddenAndRange) {
        PaGuarded guarded;
        const TypeInfo& info = InfoOf("PaGuarded");

        const auto opacity = CompileProperty<float>(info, "opacity");
        ASSERT_TRUE(opacity.has_value());
        EXPECT_TRUE(opacity->Set(&guarded, 0.75f));
        EXPECT_FLOAT_EQ(guarded._opacity, 0.75f);
        EXPECT_FALSE(opacity->Set(&guarded, 1.5f));
        EXPECT_FALSE(opacity->Set(&guarded, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FLOAT_EQ(guarded._opacity, 0.75f);

        const auto locked = CompileProperty<int32_t>(info, "locked");
        ASSERT_TRUE(locked.has_value());
        EXPECT_FALSE(locked->Set(&guarded, 1));
        EXPECT_EQ(guarded._locked, 11);
        EXPECT_EQ(locked->Get(&guarded), 11);

        const auto secret = CompileProperty<int32_t>(info, "secret");
        ASSERT_TRUE(secret.has_value());
        EXPECT_FALSE(secret->Set(&guarded, 1));
        EXPECT_EQ(secret->Get(&guarded), 12);

        // `readonly` of a struct above the leaf counts
        const auto frozen = CompileProperty<int32_t>(info, "frozen.depth");
        ASSERT_TRUE(frozen.has_value());
        EXPECT_FALSE(frozen->Set(&guarded, 1));
        const auto open = CompileProperty<int32_t>(info, "open.depth");
        ASSERT_TRUE(open.has_value());
        EXPECT_TRUE(open->Set(&guarded, 5));
        EXPECT_EQ(guarded.open.depth, 5);

        // `range` of a list field means its elements
        const auto percents = CompileProperty<Vector<int32_t>>(info, "percents");
        ASSERT_TRUE(percents.has_value());
        EXPECT_TRUE(percents->Set(&guarded, Vector<int32_t>{1, 100}));
        EXPECT_FALSE(percents->Set(&guarded, Vector<int32_t>{1, 101}));
        EXPECT_EQ(guarded._percents.size(), 2U);
        EXPECT_EQ(guarded._percents[1], 100);

        // Mutable is the unchecked write
        locked->Mutable(&guarded) = 99;
        EXPECT_EQ(guarded._locked, 99);
    }

    TEST(CompiledPathTest, AHandleReadsManyTimesWithoutTheRegistry) {
        PaScalars scalars;
        const TypeInfo& info = InfoOf("PaScalars");
        const auto property = CompileProperty<int32_t>(info, "i32");
        ASSERT_TRUE(property.has_value());

        // The reference is the thunk of the field; the handle never calls it
        const FieldDesc* field = nullptr;
        for (const FieldDesc& candidate : info.fields) {
            if (candidate.name == "i32") {
                field = &candidate;
            }
        }
        ASSERT_NE(field, nullptr);

        int64_t sum = 0;
        for (int32_t i = 0; i < 100000; ++i) {
            scalars.i32 = i;
            sum += property->Get(&scalars);
        }
        EXPECT_EQ(sum, int64_t{99999} * 100000 / 2);
        EXPECT_EQ(&property->Get(&scalars), static_cast<const int32_t*>(field->address(&scalars)));
    }

}  // namespace BECore::Tests
