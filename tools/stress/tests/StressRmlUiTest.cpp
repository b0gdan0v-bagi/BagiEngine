// The scene `rmlui` in the settings and the game: it exists only in a build with the RmlUi host (STRESS_WITH_RMLUI).

#include <BECore/Application/CommandLine.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypedRef.h>
#include <Common/EngineEnvironment.h>
#include <Stress/StressGame.h>
#include <Stress/StressGameControl.h>
#include <Stress/StressNames.h>
#include <Stress/StressSettings.h>
#include <gtest/gtest.h>

namespace BECore::Stress::Tests {

    namespace {

        std::expected<StressSettings, String> Parse(const Vector<String>& args) {
            CommandLine line;
            line.Set(args);
            return ParseStressSettings(line);
        }

    }  // namespace

#ifdef STRESS_WITH_RMLUI

    namespace {

        class StressRmlUiSceneTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                BECore::Tests::EngineEnvironment::EnsureCoreServices();
            }

            void TearDown() override {
                CoreManager::GetCommandLine().Set({});
            }
        };

    }  // namespace

    TEST_F(StressRmlUiSceneTest, TheFlagNamesTheSceneInAnyCase) {
        for (const char* flag : {"--stress-scene=rmlui", "--stress-scene=RmlUi"}) {
            const auto settings = Parse({flag});
            ASSERT_TRUE(settings.has_value()) << flag;
            EXPECT_EQ(settings->scene, StressScene::RmlUi) << flag;
        }
        EXPECT_EQ(ToFlagValue(StressScene::RmlUi), "rmlui");
        EXPECT_EQ(ParseScene("RmlUi"), StressScene::RmlUi);
    }

    TEST_F(StressRmlUiSceneTest, TheGameShowsTheSceneOfTheFlag) {
        const Vector<String> args = {"--stress-scene=rmlui"};
        CoreManager::GetCommandLine().Set(args);
        const auto game = New<StressGame>();
        EXPECT_EQ(game->GetState().GetScene(), StressScene::RmlUi);
        EXPECT_EQ(game->GetSceneName(), StressNames::rmlui);
    }

    TEST_F(StressRmlUiSceneTest, TheControlApiListsAndSetsTheScene) {
        StressState state;
        int sceneChanges = 0;
        StressGameControl control(state, [&] { ++sceneChanges; });

        const auto listed = control.GetState();
        ASSERT_TRUE(listed.has_value());
        EXPECT_EQ((*listed)["scenes"], (Json::array({"sprites", "entities", "ui", "rmlui"})));

        Json action = Json::object();
        action["type"] = "set_scene";
        action["scene"] = "rmlui";
        ASSERT_TRUE(control.PerformAction(action).has_value());
        EXPECT_EQ(state.GetScene(), StressScene::RmlUi);
        EXPECT_EQ(sceneChanges, 1);
    }

    TEST_F(StressRmlUiSceneTest, TheReloadIsARequestTheSceneServesAndEveryCallCountsOnce) {
        const auto game = New<StressGame>();
        EXPECT_EQ(game->GetRmlUiReloadRequests(), 0u);

        const TypedRef ref = TypedRef::Of(*game);
        ASSERT_TRUE(ref);
        for (uint32_t call = 1; call <= 2; ++call) {
            JsonDeserializer reader(Json::object());
            JsonSerializer writer;
            ASSERT_TRUE(CallMethod(ref, "ReloadRmlUiDocuments", reader, writer).has_value());
            EXPECT_EQ(game->GetRmlUiReloadRequests(), call);
        }
    }

#else

    TEST(StressRmlUiSceneTest, WithoutTheRmlUiHostTheSceneIsRefusedLikeAnyOtherName) {
        const auto settings = Parse({"--stress-scene=rmlui"});
        ASSERT_FALSE(settings.has_value());
        EXPECT_TRUE(settings.error().find("--stress-scene") != String::npos) << settings.error().c_str();
        EXPECT_FALSE(ParseScene("rmlui").has_value());
    }

#endif

}  // namespace BECore::Stress::Tests
