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

}  // namespace BECore::Tests
