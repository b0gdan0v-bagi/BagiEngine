#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        const PoolString kPanelConfig = "OverlayConfig"_intern;

        // Blank lines and indentation of the source that SaveToFile keeps when it lays the text out like the file that was there
        constexpr StringView kSourceText = "<OverlayConfig value=\"one\">\n\n    <Child a=\"1\"/>\n\n</OverlayConfig>\n";

        /// Folders of one test: a content root with config/ and assets/, and the folder the overlay uses
        class ContentOverlayTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _base = TestTempDirectory("BagiContentOverlayTest");
                MakeEmptyDirectory(_base);
                _root = _base / "root";
                _overlay = _base / "overlay";
                _source = _root / "config" / "OverlayConfig.xml";
                WriteFile(_source, kSourceText);
                MakeDirectory(_root / "assets");
            }

            void TearDown() override {
                RemoveDirectory(_base);
            }

            Path OverlayFile() const {
                return _overlay / "config" / "OverlayConfig.xml";
            }

            static void ChangeValue(const ConfigManager& manager, StringView value) {
                manager.GetConfig(kPanelConfig).GetPugiNode().attribute("value") = String(value).c_str();
            }

            Path _base;
            Path _root;
            Path _overlay;
            Path _source;
        };

    }  // namespace

    TEST_F(ContentOverlayTest, WriteTargetMapsAFileOfALayerBelowTheOverlayAndLeavesOtherPathsAlone) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();
        const Path nested = _root / "assets" / "ui" / "icon.png";
        const Path outside = _base / "elsewhere.xml";

        EXPECT_EQ(fileSystem.WriteTarget(_source), (_overlay / "config" / "OverlayConfig.xml").LexicallyNormal());
        EXPECT_EQ(fileSystem.WriteTarget(nested), (_overlay / "assets" / "ui" / "icon.png").LexicallyNormal());
        EXPECT_EQ(fileSystem.WriteTarget(OverlayFile()), OverlayFile());
        EXPECT_EQ(fileSystem.WriteTarget(outside), outside);
    }

    TEST_F(ContentOverlayTest, WithoutAnOverlayAWriteGoesWhereTheFileIs) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        fileSystem.Initialize();

        EXPECT_TRUE(fileSystem.GetContentOverlay().Empty());
        EXPECT_EQ(fileSystem.WriteTarget(_source), _source);
    }

    TEST_F(ContentOverlayTest, TheOverlayIsMadeAndIsTheTopLayerOfBothMountPoints) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();

        EXPECT_TRUE(IsFolder(_overlay / "config"));
        EXPECT_TRUE(IsFolder(_overlay / "assets"));
        EXPECT_EQ(fileSystem.GetMountedPath(MountPoints::config), (_overlay / "config").LexicallyNormal());
        EXPECT_EQ(fileSystem.GetMountedPath(MountPoints::assets), (_overlay / "assets").LexicallyNormal());
        EXPECT_EQ(fileSystem.ResolvePath("config/OverlayConfig.xml"), _source.LexicallyNormal());

        WriteFile(OverlayFile(), "<OverlayConfig value=\"copy\"/>\n");
        EXPECT_EQ(fileSystem.ResolvePath("config/OverlayConfig.xml"), OverlayFile().LexicallyNormal());
    }

    TEST_F(ContentOverlayTest, AnEmptyFolderIsAnErrorAndLeavesNoOverlay) {
        FileSystem fileSystem;

        EXPECT_FALSE(fileSystem.SetContentOverlay(Path{}));
        EXPECT_TRUE(fileSystem.GetContentOverlay().Empty());
    }

    TEST_F(ContentOverlayTest, AFolderThatIsAFileCannotBeMadeAndIsAnError) {
        WriteFile(_overlay, "not a folder");
        FileSystem fileSystem;

        EXPECT_FALSE(fileSystem.SetContentOverlay(_overlay / "inside"));
        EXPECT_TRUE(fileSystem.GetContentOverlay().Empty());
    }

    TEST_F(ContentOverlayTest, ASaveLeavesTheSourceByteForByteAndWritesTheOverlay) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();
        ConfigManager manager(fileSystem);
        manager.Initialize();
        ChangeValue(manager, "two");

        ASSERT_TRUE(manager.SaveConfig(kPanelConfig));

        EXPECT_EQ(ReadFile(_source), String(kSourceText));
        const String saved = ReadFile(OverlayFile());
        EXPECT_NE(saved.find("value=\"two\""), String::npos) << saved.c_str();
        EXPECT_EQ(manager.GetConfigFilePath(kPanelConfig), OverlayFile().LexicallyNormal());
    }

    TEST_F(ContentOverlayTest, TheOverlayCopyKeepsTheLayoutOfTheSourceLikeASaveInPlace) {
        // The same edit saved in place into a second root is what the overlay must hold
        const Path twin = _base / "twin";
        WriteFile(twin / "config" / "OverlayConfig.xml", kSourceText);
        FileSystem plain;
        plain.AddContentRoot(twin);
        plain.Initialize();
        ConfigManager plainManager(plain);
        plainManager.Initialize();
        ChangeValue(plainManager, "two");
        ASSERT_TRUE(plainManager.SaveConfig(kPanelConfig));
        EXPECT_EQ(plain.WriteTarget(twin / "config" / "OverlayConfig.xml"), twin / "config" / "OverlayConfig.xml");

        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();
        ConfigManager manager(fileSystem);
        manager.Initialize();
        ChangeValue(manager, "two");
        ASSERT_TRUE(manager.SaveConfig(kPanelConfig));

        EXPECT_EQ(ReadFile(OverlayFile()), ReadFile(twin / "config" / "OverlayConfig.xml"));
    }

    TEST_F(ContentOverlayTest, ASecondSaveRewritesTheOverlayFile) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();
        ConfigManager manager(fileSystem);
        manager.Initialize();
        ChangeValue(manager, "two");
        ASSERT_TRUE(manager.SaveConfig(kPanelConfig));

        ChangeValue(manager, "three");
        ASSERT_TRUE(manager.SaveConfig(kPanelConfig));

        EXPECT_NE(ReadFile(OverlayFile()).find("value=\"three\""), String::npos);
        EXPECT_EQ(ReadFile(_source), String(kSourceText));
    }

    TEST_F(ContentOverlayTest, AReloadReadsTheOverlayFileAfterASave) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();
        ConfigManager manager(fileSystem);
        manager.Initialize();
        ChangeValue(manager, "two");
        ASSERT_TRUE(manager.SaveConfig(kPanelConfig));

        WriteFile(OverlayFile(), "<OverlayConfig value=\"edited by hand\"/>\n");
        ASSERT_TRUE(manager.ReloadConfig(kPanelConfig));

        EXPECT_STREQ(manager.GetConfig(kPanelConfig).GetPugiNode().attribute("value").as_string(), "edited by hand");
    }

    TEST_F(ContentOverlayTest, ASecondRunStartsFromTheOverlayFile) {
        WriteFile(OverlayFile(), "<OverlayConfig value=\"from the last run\"/>\n");
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        ASSERT_TRUE(fileSystem.SetContentOverlay(_overlay));
        fileSystem.Initialize();
        ConfigManager manager(fileSystem);

        manager.Initialize();

        EXPECT_STREQ(manager.GetConfig(kPanelConfig).GetPugiNode().attribute("value").as_string(), "from the last run");
    }

    TEST_F(ContentOverlayTest, WithoutAnOverlayASaveWritesTheSource) {
        FileSystem fileSystem;
        fileSystem.AddContentRoot(_root);
        fileSystem.Initialize();
        ConfigManager manager(fileSystem);
        manager.Initialize();
        ChangeValue(manager, "two");

        ASSERT_TRUE(manager.SaveConfig(kPanelConfig));

        EXPECT_NE(ReadFile(_source).find("value=\"two\""), String::npos);
        EXPECT_EQ(manager.GetConfigFilePath(kPanelConfig), _source.LexicallyNormal());
    }

}  // namespace BECore::Tests
