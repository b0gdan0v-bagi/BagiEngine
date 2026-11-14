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

        /// The `type` of every <widget> in the config named @p configName.
        Vector<String> WidgetTypes(PoolString configName) {
            Vector<String> types;
            const XmlNode widgets = CoreManager::GetConfigManager().GetConfig(configName).GetChild("widgets");
            for (const XmlNode widget : widgets.Children()) {
                if (const auto type = widget.ParseAttribute<StringView>("type")) {
                    types.emplace_back(type->data(), type->size());
                }
            }
            return types;
        }

        bool Contains(const Vector<String>& types, const char* name) {
            return eastl::find(types.begin(), types.end(), name) != types.end();
        }

    }  // namespace

    TEST(RunModeTest, ResolveKeepsGame) {
        EXPECT_EQ(ResolveRunMode(RunMode::Game), RunMode::Game);
    }

    // Editor survives only in a build that has the editor; otherwise it becomes Game (and logs one warning)
    TEST(RunModeTest, ResolveKeepsEditorOnlyInABuildWithTheEditor) {
        EXPECT_EQ(ResolveRunMode(RunMode::Editor), kEditorBuilt ? RunMode::Editor : RunMode::Game);
    }

    TEST(RunModeTest, OtherRunModeIsAnInvolution) {
        EXPECT_EQ(OtherRunMode(RunMode::Editor), RunMode::Game);
        EXPECT_EQ(OtherRunMode(RunMode::Game), RunMode::Editor);
        EXPECT_EQ(OtherRunMode(OtherRunMode(RunMode::Editor)), RunMode::Editor);
        EXPECT_EQ(OtherRunMode(OtherRunMode(RunMode::Game)), RunMode::Game);
    }

    namespace {
        constexpr SDL_Keymod kCtrlShift = SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT;
    }  // namespace

    TEST(RunModeTest, CtrlShiftEIsTheToggle) {
        EXPECT_TRUE(IsEditorToggle(SDLK_E, kCtrlShift, false));
    }

    TEST(RunModeTest, TheToggleTakesEitherCtrlAndEitherShift) {
        EXPECT_TRUE(IsEditorToggle(SDLK_E, SDL_KMOD_RCTRL | SDL_KMOD_LSHIFT, false));
        EXPECT_TRUE(IsEditorToggle(SDLK_E, SDL_KMOD_LCTRL | SDL_KMOD_RSHIFT, false));
        EXPECT_TRUE(IsEditorToggle(SDLK_E, SDL_KMOD_RCTRL | SDL_KMOD_RSHIFT, false));
    }

    TEST(RunModeTest, TheToggleIgnoresLockKeys) {
        EXPECT_TRUE(IsEditorToggle(SDLK_E, kCtrlShift | SDL_KMOD_CAPS | SDL_KMOD_NUM, false));
    }

    TEST(RunModeTest, ARepeatIsNotTheToggle) {
        EXPECT_FALSE(IsEditorToggle(SDLK_E, kCtrlShift, true));
    }

    TEST(RunModeTest, OtherChordsAreNotTheToggle) {
        EXPECT_FALSE(IsEditorToggle(SDLK_E, SDL_KMOD_NONE, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_E, SDL_KMOD_LCTRL, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_E, SDL_KMOD_LSHIFT, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_Q, kCtrlShift, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_E, kCtrlShift | SDL_KMOD_LALT, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_E, kCtrlShift | SDL_KMOD_RALT, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_E, SDL_KMOD_LGUI | SDL_KMOD_LSHIFT, false));
        EXPECT_FALSE(IsEditorToggle(SDLK_E, kCtrlShift | SDL_KMOD_LGUI, false));
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
