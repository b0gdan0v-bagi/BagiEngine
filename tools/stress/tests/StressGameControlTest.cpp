#include <BECore/Control/Json.h>
#include <Stress/StressGameControl.h>
#include <gtest/gtest.h>

/**
 * Coverage of StressGameControl: the state, the actions and the way to act, on a StressState that the test owns.
 * The adapter needs no scene: a scene change reaches the test as a counter.
 */
namespace BECore::Stress::Tests {

    namespace {

        String ErrorCodeOf(const ControlError& error) {
            return error.code;
        }

        Json ActionOf(const char* type) {
            Json action = Json::object();
            action["type"] = type;
            return action;
        }

        Json SetScene(const char* scene) {
            Json action = ActionOf("set_scene");
            action["scene"] = scene;
            return action;
        }

        Json SetCount(Json count) {
            Json action = ActionOf("set_count");
            action["count"] = std::move(count);
            return action;
        }

        Json SetLanguage(const char* language) {
            Json action = ActionOf("set_language");
            action["language"] = language;
            return action;
        }

        class StressGameControlTest : public ::testing::Test {
        protected:
            StressGameControlTest() : control(state, [this] { ++sceneChanges; }) {}

            void SetUp() override {
                state.SetCount(100);
            }

            Json State() {
                auto result = control.GetState();
                EXPECT_TRUE(result.has_value());
                return result ? *result : Json();
            }

            Json Actions() {
                auto result = control.GetActions();
                EXPECT_TRUE(result.has_value());
                return result ? *result : Json();
            }

            /// Performs @p action and expects the error with @p code, and that nothing moved
            void ExpectRefused(const Json& action, const char* code) {
                const uint64_t version = state.Version();
                const auto result = control.PerformAction(action);
                ASSERT_FALSE(result.has_value()) << action.dump();
                EXPECT_EQ(ErrorCodeOf(result.error()), code) << action.dump();
                EXPECT_FALSE(result.error().message.empty());
                EXPECT_EQ(state.Version(), version) << action.dump();
                EXPECT_EQ(sceneChanges, 0);
            }

            StressState state;
            int sceneChanges = 0;
            StressGameControl control;
        };

    }  // namespace

    TEST_F(StressGameControlTest, TheStateShowsWhatTheAppShows) {
        const Json state = State();
        EXPECT_EQ(state["scene"], "sprites");
        EXPECT_EQ(state["count"], 100);
        EXPECT_EQ(state["language"], "en");
        EXPECT_EQ(state["maxCount"], static_cast<int64_t>(kMaxCount));
        EXPECT_EQ(state["scenes"], (Json::array({"sprites", "entities", "ui"})));
    }

    TEST_F(StressGameControlTest, TheStateFollowsTheState) {
        this->state.SetScene(StressScene::Ui);
        this->state.SetLanguage(StressLanguage::Ru);
        const Json state = State();
        EXPECT_EQ(state["scene"], "ui");
        EXPECT_EQ(state["language"], "ru");
    }

    TEST_F(StressGameControlTest, TheActionsListTheOtherScenesTheHalfTheDoubleAndTheOtherLanguage) {
        const Json actions = Actions();
        ASSERT_EQ(actions.size(), 5u);
        EXPECT_EQ(actions[0], SetScene("entities"));
        EXPECT_EQ(actions[1], SetScene("ui"));
        EXPECT_EQ(actions[2], SetCount(50));
        EXPECT_EQ(actions[3], SetCount(200));
        EXPECT_EQ(actions[4], SetLanguage("ru"));
    }

    TEST_F(StressGameControlTest, NoActionNamesTheActiveSceneOrLanguage) {
        state.SetScene(StressScene::Entities);
        state.SetLanguage(StressLanguage::Ru);
        for (const Json& action : Actions()) {
            EXPECT_NE(action, SetScene("entities"));
            EXPECT_NE(action, SetLanguage("ru"));
        }
    }

    TEST_F(StressGameControlTest, TheCountActionsStayInsideTheRange) {
        state.SetCount(1);
        Json actions = Actions();
        ASSERT_EQ(actions.size(), 4u);  // two scenes, the double (the half is 0), the language
        EXPECT_EQ(actions[2], SetCount(2));

        state.SetCount(kMaxCount);
        actions = Actions();
        ASSERT_EQ(actions.size(), 4u);  // the double is above the maximum
        EXPECT_EQ(actions[2], SetCount(static_cast<int64_t>(kMaxCount / 2)));

        state.SetCount(kMaxCount / 2 + 1);
        for (const Json& action : Actions()) {
            if (action["type"] == "set_count") {
                EXPECT_LE(action["count"].get<int64_t>(), static_cast<int64_t>(kMaxCount));
                EXPECT_GE(action["count"].get<int64_t>(), 1);
            }
        }
    }

    TEST_F(StressGameControlTest, EveryListedActionPerforms) {
        for (const Json& action : Actions()) {
            StressState fresh;
            fresh.SetCount(100);
            StressGameControl other(fresh, [] {});
            const uint64_t version = fresh.Version();
            const auto result = other.PerformAction(action);
            EXPECT_TRUE(result.has_value()) << action.dump();
            EXPECT_GT(fresh.Version(), version) << action.dump();
        }
    }

    TEST_F(StressGameControlTest, ASceneChangeSwitchesTheScene) {
        const auto result = control.PerformAction(SetScene("ui"));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(state.GetScene(), StressScene::Ui);
        EXPECT_EQ(sceneChanges, 1);
        EXPECT_EQ((*result)["scene"], "ui");
        EXPECT_EQ((*result)["count"], 100);
    }

    TEST_F(StressGameControlTest, TheSameSceneIsAcceptedAndChangesNothing) {
        const uint64_t version = state.Version();
        ASSERT_TRUE(control.PerformAction(SetScene("sprites")).has_value());
        EXPECT_EQ(state.Version(), version);
        EXPECT_EQ(sceneChanges, 0);
    }

    TEST_F(StressGameControlTest, AnyCountInRangeIsAccepted) {
        for (const int64_t count : {int64_t{1}, int64_t{7}, int64_t{12345}, static_cast<int64_t>(kMaxCount)}) {
            const auto result = control.PerformAction(SetCount(count));
            ASSERT_TRUE(result.has_value()) << count;
            EXPECT_EQ(state.GetCount(), static_cast<uint32_t>(count));
            EXPECT_EQ((*result)["count"], count);
        }
        EXPECT_EQ(sceneChanges, 0);
    }

    TEST_F(StressGameControlTest, TheSameCountIsAcceptedAndChangesNothing) {
        const uint64_t version = state.Version();
        ASSERT_TRUE(control.PerformAction(SetCount(100)).has_value());
        EXPECT_EQ(state.Version(), version);
    }

    TEST_F(StressGameControlTest, ALanguageChangeIsAccepted) {
        ASSERT_TRUE(control.PerformAction(SetLanguage("ru")).has_value());
        EXPECT_EQ(state.GetLanguage(), StressLanguage::Ru);
        ASSERT_TRUE(control.PerformAction(SetLanguage("en")).has_value());
        EXPECT_EQ(state.GetLanguage(), StressLanguage::En);
    }

    TEST_F(StressGameControlTest, ABadCountIsInvalidParams) {
        ExpectRefused(SetCount(0), "invalid_params");
        ExpectRefused(SetCount(-3), "invalid_params");
        ExpectRefused(SetCount(static_cast<int64_t>(kMaxCount) + 1), "invalid_params");
        ExpectRefused(SetCount(int64_t{9000000000}), "invalid_params");
        ExpectRefused(SetCount(1.5), "invalid_params");
        ExpectRefused(SetCount("12"), "invalid_params");
        ExpectRefused(SetCount(Json()), "invalid_params");
        ExpectRefused(SetCount(true), "invalid_params");
        ExpectRefused(ActionOf("set_count"), "invalid_params");
    }

    TEST_F(StressGameControlTest, ABadSceneOrLanguageIsInvalidParams) {
        ExpectRefused(SetScene("3d"), "invalid_params");
        ExpectRefused(SetScene(""), "invalid_params");
        ExpectRefused(ActionOf("set_scene"), "invalid_params");
        ExpectRefused(SetLanguage("de"), "invalid_params");
        ExpectRefused(ActionOf("set_language"), "invalid_params");

        Json numeric = ActionOf("set_scene");
        numeric["scene"] = 1;
        ExpectRefused(numeric, "invalid_params");
    }

    TEST_F(StressGameControlTest, ATypeThatIsNotAStringIsInvalidParamsAndAnUnknownOneIsIllegal) {
        ExpectRefused(Json::object(), "invalid_params");
        ExpectRefused(Json(), "invalid_params");
        ExpectRefused(Json::array(), "invalid_params");
        Json numeric = Json::object();
        numeric["type"] = 5;
        ExpectRefused(numeric, "invalid_params");
        ExpectRefused(ActionOf("explode"), "illegal_action");
        ExpectRefused(ActionOf(""), "illegal_action");
    }

    TEST_F(StressGameControlTest, TheVersionMovesOnlyOnAChange) {
        EXPECT_EQ(control.GetStateVersion(), state.Version());
        const uint64_t version = control.GetStateVersion();
        State();
        Actions();
        EXPECT_EQ(control.GetStateVersion(), version);

        ASSERT_TRUE(control.PerformAction(SetCount(101)).has_value());
        EXPECT_GT(control.GetStateVersion(), version);
        EXPECT_EQ(control.GetStateVersion(), state.Version());
    }

    TEST_F(StressGameControlTest, ResetRestoresTheDefaults) {
        state.SetScene(StressScene::Ui);
        state.SetLanguage(StressLanguage::Ru);
        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_EQ(state.GetScene(), StressScene::Sprites);
        EXPECT_EQ(state.GetCount(), kDefaultCount);
        EXPECT_EQ(state.GetLanguage(), StressLanguage::En);
        EXPECT_EQ(sceneChanges, 1);
    }

    TEST_F(StressGameControlTest, ResetTakesASceneAndACount) {
        Json options = Json::object();
        options["scene"] = "entities";
        options["count"] = 777;
        ASSERT_TRUE(control.Reset(options).has_value());
        EXPECT_EQ(state.GetScene(), StressScene::Entities);
        EXPECT_EQ(state.GetCount(), 777u);
        EXPECT_EQ(sceneChanges, 1);
    }

    TEST_F(StressGameControlTest, ResetWithNoOptionsObjectIsTheDefaults) {
        state.SetCount(5);
        ASSERT_TRUE(control.Reset(Json()).has_value());
        EXPECT_EQ(state.GetCount(), kDefaultCount);
    }

    TEST_F(StressGameControlTest, ABadResetOptionChangesNothing) {
        state.SetScene(StressScene::Ui);
        const uint64_t version = state.Version();
        Json options = Json::object();
        options["scene"] = "entities";
        options["count"] = 0;
        auto result = control.Reset(options);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params");
        EXPECT_EQ(state.Version(), version);

        options["count"] = 5;
        options["scene"] = "3d";
        result = control.Reset(options);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params");
        EXPECT_EQ(state.Version(), version);
        EXPECT_EQ(state.GetScene(), StressScene::Ui);
    }

}  // namespace BECore::Stress::Tests
