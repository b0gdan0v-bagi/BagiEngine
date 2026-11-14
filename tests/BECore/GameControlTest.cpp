#include "GameControlTest.h"

#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Generated/GameControlTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    PoolString FakeControllableGame::GetName() const {
        return "FakeControllable"_intern;
    }

    PoolString FakeControllableGame::GetSceneName() const {
        return PoolString();  // no scene: the tests have no SceneManager
    }

    void FakeControllableGame::Reset() {
        ++plainResetCalls;
    }

    IControllableGame* FakeControllableGame::AsControllable() {
        return this;
    }

    std::expected<Json, ControlError> FakeControllableGame::GetState() const {
        Json state = Json::object();
        state["counter"] = counter;
        return state;
    }

    std::expected<Json, ControlError> FakeControllableGame::GetActions() const {
        Json inc = Json::object();
        inc["type"] = "inc";
        return Json::array({inc});
    }

    std::expected<Json, ControlError> FakeControllableGame::PerformAction(const Json& action) {
        const auto type = GetJsonString(action, "type");
        if (!type) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be a string"));
        }
        if (*type != "inc") {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "only \"inc\" is legal"));
        }
        ++counter;
        ++version;
        Json outcome = Json::object();
        outcome["counter"] = counter;
        return outcome;
    }

    std::expected<void, ControlError> FakeControllableGame::Reset(const Json& options) {
        int start = 0;
        if (FindJsonMember(options, "start") != nullptr) {
            const auto requested = GetJsonInt(options, "start");
            if (!requested) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'start' has to be an integer"));
            }
            start = static_cast<int>(*requested);
        }
        counter = start;
        ++version;
        return {};
    }

    std::expected<void, ControlError> FakeControllableGame::SetState(const Json& state) {
        const auto requested = GetJsonInt(state, "counter");
        if (!requested) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'counter' has to be an integer"));
        }
        counter = static_cast<int>(*requested);
        ++version;
        return {};
    }

    uint64_t FakeControllableGame::GetStateVersion() const {
        return version;
    }

    PoolString FakePlainGame::GetName() const {
        return "FakePlain"_intern;
    }

    PoolString FakePlainGame::GetSceneName() const {
        return PoolString();
    }

    void FakePlainGame::Reset() {
        ++resetCalls;
    }

    namespace {

        /// Implements only what is pure: SetState keeps its default
        class MinimalControl : public IControllableGame {
        public:
            std::expected<Json, ControlError> GetState() const override {
                return Json::object();
            }
            std::expected<Json, ControlError> GetActions() const override {
                return Json::array();
            }
            std::expected<Json, ControlError> PerformAction(const Json&) override {
                return Json::object();
            }
            std::expected<void, ControlError> Reset(const Json&) override {
                return {};
            }
            uint64_t GetStateVersion() const override {
                return 0;
            }
        };

        std::string ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            if (error == nullptr) {
                return {};
            }
            const auto code = GetJsonString(*error, "code");
            return code ? std::string(code->c_str(), code->size()) : std::string();
        }

        class GameControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
            }

            void TearDown() override {
                GameManager::GetInstance().Shutdown();
            }

            /// Posts one request, runs the queue and returns the response ({} when nothing was answered)
            Json Call(std::string_view method, const Json& params = Json()) {
                ControlService& service = CoreManager::GetControlService();
                Json request = Json::object();
                request["id"] = ++_lastId;
                request["method"] = std::string(method);
                if (!params.is_null()) {
                    request["params"] = params;
                }

                Json response;
                service.Post(DumpJson(request), [&response](String text) {
                    auto parsed = ParseJson(text);
                    if (parsed) {
                        response = std::move(*parsed);
                    }
                });
                service.Drain();
                return response;
            }

            static const Json* Result(const Json& response) {
                return FindJsonMember(response, "result");
            }

            static bool Succeeded(const Json& response) {
                return GetJsonBool(response, "ok").value_or(false);
            }

            static FakeControllableGame* Controllable() {
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakeControllableGame>()) {
                        return fake;
                    }
                }
                return nullptr;
            }

            static FakePlainGame* Plain() {
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakePlainGame>()) {
                        return fake;
                    }
                }
                return nullptr;
            }

            int64_t _lastId = 0;
        };

        static_assert(!std::is_abstract_v<FakeControllableGame>);
        static_assert(!std::is_abstract_v<FakePlainGame>);
        static_assert(std::is_base_of_v<IControllableGame, FakeControllableGame>);

    }  // namespace

    TEST(GameControlDefaultsTest, SetStateIsUnsupportedUnlessTheGameOverridesIt) {
        MinimalControl control;

        const auto result = control.SetState(Json::object());

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, String(GameControlErrors::kUnsupported));
    }

    TEST(GameControlDefaultsTest, AGameIsNotControllableByDefault) {
        FakePlainGame plain;

        EXPECT_EQ(plain.AsControllable(), nullptr);
    }

    TEST_F(GameControlTest, ListGamesShowsEveryRegisteredGameAndTheActiveOne) {
        const Json response = Call("list_games");

        ASSERT_TRUE(Succeeded(response));
        const Json* result = Result(response);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonString(*result, "active").value_or("").c_str(), std::string("FakeControllable"));

        const Json* games = FindJsonMember(*result, "games");
        ASSERT_NE(games, nullptr);
        ASSERT_TRUE(games->is_array());
        ASSERT_EQ(games->size(), 2u);
        int controllable = 0;
        int plain = 0;
        for (const Json& game : *games) {
            const std::string name = GetJsonString(game, "name").value_or("").c_str();
            EXPECT_EQ(GetJsonString(game, "scene").value_or("x").c_str(), std::string());
            if (name == "FakeControllable") {
                EXPECT_EQ(GetJsonBool(game, "controllable"), true);
                ++controllable;
            } else if (name == "FakePlain") {
                EXPECT_EQ(GetJsonBool(game, "controllable"), false);
                ++plain;
            }
        }
        EXPECT_EQ(controllable, 1);
        EXPECT_EQ(plain, 1);
    }

    TEST_F(GameControlTest, ListGamesWithoutAnActiveGameSaysNull) {
        GameManager::GetInstance().Shutdown();

        const Json response = Call("list_games");

        ASSERT_TRUE(Succeeded(response));
        const Json* active = FindJsonMember(*Result(response), "active");
        ASSERT_NE(active, nullptr);
        EXPECT_TRUE(active->is_null());
    }

    TEST_F(GameControlTest, SwitchGameActivatesTheNamedGame) {
        Json params = Json::object();
        params["name"] = "FakePlain";

        const Json response = Call("switch_game", params);

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(GetJsonString(*Result(response), "active").value_or("").c_str(), std::string("FakePlain"));
        ASSERT_NE(GameManager::GetInstance().GetActive(), nullptr);
        EXPECT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakePlain"_intern);
    }

    TEST_F(GameControlTest, SwitchGameRefusesAnUnknownOrMissingName) {
        Json unknown = Json::object();
        unknown["name"] = "NoSuchGame";
        Json notAString = Json::object();
        notAString["name"] = 7;

        EXPECT_EQ(ErrorCode(Call("switch_game", unknown)), "unknown_game");
        EXPECT_EQ(ErrorCode(Call("switch_game")), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("switch_game", notAString)), "invalid_params");
        EXPECT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakeControllable"_intern);
    }

    TEST_F(GameControlTest, SwitchGameDoesNotInternTheNameOfAClient) {
        Json unknown = Json::object();
        unknown["name"] = "client_only_game_7f3a91";

        EXPECT_EQ(ErrorCode(Call("switch_game", unknown)), "unknown_game");
        EXPECT_FALSE(PoolString::Find("client_only_game_7f3a91").has_value());
    }

    TEST_F(GameControlTest, GetStateWrapsTheStateOfTheGame) {
        Controllable()->counter = 5;
        Controllable()->version = 9;

        const Json response = Call("get_state");

        ASSERT_TRUE(Succeeded(response));
        const Json* result = Result(response);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonString(*result, "game").value_or("").c_str(), std::string("FakeControllable"));
        EXPECT_EQ(GetJsonInt(*result, "version"), 9);
        const Json* state = FindJsonMember(*result, "state");
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(GetJsonInt(*state, "counter"), 5);
    }

    TEST_F(GameControlTest, ListActionsWrapsTheActionsOfTheGame) {
        const Json response = Call("list_actions");

        ASSERT_TRUE(Succeeded(response));
        const Json* actions = FindJsonMember(*Result(response), "actions");
        ASSERT_NE(actions, nullptr);
        ASSERT_TRUE(actions->is_array());
        ASSERT_EQ(actions->size(), 1u);
        EXPECT_EQ(GetJsonString((*actions)[0], "type").value_or("").c_str(), std::string("inc"));
    }

    TEST_F(GameControlTest, PerformActionTakesTheActionAsTheParamsAndAnswersWithTheNewVersion) {
        Json action = Json::object();
        action["type"] = "inc";
        const Json before = Call("get_state");
        ASSERT_TRUE(Succeeded(before));
        const int64_t versionBefore = GetJsonInt(*Result(before), "version").value_or(-1);

        const Json response = Call("perform_action", action);

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Controllable()->counter, 1);
        const Json* result = Result(response);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonInt(*result, "version"), versionBefore + 1);
        const Json* outcome = FindJsonMember(*result, "outcome");
        ASSERT_NE(outcome, nullptr);
        EXPECT_EQ(GetJsonInt(*outcome, "counter"), 1);
    }

    TEST_F(GameControlTest, PerformActionPassesTheErrorOfTheGameOn) {
        Json illegal = Json::object();
        illegal["type"] = "dec";

        const Json illegalResponse = Call("perform_action", illegal);
        const Json emptyResponse = Call("perform_action");

        EXPECT_FALSE(Succeeded(illegalResponse));
        EXPECT_EQ(ErrorCode(illegalResponse), "illegal_action");
        EXPECT_EQ(ErrorCode(emptyResponse), "invalid_params");
        EXPECT_EQ(Controllable()->counter, 0);
    }

    TEST_F(GameControlTest, PipelinedActionsRunInOrderBeforeTheStateIsRead) {
        ControlService& service = CoreManager::GetControlService();
        Vector<Json> responses;
        auto collect = [&responses](String text) {
            auto parsed = ParseJson(text);
            responses.push_back(parsed ? std::move(*parsed) : Json());
        };
        const auto post = [&](int64_t id, const char* method, const char* actionType) {
            Json request = Json::object();
            request["id"] = id;
            request["method"] = method;
            if (actionType != nullptr) {
                Json params = Json::object();
                params["type"] = actionType;
                request["params"] = params;
            }
            service.Post(DumpJson(request), collect);
        };

        post(1, "perform_action", "inc");
        post(2, "perform_action", "inc");
        post(3, "perform_action", "inc");
        post(4, "get_state", nullptr);
        service.Drain();

        ASSERT_EQ(responses.size(), 4u);
        const Json* state = FindJsonMember(*Result(responses[3]), "state");
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(GetJsonInt(*state, "counter"), 3);
    }

    TEST_F(GameControlTest, ResetPassesTheOptionsToTheGame) {
        Controllable()->counter = 8;
        Json options = Json::object();
        options["start"] = 4;

        const Json response = Call("reset", options);

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Controllable()->counter, 4);
        EXPECT_EQ(Controllable()->plainResetCalls, 0);  // the controllable path, not IGame::Reset()
        EXPECT_TRUE(FindJsonMember(*Result(response), "version") != nullptr);
    }

    TEST_F(GameControlTest, ResetRefusesBadOptionsAndChangesNothing) {
        Controllable()->counter = 8;
        Json options = Json::object();
        options["start"] = "four";

        const Json response = Call("reset", options);

        EXPECT_EQ(ErrorCode(response), "invalid_params");
        EXPECT_EQ(Controllable()->counter, 8);
    }

    TEST_F(GameControlTest, ResetOfAGameThatCannotBeControlledCallsIGameReset) {
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);

        const Json response = Call("reset");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Plain()->resetCalls, 1);
        EXPECT_EQ(GetJsonString(*Result(response), "game").value_or("").c_str(), std::string("FakePlain"));
        EXPECT_EQ(FindJsonMember(*Result(response), "version"), nullptr);  // it has no version to report
    }

    TEST_F(GameControlTest, SetStateLoadsTheStateOfTheGame) {
        Json state = Json::object();
        state["counter"] = 41;

        const Json response = Call("set_state", state);

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Controllable()->counter, 41);
        EXPECT_EQ(ErrorCode(Call("set_state")), "invalid_params");
        EXPECT_EQ(Controllable()->counter, 41);
    }

    TEST_F(GameControlTest, GameMethodsOfAGameThatCannotBeControlledSayNotControllable) {
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);

        for (const char* method : {"get_state", "list_actions", "perform_action", "set_state"}) {
            EXPECT_EQ(ErrorCode(Call(method)), "not_controllable") << method;
        }
    }

    TEST_F(GameControlTest, GameMethodsWithoutAnActiveGameSayNoActiveGame) {
        GameManager::GetInstance().Shutdown();

        for (const char* method : {"get_state", "list_actions", "perform_action", "set_state", "reset"}) {
            EXPECT_EQ(ErrorCode(Call(method)), "no_active_game") << method;
        }
    }

    namespace {

        /// Collects what the active control service pushes to its clients, and stops doing so when it goes out of scope
        class EventCollector {
        public:
            EventCollector() {
                CoreManager::GetControlService().SetEventSink([this](const String& text) {
                    auto parsed = ParseJson(text);
                    events.push_back(parsed ? std::move(*parsed) : Json());
                });
            }

            ~EventCollector() {
                CoreManager::GetControlService().SetEventSink(nullptr);
            }

            EventCollector(const EventCollector&) = delete;
            EventCollector& operator=(const EventCollector&) = delete;

            Vector<Json> events;
        };

        std::string EventName(const Json& event) {
            const auto name = GetJsonString(event, "event");
            return name ? std::string(name->c_str(), name->size()) : std::string();
        }

        const Json* EventData(const Json& event) {
            return FindJsonMember(event, "data");
        }

    }  // namespace

    TEST_F(GameControlTest, StateChangeIsPushedAtTheEndOfTheFrame) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();  // the first look only records the version

        const Json acted = Call("perform_action", [] {
            Json inc = Json::object();
            inc["type"] = "inc";
            return inc;
        }());
        ASSERT_TRUE(Succeeded(acted));
        EXPECT_TRUE(collector.events.empty());  // not before the frame ends
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 1u);
        EXPECT_EQ(EventName(collector.events[0]), "state_changed");
        const Json* data = EventData(collector.events[0]);
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(GetJsonString(*data, "game").value_or("").c_str(), std::string("FakeControllable"));
        EXPECT_EQ(GetJsonInt(*data, "version").value_or(-1), 1);
        const Json* state = FindJsonMember(*data, "state");
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(GetJsonInt(*state, "counter").value_or(-1), 1);
    }

    TEST_F(GameControlTest, NothingIsPushedWhenTheStateDidNotMove) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;

        service.EndFrame();
        service.EndFrame();
        Call("get_state");
        Call("list_actions");
        service.EndFrame();

        EXPECT_TRUE(collector.events.empty());
    }

    TEST_F(GameControlTest, TheFirstLookOnlyRecordsTheVersion) {
        ControlService& service = CoreManager::GetControlService();
        Controllable()->version = 7;  // moved before anybody looked
        EventCollector collector;

        service.EndFrame();

        EXPECT_TRUE(collector.events.empty());
    }

    TEST_F(GameControlTest, OneChangeIsPushedOnce) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        Controllable()->version += 5;  // a change of any size is one event
        service.EndFrame();
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 1u);
        EXPECT_EQ(GetJsonInt(*EventData(collector.events[0]), "version").value_or(-1), 5);
    }

    TEST_F(GameControlTest, AChangeThatNoClientMadeIsPushedToo) {
        // A click of a person moves the version without a request
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        Controllable()->counter = 41;
        ++Controllable()->version;
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 1u);
        const Json* state = FindJsonMember(*EventData(collector.events[0]), "state");
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(GetJsonInt(*state, "counter").value_or(-1), 41);
    }

    TEST_F(GameControlTest, ResetAndSetStateArePushedToo) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        Json start = Json::object();
        start["start"] = 10;
        ASSERT_TRUE(Succeeded(Call("reset", start)));
        service.EndFrame();
        Json state = Json::object();
        state["counter"] = 20;
        ASSERT_TRUE(Succeeded(Call("set_state", state)));
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 2u);
        EXPECT_EQ(GetJsonInt(*FindJsonMember(*EventData(collector.events[0]), "state"), "counter").value_or(-1), 10);
        EXPECT_EQ(GetJsonInt(*FindJsonMember(*EventData(collector.events[1]), "state"), "counter").value_or(-1), 20);
    }

    TEST_F(GameControlTest, AGameThatCannotBeControlledPushesNothing) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        service.EndFrame();
        service.EndFrame();

        EXPECT_TRUE(collector.events.empty());
    }

    TEST_F(GameControlTest, RegisteringTheGameMethodsAgainDoesNotDoublePushes) {
        // GameManager::Initialize() registers them again for every test; a run of the game does it once
        ControlService& service = CoreManager::GetControlService();
        RegisterGameControl(service);
        RegisterGameControl(service);
        EventCollector collector;
        service.EndFrame();

        ++Controllable()->version;
        service.EndFrame();

        EXPECT_EQ(collector.events.size(), 1u);
    }

}  // namespace BECore::Tests
