// `expr` in content (Values/ValueAccessor.h): a formula read from XML, binary and JSON, compiled against the declaration of the link scope; the saved forms,
// the refusals and the line a mistake gives. The document types are those of ValueTestTypes.h (their generated code is included in ValueContextTest.cpp).

#include <BECore/Config/ConfigFactory.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <cmath>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "units/skill.xml";
        constexpr StringView kContext = R"(<context caster="ValueUnitStats" cast="ValueCastInfo"/>)";
        constexpr StringView kFormulas = R"x(<damage expr="caster.attack * (100 + cast.level * 10) / 100"/><total expr="caster.score / 3 + 1"/><power expr="lerp(caster.speed, cast.power, 0.25)"/>)x"
                                         R"x(<precise expr="caster.precise * 2 + sqrt(2.0)"/><ready expr="caster.alive &amp;&amp; cast.level &gt; 2"/>)x";

        struct Refusal {
            StringView name;
            StringView children;
            StringView path;
            StringView words;
        };

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        template <typename Reader>
        String ReportsOf(const Reader& reader) {
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
        String LoadXmlDocument(const String& xml, Struct& target) {
            XmlDeserializer reader;
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

        template <typename Struct>
        Vector<uint8_t> SaveBinary(const Struct& value) {
            BinarySerializer serializer;
            if (serializer.BeginObject(kState)) {
                value.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        template <typename Struct>
        String LoadBinary(const Vector<uint8_t>& data, Struct& target) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(data));
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        struct State {
            bool alive;
            int32_t attack;
            int64_t score;
            float speed;
            double precise;
            int32_t level;
            float power;
        };

        const State kStates[] = {
            {true, 12, 40000000000, 1.5f, 0.25, 3, 2.5f},
            {false, 0, 0, 0.0f, 0.0, 0, 0.0f},
            {true, -7, -5, -2.0f, -0.5, 1, 100.0f},
            {true, 1000, 7, 3.25f, 1e9, 20, -4.0f},
        };

        void Apply(ValueWorld& world, const State& state) {
            world.caster.alive = state.alive;
            world.caster.attack = state.attack;
            world.caster.score = state.score;
            world.caster.speed = state.speed;
            world.caster.precise = state.precise;
            world.cast.level = state.level;
            world.cast.power = state.power;
        }

        /// The document's formulas give what the same formulas give in plain C++, over the states
        template <typename Document>
        void ExpectFormulasEqualPlainCode(const Document& document) {
            ASSERT_EQ(document.context.Size(), 2U);
            ASSERT_EQ(document.damage.Kind(), ValueKind::Program);
            ASSERT_EQ(document.power.Kind(), ValueKind::Program);
            ASSERT_EQ(document.ready.Kind(), ValueKind::Program);
            ValueWorld world;
            for (const State& state : kStates) {
                Apply(world, state);
                EXPECT_EQ(document.damage.Evaluate(world.context), state.attack * (100 + state.level * 10) / 100);
                EXPECT_EQ(document.total.Evaluate(world.context), state.score / 3 + 1);
                EXPECT_EQ(document.power.Evaluate(world.context), std::lerp(state.speed, state.power, 0.25f));
                EXPECT_EQ(document.precise.Evaluate(world.context), state.precise * 2.0 + std::sqrt(2.0));
                EXPECT_EQ(document.ready.Evaluate(world.context), state.alive && state.level > 2);
            }
        }

        void ExpectDefaults(const ValueFormulaDocument& document) {
            EXPECT_TRUE(document.damage.IsConst());
            EXPECT_EQ(document.damage.GetConst(), 7);
            EXPECT_TRUE(document.total.IsConst());
            EXPECT_EQ(document.total.GetConst(), 7);
        }

        template <typename T>
        void ExpectSameFormula(const Value<T>& actual, const Value<T>& expected, const char* field) {
            ASSERT_EQ(actual.Kind(), expected.Kind()) << field;
            if (expected.Kind() == ValueKind::Program) {
                EXPECT_EQ(actual.Id(), expected.Id()) << field;
                EXPECT_EQ(actual.Path(), expected.Path()) << field;
            } else if (expected.Kind() == ValueKind::Slot) {
                EXPECT_EQ(actual.Level(), expected.Level()) << field;
                EXPECT_EQ(actual.Offset(), expected.Offset()) << field;
            } else {
                EXPECT_EQ(actual.GetConst(), expected.GetConst()) << field;
            }
        }

        void ExpectSameDocument(const ValueFormulaDocument& actual, const ValueFormulaDocument& expected) {
            EXPECT_EQ(actual.context.Size(), expected.context.Size());
            ExpectSameFormula(actual.damage, expected.damage, "damage");
            ExpectSameFormula(actual.total, expected.total, "total");
            ExpectSameFormula(actual.power, expected.power, "power");
            ExpectSameFormula(actual.precise, expected.precise, "precise");
            ExpectSameFormula(actual.ready, expected.ready, "ready");
        }

    }  // namespace

    // =========================================================================
    // Reading
    // =========================================================================

    TEST(ValueProgramLoadTest, FormulasFromXmlEvaluateLikePlainCode) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, kFormulas), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectFormulasEqualPlainCode(document);
        EXPECT_EQ(document.damage.Path(), PoolString::Intern("caster.attack * (100 + cast.level * 10) / 100"));
    }

    TEST(ValueProgramLoadTest, TheKindProgramMayBesideExpr) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, R"(<damage kind="Program" expr="caster.attack * 2"/>)"), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_EQ(document.damage.Kind(), ValueKind::Program);
        ValueWorld world;
        world.caster.attack = 21;
        EXPECT_EQ(document.damage.Evaluate(world.context), 42);
    }

    TEST(ValueProgramLoadTest, AFormulaThatFoldsLoadsAsAConstAndOneThatIsAFieldAsASlot) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, R"(<damage expr="2 * 6"/><power expr="caster.speed"/><ready expr="1 &lt; 2"/>)"), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_TRUE(document.damage.IsConst());
        EXPECT_EQ(document.damage.GetConst(), 12);
        ASSERT_TRUE(document.power.IsSlot());
        Value<float> linked;
        ASSERT_TRUE(linked.Link(document.context, "caster.speed").has_value());
        EXPECT_EQ(document.power, linked);
        ASSERT_TRUE(document.ready.IsConst());
        EXPECT_TRUE(document.ready.GetConst());
    }

    TEST(ValueProgramLoadTest, JsonReadsTheObjectWithExpr) {
        const auto parsed = ParseJson(R"x({"context": [{"name": "caster", "type": "ValueUnitStats"}, {"name": "cast", "type": "ValueCastInfo"}],
                                          "damage": {"expr": "caster.attack * (100 + cast.level * 10) / 100"}, "total": {"expr": "caster.score / 3 + 1"},
                                          "power": {"expr": "lerp(caster.speed, cast.power, 0.25)"}, "precise": {"expr": "caster.precise * 2 + sqrt(2.0)"},
                                          "ready": {"expr": "caster.alive && cast.level > 2"}})x");
        ASSERT_TRUE(parsed.has_value());
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        JsonDeserializer reader(*parsed);
        document.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectFormulasEqualPlainCode(document);
    }

    // =========================================================================
    // Saving
    // =========================================================================

    TEST(ValueProgramLoadTest, AProgramSavesItsTextAndLoadsAgainEqualInXml) {
        ValueFormulaDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            ASSERT_TRUE(LoadXml(Format("{}{}", kContext, kFormulas), document).empty());
        }
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "expr=\"caster.attack * (100 + cast.level * 10) / 100\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "<damage expr=")) << saved.c_str();

        ValueFormulaDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXmlDocument(saved, loaded);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectSameDocument(loaded, document);
        ExpectFormulasEqualPlainCode(loaded);
    }

    TEST(ValueProgramLoadTest, AProgramSavesAndLoadsAgainEqualInBinary) {
        ValueFormulaDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            ASSERT_TRUE(LoadXml(Format("{}{}", kContext, kFormulas), document).empty());
        }
        const Vector<uint8_t> data = SaveBinary(document);

        ValueFormulaDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadBinary(data, loaded);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectSameDocument(loaded, document);
        ExpectFormulasEqualPlainCode(loaded);
    }

    TEST(ValueProgramLoadTest, AFoldedFormulaSavesAsThePlainAttributeAndASlotAsFrom) {
        ValueFormulaDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            ASSERT_TRUE(LoadXml(Format("{}{}", kContext, R"(<damage expr="2 * 6"/><power expr="caster.speed"/>)"), document).empty());
        }
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "damage=\"12\"")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "<power from=\"caster.speed\"")) << saved.c_str();
        EXPECT_FALSE(HasText(saved, "expr")) << saved.c_str();

        ValueFormulaDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        EXPECT_TRUE(LoadXmlDocument(saved, loaded).empty());
        ExpectSameDocument(loaded, document);
    }

    // =========================================================================
    // Refusals: each names the field, the words that matter and leaves the field as it was
    // =========================================================================

    TEST(ValueProgramLoadTest, TheKindProgramNeedsAFormula) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, R"(<damage kind="Program"/>)"), document);
        EXPECT_TRUE(HasText(reports, "State.damage.expr")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "needs the formula in 'expr'")) << reports.c_str();
        ExpectDefaults(document);
    }

    TEST(ValueProgramLoadTest, ExprBesideFromOrValueOrAnotherKindIsAnError) {
        const Refusal refusals[] = {
            {"from", R"(<damage expr="caster.attack" from="caster.attack"/>)", "State.damage.from", "only one of them"},
            {"value", R"(<damage expr="caster.attack" value="5"/>)", "State.damage.value", "only one of them"},
            {"kind Const", R"(<damage kind="Const" expr="caster.attack"/>)", "State.damage.expr", "kind Const has a literal"},
            {"kind Slot", R"(<damage kind="Slot" expr="caster.attack"/>)", "State.damage.expr", "kind Slot has a path to read"},
            {"kind Program with from", R"(<damage kind="Program" from="caster.attack"/>)", "State.damage.expr", "needs the formula"},
        };
        for (const Refusal& refusal : refusals) {
            ValueFormulaDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}{}", kContext, refusal.children), document);
            EXPECT_TRUE(HasText(reports, refusal.path)) << String(refusal.name).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, refusal.words)) << String(refusal.name).c_str() << ": " << reports.c_str();
            ExpectDefaults(document);
        }
    }

    TEST(ValueProgramLoadTest, ExprOnATypeNoProgramComputesListsTheTypes) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, R"(<spot expr="caster.spawn"/>)"), document);
        EXPECT_TRUE(HasText(reports, "State.spot.expr")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "bool, int, int64, float, double, vec2")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "ValuePoint")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "(document 'units/skill.xml')")) << reports.c_str();
        EXPECT_EQ(document.spot.GetConst(), (ValuePoint{9, 9}));
        EXPECT_TRUE(document.spot.IsConst());
    }

    TEST(ValueProgramLoadTest, NoDeclaredContextIsTheExistingErrorWithTheDocumentName) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(R"(<damage expr="caster.attack * 2"/>)", document);
        EXPECT_TRUE(HasText(reports, "State.damage.expr")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "No context is declared")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "caster.attack * 2")) << reports.c_str();
        EXPECT_TRUE(reports.find("(document 'units/skill.xml')") != String::npos) << reports.c_str();
        ExpectDefaults(document);
    }

    TEST(ValueProgramLoadTest, AMistakeInTheFormulaGivesTheColumnTheCaretAndTheDocument) {
        struct Mistake {
            StringView formula;
            StringView words;
        };
        const Mistake mistakes[] = {
            {"caster.attak * 2", "column 1:"},  {"caster.attack * 2.5", "column 17:"},        {"caster.attack @ 2", "column 15:"},
            {"caster.attack * ", "column 17:"}, {"caster.attack + cast.power", "column 17:"}, {"", "empty"},
        };
        for (const Mistake& mistake : mistakes) {
            ValueFormulaDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}<damage expr=\"{}\"/>", kContext, mistake.formula), document);
            EXPECT_TRUE(HasText(reports, "State.damage.expr")) << String(mistake.formula).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, mistake.words)) << String(mistake.formula).c_str() << ": " << reports.c_str();
            ExpectDefaults(document);
        }
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, R"(<damage expr="caster.attak * 2"/>)"), document);
        EXPECT_TRUE(HasText(reports, "caster.attak")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "did you mean 'attack'")) << reports.c_str();

        ValueFormulaDocument shortMistake;
        const String shortReports = LoadXml(Format("<context caster=\"ValueUnitStats\"/>{}", R"(<damage expr="caster.attack @ 2"/>)"), shortMistake);
        EXPECT_TRUE(HasText(shortReports, "column 15: ")) << shortReports.c_str();
        EXPECT_TRUE(shortReports.find("(document 'units/skill.xml')") != String::npos) << shortReports.c_str();
    }

    TEST(ValueProgramLoadTest, AFailedFormulaKeepsWhatTheFieldHad) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        ASSERT_TRUE(LoadXml(Format("{}{}", kContext, R"(<damage expr="caster.attack * 2"/>)"), document).empty());
        const Value<int32_t> before = document.damage;
        ASSERT_EQ(before.Kind(), ValueKind::Program);

        // The same field is read again from a second document that has a mistake: the target is the struct above
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(R"(<root><State><damage expr="caster.attack *"/></State></root>)"));
        ASSERT_TRUE(reader.BeginObject(kState));
        document.Deserialize(reader);
        reader.EndObject();
        EXPECT_TRUE(reader.HasErrors());
        EXPECT_EQ(document.damage, before);
    }

    TEST(ValueProgramLoadTest, BinaryAndJsonFormulasWithMistakesAreErrorsThatKeepTheField) {
        ValueWorld world;
        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject(kState));
        ASSERT_TRUE(serializer.BeginObject("damage"));
        serializer.WriteAttribute("expr", StringView("caster.attack @ 2"));
        serializer.EndObject();
        serializer.EndObject();
        ValueSkill skill;
        {
            const ValueLinkScope scope(world.declaration);
            const String errors = LoadBinary(serializer.GetData(), skill);
            EXPECT_TRUE(HasText(errors, "State.damage.expr")) << errors.c_str();
            EXPECT_TRUE(HasText(errors, "column 15")) << errors.c_str();
        }
        EXPECT_TRUE(skill.damage.IsConst());
        EXPECT_EQ(skill.damage.GetConst(), 7);

        const auto parsed = ParseJson(R"({"damage": {"expr": 5}})");
        ASSERT_TRUE(parsed.has_value());
        ValueSkill fromJson;
        {
            const ValueLinkScope scope(world.declaration);
            JsonDeserializer reader(*parsed);
            fromJson.Deserialize(reader);
            EXPECT_TRUE(reader.HasErrors());
        }
        EXPECT_EQ(fromJson.damage.GetConst(), 7);
    }

    // =========================================================================
    // Keys, hostile text
    // =========================================================================

    TEST(ValueProgramLoadTest, ExprIsAKnownKeyOfTheStrictModeAndTheDiagnostics) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, kFormulas), document, DiagnosticsMode::Warn, true);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectFormulasEqualPlainCode(document);

        ValueFormulaDocument typo;
        const ValueLinkScope typoScope(kDocumentName);
        const String typoReports = LoadXml(Format("{}{}", kContext, R"(<damage exrp="caster.attack"/>)"), typo, DiagnosticsMode::Warn);
        EXPECT_TRUE(HasText(typoReports, "Unknown attribute 'exrp'")) << typoReports.c_str();
    }

    TEST(ValueProgramLoadTest, HostileTextsAreErrorsAndNotCrashes) {
        const String tooLong = String(5000, '1');
        const String tooDeep = String(200, '(') + "1" + String(200, ')');
        const String manyNodes = [] {
            String text = "1";
            for (size_t i = 0; i < 600; ++i) {
                text += " + 1";
            }
            return text;
        }();
        const StringView texts[] = {"\x01\x02", "caster.attack \xff\xfe", "[0]", "caster.attack[0]", "1e", "1..2", "((", ")", "a.b.c.d.e.f", "caster.", ".5.5", "min(", "&&", "true", tooLong, tooDeep,
                                    manyNodes};
        for (const StringView text : texts) {
            ValueFormulaDocument document;
            const ValueLinkScope scope(kDocumentName);
            XmlDeserializer reader;
            String xml = Format("<root><State>{}<damage expr=\"", kContext);
            xml.append(text.data(), text.size());
            xml += "\"/></State></root>";
            if (!reader.LoadFromString(xml)) {
                continue;  // the XML itself is refused: nothing reaches the compiler
            }
            ASSERT_TRUE(reader.BeginObject(kState));
            document.Deserialize(reader);
            reader.EndObject();
            if (reader.HasErrors()) {
                ExpectDefaults(document);
            }
        }
    }

    // =========================================================================
    // A named config
    // =========================================================================

    class ValueProgramConfigTest : public ::testing::Test {
    protected:
        static constexpr StringView kConfig = "TestsConfig";
        static constexpr StringView kConfigPrefix = "Config 'TestsConfig'";

        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            pugi::xml_node root = CoreManager::GetConfigManager().GetConfig(kConfig).GetPugiNode();
            for (const pugi::xml_node& node : _added) {
                root.remove_child(node);
            }
            _added.clear();
        }

        void Install(StringView fragment) {
            pugi::xml_node root = CoreManager::GetConfigManager().GetConfig(kConfig).GetPugiNode();
            ASSERT_TRUE(root);
            const String wrapped = Format("<fragment>{}</fragment>", fragment);
            pugi::xml_document parsed;
            ASSERT_TRUE(parsed.load_buffer(wrapped.data(), wrapped.size()));
            for (const pugi::xml_node& child : parsed.first_child().children()) {
                _added.push_back(root.append_copy(child));
            }
        }

        Vector<pugi::xml_node> _added;
    };

    TEST_F(ValueProgramConfigTest, AConfigLoadsItsFormulas) {
        Install(Format("{}{}", kContext, kFormulas));
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueFormulaConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {kConfigPrefix}), 0U);
        ExpectFormulasEqualPlainCode(*document);
    }

    TEST_F(ValueProgramConfigTest, AMistakeInAFormulaLogsTheConfigTheFieldAndTheColumn) {
        Install(Format("{}{}", kContext, R"(<damage expr="caster.attack @ 2"/><power expr="cast.power * 2"/>)"));
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueFormulaConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {kConfigPrefix}), 1U);
        EXPECT_NE(recorder.Find(LogLevel::Error, {kConfigPrefix, "damage", "column 15", "(document 'TestsConfig')"}), nullptr);
        EXPECT_TRUE(document->damage.IsConst());
        EXPECT_EQ(document->damage.GetConst(), 7);
        EXPECT_EQ(document->power.Kind(), ValueKind::Program);  // what could be read is kept
    }

    TEST_F(ValueProgramConfigTest, AFormulaWithNoContextNamesTheConfig) {
        Install(R"(<damage expr="caster.attack"/>)");
        LogRecorder recorder;
        const auto document = MakeFromConfig<ValueFormulaConfigDocument>(kConfig);
        LogEvent::Flush();

        EXPECT_NE(recorder.Find(LogLevel::Error, {kConfigPrefix, "No context is declared", "caster.attack", "(document 'TestsConfig')"}), nullptr);
        EXPECT_TRUE(document->damage.IsConst());
    }

}  // namespace BECore::Tests
