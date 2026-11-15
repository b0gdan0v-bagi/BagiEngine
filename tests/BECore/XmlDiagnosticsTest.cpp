#include "XmlDiagnosticsTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Generated/XmlDiagnosticsTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace XmlDiagnosticsTestData;

    namespace {

        constexpr StringView kNestedDocument = "<a><b><items><item/><item/><item/></items></b></a>";

        /// Opens a.b.items[2] of kNestedDocument, the path `a.b.items[2]` for a finding
        void ReportDeep(XmlDeserializer& d) {
            ASSERT_TRUE(d.LoadFromString(kNestedDocument));
            ASSERT_TRUE(d.BeginObject("b"));
            size_t count = 0;
            ASSERT_TRUE(d.BeginArray("items", "item", count));
            ASSERT_EQ(count, 3u);
            for (size_t i = 0; i < 2; ++i) {
                ASSERT_TRUE(d.BeginArrayElement());
                d.EndArrayElement();
            }
            ASSERT_TRUE(d.BeginArrayElement());
            d.ReportFinding("key", "odd value");
            d.EndArrayElement();
            d.EndArray();
            d.EndObject();
        }

        constexpr StringView kCompleteDocument = "<Constrained needed=\"1\" both=\"7\"><requiredLeaf leafValue=\"2\"/><requiredLeaves><item leafValue=\"3\"/></requiredLeaves></Constrained>";

        /// A document that loads `Constrained` under @p mode; @p attributes go on the root element, @p children inside it
        template <typename Target = Constrained>
        struct LoadedDocument {
            XmlDeserializer reader;
            Target value;

            LoadedDocument(DiagnosticsMode mode, StringView xml) {
                reader.SetDiagnostics(mode);
                EXPECT_TRUE(reader.LoadFromString(xml));
                value.Deserialize(reader);
                reader.FinishLoad();
            }

            /// Path and message of every finding, in the order they were found, whichever list holds them
            Vector<String> Findings() const {
                const auto& list = reader.GetDiagnostics() == DiagnosticsMode::Error ? reader.GetErrors() : reader.GetWarnings();
                Vector<String> out;
                for (const DeserializeError& finding : list) {
                    out.push_back(Format("{}: {}", finding.path.ToStringView(), finding.errorMessage.ToStringView()));
                }
                return out;
            }
        };

        /// The root `Constrained` with the attributes of the complete document and one more attribute
        String WithAttribute(StringView attribute) {
            return Format("<Constrained needed=\"1\" both=\"7\" {}><requiredLeaf/><requiredLeaves/></Constrained>", attribute);
        }

    }  // namespace

    TEST(XmlDiagnosticsTest, OffByDefaultAndFindingsAreDropped) {
        XmlDeserializer d;
        EXPECT_EQ(d.GetDiagnostics(), DiagnosticsMode::Off);
        ReportDeep(d);
        EXPECT_TRUE(d.GetWarnings().empty());
        EXPECT_FALSE(d.HasErrors());
    }

    TEST(XmlDiagnosticsTest, WarnStoresTheFindingWithTheFullPath) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ReportDeep(d);
        ASSERT_EQ(d.GetWarnings().size(), 1u);
        EXPECT_EQ(d.GetWarnings()[0].path.ToStringView(), "b.items[2].key");
        EXPECT_EQ(d.GetWarnings()[0].errorMessage.ToStringView(), "odd value");
        EXPECT_FALSE(d.HasErrors());
        EXPECT_TRUE(d.GetErrors().empty());
    }

    TEST(XmlDiagnosticsTest, ErrorModeMakesTheFindingAnError) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Error);
        ReportDeep(d);
        ASSERT_TRUE(d.HasErrors());
        ASSERT_EQ(d.GetErrors().size(), 1u);
        EXPECT_EQ(d.GetErrors()[0].path.ToStringView(), "b.items[2].key");
        EXPECT_TRUE(d.GetWarnings().empty());
    }

    TEST(XmlDiagnosticsTest, ClearWarningsKeepsTheErrors) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ReportDeep(d);
        d.ReportError("x", "an error");
        d.ClearWarnings();
        EXPECT_TRUE(d.GetWarnings().empty());
        EXPECT_EQ(d.GetErrors().size(), 1u);
    }

    TEST(XmlDiagnosticsTest, TheListOfWarningsStopsAtTheLimit) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(d.LoadFromString("<root/>"));
        for (size_t i = 0; i < ReaderLimits::kMaxRecordedErrors * 3; ++i) {
            d.ReportFinding("key", "odd value");
        }
        EXPECT_EQ(d.GetWarnings().size(), ReaderLimits::kMaxRecordedErrors);
        EXPECT_NE(d.GetWarnings().back().errorMessage.ToStringView().find("Too many errors"), StringView::npos);
    }

    TEST(XmlDiagnosticsTest, TheModeSurvivesALoad) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Error);
        ASSERT_TRUE(d.LoadFromString("<root/>"));
        EXPECT_EQ(d.GetDiagnostics(), DiagnosticsMode::Error);
    }

    TEST(XmlDiagnosticsTest, HasKeyFindsAnAttributeAndAnElementOfTheCurrentNode) {
        XmlDeserializer d;
        ASSERT_TRUE(d.LoadFromString("<root size=\"3\"><child name=\"x\"><inner/></child></root>"));
        EXPECT_TRUE(d.HasKey("size"));
        EXPECT_TRUE(d.HasKey("child"));
        EXPECT_FALSE(d.HasKey("missing"));
        EXPECT_FALSE(d.HasKey("name"));
        EXPECT_FALSE(d.HasKey("inner"));

        ASSERT_TRUE(d.BeginObject("child"));
        EXPECT_TRUE(d.HasKey("name"));
        EXPECT_TRUE(d.HasKey("inner"));
        EXPECT_FALSE(d.HasKey("size"));
        d.EndObject();
        EXPECT_TRUE(d.HasKey("size"));
    }

    TEST(XmlDiagnosticsTest, HasKeyIsFalseWithoutADocument) {
        XmlDeserializer d;
        EXPECT_FALSE(d.HasKey("anything"));
    }

    TEST(XmlDiagnosticsTest, HasKeyOfTheBinaryReaderSeesEntriesOfTheCurrentObject) {
        BinarySerializer s;
        ASSERT_TRUE(s.BeginObject("root"));
        s.WriteAttribute("size", int32_t{3});
        ASSERT_TRUE(s.BeginObject("child"));
        s.WriteAttribute("name", StringView("x"));
        s.EndObject();
        s.EndObject();

        BinaryDeserializer d;
        ASSERT_TRUE(d.LoadFromBuffer(s.GetData()));
        EXPECT_FALSE(d.HasKey("size"));  // the root scope holds the object `root` only
        EXPECT_TRUE(d.HasKey("root"));
        ASSERT_TRUE(d.BeginObject("root"));
        EXPECT_TRUE(d.HasKey("size"));
        EXPECT_TRUE(d.HasKey("child"));
        EXPECT_FALSE(d.HasKey("name"));
        EXPECT_FALSE(d.HasKey("missing"));
        ASSERT_TRUE(d.BeginObject("child"));
        EXPECT_TRUE(d.HasKey("name"));
        EXPECT_FALSE(d.HasKey("size"));
        d.EndObject();
        d.EndObject();
    }

    TEST(XmlDiagnosticsTest, BinaryFindingsCarryThePath) {
        BinarySerializer s;
        ASSERT_TRUE(s.BeginObject("root"));
        s.WriteAttribute("size", int32_t{3});
        s.EndObject();

        BinaryDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(d.LoadFromBuffer(s.GetData()));
        ASSERT_TRUE(d.BeginObject("root"));
        d.ReportFinding("size", "odd value");
        d.EndObject();
        ASSERT_EQ(d.GetWarnings().size(), 1u);
        EXPECT_EQ(d.GetWarnings()[0].path.ToStringView(), "root.size");
        EXPECT_FALSE(d.HasErrors());
    }

    TEST(XmlDiagnosticsTest, AnEnumValueOutsideTheEnumIsAnErrorInEveryMode) {
        for (const DiagnosticsMode mode : {DiagnosticsMode::Off, DiagnosticsMode::Warn, DiagnosticsMode::Error}) {
            XmlDeserializer d;
            d.SetDiagnostics(mode);
            ASSERT_TRUE(d.LoadFromString("<Shapes kind=\"Huge\"/>"));
            Shapes shapes;
            shapes.Deserialize(d);
            ASSERT_EQ(d.GetErrors().size(), 1u) << static_cast<int>(mode);
            EXPECT_EQ(d.GetErrors()[0].path.ToStringView(), "kind");
            EXPECT_NE(d.GetErrors()[0].errorMessage.ToStringView().find("Invalid enum value"), StringView::npos);
            EXPECT_EQ(shapes.kind, ProbeKind::Small);
        }
    }

    // =========================================================================
    // required and range, from the generated Deserialize
    // =========================================================================

    TEST(XmlDiagnosticsTest, ACompleteDocumentHasNoFindingInAnyMode) {
        for (const DiagnosticsMode mode : {DiagnosticsMode::Off, DiagnosticsMode::Warn, DiagnosticsMode::Error}) {
            LoadedDocument<> loaded(mode, kCompleteDocument);
            EXPECT_TRUE(loaded.Findings().empty()) << static_cast<int>(mode);
            EXPECT_FALSE(loaded.reader.HasErrors());
            EXPECT_EQ(loaded.value.both, 7);
            EXPECT_EQ(loaded.value.requiredLeaves.size(), 1u);
        }
    }

    TEST(XmlDiagnosticsTest, AMissingRequiredFieldIsAFindingOfWarnAndError) {
        constexpr StringView kEmpty = "<Constrained/>";
        const Vector<String> expected = {"needed: Required field is missing", "both: Required field is missing", "requiredLeaf: Required field is missing",
                                         "requiredLeaves: Required field is missing"};

        LoadedDocument<> warned(DiagnosticsMode::Warn, kEmpty);
        EXPECT_EQ(warned.Findings(), expected);
        EXPECT_FALSE(warned.reader.HasErrors());

        LoadedDocument<> failed(DiagnosticsMode::Error, kEmpty);
        EXPECT_EQ(failed.Findings(), expected);
        EXPECT_TRUE(failed.reader.HasErrors());
    }

    TEST(XmlDiagnosticsTest, AMissingRequiredFieldIsSilentWhenDiagnosticsAreOff) {
        LoadedDocument<> loaded(DiagnosticsMode::Off, "<Constrained/>");
        EXPECT_TRUE(loaded.Findings().empty());
        EXPECT_TRUE(loaded.reader.GetWarnings().empty());
        EXPECT_FALSE(loaded.reader.HasErrors());
        EXPECT_EQ(loaded.value.both, 50);
    }

    TEST(XmlDiagnosticsTest, ARequiredFieldThatHoldsItsDefaultIsPresent) {
        LoadedDocument<> loaded(DiagnosticsMode::Warn, "<Constrained needed=\"0\" both=\"50\"><requiredLeaf/><requiredLeaves/></Constrained>");
        EXPECT_TRUE(loaded.Findings().empty());
    }

    TEST(XmlDiagnosticsTest, AFieldThatIsNeitherRequiredNorPresentIsSilent) {
        LoadedDocument<> loaded(DiagnosticsMode::Error, WithAttribute(""));
        EXPECT_TRUE(loaded.Findings().empty());
        EXPECT_EQ(loaded.value.free, 0);
        EXPECT_FLOAT_EQ(loaded.value.ratio, 0.5f);  // absent: no range check of the default
        EXPECT_EQ(loaded.value.count, 5);
    }

    TEST(XmlDiagnosticsTest, TheBoundsOfARangeAreInside) {
        for (const char* ratio : {"0", "1", "0.5"}) {
            LoadedDocument<> loaded(DiagnosticsMode::Warn, WithAttribute(Format("ratio=\"{}\"", ratio)));
            EXPECT_TRUE(loaded.Findings().empty()) << ratio;
        }
        for (const char* count : {"1", "10"}) {
            LoadedDocument<> loaded(DiagnosticsMode::Warn, WithAttribute(Format("count=\"{}\"", count)));
            EXPECT_TRUE(loaded.Findings().empty()) << count;
        }
    }

    TEST(XmlDiagnosticsTest, AValueOutsideTheRangeIsAFindingAndKeepsWhatWasRead) {
        LoadedDocument<> above(DiagnosticsMode::Warn, WithAttribute("ratio=\"1.5\""));
        EXPECT_EQ(above.Findings(), Vector<String>{"ratio: Value 1.5 is outside the range [0, 1]"});
        EXPECT_FLOAT_EQ(above.value.ratio, 1.5f);

        LoadedDocument<> below(DiagnosticsMode::Warn, WithAttribute("ratio=\"-0.25\""));
        EXPECT_EQ(below.Findings(), Vector<String>{"ratio: Value -0.25 is outside the range [0, 1]"});
        EXPECT_FLOAT_EQ(below.value.ratio, -0.25f);

        LoadedDocument<> integer(DiagnosticsMode::Error, WithAttribute("count=\"11\""));
        EXPECT_EQ(integer.Findings(), Vector<String>{"count: Value 11 is outside the range [1, 10]"});
        EXPECT_TRUE(integer.reader.HasErrors());
        EXPECT_EQ(integer.value.count, 11);

        LoadedDocument<> zero(DiagnosticsMode::Warn, WithAttribute("count=\"0\""));
        EXPECT_EQ(zero.Findings(), Vector<String>{"count: Value 0 is outside the range [1, 10]"});
    }

    TEST(XmlDiagnosticsTest, AValueOutsideTheRangeIsSilentWhenDiagnosticsAreOff) {
        LoadedDocument<> loaded(DiagnosticsMode::Off, WithAttribute("ratio=\"9\""));
        EXPECT_TRUE(loaded.Findings().empty());
        EXPECT_FLOAT_EQ(loaded.value.ratio, 9.0f);
    }

    TEST(XmlDiagnosticsTest, ANotANumberIsOutsideTheRange) {
        LoadedDocument<> loaded(DiagnosticsMode::Warn, WithAttribute("ratio=\"nan\""));
        ASSERT_EQ(loaded.Findings().size(), 1u);
        EXPECT_NE(loaded.Findings()[0].find("ratio: Value "), String::npos);
        EXPECT_NE(loaded.Findings()[0].find("is outside the range [0, 1]"), String::npos);
    }

    TEST(XmlDiagnosticsTest, ARequiredFieldWithARangeReportsBoth) {
        LoadedDocument<> missing(DiagnosticsMode::Warn, "<Constrained needed=\"1\"><requiredLeaf/><requiredLeaves/></Constrained>");
        EXPECT_EQ(missing.Findings(), Vector<String>{"both: Required field is missing"});

        LoadedDocument<> outside(DiagnosticsMode::Warn, "<Constrained needed=\"1\" both=\"101\"><requiredLeaf/><requiredLeaves/></Constrained>");
        EXPECT_EQ(outside.Findings(), Vector<String>{"both: Value 101 is outside the range [0, 100]"});
    }

    TEST(XmlDiagnosticsTest, ADerivedClassPassesTheAttributesOfItsParentsFields) {
        LoadedDocument<ConstrainedChild> loaded(DiagnosticsMode::Warn, "<ConstrainedChild own=\"4\" ratio=\"2\"/>");
        EXPECT_EQ(loaded.Findings(), (Vector<String>{"needed: Required field is missing", "ratio: Value 2 is outside the range [0, 1]", "both: Required field is missing",
                                                     "requiredLeaf: Required field is missing", "requiredLeaves: Required field is missing", "own: Value 4 is outside the range [0, 3]"}));
    }

    // =========================================================================
    // `range` on the numeric elements of a container
    // =========================================================================

    namespace {

        /// A `RangedContainers` whose every number is inside its range by default; each parameter is the text of one of its numbers
        String RangedDocument(StringView ratio = "0.5", StringView count = "3", StringView maybe = "0.25", StringView weight = "1", StringView choice = "2") {
            return Format("<RangedContainers maybe=\"{}\">"
                          "<ratios><item value=\"0\"/><item value=\"{}\"/><item value=\"1\"/></ratios>"
                          "<counts><item value=\"1\"/><item value=\"10\"/><item value=\"{}\"/></counts>"
                          "<weights><entry key=\"a\" value=\"0\"/><entry key=\"b\" value=\"{}\"/></weights>"
                          "<choice index=\"0\" value=\"{}\"/>"
                          "<leaves><item leafValue=\"99\"/></leaves>"
                          "</RangedContainers>",
                          maybe, ratio, count, weight, choice);
        }

    }  // namespace

    TEST(XmlDiagnosticsTest, ContainerElementsInsideTheRangeAreSilent) {
        LoadedDocument<RangedContainers> loaded(DiagnosticsMode::Warn, RangedDocument());
        EXPECT_TRUE(loaded.Findings().empty());
        EXPECT_FALSE(loaded.reader.HasErrors());
    }

    TEST(XmlDiagnosticsTest, AnElementOutsideTheRangeIsOneFindingAtItsPosition) {
        LoadedDocument<RangedContainers> ratio(DiagnosticsMode::Warn, RangedDocument("1.5"));
        EXPECT_EQ(ratio.Findings(), Vector<String>{"ratios[1]: Value 1.5 is outside the range [0, 1]"});
        ASSERT_EQ(ratio.value.ratios.size(), 3u);
        EXPECT_FLOAT_EQ(ratio.value.ratios[1], 1.5f);

        LoadedDocument<RangedContainers> count(DiagnosticsMode::Warn, RangedDocument("0.5", "11"));
        EXPECT_EQ(count.Findings(), Vector<String>{"counts[2]: Value 11 is outside the range [1, 10]"});
        EXPECT_EQ(count.value.counts[2], 11);

        LoadedDocument<RangedContainers> maybe(DiagnosticsMode::Warn, RangedDocument("0.5", "3", "-1"));
        EXPECT_EQ(maybe.Findings(), Vector<String>{"maybe: Value -1 is outside the range [0, 1]"});
        ASSERT_TRUE(maybe.value.maybe.has_value());
        EXPECT_FLOAT_EQ(*maybe.value.maybe, -1.0f);

        LoadedDocument<RangedContainers> weight(DiagnosticsMode::Warn, RangedDocument("0.5", "3", "0.25", "2"));
        EXPECT_EQ(weight.Findings(), Vector<String>{"weights[1]: Value 2 is outside the range [0, 1]"});
        EXPECT_FLOAT_EQ(weight.value.weights["b"], 2.0f);

        LoadedDocument<RangedContainers> choice(DiagnosticsMode::Warn, RangedDocument("0.5", "3", "0.25", "1", "12"));
        EXPECT_EQ(choice.Findings(), Vector<String>{"choice.value: Value 12 is outside the range [1, 10]"});
        EXPECT_EQ(choice.value.choice, (Variant<int32_t, String>{std::in_place_index<0>, 12}));
    }

    TEST(XmlDiagnosticsTest, TwoElementsOutsideTheRangeAreTwoFindings) {
        LoadedDocument<RangedContainers> loaded(DiagnosticsMode::Warn, "<RangedContainers><ratios><item value=\"2\"/><item value=\"0.5\"/><item value=\"-3\"/></ratios></RangedContainers>");
        EXPECT_EQ(loaded.Findings(), (Vector<String>{"ratios[0]: Value 2 is outside the range [0, 1]", "ratios[2]: Value -3 is outside the range [0, 1]"}));
    }

    TEST(XmlDiagnosticsTest, ANotANumberElementIsOutsideTheRange) {
        LoadedDocument<RangedContainers> loaded(DiagnosticsMode::Warn, RangedDocument("nan"));
        ASSERT_EQ(loaded.Findings().size(), 1u);
        EXPECT_NE(loaded.Findings()[0].find("ratios[1]: Value "), String::npos);
        EXPECT_NE(loaded.Findings()[0].find("is outside the range [0, 1]"), String::npos);
    }

    TEST(XmlDiagnosticsTest, ContainerElementsOutsideTheRangeAreErrorsUnderErrorAndNothingUnderOff) {
        const String outside = RangedDocument("1.5", "11", "-1", "2", "12");
        LoadedDocument<RangedContainers> failed(DiagnosticsMode::Error, outside);
        EXPECT_EQ(failed.Findings().size(), 5u);
        EXPECT_TRUE(failed.reader.HasErrors());

        LoadedDocument<RangedContainers> off(DiagnosticsMode::Off, outside);
        EXPECT_TRUE(off.reader.GetWarnings().empty());
        EXPECT_FALSE(off.reader.HasErrors());
        EXPECT_FLOAT_EQ(off.value.ratios[1], 1.5f);
        EXPECT_EQ(off.value.counts[2], 11);
        EXPECT_EQ(off.value.choice, (Variant<int32_t, String>{std::in_place_index<0>, 12}));
    }

    TEST(XmlDiagnosticsTest, RangeOnAListOfObjectsOrATextAlternativeReportsNothing) {
        LoadedDocument<RangedContainers> loaded(DiagnosticsMode::Warn, "<RangedContainers><leaves><item leafValue=\"-5\"/></leaves><choice index=\"1\" value=\"999\"/></RangedContainers>");
        EXPECT_TRUE(loaded.Findings().empty());
        ASSERT_EQ(loaded.value.leaves.size(), 1u);
        EXPECT_EQ(loaded.value.leaves[0].leafValue, -5);
        EXPECT_EQ(loaded.value.choice, (Variant<int32_t, String>{std::in_place_index<1>, String("999")}));
    }

    // =========================================================================
    // Unknown keys
    // =========================================================================

    TEST(XmlDiagnosticsTest, ATypoGivesTheFindingWithThePathAndTheSuggestion) {
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, "<TileConfig tileSise=\"3\" tileCount=\"2\"/>");
        EXPECT_EQ(loaded.Findings(), Vector<String>{"tileSise: Unknown attribute 'tileSise', did you mean 'tileSize'?"});
        EXPECT_FALSE(loaded.reader.HasErrors());
        EXPECT_EQ(loaded.value.tileCount, 2);
    }

    TEST(XmlDiagnosticsTest, ACaseDifferenceAloneGetsTheSuggestion) {
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, "<TileConfig TILESIZE=\"3\"/>");
        EXPECT_EQ(loaded.Findings(), Vector<String>{"TILESIZE: Unknown attribute 'TILESIZE', did you mean 'tileSize'?"});
    }

    TEST(XmlDiagnosticsTest, AnUnknownKeyWithNoNearNameHasNoSuggestion) {
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, "<TileConfig tileSize=\"1\" zzzzzz=\"1\"><wibble/></TileConfig>");
        EXPECT_EQ(loaded.Findings(), (Vector<String>{"zzzzzz: Unknown attribute 'zzzzzz'", "wibble: Unknown element 'wibble'"}));
    }

    TEST(XmlDiagnosticsTest, ANameFarFromEveryAskedNameGetsNoSuggestion) {
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, "<TileConfig tile=\"1\"/>");
        EXPECT_EQ(loaded.Findings(), Vector<String>{"tile: Unknown attribute 'tile'"});
    }

    TEST(XmlDiagnosticsTest, AnUnknownKeyIsAnErrorUnderErrorAndNothingUnderOff) {
        constexpr StringView kTypo = "<TileConfig tileSise=\"3\"/>";
        LoadedDocument<TileConfig> failed(DiagnosticsMode::Error, kTypo);
        EXPECT_EQ(failed.Findings().size(), 1u);
        EXPECT_TRUE(failed.reader.HasErrors());
        EXPECT_TRUE(failed.reader.GetWarnings().empty());

        LoadedDocument<TileConfig> off(DiagnosticsMode::Off, kTypo);
        EXPECT_TRUE(off.reader.GetWarnings().empty());
        EXPECT_FALSE(off.reader.HasErrors());
    }

    TEST(XmlDiagnosticsTest, OffTracksNothing) {
        XmlDeserializer d;
        ASSERT_TRUE(d.LoadFromString("<TileConfig tileSise=\"3\"><wibble/></TileConfig>"));
        TileConfig value;
        value.Deserialize(d);
        d.FinishLoad();
        EXPECT_TRUE(d.GetWarnings().empty());
        EXPECT_FALSE(d.HasErrors());

        // The mode switched on afterwards does not see what Off passed: a second load starts clean
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(d.LoadFromString("<TileConfig tileSize=\"3\" tileCount=\"1\"/>"));
        value.Deserialize(d);
        d.FinishLoad();
        EXPECT_TRUE(d.GetWarnings().empty());
    }

    TEST(XmlDiagnosticsTest, TheModeSwitchedOnAfterTheLoadIsHonoured) {
        XmlDeserializer d;
        ASSERT_TRUE(d.LoadFromString("<TileConfig tileSise=\"3\"/>"));
        d.SetDiagnostics(DiagnosticsMode::Warn);
        TileConfig value;
        value.Deserialize(d);
        d.FinishLoad();
        ASSERT_EQ(d.GetWarnings().size(), 1u);
        EXPECT_EQ(d.GetWarnings()[0].path.ToStringView(), "tileSise");
    }

    TEST(XmlDiagnosticsTest, TheRootIsCheckedByFinishLoadOnceAndOnlyThen) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(d.LoadFromString("<TileConfig tileSise=\"3\"/>"));
        TileConfig value;
        value.Deserialize(d);
        EXPECT_TRUE(d.GetWarnings().empty());
        d.FinishLoad();
        EXPECT_EQ(d.GetWarnings().size(), 1u);
        d.FinishLoad();
        EXPECT_EQ(d.GetWarnings().size(), 1u);

        // Reading after FinishLoad is allowed and unchecked
        value.Deserialize(d);
        d.FinishLoad();
        EXPECT_EQ(d.GetWarnings().size(), 1u);

        // A new load starts over
        ASSERT_TRUE(d.LoadFromString("<TileConfig tileSise=\"3\"/>"));
        value.Deserialize(d);
        d.FinishLoad();
        EXPECT_EQ(d.GetWarnings().size(), 2u);
    }

    TEST(XmlDiagnosticsTest, NoteKnownKeyRemovesAKeyReadByHand) {
        constexpr StringView kDocument = "<TileConfig contentVersion=\"3\" tileSize=\"1\" tileCount=\"2\"><extra/></TileConfig>";
        LoadedDocument<TileConfig> unknown(DiagnosticsMode::Warn, kDocument);
        EXPECT_EQ(unknown.Findings(), (Vector<String>{"contentVersion: Unknown attribute 'contentVersion'", "extra: Unknown element 'extra'"}));

        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(d.LoadFromString(kDocument));
        d.NoteKnownKey("contentVersion");
        d.NoteKnownKey("extra");
        TileConfig value;
        value.Deserialize(d);
        d.FinishLoad();
        EXPECT_TRUE(d.GetWarnings().empty());
    }

    TEST(XmlDiagnosticsTest, AValueThatFailsToParseIsNoUnknownKey) {
        XmlDeserializer d;
        d.SetDiagnostics(DiagnosticsMode::Warn);
        d.SetStrictValues(true);
        ASSERT_TRUE(d.LoadFromString("<TileConfig tileSize=\"abc\" tileCount=\"2\"/>"));
        TileConfig value;
        value.Deserialize(d);
        d.FinishLoad();
        EXPECT_TRUE(d.GetWarnings().empty());
        EXPECT_EQ(d.GetErrors().size(), 1u);
    }

    TEST(XmlDiagnosticsTest, ADocumentReadCompletelyIsClean) {
        constexpr StringView kDocument = "<Holder>"
                                         "<leaf leafValue=\"1\"/>"
                                         "<leaves><item leafValue=\"2\"/><item leafValue=\"3\"/></leaves>"
                                         "<table><entry key=\"7\" leafValue=\"4\"/></table>"
                                         "<probes><probe type=\"DiagProbeReads\" enabled=\"true\" id=\"1\" extra=\"5\"/></probes>"
                                         "</Holder>";
        LoadedDocument<Holder> loaded(DiagnosticsMode::Error, kDocument);
        EXPECT_TRUE(loaded.Findings().empty());
        EXPECT_FALSE(loaded.reader.HasErrors());
        ASSERT_EQ(loaded.value.probes.size(), 1u);
        EXPECT_EQ(loaded.value.probes[0]->Cast<DiagProbeReads>()->extra, 5);
        EXPECT_EQ(loaded.value.table.size(), 1u);
    }

    TEST(XmlDiagnosticsTest, UnknownKeysOfNestedScopesCarryTheFullPath) {
        constexpr StringView kDocument = "<Holder>"
                                         "<leaf leafValue=\"1\" oops=\"1\"/>"
                                         "<leaves stray=\"1\"><item leafValue=\"2\"/><item leafValue=\"3\" oops=\"1\"><deeper/></item></leaves>"
                                         "<table><entry key=\"7\" leafValue=\"4\" oops=\"1\"/></table>"
                                         "</Holder>";
        LoadedDocument<Holder> loaded(DiagnosticsMode::Warn, kDocument);
        EXPECT_EQ(loaded.Findings(), (Vector<String>{"leaf.oops: Unknown attribute 'oops'", "leaves[1].oops: Unknown attribute 'oops'", "leaves[1].deeper: Unknown element 'deeper'",
                                                     "leaves.stray: Unknown attribute 'stray'", "table[0].oops: Unknown attribute 'oops'"}));
    }

    TEST(XmlDiagnosticsTest, AnArrayChecksItsOwnAttributesAndItsChildrenAreTheElements) {
        LoadedDocument<Holder> loaded(DiagnosticsMode::Warn, "<Holder><leaves><item leafValue=\"2\"/><whatever leafValue=\"3\"/></leaves></Holder>");
        EXPECT_TRUE(loaded.Findings().empty());
        EXPECT_EQ(loaded.value.leaves.size(), 2u);
    }

    TEST(XmlDiagnosticsTest, ADisabledFactoryElementIsNotReadAndHasNoUnknownKeys) {
        LoadedDocument<Holder> loaded(DiagnosticsMode::Warn, "<Holder><probes><probe type=\"DiagProbeReads\" enabled=\"false\" id=\"2\" bogus=\"x\"/>"
                                                             "<probe type=\"DiagProbeReads\" id=\"3\" bogus=\"x\"/></probes></Holder>");
        EXPECT_EQ(loaded.Findings(), Vector<String>{"probes[1].bogus: Unknown attribute 'bogus'"});
        EXPECT_EQ(loaded.value.probes.size(), 1u);
    }

    TEST(XmlDiagnosticsTest, ChildrenOfOneNameAreOneUnknownName) {
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, "<TileConfig><zzzzzz/><zzzzzz/><zzzzzz/></TileConfig>");
        EXPECT_EQ(loaded.Findings(), Vector<String>{"zzzzzz: Unknown element 'zzzzzz'"});
    }

    TEST(XmlDiagnosticsTest, TheTextOfAnElementIsNotAnUnknownKey) {
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, "<TileConfig tileSize=\"1\">some text<!-- a comment --></TileConfig>");
        EXPECT_TRUE(loaded.Findings().empty());
    }

    TEST(XmlDiagnosticsTest, ManyUnknownKeysStopAtTheLimit) {
        String attributes;
        String elements;
        for (size_t i = 0; i < 10000; ++i) {
            attributes += Format(" k{}=\"1\"", i);
            elements += Format("<e{}/>", i);
        }
        LoadedDocument<TileConfig> byAttribute(DiagnosticsMode::Warn, Format("<TileConfig{}/>", attributes));
        EXPECT_EQ(byAttribute.reader.GetWarnings().size(), ReaderLimits::kMaxRecordedErrors);

        LoadedDocument<TileConfig> byElement(DiagnosticsMode::Error, Format("<TileConfig>{}</TileConfig>", elements));
        EXPECT_EQ(byElement.reader.GetErrors().size(), ReaderLimits::kMaxRecordedErrors);
    }

    TEST(XmlDiagnosticsTest, AVeryLongNameIsCutAndGetsNoSuggestion) {
        const String longName(100 * 1024, 't');
        LoadedDocument<TileConfig> loaded(DiagnosticsMode::Warn, Format("<TileConfig {}=\"1\"/>", longName));
        ASSERT_EQ(loaded.reader.GetWarnings().size(), 1u);
        const DeserializeError& warning = loaded.reader.GetWarnings()[0];
        EXPECT_LE(warning.path.ToStringView().size(), ReaderLimits::kMaxErrorTextBytes);
        EXPECT_LE(warning.errorMessage.ToStringView().size(), ReaderLimits::kMaxQuotedValueBytes + 64);
        EXPECT_EQ(warning.errorMessage.ToStringView().find("did you mean"), StringView::npos);
    }

}  // namespace BECore::Tests
