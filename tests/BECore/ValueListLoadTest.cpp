// Formulas over lists in content (Values/ValueAccessor.h, Programs/Lists/CLAUDE.md): a document of a Mahjong-shaped hand (ValueListMahjongTypes.h) loads `expr` with `count`, `sum`, `max`, `size`
// and an element by index from XML, binary and JSON, saves them back as `expr`, evaluates them to what C++ over the hand gives, and reports a mistake as `column N: <message>` with the document
// name; a cached result of such a formula follows the version counter of the level its list is in.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Random/Rng.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/ValueListMahjongTypes.h>
#include <BECore/Values/CachedValue.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Generated/ValueListMahjongTypes.gen.hpp>
#include <algorithm>
#include <gtest/gtest.h>
#include <numeric>

namespace BECore::Tests {

    using namespace ValueListMahjongData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kDocumentName = "scoring/hand.xml";
        constexpr StringView kShorthand = R"(<context hand="ValueListHand"/>)";
        constexpr StringView kListForm = R"(<context><level name="hand" type="ValueListHand"/></context>)";
        constexpr LevelIndex kHandLevel = 0;
        constexpr int kVariants = 24;
        constexpr int kMaxFu = 5;
        constexpr int kFuStep = 2;
        constexpr int kMaxHan = 4;

        constexpr StringView kWin = "count(hand.sets, s, s.pung && s.dragon) == 3";
        constexpr StringView kHan = "sum(hand.yaku, y, select(y.on, y.han, 0))";
        constexpr StringView kFu = "max(hand.sets, s, s.fu)";
        constexpr StringView kTiles = "count(hand.tiles, t, t.honor || t.terminal)";
        constexpr StringView kPure = "all(hand.tiles, t, t.suit == hand.tiles[0].suit)";

        /// A hand that a seed fills: every fourth is of one suit, and the number of sets and yaku changes with the seed
        void FillHand(ValueListHand& hand, int variant) {
            Rng rng(static_cast<uint64_t>(variant) + 100);
            const auto suits = static_cast<int>(EnumUtils<ValueListSuit>::Count());
            const auto ranks = static_cast<int>(EnumUtils<ValueListRank>::Count());
            const ValueListSuit pureSuit = static_cast<ValueListSuit>(rng.UniformInt(0, suits - 1));
            for (ValueListTile& tile : hand.tiles) {
                tile.suit = variant % 4 == 0 ? pureSuit : static_cast<ValueListSuit>(rng.UniformInt(0, suits - 1));
                tile.rank = static_cast<ValueListRank>(rng.UniformInt(0, ranks - 1));
                tile.honor = tile.suit == ValueListSuit::Wind || tile.suit == ValueListSuit::Dragon;
                tile.terminal = tile.rank == ValueListRank::One || tile.rank == ValueListRank::Nine;
                tile.dragon = tile.suit == ValueListSuit::Dragon;
            }
            for (int s = 0; s < variant % 7; ++s) {
                ValueListSet set;
                set.pung = rng.UniformInt(0, 2) != 0;
                set.dragon = rng.UniformInt(0, 1) == 1;
                set.suit = static_cast<ValueListSuit>(rng.UniformInt(0, suits - 1));
                set.fu = rng.UniformInt(0, kMaxFu) * kFuStep;
                hand.sets.push_back(set);
            }
            for (int y = 0; y < variant % 6; ++y) {
                ValueListYaku yaku;
                yaku.on = rng.UniformInt(0, 1) == 1;
                yaku.han = rng.UniformInt(1, kMaxHan);
                hand.yaku.push_back(yaku);
            }
        }

        bool WinOf(const ValueListHand& hand) {
            return std::count_if(hand.sets.begin(), hand.sets.end(), [](const ValueListSet& s) { return s.pung && s.dragon; }) == 3;
        }

        int32_t HanOf(const ValueListHand& hand) {
            int32_t total = 0;
            for (const ValueListYaku& yaku : hand.yaku) {
                total += yaku.on ? yaku.han : 0;
            }
            return total;
        }

        int32_t FuOf(const ValueListHand& hand) {
            int32_t best = std::numeric_limits<int32_t>::lowest();
            for (const ValueListSet& set : hand.sets) {
                best = std::max(best, set.fu);
            }
            return best;
        }

        int32_t TilesOf(const ValueListHand& hand) {
            return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const ValueListTile& t) { return t.honor || t.terminal; }));
        }

        bool PureOf(const ValueListHand& hand) {
            return std::all_of(hand.tiles.begin(), hand.tiles.end(), [&](const ValueListTile& t) { return t.suit == hand.tiles[0].suit; });
        }

        /// The text as the value of an XML attribute
        String Escaped(StringView text) {
            String out;
            for (const char c : text) {
                switch (c) {
                    case '&':
                        out += "&amp;";
                        break;
                    case '<':
                        out += "&lt;";
                        break;
                    case '>':
                        out += "&gt;";
                        break;
                    case '"':
                        out += "&quot;";
                        break;
                    default:
                        out.push_back(c);
                        break;
                }
            }
            return out;
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

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// Loads `<root><State>@p children</State></root>` into @p target; the caller has the link scope open. Every error and warning, one text
        String LoadXml(StringView children, ValueListMahjongDocument& target) {
            XmlDeserializer reader;
            const String xml = Format("<root><State>{}</State></root>", children);
            EXPECT_TRUE(reader.LoadFromString(xml)) << xml.c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            target.Deserialize(reader);
            reader.EndObject();
            return ReportsOf(reader);
        }

        /// The whole document: the context, then the five values as written above
        String AllFormulas(StringView context) {
            return Format("{}<win expr=\"{}\"/><han expr=\"{}\"/><fu expr=\"{}\"/><tiles expr=\"{}\"/><pure expr=\"{}\"/>", context, Escaped(kWin), Escaped(kHan), Escaped(kFu), Escaped(kTiles),
                          Escaped(kPure));
        }

        /// Loads one field written as @p formula after the shorthand context; the reports, with @p document as it was left
        String LoadField(StringView field, StringView formula, ValueListMahjongDocument& document) {
            return LoadXml(Format("{}<{} expr=\"{}\"/>", kShorthand, field, Escaped(formula)), document);
        }

        struct HandWorld {
            ValueListHand hand;
            Context context;

            HandWorld(const ContextDecl& declaration, int variant) {
                FillHand(hand, variant);
                context = Context(declaration);
                context.Set(kHandLevel, hand);
            }

            HandWorld(const HandWorld&) = delete;
            HandWorld& operator=(const HandWorld&) = delete;
        };

        /// Every value of @p document is what the C++ over the hand gives, for every variant of the hand
        void ExpectScoresLikePlainCode(const ValueListMahjongDocument& document) {
            ASSERT_EQ(document.context.Size(), 1U);
            for (int variant = 0; variant < kVariants; ++variant) {
                const HandWorld world(document.context, variant);
                EXPECT_EQ(document.win.Evaluate(world.context), WinOf(world.hand)) << "variant " << variant;
                EXPECT_EQ(document.han.Evaluate(world.context), HanOf(world.hand)) << "variant " << variant;
                EXPECT_EQ(document.fu.Evaluate(world.context), FuOf(world.hand)) << "variant " << variant;
                EXPECT_EQ(document.tiles.Evaluate(world.context), TilesOf(world.hand)) << "variant " << variant;
                EXPECT_EQ(document.pure.Evaluate(world.context), PureOf(world.hand)) << "variant " << variant;
            }
        }

        void ExpectSameDocument(const ValueListMahjongDocument& actual, const ValueListMahjongDocument& expected) {
            EXPECT_TRUE(actual.win == expected.win);
            EXPECT_TRUE(actual.han == expected.han);
            EXPECT_TRUE(actual.fu == expected.fu);
            EXPECT_TRUE(actual.tiles == expected.tiles);
            EXPECT_TRUE(actual.pure == expected.pure);
        }

        void ExpectUntouched(const ValueListMahjongDocument& document) {
            ASSERT_TRUE(document.han.IsConst());
            EXPECT_EQ(document.han.GetConst(), 7);
            ASSERT_TRUE(document.tiles.IsConst());
            EXPECT_EQ(document.tiles.GetConst(), 7);
            ASSERT_TRUE(document.win.IsConst());
            EXPECT_FALSE(document.win.GetConst());
        }

        ValueListMahjongDocument LoadedDocument(StringView context) {
            ValueListMahjongDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadXml(AllFormulas(context), document);
            EXPECT_TRUE(reports.empty()) << reports.c_str();
            return document;
        }

        /// The first line of @p reports names @p field, says the column of @p at in @p formula and the document
        void ExpectColumn(const String& reports, StringView field, StringView formula, StringView at, StringView words) {
            const size_t position = formula.find(at);
            ASSERT_NE(position, StringView::npos) << String(at).c_str();
            EXPECT_TRUE(HasText(reports, Format("State.{}.expr", field))) << String(formula).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, Format("column {}: ", position + 1))) << String(formula).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, words)) << String(formula).c_str() << ": " << reports.c_str();
            EXPECT_TRUE(HasText(reports, "(document 'scoring/hand.xml')")) << String(formula).c_str() << ": " << reports.c_str();
        }

        /// @p depth folds in the body of one another (a fold over the sets, then over the tiles of one of them, and again over the sets)
        String Nest(int depth) {
            String text = depth % 2 == 1 ? Format("a{}.pung", depth - 1) : Format("a{}.terminal", depth - 1);
            for (int level = depth - 1; level >= 0; --level) {
                const String list = level % 2 == 0 ? String("hand.sets") : Format("a{}.tiles", level - 1);
                text = Format("count({}, a{}, {}) > 0", list, level, text);
                if (level > 0) {
                    text = Format("({})", text);
                }
            }
            return text;
        }

    }  // namespace

    TEST(ValueListLoadTest, FormulasOverAListLoadFromXmlAndScoreLikePlainCode) {
        const ValueListMahjongDocument shorthand = LoadedDocument(kShorthand);
        for (const ValueKind kind : {shorthand.win.Kind(), shorthand.han.Kind(), shorthand.fu.Kind(), shorthand.tiles.Kind(), shorthand.pure.Kind()}) {
            EXPECT_EQ(kind, ValueKind::Program);
        }
        ExpectScoresLikePlainCode(shorthand);

        const ValueListMahjongDocument listForm = LoadedDocument(kListForm);
        ExpectScoresLikePlainCode(listForm);
        ExpectSameDocument(listForm, shorthand);
    }

    TEST(ValueListLoadTest, TheProgramsReadTheLevelOfTheirList) {
        const ValueListMahjongDocument document = LoadedDocument(kShorthand);
        EXPECT_EQ(document.han.Reads(), LevelBit(kHandLevel));
        EXPECT_EQ(document.pure.Reads(), LevelBit(kHandLevel));
    }

    TEST(ValueListLoadTest, AFormulaSavesAsExprAndLoadsAgainEqualInXml) {
        const ValueListMahjongDocument document = LoadedDocument(kShorthand);
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject(kState));
        document.Serialize(serializer);
        serializer.EndObject();
        const String saved = serializer.SaveToString();
        EXPECT_TRUE(HasText(saved, Format("expr=\"{}\"", Escaped(kWin)))) << saved.c_str();
        EXPECT_TRUE(HasText(saved, Format("expr=\"{}\"", Escaped(kPure)))) << saved.c_str();

        ValueListMahjongDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(saved)) << saved.c_str();
        ASSERT_TRUE(reader.BeginObject(kState));
        loaded.Deserialize(reader);
        reader.EndObject();
        EXPECT_TRUE(ReportsOf(reader).empty()) << ReportsOf(reader).c_str();
        ExpectSameDocument(loaded, document);
        ExpectScoresLikePlainCode(loaded);
    }

    TEST(ValueListLoadTest, BinaryHasTheSameFormulas) {
        const ValueListMahjongDocument document = LoadedDocument(kShorthand);
        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject(kState));
        document.Serialize(serializer);
        serializer.EndObject();

        ValueListMahjongDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(serializer.GetData()));
        ASSERT_TRUE(reader.BeginObject(kState));
        loaded.Deserialize(reader);
        reader.EndObject();
        EXPECT_TRUE(ReportsOf(reader).empty()) << ReportsOf(reader).c_str();
        ExpectSameDocument(loaded, document);
        ExpectScoresLikePlainCode(loaded);
    }

    TEST(ValueListLoadTest, JsonHasTheSameFormulas) {
        const ValueListMahjongDocument document = LoadedDocument(kShorthand);
        JsonSerializer serializer;
        document.Serialize(serializer);
        const Json json = serializer.Take();

        ValueListMahjongDocument loaded;
        const ValueLinkScope scope(kDocumentName);
        JsonDeserializer reader(json);
        loaded.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << ReportsOf(reader).c_str();
        ExpectSameDocument(loaded, document);
        ExpectScoresLikePlainCode(loaded);
    }

    TEST(ValueListLoadTest, AMistakeInAFormulaOverAListGivesTheColumnAndTheDocument) {
        struct Mistake {
            StringView field;
            StringView formula;
            StringView at;
            StringView words;
        };
        const Mistake mistakes[] = {
            {"tiles", "count(hand.setz, s, s.pung)", "hand.setz", "did you mean 'sets'"},
            {"tiles", "size(5)", "5", "needs a list"},
            {"win", "any(hand.sets, s, s.fu)", "s.fu", "bool body"},
            {"tiles", "count(hand.sets, s, s.pungg)", "s.pungg", "pung"},
            {"han", "sum(hand.yaku, y, y.on)", "y.on", "number body"},
            {"tiles", "hand.sets + count(hand.sets, s, s.pung)", "hand.sets", "list"},
            {"tiles", "hand.tiles[1.5].rank", "1.5", "int"},
        };
        for (const Mistake& mistake : mistakes) {
            ValueListMahjongDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadField(mistake.field, mistake.formula, document);
            ExpectColumn(reports, mistake.field, mistake.formula, mistake.at, mistake.words);
            ExpectUntouched(document);
        }
    }

    TEST(ValueListLoadTest, AReusedNameIsReportedAtTheInnerLoop) {
        constexpr StringView kFormula = "count(hand.sets, s, count(s.tiles, s, s.dragon) > 0)";
        ValueListMahjongDocument document;
        const ValueLinkScope scope(kDocumentName);
        const String reports = LoadField("tiles", kFormula, document);
        const size_t inner = kFormula.find("s, s.dragon");
        ASSERT_NE(inner, StringView::npos);
        EXPECT_TRUE(HasText(reports, Format("column {}: ", inner + 1))) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "already the element")) << reports.c_str();
        EXPECT_TRUE(HasText(reports, "(document 'scoring/hand.xml')")) << reports.c_str();
    }

    TEST(ValueListLoadTest, HostileTextsOverListsAreErrorsAndLeaveTheFieldAndThePool) {
        String chain = "count(hand.sets, s, s.pung)";
        for (int i = 1; i < 100; ++i) {
            chain += " + count(hand.sets, s, s.pung)";
        }
        String indexNest = "0";
        for (int i = 0; i < 100; ++i) {
            indexNest = Format("hand.tiles[{}].rank", indexNest);
        }
        const String longBody = Format("count(hand.sets, s, s.pung{})", String(4096, ' '));
        const String texts[] = {Nest(12), chain, indexNest, longBody, "count(hand.sets, s, s.pung", "count(hand.sets, s)", "hand.tiles[", "size(hand.tiles"};
        const size_t pool = ProgramPool::Count();
        for (const String& text : texts) {
            ValueListMahjongDocument document;
            const ValueLinkScope scope(kDocumentName);
            const String reports = LoadField("tiles", text, document);
            EXPECT_FALSE(reports.empty()) << "no error for a text of " << text.size() << " bytes";
            EXPECT_TRUE(HasText(reports, "column ") || HasText(reports, "limit") || HasText(reports, "long")) << reports.c_str();
            ExpectUntouched(document);
        }
        EXPECT_EQ(ProgramPool::Count(), pool);
    }

    TEST(ValueListLoadTest, ACachedResultFollowsTheCounterOfTheLevelTheListIsIn) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueListHand>("hand").has_value());
        ValueListHand hand;
        FillHand(hand, 6);
        ASSERT_FALSE(hand.sets.empty());
        uint64_t version = 1;
        Context context(declaration);
        context.Set(kHandLevel, hand, &version);

        const auto compiled = CompileValue<int32_t>("count(hand.sets, s, s.pung)", declaration);
        ASSERT_TRUE(compiled.has_value());
        CachedValue<int32_t> cached(*compiled);
        const auto pungs = [&] { return static_cast<int32_t>(std::count_if(hand.sets.begin(), hand.sets.end(), [](const ValueListSet& s) { return s.pung; })); };
        const int32_t before = pungs();
        EXPECT_EQ(cached.Evaluate(context), before);
        EXPECT_EQ(cached.Evaluate(context), before);
        EXPECT_EQ(cached.Computations(), 1U);

        hand.sets[0].pung = !hand.sets[0].pung;
        EXPECT_EQ(cached.Evaluate(context), before) << "a write with no bump is the owner's miss";
        ++version;
        EXPECT_EQ(cached.Evaluate(context), pungs());
        EXPECT_NE(pungs(), before);
        EXPECT_EQ(cached.Computations(), 2U);
    }

    TEST(ValueListLoadTest, ALevelWithNoCounterRunsAFormulaOverAListOnEveryCall) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueListHand>("hand").has_value());
        ValueListHand hand;
        FillHand(hand, 6);
        Context context(declaration);
        context.Set(kHandLevel, hand);
        CachedValue<int32_t> cached(Require(CompileValue<int32_t>("sum(hand.yaku, y, y.han)", declaration)));
        constexpr uint32_t kCalls = 4;
        for (uint32_t i = 0; i < kCalls; ++i) {
            EXPECT_EQ(cached.Evaluate(context), std::accumulate(hand.yaku.begin(), hand.yaku.end(), 0, [](int32_t sum, const ValueListYaku& y) { return sum + y.han; }));
            EXPECT_EQ(cached.Computations(), i + 1);
        }
    }

}  // namespace BECore::Tests
