#include "ConfigDeserializerTest.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/MountedFolderTest.h>
#include <Common/Printers.h>
#include <Generated/ConfigDeserializerTest.gen.hpp>

namespace BECore::Tests {

    namespace {

        constexpr const char* kMountPoint = "BagiConfigDeserializerTest";
        constexpr int32_t kProbeWidth = 640;
        constexpr StringView kProbeTitle = "probe";

        DeserializerProbe MakeProbe() {
            DeserializerProbe probe;
            probe.width = kProbeWidth;
            probe.title = PoolString::Intern(kProbeTitle);
            return probe;
        }

        void ExpectProbe(const DeserializerProbe& probe) {
            EXPECT_EQ(probe.width, kProbeWidth);
            EXPECT_EQ(probe.title.ToStringView(), kProbeTitle);
        }

        /// The DOM of a config file as the editor keeps it: comments and the declaration stay in it
        bool ParseWithComments(pugi::xml_document& document, const char* xml) {
            return document.load_string(xml, pugi::parse_full);
        }

        constexpr const char* kCommentedItems = R"(<?xml version="1.0"?>
<list>
  <!-- first -->
  <items>
    <!-- before the first element -->
    <item width="1" title="a" />
    <!-- between the elements -->
    <item width="2" title="b" />
    <item width="3" title="c" />
    <!-- after the last element -->
  </items>
</list>)";

    }  // namespace

    /// Needs FileSystem and ConfigManager. A temp directory is mounted as a virtual path for the file cases.
    class ConfigDeserializerTest : public MountedFolderTest {
    protected:
        ConfigDeserializerTest() : MountedFolderTest(kMountPoint) {}

        void WriteProbeXml(const char* fileName) const {
            XmlSerializer serializer;
            ASSERT_TRUE(serializer.BeginObject(ReflectionTraits<DeserializerProbe>::name));
            MakeProbe().Serialize(serializer);
            serializer.EndObject();
            std::ofstream(_dir / fileName) << serializer.SaveToString().c_str();
        }

        void WriteProbeBinary(const char* fileName) const {
            BinarySerializer serializer;
            ASSERT_TRUE(serializer.BeginObject(ReflectionTraits<DeserializerProbe>::name));
            MakeProbe().Serialize(serializer);
            serializer.EndObject();
            ASSERT_TRUE(serializer.SaveToFile(_dir / fileName));
        }
    };

    TEST_F(ConfigDeserializerTest, CreateDeserializerReadsKnownConfig) {
        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer("ApplicationConfig"_intern);
        ASSERT_NE(deserializer, nullptr);

        String path;
        EXPECT_TRUE(deserializer->ReadAttribute("path", path));
        EXPECT_FALSE(path.empty());
        EXPECT_FALSE(deserializer->HasErrors());
    }

    TEST_F(ConfigDeserializerTest, CreateDeserializerStringViewOverloadReadsKnownConfig) {
        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(StringView("ApplicationConfig"));
        ASSERT_NE(deserializer, nullptr);

        String mode;
        EXPECT_TRUE(deserializer->ReadAttribute("mode", mode));
        EXPECT_FALSE(mode.empty());
    }

    TEST_F(ConfigDeserializerTest, CreateDeserializerOfUnknownConfigIsNull) {
        EXPECT_EQ(CoreManager::GetConfigManager().CreateDeserializer("NoSuchConfig"_intern), nullptr);
        EXPECT_EQ(CoreManager::GetConfigManager().CreateDeserializer(StringView("NoSuchConfig")), nullptr);
    }

    TEST_F(ConfigDeserializerTest, OpenDeserializerReadsXmlFile) {
        ASSERT_NO_FATAL_FAILURE(WriteProbeXml("probe.xml"));

        auto opened = OpenDeserializer(VirtualPath("probe.xml"));
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();

        DeserializerProbe probe;
        ASSERT_TRUE((*opened)->BeginObject(ReflectionTraits<DeserializerProbe>::name));
        probe.Deserialize(**opened);
        (*opened)->EndObject();
        EXPECT_FALSE((*opened)->HasErrors());
        ExpectProbe(probe);
    }

    TEST_F(ConfigDeserializerTest, OpenDeserializerReadsBinaryFile) {
        ASSERT_NO_FATAL_FAILURE(WriteProbeBinary("probe.bin"));

        auto opened = OpenDeserializer(VirtualPath("probe.bin"));
        ASSERT_TRUE(opened.has_value()) << opened.error().c_str();

        DeserializerProbe probe;
        ASSERT_TRUE((*opened)->BeginObject(ReflectionTraits<DeserializerProbe>::name));
        probe.Deserialize(**opened);
        (*opened)->EndObject();
        EXPECT_FALSE((*opened)->HasErrors());
        ExpectProbe(probe);
    }

    TEST_F(ConfigDeserializerTest, OpenDeserializerRejectsUnknownExtension) {
        std::ofstream(_dir / "probe.json") << "{}";

        auto opened = OpenDeserializer(VirtualPath("probe.json"));
        ASSERT_FALSE(opened.has_value());
        EXPECT_NE(opened.error().find(".json"), String::npos) << opened.error().c_str();
    }

    TEST_F(ConfigDeserializerTest, OpenDeserializerReportsMissingFiles) {
        EXPECT_FALSE(OpenDeserializer(VirtualPath("missing.xml")).has_value());
        EXPECT_FALSE(OpenDeserializer(VirtualPath("missing.bin")).has_value());
    }

    TEST_F(ConfigDeserializerTest, OpenDeserializerReportsCorruptBinary) {
        std::ofstream(_dir / "corrupt.bin") << "not a binary stream";

        EXPECT_FALSE(OpenDeserializer(VirtualPath("corrupt.bin")).has_value());
    }

    TEST_F(ConfigDeserializerTest, LoadSerializedReadsXmlFile) {
        ASSERT_NO_FATAL_FAILURE(WriteProbeXml("probe.xml"));

        const DeserializerProbe probe = CoreManager::GetResourceManager().LoadSerialized<DeserializerProbe>(VirtualPath("probe.xml"));

        ExpectProbe(probe);
    }

    TEST_F(ConfigDeserializerTest, LoadSerializedReadsBinaryFile) {
        ASSERT_NO_FATAL_FAILURE(WriteProbeBinary("probe.bin"));

        const DeserializerProbe probe = CoreManager::GetResourceManager().LoadSerialized<DeserializerProbe>(VirtualPath("probe.bin"));

        ExpectProbe(probe);
    }

    TEST_F(ConfigDeserializerTest, LoadSerializedOfMissingFileIsDefault) {
        const DeserializerProbe probe = CoreManager::GetResourceManager().LoadSerialized<DeserializerProbe>(VirtualPath("missing.xml"));

        EXPECT_EQ(probe.width, DeserializerProbe{}.width);
        EXPECT_TRUE(probe.title.ToStringView().empty());
    }

    TEST_F(ConfigDeserializerTest, CommentsInAnArrayAreNoElements) {
        pugi::xml_document document;
        ASSERT_TRUE(ParseWithComments(document, kCommentedItems));

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode(document.document_element())));
        deserializer.SetDiagnostics(DiagnosticsMode::Error);
        DeserializerProbeList list;
        list.Deserialize(deserializer);
        deserializer.FinishLoad();

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list.items.size(), 3u);
        EXPECT_EQ(list.items[0].width, 1);
        EXPECT_EQ(list.items[1].title.ToStringView(), "b");
        EXPECT_EQ(list.items[2].width, 3);
    }

    TEST_F(ConfigDeserializerTest, TheIndexOfAnErrorCountsElementsOnly) {
        pugi::xml_document document;
        ASSERT_TRUE(ParseWithComments(document, R"(<list><items>
            <!-- one --><item width="1"/><!-- two --><item width="2"/><!-- three --><item width="x"/></items></list>)"));

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode(document.document_element())));
        deserializer.SetStrictValues(true);
        DeserializerProbeList list;
        list.Deserialize(deserializer);

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        EXPECT_EQ(deserializer.GetErrors()[0].path, "items[2].width");
    }

    TEST_F(ConfigDeserializerTest, ACommentIsNoUnknownKey) {
        pugi::xml_document document;
        ASSERT_TRUE(ParseWithComments(document, R"(<DeserializerProbe width="5" title="t">
            <!-- a note --><!-- another --></DeserializerProbe>)"));

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode(document.document_element())));
        deserializer.SetDiagnostics(DiagnosticsMode::Error);
        DeserializerProbe probe;
        probe.Deserialize(deserializer);
        deserializer.FinishLoad();

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(probe.width, 5);
        EXPECT_EQ(probe.title.ToStringView(), "t");
    }

    TEST_F(ConfigDeserializerTest, ChildrenOfAnXmlNodeAreElementsOnly) {
        pugi::xml_document document;
        ASSERT_TRUE(ParseWithComments(document, R"(<a><!-- x --><b/><!-- y --><?pi z?><c/><!-- w --></a>)"));

        Vector<String> names;
        for (const XmlNode child : XmlNode(document.document_element()).Children()) {
            names.push_back(String(child.Name()));
        }

        ASSERT_EQ(names.size(), 2u);
        EXPECT_EQ(names[0], "b");
        EXPECT_EQ(names[1], "c");
    }

}  // namespace BECore::Tests
