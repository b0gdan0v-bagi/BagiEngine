#include "ConfigDeserializerTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Generated/ConfigDeserializerTest.gen.hpp>
#include <fstream>

namespace BECore::Tests {

    namespace {

        constexpr const char* kMountPoint = "BagiConfigDeserializerTest";
        constexpr int32_t kProbeWidth = 640;
        constexpr eastl::string_view kProbeTitle = "probe";

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

    }  // namespace

    /// Needs FileSystem and ConfigManager. A temp directory is mounted as a virtual path for the file cases.
    class ConfigDeserializerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            // One directory per test: ctest runs every test in its own process
            _dir = std::filesystem::temp_directory_path() / Format("BagiConfigDeserializerTest_{}", info->name()).c_str();
            std::filesystem::remove_all(_dir);
            std::filesystem::create_directories(_dir);
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kMountPoint), _dir));
        }

        void TearDown() override {
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(kMountPoint));
            std::error_code ec;
            std::filesystem::remove_all(_dir, ec);
        }

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

        static eastl::string VirtualPath(const char* fileName) {
            return Format("{}/{}", kMountPoint, fileName);
        }

        std::filesystem::path _dir;
    };

    TEST_F(ConfigDeserializerTest, CreateDeserializerReadsKnownConfig) {
        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer("ApplicationConfig"_intern);
        ASSERT_NE(deserializer, nullptr);

        eastl::string path;
        EXPECT_TRUE(deserializer->ReadAttribute("path", path));
        EXPECT_FALSE(path.empty());
        EXPECT_FALSE(deserializer->HasErrors());
    }

    TEST_F(ConfigDeserializerTest, CreateDeserializerStringViewOverloadReadsKnownConfig) {
        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(eastl::string_view("ApplicationConfig"));
        ASSERT_NE(deserializer, nullptr);

        eastl::string mode;
        EXPECT_TRUE(deserializer->ReadAttribute("mode", mode));
        EXPECT_FALSE(mode.empty());
    }

    TEST_F(ConfigDeserializerTest, CreateDeserializerOfUnknownConfigIsNull) {
        EXPECT_EQ(CoreManager::GetConfigManager().CreateDeserializer("NoSuchConfig"_intern), nullptr);
        EXPECT_EQ(CoreManager::GetConfigManager().CreateDeserializer(eastl::string_view("NoSuchConfig")), nullptr);
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
        EXPECT_NE(opened.error().find(".json"), eastl::string::npos) << opened.error().c_str();
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

}  // namespace BECore::Tests
