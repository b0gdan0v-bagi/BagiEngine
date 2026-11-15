#include <BECore/AssetPartsTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Resource/AssetParts.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Generated/AssetPartsTest.gen.hpp>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskManager.h>
#include <chrono>
#include <fstream>

namespace BECore::Tests {

    using namespace AssetPartsTestTypes;

    namespace {

        constexpr const char* kMountPoint = "BagiAssetPartsTest";
        constexpr int32_t kSimValue = 1;
        constexpr int32_t kVisualValue = 2;
        constexpr int32_t kEditorValue = 3;
        constexpr int32_t kSplitValue = 9;
        constexpr auto kOlder = std::chrono::hours(1);

        constexpr const char* kWholeXml = R"(<asset>
            <sim><PartSimProbe value="1"/></sim>
            <visual><PartVisualDerived value="2"/><PartVisualProbe value="2"/></visual>
            <editor><PartEditorProbe value="3"/></editor>
        </asset>)";

        constexpr const char* kSplitSimXml = R"(<sim><PartSimProbe value="9"/></sim>)";

        Task<Optional<PartSimProbe>> LoadSimAsync(String path) {
            auto loaded = co_await CoreManager::GetResourceManager().LoadSerializedPartAsync<PartSimProbe>(path, AssetPurpose::Sim);
            co_return loaded ? *loaded : Optional<PartSimProbe>{};
        }

    }  // namespace

    /// Needs FileSystem, ResourceManager and TaskManager. A temp directory is mounted as a virtual path for the files.
    class AssetPartsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            // One directory per test: ctest runs every test in its own process
            _dir = TestTempDirectory("BagiAssetPartsTest");
            std::filesystem::remove_all(_dir);
            std::filesystem::create_directories(_dir);
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kMountPoint), _dir));
            PartVisualDerived::constructed = 0;
        }

        void TearDown() override {
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(kMountPoint));
            std::error_code ec;
            std::filesystem::remove_all(_dir, ec);
        }

        void WriteFile(const char* fileName, const char* text) const {
            std::ofstream(_dir / fileName) << text;  //-NoStd: the test writes the file below its own mounted temp folder, outside the engine's file layer
        }

        /// Moves the modification time of a file @p age into the past
        void Age(const char* fileName, std::chrono::hours age) const {
            std::filesystem::last_write_time(_dir / fileName, std::filesystem::file_time_type::clock::now() - age);
        }

        static String VirtualPath(const char* fileName) {
            return Format("{}/{}", kMountPoint, fileName);
        }

        template <typename T>
        static Optional<T> Load(const char* fileName, AssetPurpose part) {
            return CoreManager::GetResourceManager().LoadSerializedPart<T>(VirtualPath(fileName), part);
        }

        std::filesystem::path _dir;
    };

    TEST(AssetPartsNames, ThePartNamesAndTheSplitPath) {
        EXPECT_EQ(PartName(AssetPurpose::Sim), "sim");
        EXPECT_EQ(PartName(AssetPurpose::Visual), "visual");
        EXPECT_EQ(PartName(AssetPurpose::Editor), "editor");
        EXPECT_TRUE(PartName(AssetPurpose::Common).empty());

        EXPECT_EQ(SplitPartPath("gems/gem.asset.xml", AssetPurpose::Sim), "gems/gem.sim.xml");
        EXPECT_EQ(SplitPartPath("gem.asset.xml", AssetPurpose::Visual), "gem.visual.xml");
        EXPECT_EQ(SplitPartPath("gem.asset.xml", AssetPurpose::Editor), "gem.editor.xml");
        EXPECT_TRUE(SplitPartPath("gem.xml", AssetPurpose::Sim).empty());
        EXPECT_TRUE(SplitPartPath("gem.asset.bin", AssetPurpose::Sim).empty());
        EXPECT_TRUE(SplitPartPath("gem.asset.xml", AssetPurpose::Common).empty());
        EXPECT_TRUE(SplitPartPath(".asset.xml.bak", AssetPurpose::Sim).empty());
    }

    TEST_F(AssetPartsTest, EachPartGivesOnlyItsOwnObjectAndNothingOfTheOthersIsCreated) {
        WriteFile("gem.asset.xml", kWholeXml);

        const auto sim = Load<PartSimProbe>("gem.asset.xml", AssetPurpose::Sim);
        ASSERT_TRUE(sim.has_value());
        EXPECT_EQ(sim->value, kSimValue);
        EXPECT_EQ(PartVisualDerived::constructed, 0);

        const auto visual = Load<PartVisualProbe>("gem.asset.xml", AssetPurpose::Visual);
        ASSERT_TRUE(visual.has_value());
        EXPECT_EQ(visual->value, kVisualValue);
        const auto editor = Load<PartEditorProbe>("gem.asset.xml", AssetPurpose::Editor);
        ASSERT_TRUE(editor.has_value());
        EXPECT_EQ(editor->value, kEditorValue);
    }

    TEST_F(AssetPartsTest, AnAbsentPartObjectOrFileGivesEmpty) {
        WriteFile("gem.asset.xml", R"(<asset><sim><PartSimProbe value="1"/></sim></asset>)");

        EXPECT_FALSE(Load<PartEditorProbe>("gem.asset.xml", AssetPurpose::Editor).has_value());
        EXPECT_FALSE(Load<PartCommonProbe>("gem.asset.xml", AssetPurpose::Sim).has_value());
        EXPECT_FALSE(Load<PartSimProbe>("missing.asset.xml", AssetPurpose::Sim).has_value());
        EXPECT_FALSE(Load<PartSimProbe>("gem.asset.xml", AssetPurpose::Common).has_value());
    }

    TEST_F(AssetPartsTest, AVisualTypeInTheSimPartIsRefusedAtItsPathAndNothingOfItIsCreatedOrFilled) {
        WriteFile("gem.asset.xml", R"(<asset><sim><PartSimHolder>
            <simPart value="1"/>
            <visualPart value="5"/>
            <elements><element type="PartVisualDerived" value="6"/></elements>
        </PartSimHolder></sim></asset>)");

        auto opened = OpenAssetPart(VirtualPath("gem.asset.xml"), AssetPurpose::Sim);
        ASSERT_TRUE(opened.has_value());
        ASSERT_TRUE(*opened);
        EXPECT_EQ((*opened)->GetPurposeFilter(), AssetPurpose::Sim);
        ASSERT_TRUE((*opened)->BeginObject("PartSimHolder"));
        PartSimHolder holder;
        holder.Deserialize(**opened);
        (*opened)->EndObject();

        EXPECT_EQ(holder.simPart.value, kSimValue);
        EXPECT_EQ(holder.visualPart.value, 0);
        EXPECT_TRUE(holder.elements.empty());
        EXPECT_EQ(PartVisualDerived::constructed, 0);
        ASSERT_EQ((*opened)->GetErrors().size(), 2u);
        // The reader of a whole file stands inside the part element, so the path starts with the part
        EXPECT_EQ((*opened)->GetErrors()[0].path.ToStringView(), "sim.PartSimHolder.visualPart");

        const auto loaded = Load<PartSimHolder>("gem.asset.xml", AssetPurpose::Sim);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->visualPart.value, 0);
    }

    TEST_F(AssetPartsTest, AVisualObjectNamedInTheSimPartStaysDefault) {
        WriteFile("gem.asset.xml", R"(<asset><sim><PartVisualProbe value="5"/></sim></asset>)");

        const auto loaded = Load<PartVisualProbe>("gem.asset.xml", AssetPurpose::Sim);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->value, 0);
    }

    TEST_F(AssetPartsTest, ANewerSplitFileOfThePartWins) {
        WriteFile("gem.asset.xml", kWholeXml);
        Age("gem.asset.xml", kOlder);
        WriteFile("gem.sim.xml", kSplitSimXml);

        const auto sim = Load<PartSimProbe>("gem.asset.xml", AssetPurpose::Sim);
        ASSERT_TRUE(sim.has_value());
        EXPECT_EQ(sim->value, kSplitValue);
        // The other parts have no split file: they come from the whole file
        const auto visual = Load<PartVisualProbe>("gem.asset.xml", AssetPurpose::Visual);
        ASSERT_TRUE(visual.has_value());
        EXPECT_EQ(visual->value, kVisualValue);
    }

    TEST_F(AssetPartsTest, AnOlderSplitFileIsIgnored) {
        WriteFile("gem.sim.xml", kSplitSimXml);
        Age("gem.sim.xml", kOlder);
        WriteFile("gem.asset.xml", kWholeXml);

        const auto sim = Load<PartSimProbe>("gem.asset.xml", AssetPurpose::Sim);
        ASSERT_TRUE(sim.has_value());
        EXPECT_EQ(sim->value, kSimValue);
    }

    TEST_F(AssetPartsTest, ASplitFileServesWhenTheWholeFileIsMissing) {
        WriteFile("gem.sim.xml", kSplitSimXml);

        const auto sim = Load<PartSimProbe>("gem.asset.xml", AssetPurpose::Sim);
        ASSERT_TRUE(sim.has_value());
        EXPECT_EQ(sim->value, kSplitValue);
    }

    TEST_F(AssetPartsTest, ABinaryWholeFileIsReadByPartName) {
        BinarySerializer serializer;
        const auto writePart = [&serializer](StringView part, StringView object, int32_t value) {
            ASSERT_TRUE(serializer.BeginObject(part));
            ASSERT_TRUE(serializer.BeginObject(object));
            serializer.Write("value", value);
            serializer.EndObject();
            serializer.EndObject();
        };
        ASSERT_NO_FATAL_FAILURE(writePart("sim", "PartSimProbe", kSimValue));
        ASSERT_NO_FATAL_FAILURE(writePart("visual", "PartVisualProbe", kVisualValue));
        ASSERT_TRUE(serializer.SaveToFile(_dir / "gem.asset.bin"));

        const auto sim = Load<PartSimProbe>("gem.asset.bin", AssetPurpose::Sim);
        ASSERT_TRUE(sim.has_value());
        EXPECT_EQ(sim->value, kSimValue);
        const auto visual = Load<PartVisualProbe>("gem.asset.bin", AssetPurpose::Visual);
        ASSERT_TRUE(visual.has_value());
        EXPECT_EQ(visual->value, kVisualValue);
        EXPECT_FALSE(Load<PartEditorProbe>("gem.asset.bin", AssetPurpose::Editor).has_value());
    }

    TEST_F(AssetPartsTest, TheAsyncVersionLoadsTheSamePart) {
        WriteFile("gem.asset.xml", kWholeXml);

        auto handle = CoreManager::GetTaskManager().Run(LoadSimAsync(VirtualPath("gem.asset.xml")));
        auto result = handle->GetResult();

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->has_value());
        EXPECT_EQ((*result)->value, kSimValue);
    }

}  // namespace BECore::Tests
