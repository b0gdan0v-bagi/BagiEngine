// The key `tree` of a Value (Values/ValueAccessor.h, Values/Nodes): the same formula as a tree of node classes and as `expr` is one Value; XML, binary and JSON read it; a tree
// is saved as the text it prints; the errors of the tree and of the checker. The document types are those of ValueTestTypes.h (their generated code is included in ValueContextTest.cpp).

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Nodes/CalcNode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/ValueLinkScope.h>
#include <BECore/Values/ValueTables.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "units/fireball.xml";
        constexpr StringView kContext = R"(<context caster="ValueUnitStats" cast="ValueCastInfo"/>)";
        constexpr int32_t kDefaultDamage = 7;
        // The levels the reader of XML allows (ReaderLimits) and what a tree may be (ExprLimits::kMaxTreeHeight)
        constexpr size_t kLevelsOverTheReader = 300;
        constexpr size_t kLevelsOverTheTree = 100;

        // `caster.attack * 2` as a tree, in the element form of XML
        constexpr StringView kAttackTimesTwo = R"(<tree type="CalcBinary" op="Mul"><left type="CalcRead" path="caster.attack"/><right type="CalcNumber" text="2"/></tree>)";

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
        String LoadXml(StringView children, Struct& target, DiagnosticsMode diagnostics = DiagnosticsMode::Off) {
            XmlDeserializer reader;
            reader.SetDiagnostics(diagnostics);
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

        /// `State.damage.tree` of the binary form, written node by node as the reader reads it: `caster.attack * 2`
        Vector<uint8_t> BinaryTreeDocument() {
            BinarySerializer s;
            EXPECT_TRUE(s.BeginObject(kState));
            EXPECT_TRUE(s.BeginObject("damage"));
            EXPECT_TRUE(s.BeginObject("tree"));
            s.WriteAttribute("type", StringView("CalcBinary"));
            Ser::Save(s, ProgramOperation::Mul, "op");
            EXPECT_TRUE(s.BeginObject("left"));
            s.WriteAttribute("type", StringView("CalcRead"));
            s.WriteAttribute("path", StringView("caster.attack"));
            s.EndObject();
            EXPECT_TRUE(s.BeginObject("right"));
            s.WriteAttribute("type", StringView("CalcNumber"));
            s.WriteAttribute("text", StringView("2"));
            s.EndObject();
            s.EndObject();
            s.EndObject();
            s.EndObject();
            return s.GetData();
        }

        constexpr StringView kJsonTree =
            R"x({"damage": {"tree": {"type": "CalcBinary", "op": "Mul", "left": {"type": "CalcRead", "path": "caster.attack"}, "right": {"type": "CalcNumber", "text": "2"}}}})x";

        /// A chain of @p levels `CalcUnary` nodes over a number, as the element form of XML
        String UnaryChain(size_t levels, StringView key) {
            String open;
            String close;
            for (size_t i = 0; i < levels; ++i) {
                AppendFormat(open, "<{} type=\"CalcUnary\" op=\"Neg\">", i == 0 ? key : StringView("operand"));
                close = (i == 0 ? Format("</{}>", key) : String("</operand>")) + close;
            }
            return open + R"(<operand type="CalcNumber" text="1"/>)" + close;
        }

        void ExpectAttackTimesTwo(const Value<int32_t>& value) {
            ASSERT_EQ(value.Kind(), ValueKind::Program);
            ValueWorld world;
            for (const int32_t attack : {0, 5, -7, 1000}) {
                world.caster.attack = attack;
                EXPECT_EQ(value.Evaluate(world.context), attack * 2);
            }
        }

        Value<int32_t> FromText(StringView text) {
            ValueWorld world;
            const auto compiled = CompileValue<int32_t>(text, world.declaration);
            EXPECT_TRUE(compiled.has_value());
            return compiled.value_or(Value<int32_t>{});
        }

    }  // namespace

    // =========================================================================
    // The same formula, a tree and a text
    // =========================================================================

    TEST(ValueTreeLoadTest, ATreeLoadsAsTheProgramOfTheSameText) {
        ValueFormulaDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}<damage>{}</damage>", kContext, kAttackTimesTwo), document, DiagnosticsMode::Error);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        ExpectAttackTimesTwo(document.damage);
        EXPECT_EQ(document.damage.Path(), PoolString::Intern("caster.attack * 2"));

        // The text form of the same formula is that program: no new entry of the pool, and `==`
        const size_t pooled = ProgramPool::Count();
        ValueFormulaDocument fromText;
        {
            const ValueLinkScope scope(kDocumentName);
            EXPECT_TRUE(LoadXml(Format("{}{}", kContext, R"(<damage expr="caster.attack * 2"/>)"), fromText).empty());
        }
        EXPECT_EQ(document.damage, fromText.damage);
        EXPECT_EQ(ProgramPool::Count(), pooled);
    }

    TEST(ValueTreeLoadTest, ATreeOfEachTypeGivesTheValueOfItsText) {
        struct Field {
            StringView name;
            StringView tree;
            StringView expr;
        };
        const Field fields[] = {
            {"power", R"(<tree type="CalcBinary" op="Mul"><left type="CalcRead" path="caster.speed"/><right type="CalcNumber" text="2.0"/></tree>)", "caster.speed * 2.0"},
            {"aim", R"(<tree type="CalcBinary" op="Mul"><left type="CalcRead" path="caster.velocity"/><right type="CalcNumber" text="2"/></tree>)", "caster.velocity * 2"},
            {"element",
             R"(<tree type="CalcCall" function="select"><args><node type="CalcRead" path="caster.alive"/><node type="CalcRead" path="Fire"/><node type="CalcRead" path="caster.element"/></args></tree>)",
             "select(caster.alive, Fire, caster.element)"},
        };
        for (const Field& field : fields) {
            SCOPED_TRACE(String(field.name).c_str());
            ValueFormulaDocument fromTree;
            ValueFormulaDocument fromText;
            {
                const ValueLinkScope scope(kDocumentName);
                const String treeReports = LoadXml(Format("{}<{}>{}</{}>", kContext, field.name, field.tree, field.name), fromTree, DiagnosticsMode::Error);
                EXPECT_TRUE(treeReports.empty()) << treeReports.c_str();
            }
            {
                const ValueLinkScope scope(kDocumentName);
                const String textReports = LoadXml(Format("{}<{} expr=\"{}\"/>", kContext, field.name, field.expr), fromText);
                EXPECT_TRUE(textReports.empty()) << textReports.c_str();
            }
            if (field.name == "power") {
                ASSERT_EQ(fromTree.power.Kind(), ValueKind::Program);
                EXPECT_EQ(fromTree.power, fromText.power);
            } else if (field.name == "aim") {
                ASSERT_EQ(fromTree.aim.Kind(), ValueKind::Program);
                EXPECT_EQ(fromTree.aim, fromText.aim);
            } else {
                ASSERT_EQ(fromTree.element.Kind(), ValueKind::Program);
                EXPECT_EQ(fromTree.element, fromText.element);
            }
        }
    }

    TEST(ValueTreeLoadTest, ATreeThatFoldsIsAConstAndOneThatIsAFieldIsASlot) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}{}", kContext, R"(<damage><tree type="CalcBinary" op="Mul"><left type="CalcNumber" text="2"/><right type="CalcNumber" text="6"/></tree></damage>)",
                                              R"(<power><tree type="CalcRead" path="caster.speed"/></power>)"),
                                       document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_TRUE(document.damage.IsConst());
        EXPECT_EQ(document.damage.GetConst(), 12);
        ASSERT_TRUE(document.power.IsSlot());
        Value<float> linked;
        ASSERT_TRUE(linked.Link(document.context, "caster.speed").has_value());
        EXPECT_EQ(document.power, linked);
    }

    TEST(ValueTreeLoadTest, ATableFromTheProviderThroughCalcByLevel) {
        ValueTableSet tables;
        tables.Add("FireballDamage", {10.0, 20.0, 30.0});
        ValueLinkScope scope(kDocumentName);
        scope.UseTables(tables);
        ValueFormulaDocument document;
        const String reports = LoadXml(Format("{}<damage>{}</damage>", kContext, R"(<tree type="CalcByLevel" table="FireballDamage"><index type="CalcRead" path="cast.level"/></tree>)"), document,
                                       DiagnosticsMode::Error);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_EQ(document.damage.Kind(), ValueKind::Program);
        EXPECT_EQ(document.damage.Path(), PoolString::Intern("ByLevel('FireballDamage', cast.level)"));
        ValueWorld world;
        for (const auto [level, expected] : {Pair{-3, 10}, Pair{0, 10}, Pair{1, 20}, Pair{2, 30}, Pair{9, 30}}) {
            world.cast.level = level;
            EXPECT_EQ(document.damage.Evaluate(world.context), expected) << level;
        }
    }

    TEST(ValueTreeLoadTest, TheKindProgramMayBesideATree) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}<damage kind=\"Program\">{}</damage>", kContext, kAttackTimesTwo), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectAttackTimesTwo(document.damage);
    }

    // =========================================================================
    // The three formats, and saving
    // =========================================================================

    TEST(ValueTreeLoadTest, ATreeIsSavedAsItsTextAndReloadsEqualInXml) {
        ValueFormulaDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            ASSERT_TRUE(LoadXml(Format("{}<damage>{}</damage>", kContext, kAttackTimesTwo), document).empty());
        }
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "expr=\"caster.attack * 2\"")) << saved.c_str();
        EXPECT_FALSE(HasText(saved, "tree")) << saved.c_str();

        ValueFormulaDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXmlDocument(saved, loaded);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        EXPECT_EQ(loaded.damage, document.damage);
    }

    TEST(ValueTreeLoadTest, BinaryReadsATreeAndSavesItsText) {
        ValueWorld world;
        const ValueLinkScope scope(world.declaration);
        ValueFormulaDocument document;
        const String reports = LoadBinary(BinaryTreeDocument(), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectAttackTimesTwo(document.damage);
        EXPECT_EQ(document.damage, FromText("caster.attack * 2"));

        ValueFormulaDocument reloaded;
        EXPECT_TRUE(LoadBinary(SaveBinary(document), reloaded).empty());
        EXPECT_EQ(reloaded.damage, document.damage);
    }

    TEST(ValueTreeLoadTest, JsonReadsATreeAndSavesItsText) {
        const auto parsed = ParseJson(kJsonTree);
        ASSERT_TRUE(parsed.has_value());
        ValueWorld world;
        const ValueLinkScope scope(world.declaration);
        ValueFormulaDocument document;
        JsonDeserializer reader(*parsed);
        document.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectAttackTimesTwo(document.damage);
        EXPECT_EQ(document.damage, FromText("caster.attack * 2"));

        // Saved in XML and loaded again it is the same Value: the tree is gone, the text stays
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "expr=\"caster.attack * 2\"")) << saved.c_str();
        ValueFormulaDocument reloaded;
        EXPECT_TRUE(LoadXmlDocument(saved, reloaded).empty());
        EXPECT_EQ(reloaded.damage, document.damage);
    }

    // =========================================================================
    // Refusals
    // =========================================================================

    struct Refusal {
        StringView name;
        StringView children;
        StringView path;
        StringView words;
    };

    TEST(ValueTreeLoadTest, EveryRefusalNamesThePlaceAndLeavesTheFieldAlone) {
        const Refusal refusals[] = {
            {"tree beside expr", R"(<damage expr="caster.attack"><tree type="CalcRead" path="caster.attack"/></damage>)", "State.damage.expr", "'expr' is there beside 'tree'"},
            {"tree beside from", R"(<damage from="caster.attack"><tree type="CalcRead" path="caster.attack"/></damage>)", "State.damage.from", "'from' is there beside 'tree'"},
            {"tree beside value", R"(<damage value="5"><tree type="CalcRead" path="caster.attack"/></damage>)", "State.damage.value", "'value' is there beside 'tree'"},
            {"kind Const", R"(<damage kind="Const"><tree type="CalcRead" path="caster.attack"/></damage>)", "State.damage.tree", "has a literal"},
            {"kind Slot", R"(<damage kind="Slot"><tree type="CalcRead" path="caster.attack"/></damage>)", "State.damage.tree", "has a path to read"},
            {"a missing child", R"(<damage><tree type="CalcBinary" op="Mul"><left type="CalcRead" path="caster.attack"/></tree></damage>)", "State.damage.tree",
             "CalcBinary: 'right' is missing (at tree)"},
            {"an operator of another arity", R"(<damage><tree type="CalcBinary" op="Neg"><left type="CalcRead" path="a"/><right type="CalcRead" path="b"/></tree></damage>)", "State.damage.tree",
             "CalcBinary: 'op' Neg is not a binary operator"},
            {"a leaf of two tokens", R"(<damage><tree type="CalcNumber" text="1 + 2"/></damage>)", "State.damage.tree", "CalcNumber: 'text'"},
            {"an unknown class", R"(<damage><tree type="CalcFoo"/></damage>)", "State.damage.tree", "Unknown factory type: 'CalcFoo'"},
            {"a root with no class", R"(<damage><tree op="Mul"/></damage>)", "State.damage.tree", "needs the class of its root node"},
            {"a child of an unknown class", R"(<damage><tree type="CalcUnary" op="Neg"><operand type="CalcFoo"/></tree></damage>)", "State.damage.tree.operand", "Unknown factory type"},
            {"a name no level has", R"(<damage><tree type="CalcRead" path="nobody.attack"/></damage>)", "State.damage.tree", "column 1: "},
            {"a type mismatch", R"(<damage><tree type="CalcBinary" op="Add"><left type="CalcRead" path="caster.attack"/><right type="CalcRead" path="caster.speed"/></tree></damage>)",
             "State.damage.tree", "(the tree prints as 'caster.attack + caster.speed')"},
            {"a tree for a type no program computes", R"(<spot><tree type="CalcRead" path="caster.spawn"/></spot>)", "State.spot.tree", "a formula can have the type"},
        };
        for (const Refusal& refusal : refusals) {
            SCOPED_TRACE(String(refusal.name).c_str());
            const size_t pooled = ProgramPool::Count();
            ValueFormulaDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}{}", kContext, refusal.children), document);
            EXPECT_TRUE(HasText(reports, refusal.path)) << reports.c_str();
            EXPECT_TRUE(HasText(reports, refusal.words)) << reports.c_str();
            EXPECT_EQ(ProgramPool::Count(), pooled);
            EXPECT_TRUE(document.damage.IsConst());
            EXPECT_EQ(document.damage.GetConst(), kDefaultDamage);
        }
    }

    TEST(ValueTreeLoadTest, ACheckerErrorHasTheColumnOfThePrintedTextAndThenTheText) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(
            Format("{}{}", kContext, R"(<damage><tree type="CalcBinary" op="Add"><left type="CalcRead" path="caster.attack"/><right type="CalcRead" path="caster.speed"/></tree></damage>)"), document);
        // `caster.attack + caster.speed`: the checker blames the operator, at its byte of the printed text; the column and the message come first (the reader cuts a report at 256
        // bytes), the text after them
        const size_t column = reports.find("column 17: ");
        const size_t printed = reports.find("(the tree prints as 'caster.attack + caster.speed')");
        EXPECT_NE(column, String::npos) << reports.c_str();
        EXPECT_NE(printed, String::npos) << reports.c_str();
        EXPECT_LT(column, printed);
    }

    TEST(ValueTreeLoadTest, ATreeWithNoContextDeclaredIsTheLoadErrorOfAFormula) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("<damage>{}</damage>", kAttackTimesTwo), document);
        EXPECT_TRUE(HasText(reports, "No context is declared")) << reports.c_str();
        EXPECT_TRUE(document.damage.IsConst());
    }

    // =========================================================================
    // The diagnostics, and the limits of the readers
    // =========================================================================

    TEST(ValueTreeLoadTest, AMisspelledTreeIsAnUnknownKeyThatSaysWhatWasMeant) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}<damage>{}</damage>", kContext, R"(<trea type="CalcRead" path="caster.attack"/>)"), document, DiagnosticsMode::Warn);
        EXPECT_TRUE(HasText(reports, "trea")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "did you mean 'tree'")) << reports.c_str();
    }

    TEST(ValueTreeLoadTest, ATreeNestedPastTheReaderLimitIsTheReadersError) {
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}<damage>{}</damage>", kContext, UnaryChain(kLevelsOverTheReader, "tree")), document);
        EXPECT_FALSE(reports.empty());
        EXPECT_TRUE(document.damage.IsConst());
        EXPECT_EQ(document.damage.GetConst(), kDefaultDamage);
    }

    TEST(ValueTreeLoadTest, ATreeHigherThanAFormulaMayBeIsALoadErrorThatNamesTheNode) {
        const size_t pooled = ProgramPool::Count();
        ValueFormulaDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}<damage>{}</damage>", kContext, UnaryChain(kLevelsOverTheTree, "tree")), document);
        EXPECT_TRUE(HasText(reports, "State.damage.tree")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "levels high")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "(at tree.operand.operand")) << reports.c_str();
        EXPECT_EQ(ProgramPool::Count(), pooled);
        EXPECT_TRUE(document.damage.IsConst());
    }

}  // namespace BECore::Tests
