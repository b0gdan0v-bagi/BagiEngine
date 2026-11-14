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

}  // namespace BECore::Tests
