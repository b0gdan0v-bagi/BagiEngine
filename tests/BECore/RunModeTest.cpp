#include <BECore/Application/RunMode.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(EnumUtils<RunMode>::Count() == 2);
    static_assert(EnumUtils<RunMode>::ToString(RunMode::Editor) == "Editor");
    static_assert(EnumUtils<RunMode>::ToString(RunMode::Game) == "Game");

    TEST(RunModeTest, CompileTimeChecks) {}

    namespace {

        /// The <root> element of @p xml, the shape ConfigManager::GetConfig() hands out. @p config keeps the document alive.
        XmlNode ParseRoot(const XmlConfig& config, const char* xml) {
            EXPECT_TRUE(config.LoadFromString(xml)) << xml;
            return config.GetRoot();
        }

        /// The `type` of every <widget> in the config named @p configName.
        eastl::vector<eastl::string> WidgetTypes(PoolString configName) {
            eastl::vector<eastl::string> types;
            const XmlNode widgets = CoreManager::GetConfigManager().GetConfig(configName).GetChild("widgets");
            for (const XmlNode widget : widgets.Children()) {
                if (const auto type = widget.ParseAttribute<eastl::string_view>("type")) {
                    types.emplace_back(type->data(), type->size());
                }
            }
            return types;
        }

        bool Contains(const eastl::vector<eastl::string>& types, const char* name) {
            return eastl::find(types.begin(), types.end(), name) != types.end();
        }

    }  // namespace

    TEST(RunModeTest, MissingModeIsEditor) {
        const XmlConfig config = XmlConfig::Create();
        EXPECT_EQ(ReadRunMode(ParseRoot(config, R"(<root type="SDL3ApplicationFabric"/>)")), RunMode::Editor);
    }

    TEST(RunModeTest, ReadsEveryMode) {
        const XmlConfig editor = XmlConfig::Create();
        EXPECT_EQ(ReadRunMode(ParseRoot(editor, R"(<root mode="Editor"/>)")), RunMode::Editor);

        const XmlConfig game = XmlConfig::Create();
        EXPECT_EQ(ReadRunMode(ParseRoot(game, R"(<root mode="Game"/>)")), RunMode::Game);
    }

    TEST(RunModeTest, UnknownModeFallsBackToEditor) {
        const XmlConfig misspelled = XmlConfig::Create();
        EXPECT_EQ(ReadRunMode(ParseRoot(misspelled, R"(<root mode="game"/>)")), RunMode::Editor);

        const XmlConfig empty = XmlConfig::Create();
        EXPECT_EQ(ReadRunMode(ParseRoot(empty, R"(<root mode=""/>)")), RunMode::Editor);
    }

    TEST(RunModeTest, AMissingConfigIsEditor) {
        EXPECT_EQ(ReadRunMode(XmlNode{pugi::xml_node{}}), RunMode::Editor);
    }

    TEST(RunModeTest, EachModeHasItsOwnWidgetsConfig) {
        EXPECT_EQ(GetWidgetsConfigName(RunMode::Editor), "WidgetsConfig"_intern);
        EXPECT_EQ(GetWidgetsConfigName(RunMode::Game), "GameWidgetsConfig"_intern);
    }

    /// The engine's own configs: they pin what each mode creates.
    class RunModeConfigTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(RunModeConfigTest, EngineStartsInEditorMode) {
        const XmlNode applicationConfig = CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern);
        ASSERT_TRUE(applicationConfig);
        EXPECT_EQ(ReadRunMode(applicationConfig), RunMode::Editor);
    }

    TEST_F(RunModeConfigTest, EditorModeKeepsTheViewportAndTheEditors) {
        const auto types = WidgetTypes(GetWidgetsConfigName(RunMode::Editor));
        EXPECT_TRUE(Contains(types, "ImGuiWidget"));
        EXPECT_TRUE(Contains(types, "ViewportWidget"));
        EXPECT_TRUE(Contains(types, "SceneEditorWidget"));
        EXPECT_FALSE(Contains(types, "SceneRenderWidget"));
    }

    TEST_F(RunModeConfigTest, GameModeDrawsTheSceneWithoutImGui) {
        const auto types = WidgetTypes(GetWidgetsConfigName(RunMode::Game));
        EXPECT_TRUE(Contains(types, "ClearScreenWidget"));
        EXPECT_TRUE(Contains(types, "SceneRenderWidget"));
        EXPECT_FALSE(Contains(types, "ImGuiWidget"));
        EXPECT_FALSE(Contains(types, "ViewportWidget"));
        EXPECT_FALSE(Contains(types, "SceneEditorWidget"));
    }

}  // namespace BECore::Tests
