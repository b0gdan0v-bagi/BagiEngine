#include <BECore/Math/Vec2.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/TypeRegistryTest.h>
#include <BECore/Utils/StringUtils.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// No `Serialize`: its own `Ser::DataAccessor` is what saves it (as `UIEdges` and `UIColor` do)
    struct AccessorOnly {
        float value = 0.0f;
    };

}  // namespace BECore::Tests

namespace BECore::Ser {

    template <>
    struct DataAccessor<Tests::AccessorOnly> {
        static void Save(ISerializer& s, const Tests::AccessorOnly& value, StringView key) {
            s.WriteAttribute(key, value.value);
        }

        static bool Load(IDeserializer& d, Tests::AccessorOnly& value, StringView key) {
            return d.ReadAttribute(key, value.value);
        }
    };

}  // namespace BECore::Ser

namespace BECore::Tests {

    using RegistryTestData::RegInner;
    using RegistryTestData::RegMode;
    using RegistryTestData::RegNode;

    // Scalars
    static_assert(DescribeType<bool>().kind == TypeKind::Bool);
    static_assert(DescribeType<int8_t>().kind == TypeKind::Int8);
    static_assert(DescribeType<int16_t>().kind == TypeKind::Int16);
    static_assert(DescribeType<int32_t>().kind == TypeKind::Int32);
    static_assert(DescribeType<int64_t>().kind == TypeKind::Int64);
    static_assert(DescribeType<uint8_t>().kind == TypeKind::UInt8);
    static_assert(DescribeType<uint16_t>().kind == TypeKind::UInt16);
    static_assert(DescribeType<uint32_t>().kind == TypeKind::UInt32);
    static_assert(DescribeType<uint64_t>().kind == TypeKind::UInt64);
    static_assert(DescribeType<float>().kind == TypeKind::Float);
    static_assert(DescribeType<double>().kind == TypeKind::Double);
    static_assert(&DescribeType<int32_t>() == &DescribeType<const int32_t>());

    // Text
    static_assert(DescribeType<String>().kind == TypeKind::String);
    static_assert(DescribeType<String>().capacity == 0);
    static_assert(DescribeType<PoolString>().kind == TypeKind::String);
    static_assert(DescribeType<FixedString<8, false>>().kind == TypeKind::String);
    static_assert(DescribeType<FixedString<8, false>>().capacity == 8);
    static_assert(DescribeType<FixedString<8>>().capacity == 0);

    // Enum
    static_assert(DescribeType<RegMode>().kind == TypeKind::Enum);
    static_assert(DescribeType<RegMode>().names.size() == 3);
    static_assert(DescribeType<RegMode>().names[0] == "Off");
    static_assert(DescribeType<RegMode>().names[2] == "Auto");

    // Lists
    static_assert(DescribeType<Vector<int32_t>>().kind == TypeKind::List);
    static_assert(DescribeType<Vector<int32_t>>().resizable);
    static_assert(DescribeType<Vector<int32_t>>().capacity == 0);
    static_assert(DescribeType<Vector<int32_t>>().element == &DescribeType<int32_t>());
    static_assert(DescribeType<SmallVector<float, 4>>().resizable);
    static_assert(DescribeType<SmallVector<float, 4>>().capacity == 0);
    static_assert(DescribeType<SmallVector<float, 4>>().element->kind == TypeKind::Float);
    static_assert(DescribeType<FixedVector<int32_t, 3>>().resizable);
    static_assert(DescribeType<FixedVector<int32_t, 3>>().capacity == 3);
    static_assert(DescribeType<Array<uint8_t, 2>>().kind == TypeKind::List);
    static_assert(!DescribeType<Array<uint8_t, 2>>().resizable);
    static_assert(DescribeType<Array<uint8_t, 2>>().capacity == 2);

    // Maps
    static_assert(DescribeType<HashMap<String, int32_t>>().kind == TypeKind::Map);
    static_assert(DescribeType<HashMap<String, int32_t>>().key->kind == TypeKind::String);
    static_assert(DescribeType<HashMap<String, int32_t>>().element->kind == TypeKind::Int32);
    static_assert(DescribeType<HashMap<String, int32_t>>().capacity == 0);
    static_assert(DescribeType<FixedHashMap<int32_t, String, 4, 5, false>>().capacity == 4);
    static_assert(DescribeType<FixedHashMap<int32_t, String, 4, 5, true>>().capacity == 0);
    static_assert(DescribeType<Map<int32_t, String>>().key->kind == TypeKind::Int32);
    static_assert(DescribeType<Map<int32_t, String>>().element->kind == TypeKind::String);

    // Optional, pair, variant
    static_assert(DescribeType<Optional<double>>().kind == TypeKind::Optional);
    static_assert(DescribeType<Optional<double>>().element->kind == TypeKind::Double);
    static_assert(DescribeType<Pair<int32_t, String>>().kind == TypeKind::Pair);
    static_assert(DescribeType<Pair<int32_t, String>>().key->kind == TypeKind::Int32);
    static_assert(DescribeType<Pair<int32_t, String>>().element->kind == TypeKind::String);
    static_assert(DescribeType<Variant<int32_t, String, std::monostate>>().kind == TypeKind::Variant);
    static_assert(DescribeType<Variant<int32_t, String, std::monostate>>().alternatives.size() == 3);
    static_assert(DescribeType<Variant<int32_t, String, std::monostate>>().alternatives[0]->kind == TypeKind::Int32);
    static_assert(DescribeType<Variant<int32_t, String, std::monostate>>().alternatives[1]->kind == TypeKind::String);
    static_assert(DescribeType<Variant<int32_t, String, std::monostate>>().alternatives[2]->kind == TypeKind::Void);

    // Objects and pointers have their class on the descriptor as a function: the type's own generated code may be elsewhere
    static_assert(DescribeType<RegInner>().kind == TypeKind::Object);
    static_assert(DescribeType<RegInner>().objectMeta != nullptr);
    static_assert(DescribeType<IntrusivePtrNonAtomic<RegNode>>().kind == TypeKind::Pointer);
    static_assert(DescribeType<Vector<IntrusivePtrNonAtomic<RegNode>>>().element->kind == TypeKind::Pointer);

    // A type with a Serialize / Deserialize pair that is no reflected class
    static_assert(DescribeType<Vec2>().kind == TypeKind::Opaque);
    static_assert(DescribeType<Vec2>().objectMeta == nullptr);
    static_assert(DescribeType<AccessorOnly>().kind == TypeKind::Opaque);
    static_assert(DescribeType<AccessorOnly>().objectMeta == nullptr);

    // A signature type
    static_assert(DescribeSignatureType<void>().kind == TypeKind::Void);
    static_assert(DescribeSignatureType<const String&>().kind == TypeKind::String);
    static_assert(DescribeSignatureType<Vector<int32_t>&>().kind == TypeKind::List);
    static_assert(DescribeSignatureType<int32_t*>().kind == TypeKind::Unknown);
    static_assert(DescribeSignatureType<IDeserializer&>().kind == TypeKind::Unknown);

    namespace {

        /// TypeDesc covers exactly the types Ser::DataAccessor does
        template <typename... Ts>
        constexpr bool AgreeWithAccessor() {
            return ((Describable<Ts> == Ser::HasDataAccessor<Ts>) && ...);
        }

        static_assert(AgreeWithAccessor<bool, int8_t, uint64_t, float, double, String, FixedString<8>, FixedString<8, false>, PoolString, RegMode, RegInner, Vec2, AccessorOnly, Vector<int32_t>,
                                        SmallVector<int32_t, 2>, FixedVector<int32_t, 2>, Array<int32_t, 2>, HashMap<String, int32_t>, HashMap<int32_t, String>, FixedHashMap<String, int32_t, 4>,
                                        Map<String, int32_t>, Optional<int32_t>, Pair<int32_t, String>, Variant<int32_t, float>, std::monostate, IntrusivePtrNonAtomic<RegNode>,
                                        IntrusivePtrAtomic<RegNode>, Vector<IntrusivePtrNonAtomic<RegNode>>>());

        // The aliases ReflectionAliasesTest lists as having no accessor
        static_assert(AgreeWithAccessor<FlatMap<String, int32_t>, FlatSet<int32_t>, Span<int32_t>, UniquePtr<int32_t>, HashSet<int32_t>, Set<int32_t>, Tuple<int32_t, float>, int32_t*, void*>());
        static_assert(!Describable<FlatMap<String, int32_t>>);
        static_assert(!Describable<int32_t*>);

        template <typename A, typename B>
        bool SameAs() {
            return SameType(DescribeType<A>(), DescribeType<B>());
        }

        uint64_t HashOf(StringView name) {
            return StringUtils::GetHash(name);
        }

    }  // namespace

    TEST(TypeDescTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(TypeDescTest, ObjectIsNamedByItsClass) {
        const TypeDesc& desc = DescribeType<RegInner>();
        EXPECT_EQ(desc.ObjectName(), "RegInner");
        EXPECT_EQ(desc.ObjectHash(), HashOf("RegInner"));
    }

    TEST(TypeDescTest, PointerIsNamedByItsPointee) {
        const TypeDesc& desc = DescribeType<IntrusivePtrNonAtomic<RegNode>>();
        EXPECT_EQ(desc.ObjectName(), "RegNode");
        EXPECT_EQ(desc.ObjectHash(), HashOf("RegNode"));
        EXPECT_EQ(DescribeType<IntrusivePtrAtomic<RegNode>>().ObjectHash(), desc.ObjectHash());
    }

    TEST(TypeDescTest, OpaqueAndScalarsHaveNoClass) {
        EXPECT_EQ(DescribeType<Vec2>().ObjectName(), "");
        EXPECT_EQ(DescribeType<int32_t>().ObjectHash(), 0U);
    }

    TEST(TypeDescTest, SameTypeComparesStructure) {
        EXPECT_TRUE((SameAs<Vector<int32_t>, Vector<int32_t>>()));
        EXPECT_FALSE((SameAs<Vector<int32_t>, Vector<float>>()));
        EXPECT_TRUE((SameAs<Vector<int32_t>, SmallVector<int32_t, 2>>())) << "a SmallVector grows like a Vector: the schema does not tell them apart";
        EXPECT_FALSE((SameAs<Vector<int32_t>, FixedVector<int32_t, 2>>()));
        EXPECT_FALSE((SameAs<Array<int32_t, 2>, Array<int32_t, 3>>()));
        EXPECT_TRUE((SameAs<Map<String, Vector<int32_t>>, Map<String, Vector<int32_t>>>()));
        EXPECT_FALSE((SameAs<Map<String, Vector<int32_t>>, Map<String, Vector<float>>>()));
        EXPECT_FALSE((SameAs<Map<String, int32_t>, Map<int32_t, int32_t>>()));
        EXPECT_FALSE((SameAs<Variant<int32_t, float>, Variant<float, int32_t>>()));
        EXPECT_FALSE((SameAs<RegInner, RegNode>()));
        EXPECT_TRUE((SameAs<IntrusivePtrNonAtomic<RegNode>, IntrusivePtrAtomic<RegNode>>()));
    }

    TEST(TypeDescTest, OnlyAPoolStringInterns) {
        static_assert(DescribeType<PoolString>().interns);
        static_assert(DescribeType<Vector<PoolString>>().element->interns);
        static_assert(DescribeType<Map<int32_t, PoolString>>().element->interns);
        static_assert(!DescribeType<Vector<PoolString>>().interns);
        static_assert(!DescribeType<String>().interns);
        static_assert(!DescribeType<FixedString<8>>().interns);
        static_assert(!DescribeType<FixedString<8, false>>().interns);
        static_assert(!DescribeType<int32_t>().interns);
        EXPECT_FALSE((SameAs<PoolString, String>())) << "a field that interns is not one that does not";
        EXPECT_FALSE((SameAs<Vector<PoolString>, Vector<String>>()));
        EXPECT_TRUE((SameAs<Vector<PoolString>, Vector<PoolString>>()));
    }

    TEST(TypeDescTest, SameTypeDoesNotCompareAddresses) {
        // A copy lives at another address, as a second shared library's inline variable would
        using Mixed = Variant<int32_t, Vector<String>>;
        const TypeDesc copy = DescribeType<Mixed>();
        EXPECT_NE(&copy, &DescribeType<Mixed>());
        EXPECT_TRUE(SameType(copy, DescribeType<Mixed>()));
    }

    TEST(TypeDescTest, KindNamesAreTheSchemaWords) {
        EXPECT_EQ(TypeKindName(TypeKind::Int32), "int32");
        EXPECT_EQ(TypeKindName(TypeKind::UInt64), "uint64");
        EXPECT_EQ(TypeKindName(TypeKind::String), "string");
        EXPECT_EQ(TypeKindName(TypeKind::List), "list");
        EXPECT_EQ(TypeKindName(TypeKind::Map), "map");
        for (const TypeKind first : EnumUtils<TypeKind>::Values()) {
            EXPECT_FALSE(TypeKindName(first).empty());
            for (const TypeKind second : EnumUtils<TypeKind>::Values()) {
                if (first != second) {
                    EXPECT_NE(TypeKindName(first), TypeKindName(second));
                }
            }
        }
    }

    TEST(TypeDescTest, NestedContainersAreDescribedToTheLeaf) {
        using Nested = HashMap<String, Vector<Optional<RegMode>>>;
        const TypeDesc& desc = DescribeType<Nested>();
        ASSERT_NE(desc.element, nullptr);
        ASSERT_NE(desc.element->element, nullptr);
        ASSERT_NE(desc.element->element->element, nullptr);
        EXPECT_EQ(desc.element->kind, TypeKind::List);
        EXPECT_EQ(desc.element->element->kind, TypeKind::Optional);
        EXPECT_EQ(desc.element->element->element->kind, TypeKind::Enum);
    }

}  // namespace BECore::Tests
