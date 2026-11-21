#include "SchemaHashTest.h"

#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <Generated/SchemaHashTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace HashTestData;

    namespace {

        /// The hash of a type of the program; the types of the header register themselves
        uint64_t HashOf(StringView name) {
            const Optional<uint64_t> hash = ComputeLayoutHash(name);
            EXPECT_TRUE(hash.has_value()) << String(name).c_str();
            return hash.value_or(0);
        }

        void ExpectSame(StringView a, StringView b) {
            EXPECT_EQ(HashOf(a), HashOf(b)) << String(a).c_str() << " and " << String(b).c_str();
        }

        void ExpectMoved(StringView a, StringView b) {
            EXPECT_NE(HashOf(a), HashOf(b)) << String(a).c_str() << " and " << String(b).c_str();
        }

        /// A registry of its own with the shape list, the base and the given derived types: the hash of the list is what a pointer field makes of them
        struct ShapeRegistry {
            TypeRegistry registry;

            const TypeInfo& Add(const TypeInfo& info) {
                registry.Register(info);
                return info;
            }

            uint64_t HashOfList() const {
                const Optional<uint64_t> hash = ComputeLayoutHash(registry, "HshShapeList");
                EXPECT_TRUE(hash.has_value());
                return hash.value_or(0);
            }
        };

        uint64_t ShapesHash(bool withCircle, bool withSquare) {
            ShapeRegistry shapes;
            shapes.Add(MakeTypeInfo<HshShapeList>());
            shapes.Add(MakeTypeInfo<HshShape>());
            if (withCircle) {
                shapes.Add(MakeTypeInfo<HshCircle>());
            }
            if (withSquare) {
                shapes.Add(MakeTypeInfo<HshSquare>());
            }
            return shapes.HashOfList();
        }

    }  // namespace

    TEST(SchemaHash, IsTheSameOnEveryCallAndForAnUnknownTypeNone) {
        EXPECT_EQ(HashOf("HshBase"), HashOf("HshBase"));
        EXPECT_EQ(ComputeLayoutHash(MakeTypeInfo<HshBase>()), HashOf("HshBase"));
        EXPECT_FALSE(ComputeLayoutHash("HshNoSuchType").has_value());
    }

    // A literal on purpose: the hash is written into files, so it must not move with the compiler, the platform or a refactoring of
    // the walk. A new value here is a change of the format of every cooked file.
    TEST(SchemaHash, KeepsItsValueOfAFixedLayout) {
        EXPECT_EQ(HashOf("HshBase"), 0xad02605852cbeaa2ULL);
    }

    TEST(SchemaHash, SameLayoutUnderAnotherTypeNameIsTheSameHash) {
        ExpectSame("HshBase", "HshTwin");
    }

    TEST(SchemaHash, AttributesAreNotInTheHash) {
        ExpectSame("HshBase", "HshAttributed");
    }

    TEST(SchemaHash, MovesWithTheOwnFields) {
        ExpectMoved("HshBase", "HshFieldRenamed");
        ExpectMoved("HshBase", "HshSwapped");
        ExpectMoved("HshBase", "HshWiderId");
        ExpectMoved("HshBase", "HshFieldAdded");
    }

    // Decided: a PoolString is an index into the string table in the file and a String is inline text, so they are two layouts
    TEST(SchemaHash, PoolStringAndStringFieldsDiffer) {
        ExpectMoved("HshBase", "HshPooled");
    }

    TEST(SchemaHash, MovesWithTheContainerShape) {
        ExpectMoved("HshVectorField", "HshArrayField");
        ExpectMoved("HshVectorField", "HshFixedThree");
        ExpectMoved("HshFixedThree", "HshFixedFour");
        ExpectMoved("HshVectorField", "HshInt64Vector");
        ExpectMoved("HshMapField", "HshMapOfInt64");
        ExpectMoved("HshVectorField", "HshOptionalField");
        ExpectMoved("HshVariantField", "HshVariantOther");
    }

    TEST(SchemaHash, MovesWithTheEnumNamesAndWidth) {
        ExpectMoved("HshModeField", "HshModeRenamedField");
        ExpectMoved("HshModeField", "HshModeAddedField");
        ExpectMoved("HshModeField", "HshModeWideField");
    }

    TEST(SchemaHash, ValueFieldsMoveWithTheirElement) {
        ExpectMoved("HshValueInt", "HshValueFloat");
    }

    // The price of a hash that does not depend on the compiler's spelling of a type: an opaque field is its kind only
    TEST(SchemaHash, OpaqueTypesOfOneFieldNameAreNotTold) {
        ExpectSame("HshVec2Field", "HshVec3Field");
    }

    TEST(SchemaHash, MovesWithANestedObject) {
        ExpectMoved("HshHolderInner", "HshHolderInnerWider");
        ExpectMoved("HshHolderInner", "HshHolderInners");
    }

    TEST(SchemaHash, MovesWithAFieldOfAParentAndInheritsItsFields) {
        ExpectMoved("HshChild", "HshChildOfWider");
        ExpectSame("HshChild", "HshFlatChild");
    }

    TEST(SchemaHash, RecursiveTypeTerminates) {
        EXPECT_EQ(HashOf("HshTree"), HashOf("HshTree"));
        EXPECT_NE(HashOf("HshTree"), HashOf("HshBase"));
    }

    TEST(SchemaHash, PointerFieldMovesWithTheTypesDerivedFromItsPointee) {
        const uint64_t none = ShapesHash(false, false);
        const uint64_t circle = ShapesHash(true, false);
        const uint64_t both = ShapesHash(true, true);
        EXPECT_NE(none, circle);
        EXPECT_NE(circle, both);
        EXPECT_NE(none, both);
    }

    TEST(SchemaHash, PointerFieldMovesWithAFieldOfADerivedType) {
        ShapeRegistry wider;
        wider.Add(MakeTypeInfo<HshShapeList>());
        wider.Add(MakeTypeInfo<HshShape>());
        // A type of the name `HshCircle` with the fields of `HshCircleWider`
        TypeInfo circle = MakeTypeInfo<HshCircle>();
        circle.fields = MakeTypeInfo<HshCircleWider>().fields;
        wider.Add(circle);
        EXPECT_NE(wider.HashOfList(), ShapesHash(true, false));
    }

    TEST(SchemaHash, PointerFieldMovesWithAFieldOfTheFactoryBase) {
        ShapeRegistry wider;
        wider.Add(MakeTypeInfo<HshShapeList>());
        // A type of the name `HshShape` with one more field
        TypeInfo shape = MakeTypeInfo<HshShape>();
        shape.fields = MakeTypeInfo<HshParentWider>().fields;
        wider.Add(shape);
        wider.Add(MakeTypeInfo<HshCircle>());
        EXPECT_NE(wider.HashOfList(), ShapesHash(true, false));
    }

    TEST(SchemaHash, DerivedTypesAreCountedInTheirNameOrder) {
        // Registration order is not the layout: the hash does not depend on it
        ShapeRegistry squareFirst;
        squareFirst.Add(MakeTypeInfo<HshShapeList>());
        squareFirst.Add(MakeTypeInfo<HshShape>());
        squareFirst.Add(MakeTypeInfo<HshSquare>());
        squareFirst.Add(MakeTypeInfo<HshCircle>());
        EXPECT_EQ(squareFirst.HashOfList(), ShapesHash(true, true));
    }

    TEST(SchemaHash, UnregisteredNestedTypeIsItsNameAndDoesNotCrash) {
        TypeRegistry registry;
        registry.Register(MakeTypeInfo<HshHolderInner>());
        const Optional<uint64_t> hash = ComputeLayoutHash(registry, "HshHolderInner");
        ASSERT_TRUE(hash.has_value());
        TypeRegistry other;
        other.Register(MakeTypeInfo<HshHolderInner>());
        other.Register(MakeTypeInfo<HshInner>());
        EXPECT_NE(hash, ComputeLayoutHash(other, "HshHolderInner"));
    }

}  // namespace BECore::Tests
