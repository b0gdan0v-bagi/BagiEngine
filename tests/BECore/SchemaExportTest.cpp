#include "SchemaExportTest.h"

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Widgets/IWidget.h>
#include <Common/ControlTestSupport.h>
#include <Common/Require.h>
#include <Generated/SchemaExportTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::SchemaTestData {

    bool SchemaMethods::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    SchemaNode* SchemaMethods::Find(int32_t /*id*/) const {
        return nullptr;
    }

}  // namespace BECore::SchemaTestData

namespace BECore::Tests {

    using namespace SchemaTestData;

    namespace {

        template <typename T, typename Parent = void, typename FactoryBase = void>
        void Add(TypeRegistry& registry) {
            registry.Register(MakeTypeInfo<T, Parent, FactoryBase>());
        }

        /// Every test type, registered in the order the caller wants (@p reversed flips it)
        void Fill(TypeRegistry& registry, bool reversed) {
            using Step = void (*)(TypeRegistry&);
            const Step steps[] = {
                &Add<SchemaLeaf>,
                &Add<SchemaNode>,
                &Add<SchemaShape>,
                &Add<SchemaCircle, SchemaShape, SchemaShape>,
                &Add<SchemaDisc, SchemaCircle, SchemaShape>,
                &Add<SchemaSquare, SchemaShape, SchemaShape>,
                &Add<SchemaAllShapes>,
                &Add<SchemaAttributed>,
                &Add<SchemaValueFields>,
                &Add<SchemaMethods>,
            };
            const size_t count = sizeof(steps) / sizeof(steps[0]);
            for (size_t i = 0; i < count; ++i) {
                steps[reversed ? count - 1 - i : i](registry);
            }
        }

        const Json* Member(const Json& object, StringView key) {
            const Json* found = FindJsonMember(object, key);
            EXPECT_NE(found, nullptr) << String(key).c_str();
            return found;
        }

        /// The entry of the named type in a schema or a `list_types` array, or null
        const Json* EntryNamed(const Json& list, StringView name) {
            if (!list.is_array()) {
                return nullptr;
            }
            for (const Json& entry : list) {
                if (GetJsonString(entry, "name") == String(name)) {
                    return &entry;
                }
            }
            return nullptr;
        }

        const Json& FieldNamed(const Json& entry, StringView name) {
            static const Json kMissing;
            const Json* fields = FindJsonMember(entry, "fields");
            const Json* found = fields != nullptr ? EntryNamed(*fields, name) : nullptr;
            EXPECT_NE(found, nullptr) << String(name).c_str();
            return found != nullptr ? *found : kMissing;
        }

        /// Whether an object anywhere in @p json has a member of this name
        bool HasKeyNamed(const Json& json, StringView key) {
            if (json.is_object()) {
                if (FindJsonMember(json, key) != nullptr) {
                    return true;
                }
                for (const auto& member : json.items()) {
                    if (HasKeyNamed(member.value(), key)) {
                        return true;
                    }
                }
            } else if (json.is_array()) {
                for (const Json& element : json) {
                    if (HasKeyNamed(element, key)) {
                        return true;
                    }
                }
            }
            return false;
        }

        String KindOf(const Json& type) {
            return GetJsonString(type, "kind").value_or(String());
        }

        Json TypeOfField(const Json& entry, StringView field) {
            const Json* type = FindJsonMember(FieldNamed(entry, field), "type");
            return type != nullptr ? *type : Json();
        }

        class SchemaExportTest : public ::testing::Test {
        protected:
            void SetUp() override {
                Fill(registry, false);
                schema = ToJson(MakeSchemaAnswer(registry, roots));
            }

            const Json& Types() const {
                static const Json kNone;
                const Json* types = FindJsonMember(schema, "types");
                return types != nullptr ? *types : kNone;
            }

            const Json& Entry(StringView name) const {
                static const Json kNone;
                const Json* entry = EntryNamed(Types(), name);
                EXPECT_NE(entry, nullptr) << String(name).c_str();
                return entry != nullptr ? *entry : kNone;
            }

            const TypeInfo& Info(StringView name) const {
                const TypeInfo* info = registry.Find(name);
                EXPECT_NE(info, nullptr) << String(name).c_str();
                return info != nullptr ? *info : MakeTypeInfo<SchemaLeaf>();
            }

            TypeRegistry registry;
            ControlRoots roots;
            Json schema;
        };

    }  // namespace

    TEST_F(SchemaExportTest, TheDocumentHasTheFourParts) {
        EXPECT_EQ(GetJsonInt(schema, "version"), kSchemaVersion);
        EXPECT_TRUE(Member(schema, "types")->is_array());
        EXPECT_TRUE(Member(schema, "factories")->is_array());
        EXPECT_TRUE(Member(schema, "roots")->is_array());
        EXPECT_EQ(Types().size(), registry.All().size());
    }

    TEST_F(SchemaExportTest, TypesAreSortedByName) {
        String previous;
        for (const Json& entry : Types()) {
            const String name = GetJsonString(entry, "name").value_or(String());
            EXPECT_LT(previous, name);
            previous = name;
        }
    }

    TEST_F(SchemaExportTest, EveryShapeOfAFieldIsDescribed) {
        const Json& all = Entry("SchemaAllShapes");
        EXPECT_EQ(KindOf(TypeOfField(all, "flag")), "bool");
        EXPECT_EQ(KindOf(TypeOfField(all, "count")), "int32");
        EXPECT_EQ(KindOf(TypeOfField(all, "big")), "uint64");
        EXPECT_EQ(KindOf(TypeOfField(all, "precise")), "double");
        EXPECT_EQ(KindOf(TypeOfField(all, "text")), "string");
        EXPECT_FALSE(FindJsonMember(TypeOfField(all, "text"), "capacity") != nullptr) << "a String has no limit";
        EXPECT_EQ(KindOf(TypeOfField(all, "fixedText")), "string");
        EXPECT_EQ(GetJsonInt(TypeOfField(all, "fixedText"), "capacity"), 8);
        EXPECT_EQ(KindOf(TypeOfField(all, "name")), "string");

        const Json mode = TypeOfField(all, "mode");
        EXPECT_EQ(KindOf(mode), "enum");
        const Json* values = Member(mode, "values");
        ASSERT_TRUE(values != nullptr && values->is_array());
        ASSERT_EQ(values->size(), 3U);
        EXPECT_EQ((*values)[0], "Off");
        EXPECT_EQ((*values)[2], "Auto");

        const Json leaf = TypeOfField(all, "leaf");
        EXPECT_EQ(KindOf(leaf), "object");
        EXPECT_EQ(GetJsonString(leaf, "object"), String("SchemaLeaf"));

        const Json node = TypeOfField(all, "node");
        EXPECT_EQ(KindOf(node), "pointer");
        EXPECT_EQ(GetJsonString(node, "object"), String("SchemaNode"));

        const Json items = TypeOfField(all, "items");
        EXPECT_EQ(KindOf(items), "list");
        EXPECT_EQ(GetJsonBool(items, "resizable"), true);
        EXPECT_EQ(KindOf(*Member(items, "element")), "int32");

        const Json fixedItems = TypeOfField(all, "fixedItems");
        EXPECT_EQ(GetJsonBool(fixedItems, "resizable"), true) << "a fixed vector holds a changing number of elements, at most its capacity";
        EXPECT_EQ(GetJsonInt(fixedItems, "capacity"), 3);

        const Json ordered = TypeOfField(all, "ordered");
        EXPECT_EQ(KindOf(ordered), "map");
        EXPECT_EQ(KindOf(*Member(ordered, "key")), "int32");
        EXPECT_EQ(KindOf(*Member(ordered, "element")), "string");

        const Json maybe = TypeOfField(all, "maybe");
        EXPECT_EQ(KindOf(maybe), "optional");
        EXPECT_EQ(KindOf(*Member(maybe, "element")), "int32");

        const Json entry = TypeOfField(all, "entry");
        EXPECT_EQ(KindOf(entry), "pair");
        EXPECT_EQ(KindOf(*Member(entry, "key")), "int32");
        EXPECT_EQ(KindOf(*Member(entry, "element")), "string");

        const Json choice = TypeOfField(all, "choice");
        EXPECT_EQ(KindOf(choice), "variant");
        const Json* alternatives = Member(choice, "alternatives");
        ASSERT_TRUE(alternatives != nullptr && alternatives->is_array());
        ASSERT_EQ(alternatives->size(), 2U);
        EXPECT_EQ(KindOf((*alternatives)[0]), "int32");
        EXPECT_EQ(KindOf((*alternatives)[1]), "string");

        const Json leaves = TypeOfField(all, "leaves");
        EXPECT_EQ(GetJsonString(*Member(leaves, "element"), "object"), String("SchemaLeaf"));

        const Json shapes = TypeOfField(all, "shapes");
        EXPECT_EQ(KindOf(*Member(shapes, "element")), "pointer");
        EXPECT_EQ(GetJsonString(*Member(shapes, "element"), "object"), String("SchemaShape"));
    }

    TEST_F(SchemaExportTest, AValueFieldIsDescribedAsSuchWithItsType) {
        const Json& fields = Entry("SchemaValueFields");

        const Json damage = TypeOfField(fields, "damage");
        EXPECT_EQ(KindOf(damage), "value");
        EXPECT_EQ(KindOf(*Member(damage, "element")), "int32");
        EXPECT_EQ(KindOf(*Member(TypeOfField(fields, "alive"), "element")), "bool");
        const Json position = TypeOfField(fields, "position");
        EXPECT_EQ(KindOf(position), "value");
        EXPECT_EQ(KindOf(*Member(position, "element")), "opaque");

        const Json* attributes = FindJsonMember(FieldNamed(fields, "percent"), "attributes");
        ASSERT_NE(attributes, nullptr);
        const Json* range = FindJsonMember(*attributes, "range");
        ASSERT_TRUE(range != nullptr && range->is_array());
        EXPECT_EQ((*range)[1], 100.0);
    }

    TEST_F(SchemaExportTest, MembersThatDoNotApplyAreAbsent) {
        const Json count = TypeOfField(Entry("SchemaAllShapes"), "count");
        EXPECT_EQ(FindJsonMember(count, "element"), nullptr);
        EXPECT_EQ(FindJsonMember(count, "key"), nullptr);
        EXPECT_EQ(FindJsonMember(count, "values"), nullptr);
        EXPECT_EQ(FindJsonMember(count, "object"), nullptr);
        EXPECT_EQ(FindJsonMember(count, "resizable"), nullptr);
        EXPECT_EQ(FindJsonMember(count, "capacity"), nullptr);
    }

    TEST_F(SchemaExportTest, AttributesAppearOnlyWhenSet) {
        const Json& attributed = Entry("SchemaAttributed");

        const Json* opacity = FindJsonMember(FieldNamed(attributed, "opacity"), "attributes");
        ASSERT_NE(opacity, nullptr);
        const Json* range = FindJsonMember(*opacity, "range");
        ASSERT_TRUE(range != nullptr && range->is_array());
        ASSERT_EQ(range->size(), 2U);
        EXPECT_EQ((*range)[0], 0.0);
        EXPECT_EQ((*range)[1], 1.0);
        EXPECT_EQ(GetJsonNumber(*opacity, "step"), 0.05);
        EXPECT_EQ(GetJsonString(*opacity, "tooltip"), String("Share of the screen"));
        EXPECT_EQ(FindJsonMember(*opacity, "readonly"), nullptr);

        const Json* flags = FindJsonMember(FieldNamed(attributed, "flags"), "attributes");
        ASSERT_NE(flags, nullptr);
        EXPECT_EQ(GetJsonBool(*flags, "readonly"), true);
        EXPECT_EQ(GetJsonBool(*flags, "hidden"), true);
        EXPECT_EQ(GetJsonBool(*flags, "required"), true);
        EXPECT_EQ(FindJsonMember(*flags, "range"), nullptr);

        const Json* plain = FindJsonMember(FieldNamed(attributed, "plain"), "attributes");
        ASSERT_NE(plain, nullptr);
        EXPECT_TRUE(plain->is_object());
        EXPECT_TRUE(plain->empty());
    }

    TEST_F(SchemaExportTest, MethodsSayWhetherTheyCanBeCalled) {
        const Json* methods = Member(Entry("SchemaMethods"), "methods");
        ASSERT_TRUE(methods != nullptr && methods->is_array());

        const Json* add = EntryNamed(*methods, "Add");
        ASSERT_NE(add, nullptr);
        EXPECT_EQ(GetJsonBool(*add, "callable"), true);
        EXPECT_EQ(GetJsonBool(*add, "isConst"), false);
        EXPECT_EQ(KindOf(*Member(*add, "result")), "int32");
        const Json* params = Member(*add, "params");
        ASSERT_TRUE(params != nullptr && params->is_array());
        ASSERT_EQ(params->size(), 2U);
        EXPECT_EQ(GetJsonString((*params)[0], "name"), String("amount"));
        EXPECT_EQ(KindOf(*Member((*params)[0], "type")), "int32");
        EXPECT_EQ(KindOf(*Member((*params)[1], "type")), "string");

        const Json* reset = EntryNamed(*methods, "Reset");
        ASSERT_NE(reset, nullptr);
        EXPECT_EQ(KindOf(*Member(*reset, "result")), "void");
        EXPECT_TRUE(Member(*reset, "params")->empty());

        const Json* isEmpty = EntryNamed(*methods, "IsEmpty");
        ASSERT_NE(isEmpty, nullptr);
        EXPECT_EQ(GetJsonBool(*isEmpty, "isConst"), true);

        const Json* initialize = EntryNamed(*methods, "Initialize");
        ASSERT_NE(initialize, nullptr);
        EXPECT_EQ(GetJsonBool(*initialize, "callable"), false) << "an IDeserializer& parameter cannot come from outside";
        EXPECT_EQ(KindOf(*Member((*Member(*initialize, "params"))[0], "type")), "unknown");

        const Json* find = EntryNamed(*methods, "Find");
        ASSERT_NE(find, nullptr);
        EXPECT_EQ(GetJsonBool(*find, "callable"), false) << "a pointer result is not written";
    }

    TEST_F(SchemaExportTest, TheParentChainAndTheFlagsOfAType) {
        const Json& circle = Entry("SchemaCircle");
        EXPECT_EQ(GetJsonString(circle, "parent"), String("SchemaShape"));
        EXPECT_EQ(GetJsonString(circle, "factoryBase"), String("SchemaShape"));
        EXPECT_EQ(GetJsonBool(circle, "abstract"), false);
        EXPECT_EQ(GetJsonBool(circle, "creatable"), true);
        EXPECT_EQ(GetJsonString(circle, "icon"), String("circle"));
        EXPECT_EQ(GetJsonString(circle, "purpose"), String("common"));
        EXPECT_EQ(GetJsonString(Entry("SchemaSquare"), "purpose"), String("visual"));

        const Json& shape = Entry("SchemaShape");
        EXPECT_EQ(FindJsonMember(shape, "parent"), nullptr) << "a type with no parent has no parent key";
        EXPECT_EQ(GetJsonBool(shape, "abstract"), true);
        EXPECT_EQ(GetJsonBool(shape, "creatable"), false);
        EXPECT_EQ(FindJsonMember(shape, "factoryBase"), nullptr);

        const Json& disc = Entry("SchemaDisc");
        EXPECT_EQ(GetJsonString(disc, "parent"), String("SchemaCircle"));
        EXPECT_EQ(Member(disc, "fields")->size(), 1U) << "a type lists its own fields only";
    }

    TEST_F(SchemaExportTest, TheFactoryHierarchyHasTheElementName) {
        const Json* factories = Member(schema, "factories");
        ASSERT_TRUE(factories != nullptr && factories->is_array());
        ASSERT_EQ(factories->size(), 1U);
        const Json& factory = (*factories)[0];
        EXPECT_EQ(GetJsonString(factory, "base"), String("SchemaShape"));
        EXPECT_EQ(GetJsonString(factory, "elementName"), String("shape"));
        const Json* derived = Member(factory, "derived");
        ASSERT_TRUE(derived != nullptr && derived->is_array());
        ASSERT_EQ(derived->size(), 3U);
        EXPECT_EQ((*derived)[0], "SchemaCircle");
        EXPECT_EQ((*derived)[1], "SchemaDisc");
        EXPECT_EQ((*derived)[2], "SchemaSquare");
    }

    TEST_F(SchemaExportTest, RootsAreListedWithTheTypeOfTheirObject) {
        SchemaLeaf leaf;
        roots.Register("zeta", ControlRoot{[&leaf, this] { return TypedRef{&leaf, &Info("SchemaLeaf")}; }});
        roots.Register("alpha", ControlRoot{});

        const Json built = ToJson(MakeSchemaAnswer(registry, roots));
        const Json* list = Member(built, "roots");
        ASSERT_TRUE(list != nullptr && list->is_array());
        ASSERT_EQ(list->size(), 2U);
        EXPECT_EQ(GetJsonString((*list)[0], "name"), String("alpha"));
        EXPECT_EQ(FindJsonMember((*list)[0], "type"), nullptr) << "a root with no object has no type";
        EXPECT_EQ(GetJsonString((*list)[1], "name"), String("zeta"));
        EXPECT_EQ(GetJsonString((*list)[1], "type"), String("SchemaLeaf"));
    }

    TEST_F(SchemaExportTest, DescribeTypeAddsTheChainsAndTheOwnersOfFields) {
        const Json described = ToJson(DescribeTypeSchema(Info("SchemaDisc"), registry));
        EXPECT_EQ(GetJsonString(described, "name"), String("SchemaDisc"));

        const Json* bases = Member(described, "bases");
        ASSERT_TRUE(bases != nullptr && bases->is_array());
        ASSERT_EQ(bases->size(), 2U);
        EXPECT_EQ((*bases)[0], "SchemaCircle");
        EXPECT_EQ((*bases)[1], "SchemaShape");
        EXPECT_TRUE(Member(described, "derived")->empty());

        const Json* allFields = Member(described, "allFields");
        ASSERT_TRUE(allFields != nullptr && allFields->is_array());
        ASSERT_EQ(allFields->size(), 2U);
        EXPECT_EQ(GetJsonString((*allFields)[0], "name"), String("radius"));
        EXPECT_EQ(GetJsonString((*allFields)[0], "owner"), String("SchemaCircle"));
        EXPECT_EQ(GetJsonString((*allFields)[1], "name"), String("filled"));
        EXPECT_EQ(GetJsonString((*allFields)[1], "owner"), String("SchemaDisc"));

        const Json shape = ToJson(DescribeTypeSchema(Info("SchemaShape"), registry));
        const Json* direct = Member(shape, "derived");
        ASSERT_TRUE(direct != nullptr && direct->is_array());
        ASSERT_EQ(direct->size(), 2U) << "the direct children only, by name";
        EXPECT_EQ((*direct)[0], "SchemaCircle");
        EXPECT_EQ((*direct)[1], "SchemaSquare");
    }

    TEST_F(SchemaExportTest, ListTypesFiltersByBaseAndByCreatable) {
        const auto names = [](const Json& answer) {
            Vector<String> result;
            const Json* types = FindJsonMember(answer, "types");
            if (types != nullptr && types->is_array()) {
                for (const Json& entry : *types) {
                    result.push_back(GetJsonString(entry, "name").value_or(String()));
                }
            }
            return result;
        };

        EXPECT_EQ(names(ToJson(MakeListTypesAnswer(registry, {}))).size(), registry.All().size());

        ListTypesFilter below;
        below.base = &Info("SchemaShape");
        const Vector<String> all = names(ToJson(MakeListTypesAnswer(registry, below)));
        ASSERT_EQ(all.size(), 3U);
        EXPECT_EQ(all[0], "SchemaCircle");
        EXPECT_EQ(all[1], "SchemaDisc");
        EXPECT_EQ(all[2], "SchemaSquare");

        below.recursive = false;
        EXPECT_EQ(names(ToJson(MakeListTypesAnswer(registry, below))).size(), 2U);

        ListTypesFilter creatable;
        creatable.creatableOnly = true;
        const Vector<String> made = names(ToJson(MakeListTypesAnswer(registry, creatable)));
        ASSERT_EQ(made.size(), 3U) << "only the types of a factory, and not the abstract base";
        EXPECT_EQ(made[0], "SchemaCircle");
        EXPECT_EQ(made[2], "SchemaSquare");

        const Json listed = ToJson(MakeListTypesAnswer(registry, below));
        const Json* circle = EntryNamed(*Member(listed, "types"), "SchemaCircle");
        ASSERT_NE(circle, nullptr);
        EXPECT_EQ(GetJsonInt(*circle, "fields"), 1);
        EXPECT_EQ(GetJsonInt(*circle, "methods"), 0);
        EXPECT_EQ(GetJsonString(*circle, "parent"), String("SchemaShape"));
        EXPECT_EQ(GetJsonString(*circle, "icon"), String("circle"));
    }

    TEST(SchemaExportOrderTest, TheOrderOfRegistrationDoesNotShow) {
        TypeRegistry forward;
        TypeRegistry backward;
        Fill(forward, false);
        Fill(backward, true);
        const ControlRoots roots;

        EXPECT_EQ(DumpJson(ToJson(MakeSchemaAnswer(forward, roots))), DumpJson(ToJson(MakeSchemaAnswer(backward, roots))));
        EXPECT_EQ(DumpJson(ToJson(MakeListTypesAnswer(forward, {}))), DumpJson(ToJson(MakeListTypesAnswer(backward, {}))));
        EXPECT_EQ(DumpJson(ToJson(DescribeTypeSchema(Require(forward.Find("SchemaShape")), forward))), DumpJson(ToJson(DescribeTypeSchema(Require(backward.Find("SchemaShape")), backward))));
    }

    TEST(SchemaExportEmptyTest, AnEmptyRegistryGivesAnEmptyDocument) {
        const TypeRegistry empty;
        const Json schema = ToJson(MakeSchemaAnswer(empty, ControlRoots()));
        EXPECT_TRUE(Member(schema, "types")->empty());
        EXPECT_TRUE(Member(schema, "factories")->empty());
        EXPECT_TRUE(Member(schema, "roots")->empty());
        EXPECT_TRUE(Member(ToJson(MakeListTypesAnswer(empty, {})), "types")->empty());
    }

    TEST(SchemaExportEngineTest, TheRealRegistryHasTheWidgetFactoryAndNoHash) {
        const Json schema = ToJson(MakeSchemaAnswer(TypeRegistry::GetInstance(), ControlRoots()));
        EXPECT_GT(DumpJson(schema).size(), 0U);
        EXPECT_FALSE(HasKeyNamed(schema, "hash") || HasKeyNamed(schema, "typeHash")) << "a hash is never exported";

        const Json* factories = Member(schema, "factories");
        ASSERT_TRUE(factories != nullptr && factories->is_array());
        const Json* widgets = nullptr;
        for (const Json& factory : *factories) {
            if (GetJsonString(factory, "base") == String("IWidget")) {
                widgets = &factory;
            }
        }
        ASSERT_NE(widgets, nullptr);
        EXPECT_EQ(GetJsonString(*widgets, "elementName"), String("widget"));
        const Json* derived = Member(*widgets, "derived");
        ASSERT_TRUE(derived != nullptr && derived->is_array());
        EXPECT_FALSE(derived->empty());
        for (const Json& name : *derived) {
            EXPECT_TRUE(name.is_string());
        }
    }

}  // namespace BECore::Tests
