// The `<context>` element of a document (Values/ContextDeclAccessor.h) and the scope it lands in (Values/ValueLinkScope.h): the list form, the
// shorthand, binary and JSON, the values that link against what a document declared, the refusals, and what a scope leaves behind.
// The reflected document types are those of ValueTestTypes.h (their generated code is included in ValueContextTest.cpp).

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "units/skill.xml";

        constexpr StringView kListContext = R"(<context><level name="caster" type="ValueUnitStats"/><level name="cast" type="ValueCastInfo"/></context>)";
        constexpr StringView kShorthandContext = R"(<context caster="ValueUnitStats" cast="ValueCastInfo"/>)";
        constexpr StringView kMixedContext = R"(<context caster="ValueUnitStats"><level name="cast" type="ValueCastInfo"/></context>)";
        constexpr StringView kValues = R"(<damage from="caster.attack"/><power from="cast.power"/><aim from="caster.velocity"/>)";

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// Path and message of every error, then of every warning, one text
        String ReportsOf(const IDeserializer& reader) {
            String out;
            for (const Vector<DeserializeError>* list : {&reader.GetErrors(), &reader.GetWarnings()}) {
                for (const DeserializeError& report : *list) {
                    AppendFormat(out, "{}: {}\n", report.path.ToStringView(), report.errorMessage.ToStringView());
                }
            }
            return out;
        }

        /// Loads `<root><State>@p children</State></root>` into @p target; the caller has the link scope open. Every error and warning, one text
        template <typename Struct>
        String LoadXml(StringView children, Struct& target, DiagnosticsMode diagnostics = DiagnosticsMode::Off, bool strict = false) {
            XmlDeserializer reader;
            reader.SetDiagnostics(diagnostics);
            reader.SetStrictValues(strict);
            const String xml = Format("<root><State>{}</State></root>", children);
            EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        template <typename Struct>
        String SaveXml(const Struct& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject(kState)) {
                value.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        /// The objects a loaded document's declaration describes, put into a context over that declaration
        struct Objects {
            ValueUnitStats caster;
            ValueCastInfo cast;
            Context context;

            explicit Objects(const ContextDecl& declaration) : context(declaration) {
                context.Set(0, caster);
                context.Set(1, cast);
            }
        };

        /// The document's values read the very members of the objects of a context over its declaration
        void ExpectValuesReadTheMembers(const ValueDocument& document) {
            ASSERT_EQ(document.context.Size(), 2U);
            EXPECT_EQ(document.context.Levels()[0].name, "caster");
            EXPECT_EQ(document.context.Levels()[1].name, "cast");
            const Objects objects(document.context);
            ASSERT_TRUE(document.damage.IsSlot());
            ASSERT_TRUE(document.power.IsSlot());
            ASSERT_TRUE(document.aim.IsSlot());
            EXPECT_EQ(&document.damage.Get(objects.context), &objects.caster.attack);
            EXPECT_EQ(&document.power.Get(objects.context), &objects.cast.power);
            EXPECT_EQ(&document.aim.Get(objects.context), &objects.caster.velocity);
        }

        /// A refusal leaves the field as it was and gives the scope no declaration
        void ExpectNothingInstalled(const ValueDocument& document) {
            EXPECT_EQ(document.context.Size(), 0U);
            EXPECT_EQ(ValueLinkScope::Current(), nullptr);
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

        /// A document with the declaration of the two levels and the three values linked, made from code
        ValueDocument MakeDocument() {
            ValueDocument document;
            EXPECT_TRUE(document.context.Add<ValueUnitStats>("caster").has_value());
            EXPECT_TRUE(document.context.Add<ValueCastInfo>("cast").has_value());
            EXPECT_TRUE(document.damage.Link(document.context, "caster.attack").has_value());
            EXPECT_TRUE(document.power.Link(document.context, "cast.power").has_value());
            EXPECT_TRUE(document.aim.Link(document.context, "caster.velocity").has_value());
            return document;
        }

    }  // namespace

    // =========================================================================
    // The forms
    // =========================================================================

    TEST(ValueContextLoadTest, TheListFormDeclaresTheLevelsTheValuesLinkAgainst) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kListContext, kValues), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectValuesReadTheMembers(document);
    }

    TEST(ValueContextLoadTest, TheShorthandDeclaresTheSameLevels) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kShorthandContext, kValues), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectValuesReadTheMembers(document);
    }

    TEST(ValueContextLoadTest, AttributesComeBeforeTheLevelChildrenWhenBothAreThere) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kMixedContext, kValues), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectValuesReadTheMembers(document);
    }

    TEST(ValueContextLoadTest, TheShorthandKeepsTheOrderOfTheAttributes) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context cast="ValueCastInfo" caster="ValueUnitStats"/>)", document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_EQ(document.context.Size(), 2U);
        EXPECT_EQ(document.context.Levels()[0].name, "cast");
        EXPECT_EQ(document.context.Levels()[1].name, "caster");
    }

    TEST(ValueContextLoadTest, ADerivedTypeIsAcceptedAsALevelType) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context hero="ValueHeroStats"/><damage from="hero.renown"/>)", document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        EXPECT_TRUE(document.damage.IsSlot());
    }

    TEST(ValueContextLoadTest, BinaryReadsTheListForm) {
        const ValueDocument original = MakeDocument();
        const Vector<uint8_t> data = SaveBinary(original);

        ValueDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(data));
        ASSERT_TRUE(reader.BeginObject(kState));
        loaded.Deserialize(reader);
        reader.EndObject();
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectValuesReadTheMembers(loaded);
    }

    TEST(ValueContextLoadTest, JsonReadsTheListFormAndAPlainScalar) {
        const auto parsed = ParseJson(R"({"context": [{"name": "caster", "type": "ValueUnitStats"}, {"name": "cast", "type": "ValueCastInfo"}],
                                          "damage": {"from": "caster.attack"}, "power": {"from": "cast.power"}, "aim": {"from": "caster.velocity"}})");
        ASSERT_TRUE(parsed.has_value());
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        JsonDeserializer reader(*parsed);
        document.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectValuesReadTheMembers(document);
    }

    TEST(ValueContextLoadTest, JsonHasNoShorthand) {
        const auto parsed = ParseJson(R"({"context": {"caster": "ValueUnitStats"}, "damage": 3})");
        ASSERT_TRUE(parsed.has_value());
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        JsonDeserializer reader(*parsed);
        document.Deserialize(reader);
        EXPECT_TRUE(reader.HasErrors());
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, ALoadedDocumentSavesTheListFormAndLoadsAgainEqual) {
        ValueDocument first;
        {
            const ValueLinkScope scope(kDocumentName);
            ASSERT_TRUE(LoadXml(Format("{}{}", kShorthandContext, kValues), first).empty());
        }
        const String saved = SaveXml(first);
        EXPECT_TRUE(HasText(saved, R"(<level name="caster" type="ValueUnitStats" />)")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, R"(<level name="cast" type="ValueCastInfo" />)")) << saved.c_str();
        EXPECT_FALSE(HasText(saved, R"(<context caster=)")) << saved.c_str();

        ValueDocument second;
        const ValueLinkScope scope(kDocumentName);
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(saved));
        ASSERT_TRUE(reader.BeginObject(kState));
        second.Deserialize(reader);
        reader.EndObject();
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectValuesReadTheMembers(second);
        EXPECT_EQ(second.damage.Path(), first.damage.Path());
    }

    TEST(ValueContextLoadTest, ADeclarationWithNoLevelSavesNothing) {
        const ValueDocument document;
        EXPECT_FALSE(HasText(SaveXml(document), "context")) << SaveXml(document).c_str();
    }

    TEST(ValueContextLoadTest, StrictModeAndTheDiagnosticsKnowTheKeysOfTheElement) {
        for (const StringView context : {kListContext, kShorthandContext, kMixedContext}) {
            for (const bool strict : {false, true}) {
                ValueDocument document;
                const ValueLinkScope scope(kDocumentName);
                const String reports = LoadXml(Format("{}{}", context, kValues), document, DiagnosticsMode::Warn, strict);
                EXPECT_TRUE(reports.empty()) << reports.c_str();
            }
        }
    }

    TEST(ValueContextLoadTest, AStrayKeyOfTheElementIsStillReportedInTheListForm) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context><level name="caster" type="ValueUnitStats" colour="red"/></context>)", document, DiagnosticsMode::Warn);
        EXPECT_TRUE(HasText(reports, "colour")) << reports.c_str();
    }

    // =========================================================================
    // Refusals: each leaves the field and installs nothing
    // =========================================================================

    TEST(ValueContextLoadTest, AnUnknownTypeNamesTheTypeAndWhatItMeant) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context><level name="caster" type="ValueUnitStat"/></context>)", document);
        EXPECT_TRUE(HasText(reports, "context[0].type")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "'ValueUnitStat'")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "did you mean 'ValueUnitStats'")) << reports.c_str();
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, AnUnknownTypeInTheShorthandIsReportedUnderTheAttribute) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context caster="ValueNoSuchType"/>)", document);
        EXPECT_TRUE(HasText(reports, "context.caster")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "'ValueNoSuchType'")) << reports.c_str();
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, ADuplicateLevelIsRefused) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context caster="ValueUnitStats"><level name="caster" type="ValueCastInfo"/></context>)", document);
        EXPECT_TRUE(HasText(reports, "context[0].name")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "already has a level 'caster'")) << reports.c_str();
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, ANinthLevelIsRefused) {
        String levels;
        for (size_t i = 0; i <= kMaxContextLevels; ++i) {
            AppendFormat(levels, R"(<level name="level{}" type="ValueCastInfo"/>)", i);
        }
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("<context>{}</context>", levels), document);
        EXPECT_TRUE(HasText(reports, Format("context[{}].name", kMaxContextLevels))) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "level8")) << reports.c_str();
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, AnEmptyNameOrAnEmptyTypeIsRefused) {
        for (const StringView level : {R"(<level name="" type="ValueUnitStats"/>)", R"(<level type="ValueUnitStats"/>)", R"(<level name="caster" type=""/>)", R"(<level name="caster"/>)"}) {
            ValueDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("<context>{}</context>", level), document);
            EXPECT_TRUE(HasText(reports, "context[0]")) << level.data() << "\n" << reports.c_str();
            ExpectNothingInstalled(document);
        }
    }

    TEST(ValueContextLoadTest, AnEmptyTypeInTheShorthandIsRefused) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<context caster=""/>)", document);
        EXPECT_TRUE(HasText(reports, "needs a type")) << reports.c_str();
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, AFailedDeclarationLeavesTheValuesWithoutAContext) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", R"(<context caster="ValueNoSuchType"/>)", kValues), document);
        EXPECT_TRUE(HasText(reports, "No context is declared")) << reports.c_str();
        EXPECT_TRUE(document.damage.IsConst());
        EXPECT_EQ(document.damage.GetConst(), 7);
    }

    TEST(ValueContextLoadTest, ASecondContextInOneScopeIsRefused) {
        ValueDocument first;
        ValueDocument second;
        const ValueLinkScope scope(kDocumentName);
        EXPECT_TRUE(LoadXml(kListContext, first).empty());
        const String reports = LoadXml(R"(<context hero="ValueHeroStats"/>)", second);
        EXPECT_TRUE(HasText(reports, "already declares a context")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, kDocumentName)) << reports.c_str();
        EXPECT_EQ(second.context.Size(), 0U);
        EXPECT_EQ(first.context.Size(), 2U);
        ASSERT_NE(ValueLinkScope::Current(), nullptr);
        EXPECT_EQ(ValueLinkScope::Current()->Size(), 2U);
    }

    TEST(ValueContextLoadTest, AContextWithNoScopeOpenIsRefused) {
        ValueDocument document;
        const String reports = LoadXml(kListContext, document);
        EXPECT_TRUE(HasText(reports, "no link scope is open")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "MakeFromConfig")) << reports.c_str();
        ExpectNothingInstalled(document);
    }

    TEST(ValueContextLoadTest, AContextUnderAScopeOpenedFromCodeWithADeclarationIsRefused) {
        ContextDecl fromCode;
        ASSERT_TRUE(fromCode.Add<ValueCastInfo>("cast").has_value());
        const ValueLinkScope scope(fromCode);
        ValueDocument document;
        const String reports = LoadXml(kListContext, document);
        EXPECT_TRUE(HasText(reports, "already declares a context")) << reports.c_str();
        EXPECT_EQ(document.context.Size(), 0U);
        EXPECT_EQ(ValueLinkScope::Current(), &fromCode);
    }

    TEST(ValueContextLoadTest, AValueReadBeforeTheContextHasNoContextAndTheErrorNamesTheDocument) {
        ValueLateDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", R"(<damage from="caster.attack"/>)", kListContext), document);
        EXPECT_TRUE(HasText(reports, "No context is declared")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, kDocumentName)) << reports.c_str();
        EXPECT_TRUE(document.damage.IsConst());
        EXPECT_EQ(document.context.Size(), 2U);
    }

    // =========================================================================
    // What a scope leaves behind and what it shows
    // =========================================================================

    TEST(ValueContextLoadTest, NothingLeaksPastTheScope) {
        ValueDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            EXPECT_EQ(ValueLinkScope::Current(), nullptr);
            EXPECT_TRUE(LoadXml(kListContext, document).empty());
            ASSERT_NE(ValueLinkScope::Current(), nullptr);
            EXPECT_EQ(ValueLinkScope::Current()->Size(), 2U);
            EXPECT_EQ(ValueLinkScope::DocumentName(), kDocumentName);
        }
        EXPECT_EQ(ValueLinkScope::Current(), nullptr);
        EXPECT_TRUE(ValueLinkScope::DocumentName().empty());

        // The next document in a scope of its own does not see the last one's context
        ValueDocument next;
        const ValueLinkScope other("other.xml");
        EXPECT_EQ(ValueLinkScope::Current(), nullptr);
        const String reports = LoadXml(R"(<damage from="caster.attack"/>)", next);
        EXPECT_TRUE(HasText(reports, "No context is declared")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "other.xml")) << reports.c_str();
        EXPECT_TRUE(next.damage.IsConst());
    }

    TEST(ValueContextLoadTest, TheScopeKeepsItsOwnCopyOfTheDeclaration) {
        const ValueLinkScope scope(kDocumentName);
        {
            ValueDocument document;
            EXPECT_TRUE(LoadXml(kListContext, document).empty());
        }
        const ContextDecl* declared = ValueLinkScope::Current();
        ASSERT_NE(declared, nullptr);
        ASSERT_EQ(declared->Size(), 2U);
        EXPECT_EQ(declared->Levels()[0].name, "caster");
        EXPECT_EQ(declared->Levels()[1].type->Name(), "ValueCastInfo");
    }

    TEST(ValueContextLoadTest, AnEmptyScopeShowsTheDeclarationOfTheScopeAroundItUntilTheDocumentDeclaresItsOwn) {
        ContextDecl outerDecl;
        ASSERT_TRUE(outerDecl.Add<ValueHeroStats>("hero").has_value());
        const ValueLinkScope outer(outerDecl);
        {
            const ValueLinkScope inner(kDocumentName);
            EXPECT_EQ(ValueLinkScope::Current(), &outerDecl);

            ValueDocument usesOuter;
            EXPECT_TRUE(LoadXml(R"(<damage from="hero.renown"/>)", usesOuter).empty());
            EXPECT_TRUE(usesOuter.damage.IsSlot());

            ValueDocument shadows;
            EXPECT_TRUE(LoadXml(Format("{}{}", kListContext, R"(<damage from="caster.attack"/>)"), shadows).empty());
            EXPECT_NE(ValueLinkScope::Current(), &outerDecl);
            ASSERT_NE(ValueLinkScope::Current(), nullptr);
            EXPECT_EQ(ValueLinkScope::Current()->Size(), 2U);
            EXPECT_TRUE(shadows.damage.IsSlot());
        }
        EXPECT_EQ(ValueLinkScope::Current(), &outerDecl);
    }

    TEST(ValueContextLoadTest, TheDocumentNameIsTheInnermostNamedScopes) {
        const ValueLinkScope named("outer.xml");
        ContextDecl declaration;
        const ValueLinkScope fromCode(declaration);
        EXPECT_EQ(ValueLinkScope::DocumentName(), "outer.xml");
        const ValueLinkScope inner("inner.xml");
        EXPECT_EQ(ValueLinkScope::DocumentName(), "inner.xml");
        EXPECT_TRUE(HasText(ValueLinkScope::AtDocument("message"), "inner.xml"));
    }

    TEST(ValueContextLoadTest, ADeclarationOfOneThreadIsNotSeenByAnother) {
        ValueDocument document;
        const ValueLinkScope scope(kDocumentName);
        EXPECT_TRUE(LoadXml(kListContext, document).empty());
        bool seen = true;
        Thread thread("ValueContextLoadTest", [&seen] { seen = ValueLinkScope::Current() != nullptr; });
        thread.join();
        EXPECT_FALSE(seen);
    }

}  // namespace BECore::Tests
