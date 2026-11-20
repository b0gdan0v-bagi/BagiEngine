// `expr` over a game's registered types in content (Values/ValueAccessor.h, Values/Programs/Domain/CLAUDE.md): a `Value<DomainBits256>` field read from XML, binary and JSON,
// saved again, and the line a mistake gives. The document is `DomainDocument` of ProgramDomainDocument.h (its generated code is included here); the operations are the toy ones.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/ProgramDomainDocument.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Values/CachedValue.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <Generated/ProgramDomainDocument.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "units/board.xml";
        constexpr StringView kContext = R"(<context caster="DomainBoard" cast="DomainBoard"/>)";
        constexpr StringView kFormulas =
            R"x(<blocked expr="and_mask(caster.walls, shift(cast.walls, East))"/><reach expr="or_mask(shift(caster.walls, South), cast.walls)"/>)x"
            R"x(<crowd expr="popcount(and_mask(caster.walls, cast.walls)) + caster.turn"/><open expr="any(and_mask(caster.walls, cast.walls)) &amp;&amp; cast.turn &gt; 1"/>)x";

        String Repeated(StringView part, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text.append(part.data(), part.size());
            }
            return text;
        }

        [[nodiscard]] DomainBits256 PatternMask(uint64_t seed) {
            DomainBits256 mask;
            for (size_t i = 0; i < mask.words.size(); ++i) {
                mask.words[i] = (seed + i) * 0x9E3779B97F4A7C15ULL;
            }
            return mask;
        }

        template <typename Reader>
        String ReportsOf(const Reader& reader) {
            String out;
            for (const Vector<DeserializeError>* list : {&reader.GetErrors(), &reader.GetWarnings()}) {
                for (const DeserializeError& report : *list) {
                    AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
                }
            }
            return out;
        }

        /// Loads `<root><State>@p children</State></root>` into @p target; the caller has the link scope open. Every error and warning, one text
        String LoadXml(StringView children, DomainDocument& target) {
            XmlDeserializer reader;
            const String xml = Format("<root><State>{}</State></root>", children);
            EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        String LoadXmlDocument(const String& xml, DomainDocument& target) {
            XmlDeserializer reader;
            EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        String SaveXml(const DomainDocument& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject(kState)) {
                value.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        String LoadBinary(const DomainDocument& source, DomainDocument& target) {
            BinarySerializer serializer;
            if (serializer.BeginObject(kState)) {
                source.Serialize(serializer);
                serializer.EndObject();
            }
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(serializer.GetData()));
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// Two boards behind the two levels of `kContext`
        struct TwoBoards {
            ContextDecl declaration;
            DomainBoard caster;
            DomainBoard cast;
            Context context;

            TwoBoards() {
                EXPECT_TRUE(declaration.Add<DomainBoard>("caster").has_value());
                EXPECT_TRUE(declaration.Add<DomainBoard>("cast").has_value());
                context = Context(declaration);
                context.Set(0, caster);
                context.Set(1, cast);
            }

            TwoBoards(const TwoBoards&) = delete;
            TwoBoards& operator=(const TwoBoards&) = delete;
        };

        void ExpectFormulasEqualPlainCode(const DomainDocument& document) {
            ASSERT_EQ(document.blocked.Kind(), ValueKind::Program);
            ASSERT_EQ(document.reach.Kind(), ValueKind::Program);
            ASSERT_EQ(document.crowd.Kind(), ValueKind::Program);
            ASSERT_EQ(document.open.Kind(), ValueKind::Program);
            TwoBoards boards;
            for (const uint64_t seed : {1ULL, 8ULL, 40ULL}) {
                boards.caster.walls = PatternMask(seed);
                boards.cast.walls = PatternMask(seed * 3 + 1);
                boards.caster.turn = static_cast<int32_t>(seed);
                boards.cast.turn = static_cast<int32_t>(seed % 4);
                EXPECT_EQ(document.blocked.Evaluate(boards.context), BitsAnd(boards.caster.walls, BitsShift(boards.cast.walls, DomainDirection::East)));
                EXPECT_EQ(document.reach.Evaluate(boards.context), BitsOr(BitsShift(boards.caster.walls, DomainDirection::South), boards.cast.walls));
                EXPECT_EQ(document.crowd.Evaluate(boards.context), BitsPopcount(BitsAnd(boards.caster.walls, boards.cast.walls)) + boards.caster.turn);
                EXPECT_EQ(document.open.Evaluate(boards.context), BitsAny(BitsAnd(boards.caster.walls, boards.cast.walls)) && boards.cast.turn > 1);
            }
        }

        void ExpectSameFormulas(const DomainDocument& actual, const DomainDocument& expected) {
            EXPECT_EQ(actual.blocked.Id(), expected.blocked.Id());
            EXPECT_EQ(actual.blocked.Path(), expected.blocked.Path());
            EXPECT_EQ(actual.reach.Id(), expected.reach.Id());
            EXPECT_EQ(actual.crowd.Id(), expected.crowd.Id());
            EXPECT_EQ(actual.open.Id(), expected.open.Id());
        }

        void ExpectDefaults(const DomainDocument& document) {
            EXPECT_TRUE(document.blocked.IsConst());
            EXPECT_EQ(document.blocked.GetConst(), DomainBits256{});
            EXPECT_TRUE(document.crowd.IsConst());
            EXPECT_EQ(document.crowd.GetConst(), 7);
        }

        /// A context function that declares the level it reads, registered once per process
        [[nodiscard]] DomainBits256 WatchedBoardWalls(const Context& context) {
            return BoardWalls(context);
        }

        [[nodiscard]] DomainOperationId RegisterWatchedBoardWalls() {
            static const DomainOperationId id = [] {
                static_cast<void>(RegisterDomainToys());
                return Require(ProgramDomain::AddOperation<&WatchedBoardWalls>("board_walls_watched", DomainOperationOptions{LevelBit(0)}));
            }();
            return id;
        }

    }  // namespace

    TEST(ProgramDomainLoadTest, FormulasOverARegisteredTypeLoadFromXmlAndEvaluateLikePlainCode) {
        static_cast<void>(RegisterDomainToys());
        DomainDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}{}", kContext, kFormulas), document);
        EXPECT_TRUE(reports.empty()) << reports.c_str();
        ExpectFormulasEqualPlainCode(document);
        EXPECT_EQ(document.blocked.Path(), "and_mask(caster.walls, shift(cast.walls, East))"_intern);
    }

    TEST(ProgramDomainLoadTest, AProgramSavesItsTextAndLoadsAgainEqualInXmlBinaryAndJson) {
        static_cast<void>(RegisterDomainToys());
        DomainDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            ASSERT_TRUE(LoadXml(Format("{}{}", kContext, kFormulas), document).empty());
        }
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "<blocked expr=\"and_mask(caster.walls, shift(cast.walls, East))\"")) << saved.c_str();

        DomainDocument fromXml;
        {
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXmlDocument(saved, fromXml);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        ExpectSameFormulas(fromXml, document);
        ExpectFormulasEqualPlainCode(fromXml);

        DomainDocument fromBinary;
        {
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadBinary(document, fromBinary);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        ExpectSameFormulas(fromBinary, document);
        ExpectFormulasEqualPlainCode(fromBinary);

        JsonSerializer serializer;
        document.Serialize(serializer);
        const Json json = serializer.Take();
        DomainDocument fromJson;
        {
            const ValueLinkScope scope(kDocumentName);
            JsonDeserializer reader(json);
            fromJson.Deserialize(reader);
            EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        }
        ExpectSameFormulas(fromJson, document);
        ExpectFormulasEqualPlainCode(fromJson);
    }

    TEST(ProgramDomainLoadTest, JsonReadsTheObjectWithExprOfARegisteredType) {
        static_cast<void>(RegisterDomainToys());
        const auto parsed = ParseJson(R"x({"context": [{"name": "caster", "type": "DomainBoard"}, {"name": "cast", "type": "DomainBoard"}],
                                          "blocked": {"expr": "and_mask(caster.walls, shift(cast.walls, East))"}, "reach": {"expr": "or_mask(shift(caster.walls, South), cast.walls)"},
                                          "crowd": {"expr": "popcount(and_mask(caster.walls, cast.walls)) + caster.turn"},
                                          "open": {"expr": "any(and_mask(caster.walls, cast.walls)) && cast.turn > 1"}})x");
        ASSERT_TRUE(parsed.has_value());
        DomainDocument document;
        const ValueLinkScope scope(kDocumentName);
        JsonDeserializer reader(*parsed);
        document.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectFormulasEqualPlainCode(document);
    }

    TEST(ProgramDomainLoadTest, ALiteralOfTheTypeKeepsTheObjectFormItsOwnSerializeWrites) {
        static_cast<void>(RegisterDomainToys());
        DomainDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}<blocked w0=\"1\" w1=\"2\" w2=\"3\" w3=\"4\"/>", kContext), document);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        ASSERT_TRUE(document.blocked.IsConst());
        EXPECT_EQ(document.blocked.GetConst(), (DomainBits256{{1, 2, 3, 4}}));
        const String saved = SaveXml(document);
        EXPECT_TRUE(HasText(saved, "w3=\"4\"")) << saved.c_str();
        EXPECT_FALSE(HasText(saved, "<blocked expr")) << saved.c_str();
        DomainDocument again;
        const ValueLinkScope scope(kDocumentName);
        EXPECT_TRUE(LoadXmlDocument(saved, again).empty());
        EXPECT_EQ(again.blocked.GetConst(), document.blocked.GetConst());
    }

    TEST(ProgramDomainLoadTest, AFormulaThatFoldsOrIsOneFieldLoadsAsAConstOrASlotAndSavesAsSuch) {
        static_cast<void>(RegisterDomainToys());
        DomainDocument document;
        {
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}<blocked expr=\"shift(cell(3, 4), East)\"/><reach expr=\"caster.walls\"/>", kContext), document);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
        }
        ASSERT_TRUE(document.blocked.IsConst());
        EXPECT_EQ(document.blocked.GetConst(), BitsShift(BitsCell(3, 4), DomainDirection::East));
        ASSERT_EQ(document.reach.Kind(), ValueKind::Slot);
        TwoBoards boards;
        boards.caster.walls = PatternMask(5);
        EXPECT_EQ(document.reach.Evaluate(boards.context), boards.caster.walls);

        const String saved = SaveXml(document);
        EXPECT_FALSE(HasText(saved, "<blocked expr")) << saved.c_str();
        EXPECT_TRUE(HasText(saved, "<reach from=\"caster.walls\"")) << saved.c_str();
        DomainDocument again;
        const ValueLinkScope scope(kDocumentName);
        EXPECT_TRUE(LoadXmlDocument(saved, again).empty());
        EXPECT_EQ(again.blocked.GetConst(), document.blocked.GetConst());
        EXPECT_EQ(again.reach.Kind(), ValueKind::Slot);
    }

    TEST(ProgramDomainLoadTest, AMistakeInTheFormulaReadsColumnMessageAndDocument) {
        static_cast<void>(RegisterDomainToys());
        struct Mistake {
            StringView children;
            StringView path;
            StringView words;
        };
        const Mistake mistakes[] = {
            // An unknown operation lists the registered ones
            {R"x(<blocked expr="and_masks(caster.walls, cast.walls)"/>)x", "State.blocked.expr", "column 1: unknown function 'and_masks'"},
            {R"x(<blocked expr="and_masks(caster.walls, cast.walls)"/>)x", "State.blocked.expr", "and_mask"},
            // A wrong argument type names the call, the types found and the signature
            {R"x(<blocked expr="and_mask(caster.walls, caster.turn)"/>)x", "State.blocked.expr", "column 1: no overload of 'and_mask' takes (mask, int)"},
            {R"x(<blocked expr="and_mask(caster.walls, caster.turn)"/>)x", "State.blocked.expr", "and_mask(mask, mask) -> mask"},
            {R"x(<crowd expr="popcount(caster.walls, cast.walls)"/>)x", "State.crowd.expr", "'popcount' takes 1 argument"},
            // A field of a type nobody registered
            {R"x(<loose expr="caster.walls"/>)x", "State.loose.expr", "DomainUnregistered"},
            // A formula of the wrong type for the field
            {R"x(<blocked expr="popcount(caster.walls)"/>)x", "State.blocked.expr", "a value of mask is asked for"},
        };
        for (const Mistake& mistake : mistakes) {
            DomainDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(Format("{}{}", kContext, mistake.children), document);
            EXPECT_TRUE(HasText(reports, mistake.path)) << String(mistake.children).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, mistake.words)) << String(mistake.children).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, "(document 'units/board.xml')")) << reports.c_str();
            ExpectDefaults(document);
            EXPECT_TRUE(document.loose.IsConst());
        }
    }

    TEST(ProgramDomainLoadTest, HostileTextsOverRegisteredOperationsAreErrorsThatKeepTheFieldAndNeverCrash) {
        static_cast<void>(RegisterDomainToys());
        const String tooLong = Format("and_mask(caster.walls, {}cast.walls)", String(5000, ' '));
        const String tooDeep = Format("{}caster.walls{}", Repeated("and_mask(", 200), Repeated(", cast.walls)", 200));
        const String manyArguments = Format("majority({}cast.walls)", Repeated("cast.walls, ", 600));
        const StringView texts[] = {"\x01and_mask(", "and_mask(,", "and_mask(caster.walls)", "shift(caster.walls, 99)", "cell(1.5, 2)",
                                    "mask",          "popcount",   "caster.walls.w0",        "caster.walls[0]",         tooLong,
                                    tooDeep,         manyArguments};
        const size_t pooled = ProgramPool::Count();
        for (const StringView text : texts) {
            DomainDocument document;
            const ValueLinkScope scope(kDocumentName);
            XmlDeserializer reader;
            String xml = Format("<root><State>{}<blocked expr=\"", kContext);
            xml.append(text.data(), text.size());
            xml += "\"/></State></root>";
            if (!reader.LoadFromString(xml)) {
                continue;  // the XML itself is refused: nothing reaches the compiler
            }
            ASSERT_TRUE(reader.BeginObject(kState));
            document.Deserialize(reader);
            reader.EndObject();
            EXPECT_TRUE(reader.HasErrors()) << "no error for a hostile text of " << text.size() << " bytes";
            ExpectDefaults(document);
        }
        EXPECT_EQ(ProgramPool::Count(), pooled);
    }

    TEST(ProgramDomainLoadTest, AFailedLoadAddsNothingToThePool) {
        static_cast<void>(RegisterDomainToys());
        const size_t count = ProgramPool::Count();
        DomainDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadXml(Format("{}<blocked expr=\"and_mask(caster.walls, caster.turn)\"/><reach expr=\"popcount(caster.walls)\"/>", kContext), document);
        EXPECT_FALSE(reports.empty());
        EXPECT_EQ(ProgramPool::Count(), count);
    }

    TEST(ProgramDomainLoadTest, ACachedValueOverAProgramWithAContextOperationRecomputesWhenTheDeclaredLevelMoves) {
        static_cast<void>(RegisterWatchedBoardWalls());
        TwoBoards boards;
        const auto compiled = CompileValue<DomainBits256>("shift(board_walls_watched(), East)", boards.declaration);
        ASSERT_TRUE(compiled.has_value()) << compiled.error().message.c_str();
        ASSERT_EQ(compiled->Kind(), ValueKind::Program);
        ASSERT_EQ(compiled->Reads(), LevelBit(0));

        uint64_t casterVersion = 1;
        uint64_t castVersion = 1;
        boards.context.Set(0, boards.caster, &casterVersion);
        boards.context.Set(1, boards.cast, &castVersion);
        boards.caster.walls = PatternMask(2);
        CachedValue<DomainBits256> cached(*compiled);
        EXPECT_EQ(cached.Evaluate(boards.context), BitsShift(boards.caster.walls, DomainDirection::East));
        EXPECT_EQ(cached.Evaluate(boards.context), BitsShift(boards.caster.walls, DomainDirection::East));
        EXPECT_EQ(cached.Computations(), 1U);

        ++castVersion;
        static_cast<void>(cached.Evaluate(boards.context));
        EXPECT_EQ(cached.Computations(), 1U) << "the level the operation did not declare moved";

        boards.caster.walls = PatternMask(9);
        ++casterVersion;
        EXPECT_EQ(cached.Evaluate(boards.context), BitsShift(boards.caster.walls, DomainDirection::East));
        EXPECT_EQ(cached.Computations(), 2U);
    }

}  // namespace BECore::Tests
