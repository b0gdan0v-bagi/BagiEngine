#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/Export/ExportScenes.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Two config folders of a game, the engine's below the game's
        class ExportScenesTest : public ::testing::Test {
        protected:
            ExportScenesTest() {
                MakeDirectory(_layers[0]);
                MakeDirectory(_layers[1]);
            }

            std::expected<Vector<String>, String> Read() const {
                return ReadExportScenes({_layers.data(), _layers.size()});
            }

            TestTempFolder _folder{"export_scenes"};
            Vector<Path> _layers{Path(_folder.PathOf("engine")), Path(_folder.PathOf("game"))};
        };

        constexpr StringView kEngineScenes = "<SceneManager><scenes><scene name=\"EngineOnly\"/></scenes></SceneManager>";

    }  // namespace

    TEST_F(ExportScenesTest, TheScenesAreThoseOfTheHighestLayerThatHasTheFileInItsOrder) {
        WriteFile(_layers[0] / "SceneConfig.xml", kEngineScenes);
        const auto below = Read();
        ASSERT_TRUE(below.has_value()) << below.error().c_str();
        EXPECT_EQ(*below, (Vector<String>{"EngineOnly"}));

        WriteFile(_layers[1] / "SceneConfig.xml", "<?xml version=\"1.0\"?>\n<SceneManager>\n  <!-- a note -- <scene name=\"Gone\"/> -->\n  <scenes>\n"
                                                  "    <scene name=\"Menu &amp; Credits\"><nodes><node name=\"inner\"/></nodes></scene>\n    <scene name=\"Level\"/>\n  </scenes>\n</SceneManager>\n");
        const auto top = Read();
        ASSERT_TRUE(top.has_value()) << top.error().c_str();
        EXPECT_EQ(*top, (Vector<String>{"Menu & Credits", "Level"}));
    }

    TEST_F(ExportScenesTest, AGameWhoseScenesCannotBeReadSaysSoAndNeverShowsThoseOfALowerLayer) {
        EXPECT_FALSE(Read().has_value()) << "no layer has the file";
        WriteFile(_layers[0] / "SceneConfig.xml", kEngineScenes);

        WriteFile(_layers[1] / "SceneConfig.bin", "cooked");
        const auto cooked = Read();
        ASSERT_FALSE(cooked.has_value());
        EXPECT_NE(cooked.error().find("cooked"), String::npos) << cooked.error().c_str();

        WriteFile(_layers[1] / "SceneConfig.xml", "<SceneManager><scenes>");
        EXPECT_FALSE(Read().has_value());

        WriteFile(_layers[1] / "SceneConfig.xml", "<SceneManager/>");
        EXPECT_FALSE(Read().has_value());
    }

}  // namespace BECore::Tests
