// IDeserializer::ReadAttributeView: the text of an attribute as a view, the same in every reader.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kShort = "Sprite";
        // Over the 23 bytes a small string keeps inside itself
        constexpr StringView kLong = "ParticleEmitterComponent_with_a_long_tail";

        String ViewOf(IDeserializer& reader, StringView name, bool& found) {
            StringView view;
            found = reader.ReadAttributeView(name, view);
            return String(view);
        }

        void ExpectViews(IDeserializer& reader, const char* readerName) {
            bool found = false;
            EXPECT_EQ(ViewOf(reader, "shortType", found), kShort) << readerName;
            EXPECT_TRUE(found) << readerName;
            EXPECT_EQ(ViewOf(reader, "longType", found), kLong) << readerName;
            EXPECT_TRUE(found) << readerName;
            ViewOf(reader, "missing", found);
            EXPECT_FALSE(found) << readerName;
        }

    }  // namespace

    TEST(ReaderAttributeViewTest, TheThreeReadersGiveTheSameText) {
        XmlDeserializer xml;
        ASSERT_TRUE(xml.LoadFromString(Format(R"(<root><item shortType="{}" longType="{}"/></root>)", kShort, kLong)));
        ASSERT_TRUE(xml.BeginObject("item"));
        ExpectViews(xml, "xml");
        xml.EndObject();

        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("item"));
        serializer.WriteAttribute("shortType", String(kShort));
        serializer.WriteAttribute("longType", String(kLong));
        serializer.EndObject();
        BinaryDeserializer binary;
        ASSERT_TRUE(binary.LoadFromBuffer(serializer.GetData()));
        ASSERT_TRUE(binary.BeginObject("item"));
        ExpectViews(binary, "binary");
        binary.EndObject();

        const auto parsed = ParseJson(Format(R"({{"item":{{"shortType":"{}","longType":"{}"}}}})", kShort, kLong));
        ASSERT_TRUE(parsed.has_value());
        JsonDeserializer json(*parsed);
        ASSERT_TRUE(json.BeginObject("item"));
        ExpectViews(json, "json");
        json.EndObject();
    }

    TEST(ReaderAttributeViewTest, TheViewOfAnXmlAttributeIsNoCopyAndAMissingOneKeepsTheView) {
        XmlDeserializer xml;
        ASSERT_TRUE(xml.LoadFromString(R"(<root type="ParticleEmitterComponent_with_a_long_tail"/>)"));
        StringView first;
        StringView second;
        ASSERT_TRUE(xml.ReadAttributeView("type", first));
        ASSERT_TRUE(xml.ReadAttributeView("type", second));
        EXPECT_EQ(first.data(), second.data()) << "both views point into the document";

        StringView kept = "kept";
        EXPECT_FALSE(xml.ReadAttributeView("missing", kept));
        EXPECT_EQ(kept, "kept");
    }

    TEST(ReaderAttributeViewTest, AskingTheViewCountsAsAskingTheKey) {
        XmlDeserializer xml;
        xml.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(xml.LoadFromString(R"(<root type="Sprite" other="1"/>)"));
        StringView type;
        ASSERT_TRUE(xml.ReadAttributeView("type", type));
        xml.FinishLoad();
        ASSERT_EQ(xml.GetWarnings().size(), 1u);
        EXPECT_NE(xml.GetWarnings()[0].errorMessage.find("Unknown attribute 'other'"), String::npos);
    }

}  // namespace BECore::Tests
