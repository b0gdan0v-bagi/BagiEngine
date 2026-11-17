// Loading and saving of Value<T> (Values/ValueAccessor.h): the three spellings, the link scope, the errors, XML and binary. The reflected
// structs are those of ValueTestTypes.h (their generated code is included in ValueContextTest.cpp).

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/Printers.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kState = "State";

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        struct LoadOptions {
            DiagnosticsMode diagnostics = DiagnosticsMode::Off;
            bool strict = false;
            const ContextDecl* declaration = nullptr;
        };

        /// Loads `<root><State>...</State></root>` into a struct under the options; the declaration, when given, is the link scope of the load
        template <typename Struct>
        struct XmlLoad {
            XmlDeserializer reader;

            /// @p attributes and @p children are the text inside `<State ...>...</State>`
            XmlLoad(StringView attributes, StringView children, Struct& target, const LoadOptions& options = {}) {
                reader.SetDiagnostics(options.diagnostics);
                reader.SetStrictValues(options.strict);
                const String xml = Format("<root><State {}>{}</State></root>", attributes, children);
                EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
                EXPECT_TRUE(reader.BeginObject(kState));
                Optional<ValueLinkScope> scope;
                if (options.declaration != nullptr) {
                    scope.emplace(*options.declaration);
                }
                target.Deserialize(reader);
                reader.EndObject();
            }

            /// Path and message of every error, then of every warning, one text
            [[nodiscard]] String Reports() const {
                String out;
                for (const Vector<DeserializeError>* list : {&reader.GetErrors(), &reader.GetWarnings()}) {
                    for (const DeserializeError& report : *list) {
                        AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
                    }
                }
                return out;
            }
        };

        template <typename Struct>
        String SaveXml(const Struct& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject(kState)) {
                value.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        template <typename Struct>
        Vector<uint8_t> SaveBinary(const Struct& value) {
            BinarySerializer serializer;
            if (serializer.BeginObject(kState)) {
                value.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// Loads a binary document of the struct; the load scope is the declaration when given
        template <typename Struct>
        String LoadBinary(const Vector<uint8_t>& data, Struct& target, const ContextDecl* declaration) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(data));
            EXPECT_TRUE(reader.BeginObject(kState));
            Optional<ValueLinkScope> scope;
            if (declaration != nullptr) {
                scope.emplace(*declaration);
            }
            target.Deserialize(reader);
            reader.EndObject();
            String out;
            for (const DeserializeError& report : reader.GetErrors()) {
                AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
            }
            return out;
        }

        /// Loads the JSON object @p text into @p target (the load scope is the declaration when given); every error, one per line
        template <typename Struct>
        String LoadJson(StringView text, Struct& target, const ContextDecl* declaration) {
            const auto parsed = ParseJson(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str();
            if (!parsed) {
                return {};
            }
            JsonDeserializer reader(*parsed);
            Optional<ValueLinkScope> scope;
            if (declaration != nullptr) {
                scope.emplace(*declaration);
            }
            target.Deserialize(reader);
            String out;
            for (const DeserializeError& report : reader.GetErrors()) {
                AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
            }
            return out;
        }

        /// A value loaded is the same one: the same kind, then the same literal bytes or the same slot and path
        template <typename T>
        void ExpectSameValue(const Value<T>& actual, const Value<T>& expected, const char* field) {
            ASSERT_EQ(actual.Kind(), expected.Kind()) << field;
            if (expected.IsSlot()) {
                EXPECT_EQ(actual.Level(), expected.Level()) << field;
                EXPECT_EQ(actual.Offset(), expected.Offset()) << field;
                EXPECT_EQ(actual.Path(), expected.Path()) << field;
            } else {
                EXPECT_EQ(std::memcmp(&actual.GetConst(), &expected.GetConst(), sizeof(T)), 0) << field;
            }
        }

        void ExpectSameSkill(const ValueSkill& actual, const ValueSkill& expected) {
            ExpectSameValue(actual.damage, expected.damage, "damage");
            ExpectSameValue(actual.scale, expected.scale, "scale");
            ExpectSameValue(actual.critical, expected.critical, "critical");
            ExpectSameValue(actual.label, expected.label, "label");
            ExpectSameValue(actual.element, expected.element, "element");
            ExpectSameValue(actual.aim, expected.aim, "aim");
            ExpectSameValue(actual.tint, expected.tint, "tint");
            ExpectSameValue(actual.spot, expected.spot, "spot");
        }

        /// What a load that failed left: the defaults of the struct
        void ExpectDefaultDamage(const ValueSkill& skill) {
            EXPECT_TRUE(skill.damage.IsConst());
            EXPECT_EQ(skill.damage.GetConst(), 7);
        }

        /// A skill that reads some fields and holds literals in the others
        ValueSkill MakeMixedSkill(const ValueWorld& world) {
            ValueSkill skill;
            skill.damage = 12;
            EXPECT_TRUE(skill.scale.Link(world.declaration, "caster.speed").has_value());
            skill.critical = true;
            skill.label = PoolString{"Hero"_intern};
            skill.element = ValueElement::Fire;
            EXPECT_TRUE(skill.aim.Link(world.declaration, "caster.velocity").has_value());
            skill.tint = Color{10, 20, 30, 40};
            EXPECT_TRUE(skill.spot.Link(world.declaration, "caster.spawn").has_value());
            return skill;
        }

        constexpr StringView kPlainAttributes = R"(damage="12" scale="0.5" critical="true" label="Hero" element="Fire")";
        constexpr StringView kPlainChildren = R"(<aim x="1" y="2" z="3"/><tint r="10" g="20" b="30" a="40"/><spot column="5" row="6"/>)";

    }  // namespace

    static_assert(DescribeType<Value<float>>().kind == TypeKind::Opaque);
    static_assert(Describable<Value<Vec3>> && Ser::HasDataAccessor<Value<Vec3>>);

    TEST(ValueAccessorTest, CompileTimeChecks) {}

    // =========================================================================
    // The three spellings
    // =========================================================================

    TEST(ValueAccessorTest, PlainAttributesLoadAsConstsExactlyLikePlainFields) {
        ValueSkill skill;
        ValuePlainSkill plain;
        XmlLoad valueLoad(kPlainAttributes, kPlainChildren, skill);
        XmlLoad plainLoad(kPlainAttributes, kPlainChildren, plain);
        EXPECT_TRUE(valueLoad.Reports().empty()) << valueLoad.Reports().c_str();
        EXPECT_TRUE(plainLoad.Reports().empty());
        for (const ValueKind kind :
             {skill.damage.Kind(), skill.scale.Kind(), skill.critical.Kind(), skill.label.Kind(), skill.element.Kind(), skill.aim.Kind(), skill.tint.Kind(), skill.spot.Kind()}) {
            EXPECT_EQ(kind, ValueKind::Const);
        }
        EXPECT_EQ(skill.damage.GetConst(), plain.damage);
        EXPECT_EQ(skill.scale.GetConst(), plain.scale);
        EXPECT_EQ(skill.critical.GetConst(), plain.critical);
        EXPECT_EQ(skill.label.GetConst(), plain.label);
        EXPECT_EQ(skill.element.GetConst(), plain.element);
        EXPECT_EQ(skill.aim.GetConst(), plain.aim);
        EXPECT_EQ(std::memcmp(&skill.tint.GetConst(), &plain.tint, sizeof(Color)), 0);
        EXPECT_EQ(skill.spot.GetConst(), plain.spot);
        EXPECT_EQ(plain.damage, 12);
        EXPECT_EQ(plain.aim, (Vec3{1.0f, 2.0f, 3.0f}));
    }

    TEST(ValueAccessorTest, TheObjectFormWithValueIsAConst) {
        ValueSkill skill;
        XmlLoad load("", R"(<damage value="12"/><scale value="0.5"/><critical value="true"/><label value="Hero"/><element value="Fire"/>)", skill);
        EXPECT_TRUE(load.Reports().empty()) << load.Reports().c_str();
        EXPECT_TRUE(skill.damage.IsConst());
        EXPECT_EQ(skill.damage.GetConst(), 12);
        EXPECT_EQ(skill.scale.GetConst(), 0.5f);
        EXPECT_TRUE(skill.critical.GetConst());
        EXPECT_EQ(skill.label.GetConst(), "Hero"_intern);
        EXPECT_EQ(skill.element.GetConst(), ValueElement::Fire);
        // What the file does not say keeps its default
        EXPECT_EQ(skill.aim.GetConst(), (Vec3{9.0f, 9.0f, 9.0f}));
    }

    TEST(ValueAccessorTest, FromReadsTheContextOfTheScope) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage from="caster.attack"/><scale from="caster.speed"/><critical from="caster.alive"/><label from="caster.title"/><element from="caster.element"/>
                            <aim from="caster.velocity"/><tint from="caster.tint"/><spot from="caster.spawn"/>)",
                     skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.Reports().empty()) << load.Reports().c_str();
        EXPECT_TRUE(skill.damage.IsSlot());
        EXPECT_EQ(skill.damage.Path(), "caster.attack"_intern);
        EXPECT_EQ(&skill.damage.Get(world.context), &world.caster.attack);
        EXPECT_EQ(&skill.scale.Get(world.context), &world.caster.speed);
        EXPECT_EQ(&skill.critical.Get(world.context), &world.caster.alive);
        EXPECT_EQ(&skill.label.Get(world.context), &world.caster.title);
        EXPECT_EQ(&skill.element.Get(world.context), &world.caster.element);
        EXPECT_EQ(&skill.aim.Get(world.context), &world.caster.velocity);
        EXPECT_EQ(&skill.tint.Get(world.context), &world.caster.tint);
        EXPECT_EQ(&skill.spot.Get(world.context), &world.caster.spawn);
    }

    TEST(ValueAccessorTest, AnObjectLiteralKeepsItsOwnElementAndFromReplacesIt) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<aim x="4" y="5" z="6"/><spot from="caster.spawn"/>)", skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.Reports().empty()) << load.Reports().c_str();
        EXPECT_TRUE(skill.aim.IsConst());
        EXPECT_EQ(skill.aim.GetConst(), (Vec3{4.0f, 5.0f, 6.0f}));
        EXPECT_TRUE(skill.spot.IsSlot());
    }

    TEST(ValueAccessorTest, PlainAttributesLoadWithAScopeToo) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load(kPlainAttributes, kPlainChildren, skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.Reports().empty()) << load.Reports().c_str();
        EXPECT_TRUE(skill.damage.IsConst());
        EXPECT_EQ(skill.damage.GetConst(), 12);
    }

    // =========================================================================
    // Errors: the field stays as it was, the report names the path
    // =========================================================================

    TEST(ValueAccessorTest, AFromWithoutAScopeIsAnErrorAndKeepsTheField) {
        ValueSkill skill;
        XmlLoad load("", R"(<damage from="caster.attack"/>)", skill);
        EXPECT_TRUE(load.reader.HasErrors());
        const String reports = load.Reports();
        EXPECT_TRUE(HasText(reports, "State.damage.from")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "No context is declared")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "caster.attack")) << reports.c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, ATypeMismatchNamesThePathTheFieldAndBothTypes) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage from="caster.speed"/>)", skill, {.declaration = &world.declaration});
        const String reports = load.Reports();
        EXPECT_TRUE(load.reader.HasErrors());
        EXPECT_TRUE(HasText(reports, "State.damage.from")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "caster.speed")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "speed")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "float")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "int32")) << reports.c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, AnOpaqueFieldOfAnotherTypeIsAMismatch) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<aim from="caster.position"/>)", skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.reader.HasErrors());
        EXPECT_EQ(skill.aim.GetConst(), (Vec3{9.0f, 9.0f, 9.0f}));
        EXPECT_TRUE(skill.aim.IsConst());
    }

    TEST(ValueAccessorTest, AnUnknownLevelAndAnUnknownFieldAreReportedWithTheNames) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad levelLoad("", R"(<damage from="castr.attack"/>)", skill, {.declaration = &world.declaration});
        const String level = levelLoad.Reports();
        EXPECT_TRUE(HasText(level, "caster")) << level.c_str();
        EXPECT_TRUE(HasText(level, "castr")) << level.c_str();

        ValueSkill other;
        XmlLoad fieldLoad("", R"(<damage from="caster.attak"/>)", other, {.declaration = &world.declaration});
        const String field = fieldLoad.Reports();
        EXPECT_TRUE(HasText(field, "attak")) << field.c_str();
        EXPECT_TRUE(HasText(field, "attack")) << field.c_str();
        ExpectDefaultDamage(skill);
        ExpectDefaultDamage(other);
    }

    TEST(ValueAccessorTest, APathBehindAListIsNotFixedAndIsReported) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage from="caster.buffs[0]"/>)", skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.reader.HasErrors());
        EXPECT_TRUE(HasText(load.Reports(), "buffs")) << load.Reports().c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, AValueAndAFromTogetherAreAnError) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage value="3" from="caster.attack"/>)", skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.reader.HasErrors());
        EXPECT_TRUE(HasText(load.Reports(), "not both")) << load.Reports().c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, AnElementWithNeitherValueNorFromIsAnError) {
        ValueSkill skill;
        XmlLoad load("", R"(<damage/>)", skill);
        EXPECT_TRUE(load.reader.HasErrors());
        EXPECT_TRUE(HasText(load.Reports(), "State.damage.value")) << load.Reports().c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, AnEmptyFromIsAnError) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage from=""/>)", skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.reader.HasErrors());
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, ABadLiteralInTheObjectFormIsAnErrorUnderStrictValues) {
        ValueSkill skill;
        XmlLoad load("", R"(<damage value="abc"/>)", skill, {.strict = true});
        EXPECT_TRUE(load.reader.HasErrors());
        EXPECT_TRUE(HasText(load.Reports(), "State.damage.value")) << load.Reports().c_str();
        ExpectDefaultDamage(skill);

        ValueSkill attribute;
        XmlLoad plain("damage=\"abc\"", "", attribute, {.strict = true});
        EXPECT_TRUE(plain.reader.HasErrors());
        ExpectDefaultDamage(attribute);
    }

    TEST(ValueAccessorTest, TheKindEntryIsCheckedAndAProgramNeedsAFormula) {
        const ValueWorld world;
        ValueSkill program;
        XmlLoad programLoad("", R"(<damage kind="Program" value="1"/>)", program, {.declaration = &world.declaration});
        EXPECT_TRUE(HasText(programLoad.Reports(), "State.damage.expr")) << programLoad.Reports().c_str();
        EXPECT_TRUE(HasText(programLoad.Reports(), "needs the formula in 'expr'")) << programLoad.Reports().c_str();
        ExpectDefaultDamage(program);

        ValueSkill unknown;
        XmlLoad unknownLoad("", R"(<damage kind="Formula" value="1"/>)", unknown, {.declaration = &world.declaration});
        EXPECT_TRUE(HasText(unknownLoad.Reports(), "Formula")) << unknownLoad.Reports().c_str();
        ExpectDefaultDamage(unknown);

        ValueSkill slotWithoutPath;
        XmlLoad slotLoad("", R"(<damage kind="Slot" value="1"/>)", slotWithoutPath, {.declaration = &world.declaration});
        EXPECT_TRUE(slotLoad.reader.HasErrors());
        ExpectDefaultDamage(slotWithoutPath);

        ValueSkill constWithPath;
        XmlLoad constLoad("", R"(<damage kind="Const" from="caster.attack"/>)", constWithPath, {.declaration = &world.declaration});
        EXPECT_TRUE(constLoad.reader.HasErrors());
        ExpectDefaultDamage(constWithPath);

        ValueSkill good;
        XmlLoad goodLoad("", R"(<damage kind="Const" value="5"/><scale kind="Slot" from="caster.speed"/>)", good, {.declaration = &world.declaration});
        EXPECT_TRUE(goodLoad.Reports().empty()) << goodLoad.Reports().c_str();
        EXPECT_EQ(good.damage.GetConst(), 5);
        EXPECT_TRUE(good.scale.IsSlot());
    }

    TEST(ValueAccessorTest, AFailedLoadOfOneFieldDoesNotStopTheOthers) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage from="caster.speed"/><scale from="caster.speed"/>)", skill, {.declaration = &world.declaration});
        EXPECT_TRUE(load.reader.HasErrors());
        ExpectDefaultDamage(skill);
        EXPECT_TRUE(skill.scale.IsSlot());
    }

    TEST(ValueAccessorTest, TheInnerScopeIsTheOneALoadLinksAgainst) {
        const ValueWorld world;
        ContextDecl other;
        ASSERT_TRUE(other.Add<ValueCastInfo>("cast").has_value());
        const ValueLinkScope outer(world.declaration);
        ASSERT_EQ(ValueLinkScope::Current(), &world.declaration);
        {
            const ValueLinkScope inner(other);
            EXPECT_EQ(ValueLinkScope::Current(), &other);
            ValueSkill skill;
            XmlLoad load("", R"(<damage from="caster.attack"/>)", skill);
            EXPECT_TRUE(load.reader.HasErrors());
        }
        EXPECT_EQ(ValueLinkScope::Current(), &world.declaration);
    }

    TEST(ValueAccessorTest, NoScopeIsOpenOutsideALoad) {
        EXPECT_EQ(ValueLinkScope::Current(), nullptr);
    }

    // =========================================================================
    // Diagnostics: from, value and kind are known keys, a typo beside them is a finding
    // =========================================================================

    TEST(ValueAccessorTest, EverySpellingIsCleanUnderDiagnostics) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load(R"(damage="3")", R"(<scale value="2"/><critical from="caster.alive"/><element kind="Const" value="Fire"/><aim x="1" y="2" z="3"/><spot from="caster.spawn"/>)", skill,
                     {.diagnostics = DiagnosticsMode::Error, .declaration = &world.declaration});
        EXPECT_TRUE(load.Reports().empty()) << load.Reports().c_str();
    }

    TEST(ValueAccessorTest, ATypoBesideValueOrFromIsAFinding) {
        const ValueWorld world;
        ValueSkill skill;
        XmlLoad load("", R"(<damage value="3" vlue="4"/><scale from="caster.speed" fro="x"/>)", skill, {.diagnostics = DiagnosticsMode::Warn, .declaration = &world.declaration});
        EXPECT_FALSE(load.reader.HasErrors()) << load.Reports().c_str();
        const String reports = load.Reports();
        EXPECT_TRUE(HasText(reports, "vlue")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "fro")) << reports.c_str();
        EXPECT_EQ(skill.damage.GetConst(), 3);
        EXPECT_TRUE(skill.scale.IsSlot());
    }

    // =========================================================================
    // JSON: a member that is a plain scalar is the literal, as in XML and binary
    // =========================================================================

    TEST(ValueAccessorTest, JsonReadsAPlainScalarAsTheLiteral) {
        ValueSkill skill;
        const String errors = LoadJson(R"({"damage":12,"scale":0.5,"critical":true,"label":"Hero","element":"Fire","aim":{"x":1,"y":2,"z":3}})", skill, nullptr);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        EXPECT_EQ(skill.damage.GetConst(), 12);
        EXPECT_EQ(skill.scale.GetConst(), 0.5f);
        EXPECT_TRUE(skill.critical.GetConst());
        EXPECT_EQ(skill.label.GetConst(), "Hero"_intern);
        EXPECT_EQ(skill.element.GetConst(), ValueElement::Fire);
        EXPECT_EQ(skill.aim.GetConst(), (Vec3{1.0f, 2.0f, 3.0f}));
    }

    TEST(ValueAccessorTest, JsonReadsTheObjectFormWithValue) {
        ValueSkill skill;
        const String errors = LoadJson(R"({"damage":{"value":12},"scale":{"kind":"Const","value":0.5},"label":{"value":"Hero"},"element":{"value":"Fire"}})", skill, nullptr);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        EXPECT_TRUE(skill.damage.IsConst());
        EXPECT_EQ(skill.damage.GetConst(), 12);
        EXPECT_EQ(skill.scale.GetConst(), 0.5f);
        EXPECT_EQ(skill.label.GetConst(), "Hero"_intern);
        EXPECT_EQ(skill.element.GetConst(), ValueElement::Fire);
    }

    TEST(ValueAccessorTest, JsonReadsFromAgainstTheScope) {
        const ValueWorld world;
        ValueSkill skill;
        const String errors = LoadJson(R"({"damage":{"from":"caster.attack"},"aim":{"from":"caster.velocity"},"label":{"from":"caster.title"},"scale":0.25})", skill, &world.declaration);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        EXPECT_EQ(&skill.damage.Get(world.context), &world.caster.attack);
        EXPECT_EQ(&skill.aim.Get(world.context), &world.caster.velocity);
        EXPECT_EQ(&skill.label.Get(world.context), &world.caster.title);
        EXPECT_TRUE(skill.scale.IsConst());
        EXPECT_EQ(skill.scale.GetConst(), 0.25f);
    }

    TEST(ValueAccessorTest, JsonFromWithoutAScopeIsAnErrorAndKeepsTheField) {
        ValueSkill skill;
        const String errors = LoadJson(R"({"damage":{"from":"caster.attack"}})", skill, nullptr);
        EXPECT_TRUE(HasText(errors, "damage.from")) << errors.c_str();
        EXPECT_TRUE(HasText(errors, "No context is declared")) << errors.c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, JsonABadShapeIsAnErrorThatKeepsTheField) {
        // A text for a number, a fraction for an integer, a list and an object that holds neither value nor from
        for (const StringView member : {R"("damage":"12")", R"("damage":1.5)", R"("damage":[12])", R"("damage":{})", R"("damage":null)"}) {
            ValueSkill skill;
            const String errors = LoadJson(Format("{{{}}}", member), skill, nullptr);
            EXPECT_TRUE(HasText(errors, "damage")) << String(member).c_str() << " -> " << errors.c_str();
            ExpectDefaultDamage(skill);
        }
        ValueSkill skill;
        const String errors = LoadJson(R"({"aim":5,"scale":"x"})", skill, nullptr);
        EXPECT_TRUE(HasText(errors, "aim")) << errors.c_str();
        EXPECT_TRUE(HasText(errors, "scale")) << errors.c_str();
        EXPECT_EQ(skill.aim.GetConst(), (Vec3{9.0f, 9.0f, 9.0f}));
        EXPECT_EQ(skill.scale.GetConst(), 1.0f);
    }

    TEST(ValueAccessorTest, JsonAMissingMemberIsNoError) {
        ValueSkill skill;
        const String errors = LoadJson("{}", skill, nullptr);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        ExpectDefaultDamage(skill);
    }

    TEST(ValueAccessorTest, JsonAWronglyTypedObjectOfAPlainFieldStillReportsAsBefore) {
        ValuePlainSkill plain;
        const String errors = LoadJson(R"({"aim":5})", plain, nullptr);
        EXPECT_EQ(errors, "aim: Invalid object value '5'\n");
        EXPECT_EQ(plain.aim, (Vec3{9.0f, 9.0f, 9.0f}));
    }

    TEST(ValueAccessorTest, TheJsonRoundTripOfEveryKindIsEqual) {
        const ValueWorld world;
        const ValueSkill skill = MakeMixedSkill(world);
        JsonSerializer serializer;
        skill.Serialize(serializer);
        const Json document = serializer.Take();

        ValueSkill loaded;
        JsonDeserializer reader(document);
        {
            const ValueLinkScope scope(world.declaration);
            loaded.Deserialize(reader);
        }
        EXPECT_FALSE(reader.HasErrors());
        ExpectSameSkill(loaded, skill);
    }

    TEST(ValueAccessorTest, JsonDiagnosticsKnowTheValueKeys) {
        const ValueWorld world;
        const auto parsed = ParseJson(R"({"damage":12,"scale":{"value":2},"critical":{"from":"caster.alive"},"label":{"value":"x","vlue":"y"}})");
        ASSERT_TRUE(parsed.has_value());
        JsonDeserializer reader(*parsed);
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ValueSkill skill;
        {
            const ValueLinkScope scope(world.declaration);
            skill.Deserialize(reader);
        }
        reader.FinishLoad();
        String warnings;
        for (const DeserializeError& warning : reader.GetWarnings()) {
            AppendFormat(warnings, "{}: {}\n", warning.path, warning.errorMessage);
        }
        EXPECT_TRUE(HasText(warnings, "vlue")) << warnings.c_str();
        EXPECT_FALSE(HasText(warnings, "damage")) << warnings.c_str();
        EXPECT_EQ(reader.GetWarnings().size(), 1u) << warnings.c_str();
    }

    // =========================================================================
    // Saving
    // =========================================================================

    TEST(ValueAccessorTest, AConstScalarIsSavedAsThePlainAttributeItWasLoadedFrom) {
        ValueSkill skill;
        XmlLoad load(kPlainAttributes, kPlainChildren, skill);
        const String saved = SaveXml(skill);
        EXPECT_TRUE(HasText(saved, "damage=\"12\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "label=\"Hero\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "element=\"Fire\"")) << saved.c_str();
        EXPECT_FALSE(HasText(saved, "<damage")) << saved.c_str();
        EXPECT_FALSE(HasText(saved, "value=")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "<aim")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "x=\"1\"")) << saved.c_str();

        // Saved the same way as the plain struct of the same data
        ValuePlainSkill plain;
        XmlLoad plainLoad(kPlainAttributes, kPlainChildren, plain);
        EXPECT_EQ(saved, SaveXml(plain));
    }

    TEST(ValueAccessorTest, ASlotIsSavedAsFromWithItsPath) {
        const ValueWorld world;
        const ValueSkill skill = MakeMixedSkill(world);
        const String saved = SaveXml(skill);
        EXPECT_TRUE(HasText(saved, "<scale from=\"caster.speed\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "<aim from=\"caster.velocity\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "<spot from=\"caster.spawn\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "damage=\"12\"")) << saved.c_str();
    }

    TEST(ValueAccessorTest, TheXmlRoundTripOfEveryKindIsEqual) {
        const ValueWorld world;
        const ValueSkill skill = MakeMixedSkill(world);
        const String saved = SaveXml(skill);

        ValueSkill loaded;
        // Parse the saved document as it is: <root><State .../></root>
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(saved)) << saved.c_str();
        ASSERT_TRUE(reader.BeginObject(kState));
        {
            const ValueLinkScope scope(world.declaration);
            loaded.Deserialize(reader);
        }
        reader.EndObject();
        EXPECT_FALSE(reader.HasErrors());
        ExpectSameSkill(loaded, skill);
    }

    TEST(ValueAccessorTest, TheBinaryRoundTripOfEveryKindIsEqual) {
        const ValueWorld world;
        const ValueSkill skill = MakeMixedSkill(world);
        const Vector<uint8_t> data = SaveBinary(skill);

        ValueSkill loaded;
        const String errors = LoadBinary(data, loaded, &world.declaration);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        ExpectSameSkill(loaded, skill);
    }

    TEST(ValueAccessorTest, ABinarySlotWithoutAScopeIsAnErrorAndKeepsTheField) {
        const ValueWorld world;
        const ValueSkill skill = MakeMixedSkill(world);
        const Vector<uint8_t> data = SaveBinary(skill);

        ValueSkill loaded;
        const String errors = LoadBinary(data, loaded, nullptr);
        EXPECT_TRUE(HasText(errors, "No context is declared")) << errors.c_str();
        // The scalar literal still loads, the slots are what the struct had
        EXPECT_EQ(loaded.damage.GetConst(), 12);
        EXPECT_TRUE(loaded.scale.IsConst());
        EXPECT_EQ(loaded.scale.GetConst(), 1.0f);
        EXPECT_TRUE(loaded.aim.IsConst());
    }

    TEST(ValueAccessorTest, AProgramWithoutAFormulaOrAnUnknownKindInBinaryIsAnErrorThatKeepsTheField) {
        for (const StringView kind : {StringView("Program"), StringView("Formula")}) {
            BinarySerializer serializer;
            ASSERT_TRUE(serializer.BeginObject(kState));
            ASSERT_TRUE(serializer.BeginObject("damage"));
            serializer.WriteAttribute("kind", kind);
            serializer.WriteAttribute("value", 5);
            serializer.EndObject();
            serializer.EndObject();

            ValueSkill loaded;
            const String errors = LoadBinary(serializer.GetData(), loaded, nullptr);
            EXPECT_FALSE(errors.empty()) << String(kind).c_str();
            EXPECT_TRUE(HasText(errors, "kind")) << errors.c_str();
            ExpectDefaultDamage(loaded);
        }
    }

    TEST(ValueAccessorTest, ABinaryObjectFormWithValueIsAConst) {
        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject(kState));
        ASSERT_TRUE(serializer.BeginObject("damage"));
        serializer.WriteAttribute("value", 5);
        serializer.EndObject();
        serializer.EndObject();

        ValueSkill loaded;
        const String errors = LoadBinary(serializer.GetData(), loaded, nullptr);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        EXPECT_TRUE(loaded.damage.IsConst());
        EXPECT_EQ(loaded.damage.GetConst(), 5);
    }

    // =========================================================================
    // The type: opaque to the reflection, a compound to the inspector
    // =========================================================================

    namespace {

        /// Records what an edit of a value shows: compounds and numbers, and adds one to every int32_t
        class TraceVisitor final : public IPropertyVisitor {
        public:
            String trace;

            bool Visit(StringView, bool&) override {
                return false;
            }
            bool Visit(StringView, int8_t&) override {
                return false;
            }
            bool Visit(StringView, uint8_t&) override {
                return false;
            }
            bool Visit(StringView, int16_t&) override {
                return false;
            }
            bool Visit(StringView, uint16_t&) override {
                return false;
            }
            bool Visit(StringView name, int32_t& value) override {
                AppendFormat(trace, "{}={} ", name, value);
                ++value;
                return true;
            }
            bool Visit(StringView, uint32_t&) override {
                return false;
            }
            bool Visit(StringView, int64_t&) override {
                return false;
            }
            bool Visit(StringView, uint64_t&) override {
                return false;
            }
            bool Visit(StringView name, float& value) override {
                AppendFormat(trace, "{}={} ", name, value);
                return false;
            }
            bool Visit(StringView, double&) override {
                return false;
            }
            bool Visit(StringView, PoolString&) override {
                return false;
            }
            bool Visit(StringView, String&) override {
                return false;
            }
            bool BeginCompound(StringView name) override {
                AppendFormat(trace, "<{}> ", name);
                return true;
            }
        };

    }  // namespace

    TEST(ValueAccessorTest, TheInspectorSeesTheLiteralOfAConstUnderTheNameValue) {
        Value<int32_t> value = 12;
        TraceVisitor visitor;
        EXPECT_TRUE(PropVisit::Visit(visitor, value, "damage"));
        EXPECT_EQ(visitor.trace, "<damage> value=12 ");
        EXPECT_EQ(value.GetConst(), 13);
        EXPECT_TRUE(value.IsConst());
    }

    TEST(ValueAccessorTest, TheInspectorSeesNothingOfASlot) {
        const ValueWorld world;
        Value<int32_t> value;
        ASSERT_TRUE(value.Link(world.declaration, "caster.attack").has_value());
        TraceVisitor visitor;
        EXPECT_FALSE(PropVisit::Visit(visitor, value, "damage"));
        EXPECT_EQ(visitor.trace, "<damage> ");
        EXPECT_TRUE(value.IsSlot());
    }

    TEST(ValueAccessorTest, AValueIsOpaqueAndTheTypesAreTold) {
        EXPECT_EQ(DescribeType<Value<float>>().kind, TypeKind::Opaque);
        EXPECT_TRUE(SameType(DescribeType<Value<float>>(), DescribeType<Value<float>>()));
        EXPECT_FALSE(SameType(DescribeType<Value<float>>(), DescribeType<Value<double>>()));
        EXPECT_FALSE(SameType(DescribeType<Value<float>>(), DescribeType<Value<Vec3>>()));
        EXPECT_FALSE(SameType(DescribeType<Value<Vec2>>(), DescribeType<Value<Vec3>>()));
        EXPECT_FALSE(SameType(DescribeType<Value<float>>(), DescribeType<float>()));
    }

}  // namespace BECore::Tests
