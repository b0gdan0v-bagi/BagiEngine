#include "SchemaExportTest.h"

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/SchemaExportReference.h>
#include <BECore/Widgets/IWidget.h>
#include <Common/ControlTestSupport.h>
#include <Common/JsonDoc.h>
#include <Common/Require.h>
#include <Common/ScratchScopeFixture.h>
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
                &Add<SchemaNested>,
                &Add<SchemaValueFields>,
                &Add<SchemaMethods>,
            };
            const size_t count = sizeof(steps) / sizeof(steps[0]);
            for (size_t i = 0; i < count; ++i) {
                steps[reversed ? count - 1 - i : i](registry);
            }
        }

        /// The member @p key of @p object, which has to be there
        JsonView Member(JsonView object, StringView key) {
            const Optional<JsonView> found = object.Find(key);
            EXPECT_TRUE(found.has_value()) << String(key).c_str();
            return found ? *found : JsonView();
        }

        /// The text of an answer with its own tape: the views of it live as long as the object
        template <typename T>
        JsonDoc DocOf(const T& answer) {
            const String text = WrittenText(answer);
            return JsonDoc(StringView(text.c_str(), text.size()));
        }

        /// The entry of the named type in a schema or a `list_types` array; not valid when there is none
        JsonView EntryNamed(JsonView list, StringView name) {
            for (const JsonView entry : list.Items()) {
                if (StringOf(entry, "name") == String(name)) {
                    return entry;
                }
            }
            return JsonView();
        }

        JsonView FieldNamed(JsonView entry, StringView name) {
            const JsonView found = EntryNamed(MemberOf(entry, "fields"), name);
            EXPECT_TRUE(found.IsValid()) << String(name).c_str();
            return found;
        }

        /// Whether an object anywhere in @p json has a member of this name
        bool HasKeyNamed(JsonView json, StringView key) {
            if (json.IsObject()) {
                if (json.Find(key).has_value()) {
                    return true;
                }
                for (const JsonMember member : json.Members()) {
                    if (HasKeyNamed(member.value, key)) {
                        return true;
                    }
                }
            } else if (json.IsArray()) {
                for (const JsonView element : json.Items()) {
                    if (HasKeyNamed(element, key)) {
                        return true;
                    }
                }
            }
            return false;
        }

        String KindOf(JsonView type) {
            return StringOf(type, "kind");
        }

        JsonView TypeOfField(JsonView entry, StringView field) {
            return MemberOf(FieldNamed(entry, field), "type");
        }

        void ExpectEmptyArray(JsonView array) {
            EXPECT_TRUE(array.IsArray() && array.Size() == 0U);
        }

        /// The names of the entries of a `list_types` answer
        Vector<String> NamesOf(JsonView answer) {
            Vector<String> result;
            for (const JsonView entry : MemberOf(answer, "types").Items()) {
                result.push_back(StringOf(entry, "name"));
            }
            return result;
        }

        class SchemaExportTest : public ScratchScopeFixture {
        protected:
            void SetUp() override {
                Fill(registry, false);
                schemaDoc = MakeUnique<JsonDoc>(StringView(WrittenText(MakeSchemaAnswer(registry, roots))));
                schema = schemaDoc->Root();
            }

            JsonView Types() const {
                return MemberOf(schema, "types");
            }

            JsonView Entry(StringView name) const {
                const JsonView entry = EntryNamed(Types(), name);
                EXPECT_TRUE(entry.IsValid()) << String(name).c_str();
                return entry;
            }

            const TypeInfo& Info(StringView name) const {
                const TypeInfo* info = registry.Find(name);
                EXPECT_NE(info, nullptr) << String(name).c_str();
                return info != nullptr ? *info : MakeTypeInfo<SchemaLeaf>();
            }

            TypeRegistry registry;
            ControlRoots roots;
            UniquePtr<JsonDoc> schemaDoc;
            JsonView schema;
        };

        // The builders return answer structs on the thread's scratch arena: a case holds them inside the scope of its fixture.
        using SchemaExportPayloadTest = ScratchScopeFixture;
        using SchemaExportReferenceTest = ScratchScopeFixture;
        using SchemaExportOrderTest = ScratchScopeFixture;
        using SchemaExportEmptyTest = ScratchScopeFixture;
        using SchemaExportEngineTest = ScratchScopeFixture;

    }  // namespace

    TEST_F(SchemaExportTest, TheDocumentHasTheFourParts) {
        EXPECT_EQ(IntOf(schema, "version"), kSchemaVersion);
        EXPECT_TRUE(Member(schema, "types").IsArray());
        EXPECT_TRUE(Member(schema, "factories").IsArray());
        EXPECT_TRUE(Member(schema, "roots").IsArray());
        EXPECT_EQ(Types().Size(), registry.All().size());
    }

    TEST_F(SchemaExportTest, TypesAreSortedByName) {
        String previous;
        for (const JsonView entry : Types().Items()) {
            const String name = StringOf(entry, "name");
            EXPECT_LT(previous, name);
            previous = name;
        }
    }

    TEST_F(SchemaExportTest, EveryShapeOfAFieldIsDescribed) {
        const JsonView all = Entry("SchemaAllShapes");
        EXPECT_EQ(KindOf(TypeOfField(all, "flag")), "bool");
        EXPECT_EQ(KindOf(TypeOfField(all, "count")), "int32");
        EXPECT_EQ(KindOf(TypeOfField(all, "big")), "uint64");
        EXPECT_EQ(KindOf(TypeOfField(all, "precise")), "double");
        EXPECT_EQ(KindOf(TypeOfField(all, "text")), "string");
        EXPECT_FALSE(MemberOf(TypeOfField(all, "text"), "capacity").IsValid()) << "a String has no limit";
        EXPECT_EQ(KindOf(TypeOfField(all, "fixedText")), "string");
        EXPECT_EQ(IntOf(TypeOfField(all, "fixedText"), "capacity"), 8);
        EXPECT_EQ(KindOf(TypeOfField(all, "name")), "string");

        const JsonView mode = TypeOfField(all, "mode");
        EXPECT_EQ(KindOf(mode), "enum");
        const JsonView values = Member(mode, "values");
        ASSERT_TRUE(values.IsArray());
        ASSERT_EQ(values.Size(), 3U);
        EXPECT_TRUE(ItemAt(values, 0).TextEquals("Off"));
        EXPECT_TRUE(ItemAt(values, 2).TextEquals("Auto"));

        const JsonView leaf = TypeOfField(all, "leaf");
        EXPECT_EQ(KindOf(leaf), "object");
        EXPECT_EQ(StringOf(leaf, "object"), "SchemaLeaf");

        const JsonView node = TypeOfField(all, "node");
        EXPECT_EQ(KindOf(node), "pointer");
        EXPECT_EQ(StringOf(node, "object"), "SchemaNode");

        const JsonView items = TypeOfField(all, "items");
        EXPECT_EQ(KindOf(items), "list");
        EXPECT_EQ(BoolOf(items, "resizable"), true);
        EXPECT_EQ(KindOf(Member(items, "element")), "int32");

        const JsonView fixedItems = TypeOfField(all, "fixedItems");
        EXPECT_EQ(BoolOf(fixedItems, "resizable"), true) << "a fixed vector holds a changing number of elements, at most its capacity";
        EXPECT_EQ(IntOf(fixedItems, "capacity"), 3);

        const JsonView ordered = TypeOfField(all, "ordered");
        EXPECT_EQ(KindOf(ordered), "map");
        EXPECT_EQ(KindOf(Member(ordered, "key")), "int32");
        EXPECT_EQ(KindOf(Member(ordered, "element")), "string");

        const JsonView maybe = TypeOfField(all, "maybe");
        EXPECT_EQ(KindOf(maybe), "optional");
        EXPECT_EQ(KindOf(Member(maybe, "element")), "int32");

        const JsonView entry = TypeOfField(all, "entry");
        EXPECT_EQ(KindOf(entry), "pair");
        EXPECT_EQ(KindOf(Member(entry, "key")), "int32");
        EXPECT_EQ(KindOf(Member(entry, "element")), "string");

        const JsonView choice = TypeOfField(all, "choice");
        EXPECT_EQ(KindOf(choice), "variant");
        const JsonView alternatives = Member(choice, "alternatives");
        ASSERT_TRUE(alternatives.IsArray());
        ASSERT_EQ(alternatives.Size(), 2U);
        EXPECT_EQ(KindOf(ItemAt(alternatives, 0)), "int32");
        EXPECT_EQ(KindOf(ItemAt(alternatives, 1)), "string");

        const JsonView leaves = TypeOfField(all, "leaves");
        EXPECT_EQ(StringOf(Member(leaves, "element"), "object"), "SchemaLeaf");

        const JsonView shapes = TypeOfField(all, "shapes");
        EXPECT_EQ(KindOf(Member(shapes, "element")), "pointer");
        EXPECT_EQ(StringOf(Member(shapes, "element"), "object"), "SchemaShape");
    }

    TEST_F(SchemaExportPayloadTest, AJsonPayloadFieldIsAny) {
        TypeRegistry local;
        Add<SchemaPayloadFields>(local);
        const TypeInfo& info = Require(local.Find("SchemaPayloadFields"));

        const JsonDoc described = DocOf(DescribeTypeSchema(info, local));
        const JsonView body = TypeOfField(described, "body");
        EXPECT_EQ(KindOf(body), "any");
        EXPECT_EQ(String(body.Raw()), R"({"kind":"any"})");

        const JsonView maybeBody = TypeOfField(described, "maybeBody");
        EXPECT_EQ(KindOf(maybeBody), "optional");
        EXPECT_EQ(KindOf(Member(maybeBody, "element")), "any");

        EXPECT_EQ(KindOf(TypeOfField(described, "position")), "opaque") << "another opaque type is still told as such";

        ControlRoots roots;
        const JsonDoc schema = DocOf(MakeSchemaAnswer(local, roots));
        const JsonView entry = EntryNamed(Member(schema, "types"), "SchemaPayloadFields");
        ASSERT_TRUE(entry.IsValid());
        EXPECT_EQ(KindOf(TypeOfField(entry, "body")), "any");
    }

    TEST_F(SchemaExportTest, AValueFieldIsDescribedAsSuchWithItsType) {
        const JsonView fields = Entry("SchemaValueFields");

        const JsonView damage = TypeOfField(fields, "damage");
        EXPECT_EQ(KindOf(damage), "value");
        EXPECT_EQ(KindOf(Member(damage, "element")), "int32");
        EXPECT_EQ(KindOf(Member(TypeOfField(fields, "alive"), "element")), "bool");
        const JsonView position = TypeOfField(fields, "position");
        EXPECT_EQ(KindOf(position), "value");
        EXPECT_EQ(KindOf(Member(position, "element")), "opaque");

        const JsonView attributes = MemberOf(FieldNamed(fields, "percent"), "attributes");
        ASSERT_TRUE(attributes.IsValid());
        const JsonView range = MemberOf(attributes, "range");
        ASSERT_TRUE(range.IsArray());
        EXPECT_EQ(ItemAt(range, 1).AsDouble(), 100.0);
    }

    TEST_F(SchemaExportTest, MembersThatDoNotApplyAreAbsent) {
        const JsonView count = TypeOfField(Entry("SchemaAllShapes"), "count");
        EXPECT_FALSE(MemberOf(count, "element").IsValid());
        EXPECT_FALSE(MemberOf(count, "key").IsValid());
        EXPECT_FALSE(MemberOf(count, "values").IsValid());
        EXPECT_FALSE(MemberOf(count, "object").IsValid());
        EXPECT_FALSE(MemberOf(count, "resizable").IsValid());
        EXPECT_FALSE(MemberOf(count, "capacity").IsValid());
    }

    TEST_F(SchemaExportTest, AttributesAppearOnlyWhenSet) {
        const JsonView attributed = Entry("SchemaAttributed");

        const JsonView opacity = MemberOf(FieldNamed(attributed, "opacity"), "attributes");
        ASSERT_TRUE(opacity.IsValid());
        const JsonView range = MemberOf(opacity, "range");
        ASSERT_TRUE(range.IsArray());
        ASSERT_EQ(range.Size(), 2U);
        EXPECT_EQ(ItemAt(range, 0).AsDouble(), 0.0);
        EXPECT_EQ(ItemAt(range, 1).AsDouble(), 1.0);
        EXPECT_EQ(MemberOf(opacity, "step").AsDouble(), 0.05);
        EXPECT_EQ(StringOf(opacity, "tooltip"), "Share of the screen");
        EXPECT_FALSE(MemberOf(opacity, "readonly").IsValid());

        const JsonView flags = MemberOf(FieldNamed(attributed, "flags"), "attributes");
        ASSERT_TRUE(flags.IsValid());
        EXPECT_EQ(BoolOf(flags, "readonly"), true);
        EXPECT_EQ(BoolOf(flags, "hidden"), true);
        EXPECT_EQ(BoolOf(flags, "required"), true);
        EXPECT_FALSE(MemberOf(flags, "range").IsValid());

        const JsonView plain = MemberOf(FieldNamed(attributed, "plain"), "attributes");
        ASSERT_TRUE(plain.IsValid());
        EXPECT_TRUE(plain.IsObject());
        EXPECT_EQ(plain.Size(), 0U);
    }

    TEST_F(SchemaExportTest, MethodsSayWhetherTheyCanBeCalled) {
        const JsonView methods = Member(Entry("SchemaMethods"), "methods");
        ASSERT_TRUE(methods.IsArray());

        const JsonView add = EntryNamed(methods, "Add");
        ASSERT_TRUE(add.IsValid());
        EXPECT_EQ(BoolOf(add, "callable"), true);
        EXPECT_EQ(BoolOf(add, "isConst"), false);
        EXPECT_EQ(KindOf(Member(add, "result")), "int32");
        const JsonView params = Member(add, "params");
        ASSERT_TRUE(params.IsArray());
        ASSERT_EQ(params.Size(), 2U);
        EXPECT_EQ(StringOf(ItemAt(params, 0), "name"), "amount");
        EXPECT_EQ(KindOf(Member(ItemAt(params, 0), "type")), "int32");
        EXPECT_EQ(KindOf(Member(ItemAt(params, 1), "type")), "string");

        const JsonView reset = EntryNamed(methods, "Reset");
        ASSERT_TRUE(reset.IsValid());
        EXPECT_EQ(KindOf(Member(reset, "result")), "void");
        ExpectEmptyArray(Member(reset, "params"));

        const JsonView isEmpty = EntryNamed(methods, "IsEmpty");
        ASSERT_TRUE(isEmpty.IsValid());
        EXPECT_EQ(BoolOf(isEmpty, "isConst"), true);

        const JsonView initialize = EntryNamed(methods, "Initialize");
        ASSERT_TRUE(initialize.IsValid());
        EXPECT_EQ(BoolOf(initialize, "callable"), false) << "an IDeserializer& parameter cannot come from outside";
        EXPECT_EQ(KindOf(Member(ItemAt(Member(initialize, "params"), 0), "type")), "unknown");

        const JsonView find = EntryNamed(methods, "Find");
        ASSERT_TRUE(find.IsValid());
        EXPECT_EQ(BoolOf(find, "callable"), false) << "a pointer result is not written";
    }

    TEST_F(SchemaExportTest, TheParentChainAndTheFlagsOfAType) {
        const JsonView circle = Entry("SchemaCircle");
        EXPECT_EQ(StringOf(circle, "parent"), "SchemaShape");
        EXPECT_EQ(StringOf(circle, "factoryBase"), "SchemaShape");
        EXPECT_EQ(BoolOf(circle, "abstract"), false);
        EXPECT_EQ(BoolOf(circle, "creatable"), true);
        EXPECT_EQ(StringOf(circle, "icon"), "circle");
        EXPECT_EQ(StringOf(circle, "purpose"), "common");
        EXPECT_EQ(StringOf(Entry("SchemaSquare"), "purpose"), "visual");

        const JsonView shape = Entry("SchemaShape");
        EXPECT_FALSE(MemberOf(shape, "parent").IsValid()) << "a type with no parent has no parent key";
        EXPECT_EQ(BoolOf(shape, "abstract"), true);
        EXPECT_EQ(BoolOf(shape, "creatable"), false);
        EXPECT_FALSE(MemberOf(shape, "factoryBase").IsValid());

        const JsonView disc = Entry("SchemaDisc");
        EXPECT_EQ(StringOf(disc, "parent"), "SchemaCircle");
        EXPECT_EQ(Member(disc, "fields").Size(), 1U) << "a type lists its own fields only";
    }

    TEST_F(SchemaExportTest, TheFactoryHierarchyHasTheElementName) {
        const JsonView factories = Member(schema, "factories");
        ASSERT_TRUE(factories.IsArray());
        ASSERT_EQ(factories.Size(), 1U);
        const JsonView factory = ItemAt(factories, 0);
        EXPECT_EQ(StringOf(factory, "base"), "SchemaShape");
        EXPECT_EQ(StringOf(factory, "elementName"), "shape");
        const JsonView derived = Member(factory, "derived");
        ASSERT_TRUE(derived.IsArray());
        ASSERT_EQ(derived.Size(), 3U);
        EXPECT_TRUE(ItemAt(derived, 0).TextEquals("SchemaCircle"));
        EXPECT_TRUE(ItemAt(derived, 1).TextEquals("SchemaDisc"));
        EXPECT_TRUE(ItemAt(derived, 2).TextEquals("SchemaSquare"));
    }

    TEST_F(SchemaExportTest, RootsAreListedWithTheTypeOfTheirObject) {
        SchemaLeaf leaf;
        roots.Register("zeta", ControlRoot{[&leaf, this] { return TypedRef{&leaf, &Info("SchemaLeaf")}; }});
        roots.Register("alpha", ControlRoot{});

        const JsonDoc built = DocOf(MakeSchemaAnswer(registry, roots));
        const JsonView list = Member(built, "roots");
        ASSERT_TRUE(list.IsArray());
        ASSERT_EQ(list.Size(), 2U);
        EXPECT_EQ(StringOf(ItemAt(list, 0), "name"), "alpha");
        EXPECT_FALSE(MemberOf(ItemAt(list, 0), "type").IsValid()) << "a root with no object has no type";
        EXPECT_EQ(StringOf(ItemAt(list, 1), "name"), "zeta");
        EXPECT_EQ(StringOf(ItemAt(list, 1), "type"), "SchemaLeaf");
    }

    TEST_F(SchemaExportTest, DescribeTypeAddsTheChainsAndTheOwnersOfFields) {
        const JsonDoc described = DocOf(DescribeTypeSchema(Info("SchemaDisc"), registry));
        EXPECT_EQ(StringOf(described, "name"), "SchemaDisc");

        const JsonView bases = Member(described, "bases");
        ASSERT_TRUE(bases.IsArray());
        ASSERT_EQ(bases.Size(), 2U);
        EXPECT_TRUE(ItemAt(bases, 0).TextEquals("SchemaCircle"));
        EXPECT_TRUE(ItemAt(bases, 1).TextEquals("SchemaShape"));
        ExpectEmptyArray(Member(described, "derived"));

        const JsonView allFields = Member(described, "allFields");
        ASSERT_TRUE(allFields.IsArray());
        ASSERT_EQ(allFields.Size(), 2U);
        EXPECT_EQ(StringOf(ItemAt(allFields, 0), "name"), "radius");
        EXPECT_EQ(StringOf(ItemAt(allFields, 0), "owner"), "SchemaCircle");
        EXPECT_EQ(StringOf(ItemAt(allFields, 1), "name"), "filled");
        EXPECT_EQ(StringOf(ItemAt(allFields, 1), "owner"), "SchemaDisc");

        const JsonDoc shape = DocOf(DescribeTypeSchema(Info("SchemaShape"), registry));
        const JsonView direct = Member(shape, "derived");
        ASSERT_TRUE(direct.IsArray());
        ASSERT_EQ(direct.Size(), 2U) << "the direct children only, by name";
        EXPECT_TRUE(ItemAt(direct, 0).TextEquals("SchemaCircle"));
        EXPECT_TRUE(ItemAt(direct, 1).TextEquals("SchemaSquare"));
    }

    TEST_F(SchemaExportTest, ListTypesFiltersByBaseAndByCreatable) {
        EXPECT_EQ(NamesOf(DocOf(MakeListTypesAnswer(registry, {}))).size(), registry.All().size());

        ListTypesFilter below;
        below.base = &Info("SchemaShape");
        const Vector<String> all = NamesOf(DocOf(MakeListTypesAnswer(registry, below)));
        ASSERT_EQ(all.size(), 3U);
        EXPECT_EQ(all[0], "SchemaCircle");
        EXPECT_EQ(all[1], "SchemaDisc");
        EXPECT_EQ(all[2], "SchemaSquare");

        below.recursive = false;
        EXPECT_EQ(NamesOf(DocOf(MakeListTypesAnswer(registry, below))).size(), 2U);

        ListTypesFilter creatable;
        creatable.creatableOnly = true;
        const Vector<String> made = NamesOf(DocOf(MakeListTypesAnswer(registry, creatable)));
        ASSERT_EQ(made.size(), 3U) << "only the types of a factory, and not the abstract base";
        EXPECT_EQ(made[0], "SchemaCircle");
        EXPECT_EQ(made[2], "SchemaSquare");

        const JsonDoc listed = DocOf(MakeListTypesAnswer(registry, below));
        const JsonView circle = EntryNamed(Member(listed, "types"), "SchemaCircle");
        ASSERT_TRUE(circle.IsValid());
        EXPECT_EQ(IntOf(circle, "fields"), 1);
        EXPECT_EQ(IntOf(circle, "methods"), 0);
        EXPECT_EQ(StringOf(circle, "parent"), "SchemaShape");
        EXPECT_EQ(StringOf(circle, "icon"), "circle");
    }

    TEST_F(SchemaExportTest, AFieldWithNoAttributeWritesAnEmptyObject) {
        const String text = WrittenText(MakeTypeSchemaEntry(Info("SchemaAttributed"), registry));
        EXPECT_NE(text.find(R"({"attributes":{},"name":"plain",)"), String::npos) << text.c_str();
    }

    TEST_F(SchemaExportTest, ATypeInsideATypeNestsAsDeepAsTheFieldIs) {
        const JsonView table = TypeOfField(Entry("SchemaNested"), "table");
        EXPECT_EQ(KindOf(table), "map");
        EXPECT_EQ(KindOf(Member(table, "key")), "int32");
        const JsonView choice = Member(table, "element");
        ASSERT_TRUE(choice.IsValid());
        EXPECT_EQ(KindOf(choice), "variant");
        const JsonView alternatives = Member(choice, "alternatives");
        ASSERT_TRUE(alternatives.IsArray());
        ASSERT_EQ(alternatives.Size(), 2U);
        EXPECT_EQ(KindOf(ItemAt(alternatives, 1)), "list");
        EXPECT_EQ(KindOf(Member(ItemAt(alternatives, 1), "element")), "string");
    }

    // The text of the reflected answers equals the reference writer of `SchemaExportReference.h`, whatever order the types were registered in
    TEST_F(SchemaExportReferenceTest, TheReflectedTextEqualsTheReference) {
        for (const bool reversed : {false, true}) {
            TypeRegistry registry;
            Fill(registry, reversed);
            SchemaLeaf leaf;
            ControlRoots roots;
            roots.Register("zeta", ControlRoot{[&leaf, &registry] { return TypedRef{&leaf, &Require(registry.Find("SchemaLeaf"))}; }});
            roots.Register("alpha", ControlRoot{});

            EXPECT_EQ(WrittenText(MakeSchemaAnswer(registry, roots)), ReferenceSchemaText(registry, roots)) << "schema, reversed " << reversed;

            for (const TypeInfo* info : registry.All()) {
                EXPECT_EQ(WrittenText(DescribeTypeSchema(*info, registry)), ReferenceDescribeTypeText(*info, registry)) << "describe_type " << info->Name().data();
            }

            ListTypesFilter below;
            below.base = &Require(registry.Find("SchemaShape"));
            ListTypesFilter direct = below;
            direct.recursive = false;
            ListTypesFilter creatable;
            creatable.creatableOnly = true;
            ListTypesFilter creatableBelow = below;
            creatableBelow.creatableOnly = true;
            for (const ListTypesFilter& filter : {ListTypesFilter{}, below, direct, creatable, creatableBelow}) {
                EXPECT_EQ(WrittenText(MakeListTypesAnswer(registry, filter)), ReferenceListTypesText(registry, filter));
            }
        }
    }

    TEST_F(SchemaExportReferenceTest, TheRealRegistryWritesTheSameTextAsTheReference) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const ControlRoots roots;
        EXPECT_TRUE(WrittenText(MakeSchemaAnswer(registry, roots)) == ReferenceSchemaText(registry, roots));
    }

    // The bytes of the three answers over a small registry, printed once from the DOM they replaced: the reference writer and the generated one could share a mistake
    TEST_F(SchemaExportReferenceTest, ASmallRegistryWritesTheseBytes) {
        TypeRegistry registry;
        Add<SchemaLeaf>(registry);
        Add<SchemaShape>(registry);
        Add<SchemaCircle, SchemaShape, SchemaShape>(registry);
        Add<SchemaAttributed>(registry);
        SchemaLeaf leaf;
        ControlRoots roots;
        roots.Register("zeta", ControlRoot{[&leaf, &registry] { return TypedRef{&leaf, &Require(registry.Find("SchemaLeaf"))}; }});
        roots.Register("alpha", ControlRoot{});

        EXPECT_EQ(
            WrittenText(MakeSchemaAnswer(registry, roots)),
            R"json({"factories":[{"base":"SchemaShape","derived":["SchemaCircle"]}],"roots":[{"name":"alpha"},{"name":"zeta","type":"SchemaLeaf"}],"types":[{"abstract":false,"creatable":false,"fields":[{"attributes":{"range":[0.0,1.0],"step":0.05,"tooltip":"Share of the screen"},"name":"opacity","type":{"kind":"float"}},{"attributes":{"hidden":true,"readonly":true,"required":true},"name":"flags","type":{"kind":"bool"}},{"attributes":{},"name":"plain","type":{"kind":"int32"}}],"icon":"","methods":[],"name":"SchemaAttributed","purpose":"common"},{"abstract":false,"creatable":true,"factoryBase":"SchemaShape","fields":[{"attributes":{},"name":"radius","type":{"kind":"float"}}],"icon":"circle","methods":[],"name":"SchemaCircle","parent":"SchemaShape","purpose":"common"},{"abstract":false,"creatable":false,"fields":[{"attributes":{},"name":"id","type":{"kind":"int32"}}],"icon":"","methods":[],"name":"SchemaLeaf","purpose":"common"},{"abstract":true,"creatable":false,"fields":[],"icon":"","methods":[],"name":"SchemaShape","purpose":"common"}],"version":1})json");
        EXPECT_EQ(
            WrittenText(DescribeTypeSchema(Require(registry.Find("SchemaCircle")), registry)),
            R"json({"abstract":false,"allFields":[{"attributes":{},"name":"radius","owner":"SchemaCircle","type":{"kind":"float"}}],"bases":["SchemaShape"],"creatable":true,"derived":[],"factoryBase":"SchemaShape","fields":[{"attributes":{},"name":"radius","type":{"kind":"float"}}],"icon":"circle","methods":[],"name":"SchemaCircle","parent":"SchemaShape","purpose":"common"})json");
        EXPECT_EQ(
            WrittenText(MakeListTypesAnswer(registry, {})),
            R"json({"types":[{"abstract":false,"creatable":false,"fields":3,"icon":"","methods":0,"name":"SchemaAttributed","purpose":"common"},{"abstract":false,"creatable":true,"fields":1,"icon":"circle","methods":0,"name":"SchemaCircle","parent":"SchemaShape","purpose":"common"},{"abstract":false,"creatable":false,"fields":1,"icon":"","methods":0,"name":"SchemaLeaf","purpose":"common"},{"abstract":true,"creatable":false,"fields":0,"icon":"","methods":0,"name":"SchemaShape","purpose":"common"}]})json");
    }

    TEST_F(SchemaExportOrderTest, TheOrderOfRegistrationDoesNotShow) {
        TypeRegistry forward;
        TypeRegistry backward;
        Fill(forward, false);
        Fill(backward, true);
        const ControlRoots roots;

        EXPECT_EQ(WrittenText(MakeSchemaAnswer(forward, roots)), WrittenText(MakeSchemaAnswer(backward, roots)));
        EXPECT_EQ(WrittenText(MakeListTypesAnswer(forward, {})), WrittenText(MakeListTypesAnswer(backward, {})));
        EXPECT_EQ(WrittenText(DescribeTypeSchema(Require(forward.Find("SchemaShape")), forward)), WrittenText(DescribeTypeSchema(Require(backward.Find("SchemaShape")), backward)));
    }

    TEST_F(SchemaExportEmptyTest, AnEmptyRegistryGivesAnEmptyDocument) {
        const TypeRegistry empty;
        const JsonDoc schema = DocOf(MakeSchemaAnswer(empty, ControlRoots()));
        ExpectEmptyArray(Member(schema, "types"));
        ExpectEmptyArray(Member(schema, "factories"));
        ExpectEmptyArray(Member(schema, "roots"));
        ExpectEmptyArray(Member(DocOf(MakeListTypesAnswer(empty, {})), "types"));
    }

    TEST_F(SchemaExportEngineTest, TheRealRegistryHasTheWidgetFactoryAndNoHash) {
        const JsonDoc schema = DocOf(MakeSchemaAnswer(TypeRegistry::GetInstance(), ControlRoots()));
        EXPECT_GT(schema.Text().size(), 0U);
        EXPECT_FALSE(HasKeyNamed(schema, "hash") || HasKeyNamed(schema, "typeHash")) << "a hash is never exported";

        const JsonView factories = Member(schema, "factories");
        ASSERT_TRUE(factories.IsArray());
        JsonView widgets;
        for (const JsonView factory : factories.Items()) {
            if (StringOf(factory, "base") == "IWidget") {
                widgets = factory;
            }
        }
        ASSERT_TRUE(widgets.IsValid());
        EXPECT_EQ(StringOf(widgets, "elementName"), "widget");
        const JsonView derived = Member(widgets, "derived");
        ASSERT_TRUE(derived.IsArray());
        EXPECT_GT(derived.Size(), 0U);
        for (const JsonView name : derived.Items()) {
            EXPECT_TRUE(name.IsString());
        }
    }

}  // namespace BECore::Tests
