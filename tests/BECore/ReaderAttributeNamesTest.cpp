// IDeserializer::AttributeNames: the attributes of the open scope in document order, from the XML reader only.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Joined(const Vector<String>& names) {
            String text;
            for (const String& name : names) {
                text += name;
                text += ',';
            }
            return text;
        }

        String NamesOf(const IDeserializer& reader) {
            Vector<String> names;
            reader.AttributeNames(names);
            return Joined(names);
        }

        String Warnings(const IDeserializer& reader) {
            String text;
            for (const DeserializeError& warning : reader.GetWarnings()) {
                AppendFormat(text, "{}: {}\n", warning.path.ToStringView(), warning.errorMessage.ToStringView());
            }
            return text;
        }

    }  // namespace

    TEST(ReaderAttributeNamesTest, XmlListsTheAttributesOfTheOpenElementInDocumentOrder) {
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(R"(<root b="1" a="2"><context zeta="Z" alpha="A" mid="M"><child x="1"/></context><bare/></root>)"));
        EXPECT_EQ(NamesOf(reader), "b,a,");
        ASSERT_TRUE(reader.BeginObject("context"));
        EXPECT_EQ(NamesOf(reader), "zeta,alpha,mid,");
        reader.EndObject();
        ASSERT_TRUE(reader.BeginObject("bare"));
        EXPECT_EQ(NamesOf(reader), "");
        reader.EndObject();
    }

    TEST(ReaderAttributeNamesTest, TheNamesAreAppendedToTheListTheCallerPasses) {
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(R"(<root a="1"/>)"));
        Vector<String> names;
        names.push_back(String("kept"));
        reader.AttributeNames(names);
        EXPECT_EQ(Joined(names), "kept,a,");
    }

    TEST(ReaderAttributeNamesTest, XmlWithNoDocumentHasNone) {
        const XmlDeserializer reader;
        EXPECT_EQ(NamesOf(reader), "");
    }

    TEST(ReaderAttributeNamesTest, AskingTheNamesIsNoAskOfTheKeys) {
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(reader.LoadFromString(R"(<root><context caster="UnitStats" cast="CastInfo"/></root>)"));
        ASSERT_TRUE(reader.BeginObject("context"));
        EXPECT_EQ(NamesOf(reader), "caster,cast,");
        reader.EndObject();
        reader.FinishLoad();
        // Listed but never read: both are unknown attributes, as if the names had not been listed
        const String warnings = Warnings(reader);
        EXPECT_NE(warnings.find("Unknown attribute 'caster'"), String::npos) << warnings.c_str();
        EXPECT_NE(warnings.find("Unknown attribute 'cast'"), String::npos) << warnings.c_str();
    }

    TEST(ReaderAttributeNamesTest, AttributesTheCallerReadsAfterListingThemAreKnown) {
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(reader.LoadFromString(R"(<root><context caster="UnitStats" cast="CastInfo"/></root>)"));
        ASSERT_TRUE(reader.BeginObject("context"));
        Vector<String> names;
        reader.AttributeNames(names);
        for (const String& name : names) {
            String type;
            EXPECT_TRUE(reader.ReadAttribute(name, type));
        }
        reader.EndObject();
        reader.FinishLoad();
        EXPECT_TRUE(Warnings(reader).empty()) << Warnings(reader).c_str();
    }

    TEST(ReaderAttributeNamesTest, BinaryAndJsonKeepTheDefaultOfNone) {
        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("context"));
        serializer.WriteAttribute("caster", String("UnitStats"));
        serializer.EndObject();
        BinaryDeserializer binary;
        ASSERT_TRUE(binary.LoadFromBuffer(serializer.GetData()));
        ASSERT_TRUE(binary.BeginObject("context"));
        EXPECT_EQ(NamesOf(binary), "");
        binary.EndObject();

        const auto parsed = ParseJson(R"({"context":{"caster":"UnitStats"}})");
        ASSERT_TRUE(parsed.has_value());
        JsonDeserializer json(*parsed);
        EXPECT_EQ(NamesOf(json), "");
        ASSERT_TRUE(json.BeginObject("context"));
        EXPECT_EQ(NamesOf(json), "");
        json.EndObject();
    }

    TEST(ReaderAttributeNamesTest, OnlyJsonCallsAScalarMemberScalar) {
        const auto parsed = ParseJson(R"({"n":12,"s":"x","o":{},"a":[1],"z":null})");
        ASSERT_TRUE(parsed.has_value());
        JsonDeserializer json(*parsed);
        EXPECT_TRUE(json.IsScalarMember("n"));
        EXPECT_TRUE(json.IsScalarMember("s"));
        EXPECT_TRUE(json.IsScalarMember("z"));
        EXPECT_FALSE(json.IsScalarMember("o"));
        EXPECT_FALSE(json.IsScalarMember("a"));
        EXPECT_FALSE(json.IsScalarMember("missing"));
        EXPECT_FALSE(json.HasErrors());

        XmlDeserializer xml;
        ASSERT_TRUE(xml.LoadFromString(R"(<root n="12"/>)"));
        EXPECT_FALSE(xml.IsScalarMember("n"));
    }

}  // namespace BECore::Tests
