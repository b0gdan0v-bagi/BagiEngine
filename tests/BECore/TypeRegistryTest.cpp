#include "TypeRegistryTest.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Widgets/IWidget.h>
#include <Common/Printers.h>
#include <Generated/TypeRegistryTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::RegistryTestData {

    bool RegMethods::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    RegNode* RegMethods::Find(int32_t /*id*/) const {
        return nullptr;
    }

}  // namespace BECore::RegistryTestData

namespace BECore::Tests {

    using namespace RegistryTestData;

    namespace {

        /// A registry of its own with every test type, the parents named by hand
        class TypeRegistryTest : public ::testing::Test {
        protected:
            void SetUp() override {
                registry.Register(MakeTypeInfo<RegInner, void>());
                registry.Register(MakeTypeInfo<RegNode, void>());
                registry.Register(MakeTypeInfo<RegAllKinds, void>());
                registry.Register(MakeTypeInfo<RegBase, void>());
                registry.Register(MakeTypeInfo<RegDerived, RegBase>());
                registry.Register(MakeTypeInfo<RegShifted, RegBase>());
                registry.Register(MakeTypeInfo<RegAttributed, void>());
                registry.Register(MakeTypeInfo<RegMethods, void>());
                registry.Register(MakeTypeInfo<RegEvent, void>());
            }

            const TypeInfo& Get(StringView name) const {
                const TypeInfo* info = registry.Find(name);
                EXPECT_NE(info, nullptr) << String(name).c_str();
                return info != nullptr ? *info : MakeTypeInfo<RegInner, void>();
            }

            TypeRegistry registry;
        };

        const FieldDesc* FindField(const TypeInfo& info, StringView name) {
            for (const FieldDesc& field : info.fields) {
                if (field.name == name) {
                    return &field;
                }
            }
            return nullptr;
        }

        const MethodDesc* FindMethod(const TypeInfo& info, StringView name) {
            for (const MethodDesc& method : info.methods) {
                if (method.name == name) {
                    return &method;
                }
            }
            return nullptr;
        }

        uint64_t HashOf(StringView name) {
            return StringUtils::GetHash(name);
        }

        template <typename Member>
        bool FieldIs(const TypeInfo& info, StringView name) {
            const FieldDesc* field = FindField(info, name);
            return field != nullptr && SameType(*field->type, DescribeType<Member>());
        }

        /// The offset of every own field agrees with the typed address thunk, on a live object
        void ExpectOffsetsMatchThunks(const TypeInfo& info, void* object) {
            for (const FieldDesc& field : info.fields) {
                const auto* address = static_cast<const char*>(field.address(object));
                EXPECT_EQ(static_cast<size_t>(address - static_cast<const char*>(object)), field.offset) << String(info.Name()).c_str() << "." << String(field.cppName).c_str();
            }
        }

    }  // namespace

    TEST_F(TypeRegistryTest, FindsTypesByNameAndHash) {
        const TypeInfo& byName = Get("RegAllKinds");
        EXPECT_EQ(byName.Name(), "RegAllKinds");
        EXPECT_EQ(byName.Hash(), HashOf("RegAllKinds"));
        EXPECT_EQ(registry.Find(HashOf("RegAllKinds")), &byName);
        EXPECT_EQ(registry.All().size(), 9U);
        EXPECT_EQ(byName.meta.typeName, RegAllKinds::GetStaticTypeName());
    }

    TEST_F(TypeRegistryTest, UnknownNameIsNullAndInternsNothing) {
        constexpr StringView kNeverSeen = "registry_unknown_type_8c41d7";
        EXPECT_EQ(registry.Find(kNeverSeen), nullptr);
        EXPECT_EQ(registry.Find(HashOf(kNeverSeen)), nullptr);
        EXPECT_FALSE(PoolString::Find(kNeverSeen).has_value());
        EXPECT_EQ(registry.Find(""), nullptr);
    }

    TEST_F(TypeRegistryTest, SecondRegistrationOfTheSameTypeIsIgnored) {
        const size_t before = registry.All().size();
        EXPECT_FALSE(registry.Register(MakeTypeInfo<RegInner, void>()));
        EXPECT_EQ(registry.All().size(), before);
    }

    TEST_F(TypeRegistryTest, DifferentTypeWithTheSameHashIsRefused) {
        TypeInfo clash;
        clash.meta = ClassMeta{HashOf("RegInner"), "OtherName"};
        const size_t before = registry.All().size();
        EXPECT_FALSE(registry.Register(clash));
        EXPECT_EQ(registry.All().size(), before);
        EXPECT_EQ(registry.Find("RegInner")->Name(), "RegInner");
        EXPECT_EQ(registry.Find("OtherName"), nullptr);
        EXPECT_EQ(registry.RefusedCount(), 1U);
    }

    TEST_F(TypeRegistryTest, FieldsHaveNamesAndDescriptors) {
        const TypeInfo& info = Get("RegAllKinds");
        EXPECT_EQ(info.fields.size(), RegAllKinds::GetStaticFieldCount());
        EXPECT_TRUE((FieldIs<bool>(info, "flag")));
        EXPECT_TRUE((FieldIs<int8_t>(info, "tiny")));
        EXPECT_TRUE((FieldIs<uint64_t>(info, "ubig")));
        EXPECT_TRUE((FieldIs<double>(info, "precise")));
        EXPECT_TRUE((FieldIs<String>(info, "text")));
        EXPECT_TRUE((FieldIs<FixedString<8, false>>(info, "fixedText")));
        EXPECT_TRUE((FieldIs<PoolString>(info, "name")));
        EXPECT_TRUE((FieldIs<RegMode>(info, "mode")));
        EXPECT_TRUE((FieldIs<RegInner>(info, "inner")));
        EXPECT_TRUE((FieldIs<Vector<int32_t>>(info, "items")));
        EXPECT_TRUE((FieldIs<SmallVector<int32_t, 4>>(info, "smallItems")));
        EXPECT_TRUE((FieldIs<FixedVector<int32_t, 3>>(info, "fixedItems")));
        EXPECT_TRUE((FieldIs<Array<int32_t, 2>>(info, "pairOfInts")));
        EXPECT_TRUE((FieldIs<HashMap<String, int32_t>>(info, "table")));
        EXPECT_TRUE((FieldIs<FixedHashMap<String, int32_t, 4, 5, false>>(info, "fixedTable")));
        EXPECT_TRUE((FieldIs<Map<int32_t, String>>(info, "ordered")));
        EXPECT_TRUE((FieldIs<Optional<int32_t>>(info, "maybe")));
        EXPECT_TRUE((FieldIs<Pair<int32_t, String>>(info, "entry")));
        EXPECT_TRUE((FieldIs<Variant<int32_t, String>>(info, "choice")));
        EXPECT_TRUE((FieldIs<IntrusivePtrNonAtomic<RegNode>>(info, "node")));
        EXPECT_TRUE((FieldIs<Vector<RegInner>>(info, "inners")));
        EXPECT_FALSE((FieldIs<float>(info, "count")));
    }

    TEST_F(TypeRegistryTest, FieldNameIsTheSerializedOneAndKeepsTheCppName) {
        const FieldDesc* field = FindField(Get("RegAttributed"), "opacity");
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->cppName, "_opacity");
        const FieldDesc* plain = FindField(Get("RegAttributed"), "plain");
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(plain->cppName, "plain");
    }

    TEST_F(TypeRegistryTest, FieldAttributesAreCarried) {
        const TypeInfo& info = Get("RegAttributed");
        const FieldDesc* opacity = FindField(info, "opacity");
        ASSERT_NE(opacity, nullptr);
        EXPECT_TRUE(opacity->attributes.hasRange);
        EXPECT_DOUBLE_EQ(opacity->attributes.rangeMin, 0.0);
        EXPECT_DOUBLE_EQ(opacity->attributes.rangeMax, 1.0);
        EXPECT_DOUBLE_EQ(opacity->attributes.step, 0.05);
        EXPECT_EQ(opacity->attributes.tooltip, "Share of the screen");
        const FieldDesc* flags = FindField(info, "flags");
        ASSERT_NE(flags, nullptr);
        EXPECT_TRUE(flags->attributes.readOnly);
        EXPECT_TRUE(flags->attributes.hidden);
        EXPECT_TRUE(flags->attributes.required);
        const FieldDesc* plain = FindField(info, "plain");
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(plain->attributes, FieldAttributes{});
    }

    TEST_F(TypeRegistryTest, MethodsAreListedWithCallableDecidedByTheSignature) {
        const TypeInfo& info = Get("RegMethods");
        ASSERT_EQ(info.methods.size(), 6U);

        const MethodDesc* add = FindMethod(info, "Add");
        ASSERT_NE(add, nullptr);
        EXPECT_TRUE(add->callable);
        EXPECT_NE(add->invoke, nullptr);
        EXPECT_FALSE(add->isConst);
        ASSERT_EQ(add->params.size(), 2U);
        EXPECT_EQ(add->params[0].type->kind, TypeKind::Int32);
        EXPECT_EQ(add->params[1].type->kind, TypeKind::String);
        EXPECT_EQ(add->result->kind, TypeKind::Int32);

        const MethodDesc* reset = FindMethod(info, "Reset");
        ASSERT_NE(reset, nullptr);
        EXPECT_TRUE(reset->callable);
        EXPECT_EQ(reset->params.size(), 0U);
        EXPECT_EQ(reset->result->kind, TypeKind::Void);

        const MethodDesc* isEmpty = FindMethod(info, "IsEmpty");
        ASSERT_NE(isEmpty, nullptr);
        EXPECT_TRUE(isEmpty->callable);
        EXPECT_TRUE(isEmpty->isConst);
        EXPECT_EQ(isEmpty->result->kind, TypeKind::Bool);
    }

    TEST_F(TypeRegistryTest, MethodThatCannotBeCalledFromOutsideIsListedNotCallable) {
        const TypeInfo& info = Get("RegMethods");

        const MethodDesc* initialize = FindMethod(info, "Initialize");
        ASSERT_NE(initialize, nullptr);
        EXPECT_FALSE(initialize->callable);
        EXPECT_EQ(initialize->invoke, nullptr);
        ASSERT_EQ(initialize->params.size(), 1U);
        EXPECT_EQ(initialize->params[0].type->kind, TypeKind::Unknown);
        EXPECT_EQ(initialize->result->kind, TypeKind::Bool);

        const MethodDesc* fill = FindMethod(info, "Fill");
        ASSERT_NE(fill, nullptr);
        EXPECT_FALSE(fill->callable) << "a non-const reference cannot carry its change back";
        EXPECT_EQ(fill->invoke, nullptr);
        EXPECT_EQ(fill->params[0].type->kind, TypeKind::List);

        const MethodDesc* find = FindMethod(info, "Find");
        ASSERT_NE(find, nullptr);
        EXPECT_FALSE(find->callable) << "a raw pointer cannot be written";
        EXPECT_EQ(find->invoke, nullptr);
        EXPECT_EQ(find->result->kind, TypeKind::Unknown);
        EXPECT_TRUE(find->isConst);
    }

    TEST(TypeRegistryParamNamesTest, NamesAreSplitAtCommasAndMissingOnesAreArgN) {
        EXPECT_EQ(Detail::ParamNameAt("amount,label", 0), "amount");
        EXPECT_EQ(Detail::ParamNameAt("amount,label", 1), "label");
        EXPECT_EQ(Detail::ParamNameAt("amount,label", 2), "");
        EXPECT_EQ(Detail::ParamNameAt("", 0), "");
        EXPECT_EQ(Detail::ParamNameAt("a,,c", 1), "");
        EXPECT_EQ(Detail::ParamName<0>("amount,label"), "amount");
        EXPECT_EQ(Detail::ParamName<1>(""), "arg1");
        EXPECT_EQ(Detail::ParamName<0>("a,,c"), "a");
        EXPECT_EQ(Detail::ParamName<1>("a,,c"), "arg1");
        EXPECT_EQ(Detail::ParamName<12>(""), "arg12");
        EXPECT_EQ(Detail::ParamName<7>("x"), "arg7");
    }

    TEST_F(TypeRegistryTest, ParametersCarryTheNamesOfTheDeclaration) {
        const MethodDesc* add = FindMethod(Get("RegMethods"), "Add");
        ASSERT_NE(add, nullptr);
        ASSERT_EQ(add->params.size(), 2U);
        EXPECT_EQ(add->params[0].name, "amount");
        EXPECT_EQ(add->params[1].name, "label");
    }

    TEST_F(TypeRegistryTest, ParentChainAndDerivedLists) {
        const TypeInfo& base = Get("RegBase");
        const TypeInfo& derived = Get("RegDerived");
        const TypeInfo& shifted = Get("RegShifted");

        EXPECT_EQ(registry.Parent(derived), &base);
        EXPECT_EQ(registry.Parent(base), nullptr);
        EXPECT_EQ(registry.Bases(derived).size(), 1U);
        EXPECT_EQ(registry.Bases(derived)[0], &base);
        EXPECT_TRUE(registry.Bases(base).empty());

        const Vector<const TypeInfo*>& direct = registry.Derived(base, false);
        ASSERT_EQ(direct.size(), 2U);
        EXPECT_EQ(direct[0], &derived);
        EXPECT_EQ(direct[1], &shifted);
        EXPECT_EQ(registry.Derived(base, true).size(), 2U);
        EXPECT_TRUE(registry.Derived(derived, true).empty());
        EXPECT_TRUE(registry.Derived(Get("RegInner"), true).empty());

        EXPECT_TRUE(registry.IsDerivedFrom(derived, base));
        EXPECT_TRUE(registry.IsDerivedFrom(derived, derived));
        EXPECT_FALSE(registry.IsDerivedFrom(base, derived));
        EXPECT_FALSE(registry.IsDerivedFrom(derived, Get("RegInner")));
    }

    TEST_F(TypeRegistryTest, TypedRefOfADerivedObjectThroughItsBase) {
        RegDerived derived;
        RegBase& asBase = derived;
        const TypedRef ref = TypedRef::Of(asBase, registry);
        ASSERT_TRUE(ref);
        EXPECT_EQ(ref.type, &Get("RegDerived"));
        EXPECT_EQ(ref.Address(), static_cast<void*>(&derived));
        EXPECT_EQ(ref.ConstAddress(), static_cast<const void*>(&derived));
        EXPECT_FALSE(ref.isConst);
    }

    TEST_F(TypeRegistryTest, TypedRefMovesBackWhenTheBaseIsNotAtOffsetZero) {
        RegShifted shifted;
        RegBase& asBase = shifted;
        ASSERT_NE(static_cast<void*>(&asBase), static_cast<void*>(&shifted));
        const TypedRef ref = TypedRef::Of(asBase, registry);
        ASSERT_TRUE(ref);
        EXPECT_EQ(ref.type, &Get("RegShifted"));
        EXPECT_EQ(ref.Address(), static_cast<void*>(&shifted));

        const TypedRef base = ref.Base(Get("RegBase"), registry);
        ASSERT_TRUE(base);
        EXPECT_EQ(base.type, &Get("RegBase"));
        EXPECT_EQ(base.Address(), static_cast<void*>(&asBase));
        EXPECT_EQ(ref.Base(Get("RegShifted"), registry).Address(), ref.Address());
        EXPECT_FALSE(ref.Base(Get("RegInner"), registry));
        EXPECT_FALSE(base.Base(Get("RegShifted"), registry));
    }

    TEST_F(TypeRegistryTest, TypedRefOfAConstObjectIsConst) {
        const RegDerived derived;
        const RegBase& asBase = derived;
        const TypedRef ref = TypedRef::Of(asBase, registry);
        ASSERT_TRUE(ref);
        EXPECT_TRUE(ref.isConst);
        EXPECT_EQ(ref.type, &Get("RegDerived"));
        EXPECT_TRUE(ref.Base(Get("RegBase"), registry).isConst);
    }

    TEST_F(TypeRegistryTest, TypedRefOfATypeWithoutAVtableIsItsStaticType) {
        RegInner inner;
        const TypedRef ref = TypedRef::Of(inner, registry);
        ASSERT_TRUE(ref);
        EXPECT_EQ(ref.type, &Get("RegInner"));
        EXPECT_EQ(ref.Address(), static_cast<void*>(&inner));
    }

    TEST_F(TypeRegistryTest, TypedRefOfAnUnregisteredTypeIsEmpty) {
        const TypeRegistry empty;
        RegDerived derived;
        EXPECT_FALSE(TypedRef::Of(derived, empty));
        EXPECT_FALSE(TypedRef{});

        // The static type is known and the object's own type is not
        TypeRegistry onlyBase;
        onlyBase.Register(MakeTypeInfo<RegBase, void>());
        RegBase& asBase = derived;
        EXPECT_FALSE(TypedRef::Of(asBase, onlyBase));
        RegBase plain;
        EXPECT_TRUE(TypedRef::Of(plain, onlyBase));
    }

    TEST_F(TypeRegistryTest, DerivedListsFollowALaterRegistration) {
        TypeRegistry late;
        late.Register(MakeTypeInfo<RegBase, void>());
        EXPECT_TRUE(late.Derived(MakeTypeInfo<RegBase, void>(), true).empty());
        late.Register(MakeTypeInfo<RegDerived, RegBase>());
        EXPECT_EQ(late.Derived(MakeTypeInfo<RegBase, void>(), true).size(), 1U);
    }

    TEST_F(TypeRegistryTest, AllFieldsWalkTheChainRootFirst) {
        const TypeInfo& derived = Get("RegDerived");
        const Vector<FieldRef> fields = registry.AllFields(derived);
        ASSERT_EQ(fields.size(), 3U);
        EXPECT_EQ(fields[0].field->name, "baseValue");
        EXPECT_EQ(fields[0].owner, &Get("RegBase"));
        EXPECT_EQ(fields[1].field->name, "ownValue");
        EXPECT_EQ(fields[1].owner, &derived);
        EXPECT_EQ(fields[2].field->name, "nested");
        EXPECT_TRUE((FieldIs<RegInner>(derived, "nested")));
    }

    TEST_F(TypeRegistryTest, OffsetsAgreeWithTheTypedAddressThunks) {
        RegAllKinds all;
        ExpectOffsetsMatchThunks(Get("RegAllKinds"), &all);
        RegAttributed attributed;
        ExpectOffsetsMatchThunks(Get("RegAttributed"), &attributed);
        RegMethods methods;
        ExpectOffsetsMatchThunks(Get("RegMethods"), &methods);
        RegEvent event;
        ExpectOffsetsMatchThunks(Get("RegEvent"), &event);
    }

    TEST_F(TypeRegistryTest, OffsetsOfInheritedFieldsAgreeWithTheThunksOnTheWholeObject) {
        RegDerived derived;
        RegShifted shifted;
        EXPECT_EQ(Get("RegDerived").parentOffset, static_cast<size_t>(reinterpret_cast<char*>(static_cast<RegBase*>(&derived)) - reinterpret_cast<char*>(&derived)));
        EXPECT_EQ(Get("RegShifted").parentOffset, static_cast<size_t>(reinterpret_cast<char*>(static_cast<RegBase*>(&shifted)) - reinterpret_cast<char*>(&shifted)));
        EXPECT_GT(Get("RegShifted").parentOffset, 0U) << "another base comes first, so the base subobject is not at the start";

        for (const FieldRef& ref : registry.AllFields(Get("RegDerived"))) {
            void* owner = ref.owner == &Get("RegBase") ? static_cast<void*>(static_cast<RegBase*>(&derived)) : static_cast<void*>(&derived);
            const auto* address = static_cast<const char*>(ref.field->address(owner));
            EXPECT_EQ(static_cast<size_t>(address - reinterpret_cast<const char*>(&derived)), ref.offset) << String(ref.field->cppName).c_str();
        }
        for (const FieldRef& ref : registry.AllFields(Get("RegShifted"))) {
            void* owner = ref.owner == &Get("RegBase") ? static_cast<void*>(static_cast<RegBase*>(&shifted)) : static_cast<void*>(&shifted);
            const auto* address = static_cast<const char*>(ref.field->address(owner));
            EXPECT_EQ(static_cast<size_t>(address - reinterpret_cast<const char*>(&shifted)), ref.offset) << String(ref.field->cppName).c_str();
        }
    }

    TEST_F(TypeRegistryTest, ThunksWorkOnAnErasedPointer) {
        const TypeInfo& info = Get("RegInner");
        RegInner inner;
        inner.value = 17;
        inner.label = "hello";

        XmlSerializer direct;
        ASSERT_TRUE(direct.BeginObject("RegInner"));
        inner.Serialize(direct);
        direct.EndObject();

        XmlSerializer erased;
        ASSERT_NE(info.serialize, nullptr);
        ASSERT_TRUE(erased.BeginObject("RegInner"));
        info.serialize(&inner, erased);
        erased.EndObject();
        EXPECT_EQ(erased.SaveToString(), direct.SaveToString());

        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString("<root><RegInner value=\"5\" label=\"loaded\"/></root>"));
        RegInner loaded;
        ASSERT_NE(info.deserialize, nullptr);
        ASSERT_TRUE(reader.BeginObject("RegInner"));
        info.deserialize(&loaded, reader);
        reader.EndObject();
        EXPECT_EQ(loaded.value, 5);
        EXPECT_EQ(loaded.label, "loaded");
    }

    TEST_F(TypeRegistryTest, AnEventHasNoVisitorAndAClassHasOne) {
        EXPECT_EQ(Get("RegEvent").acceptVisitor, nullptr);
        EXPECT_NE(Get("RegEvent").serialize, nullptr);
        EXPECT_NE(Get("RegAllKinds").acceptVisitor, nullptr);
    }

    TEST_F(TypeRegistryTest, NoFactoryMeansNoCreator) {
        EXPECT_EQ(Get("RegDerived").create, nullptr);
        EXPECT_EQ(Get("RegDerived").factoryBase, 0U);
    }

    TEST(TypeRegistryCreatorTest, CreatorGivesAnOwnedObjectOfTheMadeType) {
        const TypeInfo& info = MakeTypeInfo<RegDerived, RegBase, RegBase>();
        ASSERT_NE(info.create, nullptr);
        EXPECT_EQ(info.factoryBase, HashOf("RegBase"));
        EXPECT_FALSE(info.isAbstract);

        OwnedObject object = info.create();
        ASSERT_TRUE(object);
        auto* derived = static_cast<RegDerived*>(object.Get());
        EXPECT_EQ(derived->GetTypeMeta(), RegDerived::GetStaticTypeMeta());
        EXPECT_EQ(derived->GetRefCount(), 1);
        EXPECT_EQ(derived->ownValue, 2);

        OwnedObject moved = std::move(object);
        EXPECT_FALSE(object);
        EXPECT_TRUE(moved);
        moved.Reset();
        EXPECT_FALSE(moved);
    }

    // The registration block the generator writes into every .gen.hpp, seen through the process-wide registry
    TEST(TypeRegistryGeneratedTest, EveryReflectedTypeOfThisFileRegisteredItself) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        for (const StringView name : {"RegInner", "RegNode", "RegAllKinds", "RegBase", "RegDerived", "RegShifted", "RegAttributed", "RegMethods", "RegEvent"}) {
            const TypeInfo* info = registry.Find(name);
            ASSERT_NE(info, nullptr) << String(name).c_str();
            EXPECT_EQ(info->Hash(), HashOf(name));
            EXPECT_EQ(registry.Find(info->Hash()), info);
        }
    }

    TEST(TypeRegistryGeneratedTest, TheGeneratorNamesTheReflectedParent) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* derived = registry.Find("RegDerived");
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->parentHash, HashOf("RegBase"));
        EXPECT_EQ(registry.Parent(*derived), registry.Find("RegBase"));
        // The first base is not reflected, so the generator writes no parent (the chain holds one parent per type)
        const TypeInfo* shifted = registry.Find("RegShifted");
        ASSERT_NE(shifted, nullptr);
        EXPECT_EQ(shifted->parentHash, 0U);
    }

    TEST(TypeRegistryCreatorTest, CreateAsMovesToTheBaseSubobjectWhenAnotherBaseComesFirst) {
        TypeRegistry registry;
        registry.Register(MakeTypeInfo<RegBase, void, RegBase>());
        registry.Register(MakeTypeInfo<RegShifted, RegBase, RegBase>());
        registry.Register(MakeTypeInfo<RegDerived, RegBase, RegBase>());

        IntrusivePtr<RegBase> base = registry.CreateAs<RegBase>("RegShifted");
        ASSERT_TRUE(base);
        EXPECT_EQ(base->GetTypeMeta(), RegShifted::GetStaticTypeMeta());
        EXPECT_EQ(base->GetRefCount(), 1);
        // The object was made at the derived address, which is not the address of its RegBase part
        const auto* shifted = static_cast<const RegShifted*>(base.Get());
        EXPECT_EQ(shifted->shiftedValue, 3);
        EXPECT_NE(static_cast<const void*>(shifted), static_cast<const void*>(base.Get()));
        EXPECT_FALSE(registry.CreateAs<RegDerived>("RegShifted"));
    }

    // The engine's own hierarchy, through the process-wide registry the generated blocks fill

    namespace {

        bool Contains(const Vector<const TypeInfo*>& list, StringView name) {
            for (const TypeInfo* info : list) {
                if (info->Name() == name) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(TypeRegistryEngineTest, NoTwoRegisteredTypesShareAHash) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        EXPECT_EQ(registry.RefusedCount(), 0U);
        for (const TypeInfo* info : registry.All()) {
            EXPECT_EQ(registry.Find(info->Hash()), info) << String(info->Name()).c_str();
        }
    }

    TEST(TypeRegistryEngineTest, DerivedOfTheComponentBaseHoldsTheComponents) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* component = registry.Find("IComponent");
        ASSERT_NE(component, nullptr);
        const Vector<const TypeInfo*>& derived = registry.Derived(*component, true);
        EXPECT_TRUE(Contains(derived, "TransformComponent"));
        EXPECT_TRUE(Contains(derived, "SpriteRendererComponent"));
        EXPECT_FALSE(Contains(derived, "IComponent"));
        EXPECT_TRUE(Contains(registry.Derived(*component, false), "TransformComponent"));
    }

    TEST(TypeRegistryEngineTest, EveryTypeOfAnAbstractFactoryIsRegisteredWithTheSameHash) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const auto& registered = AbstractFactory<IComponent>::GetInstance().GetRegisteredTypes();
        ASSERT_FALSE(registered.empty());
        for (const ClassMeta& meta : registered) {
            const TypeInfo* info = registry.Find(meta.typeName);
            ASSERT_NE(info, nullptr) << String(meta.typeName).c_str();
            EXPECT_EQ(info->Hash(), meta.typeHash);
            EXPECT_EQ(info->factoryBase, IComponent::GetStaticTypeMeta().typeHash);
        }
    }

    TEST(TypeRegistryEngineTest, FactoryBasesListTheBasesOfTheAbstractFactories) {
        const Vector<const TypeInfo*>& bases = TypeRegistry::GetInstance().FactoryBases();
        EXPECT_TRUE(Contains(bases, "IComponent"));
        EXPECT_TRUE(Contains(bases, "SceneNode"));
        EXPECT_FALSE(Contains(bases, "TransformComponent"));
    }

    TEST(TypeRegistryEngineTest, BasesOfAComponentEndAtTheComponentBase) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* transform = registry.Find("TransformComponent");
        ASSERT_NE(transform, nullptr);
        const Vector<const TypeInfo*> bases = registry.Bases(*transform);
        ASSERT_FALSE(bases.empty());
        EXPECT_EQ(bases.back()->Name(), "IComponent");
        EXPECT_TRUE(registry.IsDerivedFrom(*transform, *bases.back()));
    }

    TEST(TypeRegistryEngineTest, CreateMakesTheTypeByName) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        OwnedObject object = registry.Create("TransformComponent");
        ASSERT_TRUE(object);
        const auto* transform = static_cast<const TransformComponent*>(object.Get());
        EXPECT_EQ(transform->GetTypeMeta(), TransformComponent::GetStaticTypeMeta());
    }

    TEST(TypeRegistryEngineTest, AnAbstractTypeAnUnknownNameAndATypeWithNoFactoryMakeNothing) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        EXPECT_FALSE(registry.Create("IComponent"));
        EXPECT_FALSE(registry.Create("NoSuchType"));
        EXPECT_FALSE(registry.Create(*registry.Find("RegInner")));
        EXPECT_FALSE(PoolString::Find("NoSuchType").has_value());
    }

    TEST(TypeRegistryEngineTest, CreateAsGivesTheBaseOnlyForADerivedType) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        IntrusivePtr<IComponent> component = registry.CreateAs<IComponent>("SpriteRendererComponent");
        ASSERT_TRUE(component);
        EXPECT_EQ(component->GetTypeMeta(), SpriteRendererComponent::GetStaticTypeMeta());
        EXPECT_EQ(component->GetRefCount(), 1);

        EXPECT_FALSE(registry.CreateAs<IComponent>("NoSuchType"));
        EXPECT_FALSE(registry.CreateAs<IComponent>("IComponent"));
        // Registered and creatable, but not a component
        EXPECT_FALSE(registry.CreateAs<IComponent>("SceneNode"));
        EXPECT_TRUE(registry.CreateAs<SceneNode>("SceneNode"));
    }

    namespace {

        /// The registry's offset of every creatable type under @p Base against the compiler's own conversion made by the factory
        template <typename Base>
        size_t CheckParentOffsetsAgainstTheCompiler() {
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            const TypeInfo* baseInfo = registry.Find(Base::GetStaticTypeMeta().typeHash);
            EXPECT_NE(baseInfo, nullptr);
            size_t checked = 0;
            for (const TypeInfo* info : registry.All()) {
                if (baseInfo == nullptr || info->create == nullptr || info->factoryBase != baseInfo->Hash()) {
                    continue;
                }
                const auto made = AbstractFactory<Base>::GetInstance().Create(info->Name());
                if (!made) {
                    continue;
                }
                // The most derived object's address is the compiler's, not the registry's
                char* const most = static_cast<char*>(dynamic_cast<void*>(made.Get()));
                const Optional<size_t> offset = registry.OffsetOfBase(*info, *baseInfo);
                EXPECT_TRUE(offset.has_value()) << String(info->Name()).c_str();
                if (offset.has_value()) {
                    EXPECT_EQ(most + *offset, reinterpret_cast<char*>(made.Get())) << String(info->Name()).c_str();
                    EXPECT_EQ(TypedRef::Of(*made).Address(), static_cast<void*>(most)) << String(info->Name()).c_str();
                }
                ++checked;
            }
            return checked;
        }

    }  // namespace

    TEST(TypeRegistryEngineTest, BaseOffsetsAgreeWithTheCompilersConversion) {
        EXPECT_GT(CheckParentOffsetsAgainstTheCompiler<IComponent>(), 5U);
        EXPECT_GT(CheckParentOffsetsAgainstTheCompiler<IWidget>(), 0U);
    }

    // The live check of the offset rule on the real classes (IWidget has two bases, the UI tree a vtable and nested structs)
    TEST(TypeRegistryEngineTest, OffsetsAgreeWithTheThunksOnEveryCreatableType) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        size_t checkedTypes = 0;
        size_t checkedFields = 0;
        for (const TypeInfo* info : registry.All()) {
            if (info->create == nullptr) {
                continue;
            }
            OwnedObject object = registry.Create(*info);
            ASSERT_TRUE(object) << String(info->Name()).c_str();
            char* const base = static_cast<char*>(object.Get());
            for (const FieldRef& ref : registry.AllFields(*info)) {
                // The object of the type that declares the field sits at the offset of the ancestor's subobject
                char* const owner = base + (ref.offset - ref.field->offset);
                EXPECT_EQ(static_cast<char*>(ref.field->address(owner)), base + ref.offset) << String(info->Name()).c_str() << "." << String(ref.field->cppName).c_str();
                ++checkedFields;
            }
            ++checkedTypes;
        }
        EXPECT_GT(checkedTypes, 20U);
        EXPECT_GT(checkedFields, 100U);
    }

    // The widget lifecycle is called by the loader and the manager through IWidget; a mark added again is a decision, not an accident
    TEST(TypeRegistryEngineTest, NoWidgetListsAMethod) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* widget = registry.Find(IWidget::GetStaticTypeMeta().typeHash);
        ASSERT_NE(widget, nullptr);
        EXPECT_TRUE(widget->methods.empty());
        for (const TypeInfo* info : registry.Derived(*widget, true)) {
            EXPECT_TRUE(info->methods.empty()) << String(info->Name()).c_str();
        }
    }

}  // namespace BECore::Tests
