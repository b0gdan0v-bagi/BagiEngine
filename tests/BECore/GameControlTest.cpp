#include "GameControlTest.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/GameText.h>
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

#if BE_WITH_EDITOR
    void FakeControllableGame::RegisterEditorTools(GameEditorTools& tools) {
        if (editorTools) {
            editorTools(tools);
        }
    }
#endif

    std::expected<void, ControlError> FakeControllableGame::WriteState(JsonWriter& writer) const {
        ++writeStateCalls;
        if (!rawState.empty()) {
            writer.Raw(rawState);
            return {};
        }
        writer.BeginObject();
        if (failWrites) {
            writer.Member("half", 1);
            writer.Key("more");
            return std::unexpected(MakeControlError("write_failed", "the state cannot be written"));
        }
        writer.Member("counter", counter);
        writer.EndObject();
        return {};
    }

    std::expected<void, ControlError> FakeControllableGame::WriteActions(JsonWriter& writer) const {
        ++writeActionsCalls;
        writer.BeginArray();
        if (failWrites) {
            writer.Value("half");
            return std::unexpected(MakeControlError("write_failed", "the actions cannot be written"));
        }
        writer.BeginObject();
        writer.Member("type", "inc");
        writer.EndObject();
        writer.EndArray();
        return {};
    }

    std::expected<void, ControlError> FakeControllableGame::PerformAction(JsonView action, JsonWriter& outcome) {
        const auto type = GetParamString(action, "type");
        if (!type) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be a string"));
        }
        if (*type != "inc") {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "only \"inc\" is legal"));
        }
        ++counter;
        ++version;
        if (silentAction) {
            return {};
        }
        outcome.BeginObject();
        outcome.Member("counter", counter);
        outcome.EndObject();
        return {};
    }

    std::expected<void, ControlError> FakeControllableGame::Reset(JsonView options) {
        int start = 0;
        if (FindParam(options, "start")) {
            const auto requested = GetParamInt(options, "start");
            if (!requested) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'start' has to be an integer"));
            }
            start = static_cast<int>(*requested);
        }
        counter = start;
        ++version;
        return {};
    }

    std::expected<void, ControlError> FakeControllableGame::SetState(JsonView state) {
        const auto requested = GetParamInt(state, "counter");
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

    void FakeControllableGame::AddToCounter(int32_t amount) {
        counter += amount;
        ++version;
    }

    CounterReport FakeControllableGame::Report(int32_t scale) const {
        CounterReport report;
        report.total = counter * scale;
        report.label = "counter";
        return report;
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
            std::expected<void, ControlError> WriteState(JsonWriter& writer) const override {
                writer.BeginObject();
                writer.EndObject();
                return {};
            }
            std::expected<void, ControlError> WriteActions(JsonWriter& writer) const override {
                writer.BeginArray();
                writer.EndArray();
                return {};
            }
            std::expected<void, ControlError> PerformAction(JsonView, JsonWriter& outcome) override {
                outcome.Null();
                return {};
            }
            std::expected<void, ControlError> Reset(JsonView) override {
                return {};
            }
            uint64_t GetStateVersion() const override {
                return 0;
            }
        };

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
            ControlReply Call(StringView method, StringView paramsText = {}) {
                return CallControlText(CoreManager::GetControlService(), ++_lastId, method, paramsText);
            }

            static JsonView Result(const ControlReply& response) {
                return response.Result();
            }

            static bool Succeeded(const ControlReply& response) {
                return response.IsOk();
            }

            /// The registered game of type @p T; nullptr when there is none
            template <typename T>
            static T* Find() {
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<T>()) {
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

    TEST_F(GameControlTest, TheGameRootResolvesTheActiveGameToItsMostDerivedType) {
        const ControlRoots& roots = CoreManager::GetControlService().Roots();
        ASSERT_NE(roots.Find(kGameRootName), nullptr);
        EXPECT_FALSE(Require(roots.Find(kGameRootName)).allowInterning);

        const TypedRef ref = roots.Resolve(kGameRootName);
        ASSERT_TRUE(ref);
        ASSERT_NE(Find<FakeControllableGame>(), nullptr);
        EXPECT_EQ(ref.type->Name(), "FakeControllableGame");
        EXPECT_EQ(ref.Address(), static_cast<void*>(Find<FakeControllableGame>()));
    }

    TEST_F(GameControlTest, TheGameRootFollowsTheActiveGame) {
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        const TypedRef ref = CoreManager::GetControlService().Roots().Resolve(kGameRootName);
        ASSERT_TRUE(ref);
        EXPECT_EQ(ref.type->Name(), "FakePlainGame");
    }

    TEST_F(GameControlTest, TheGameRootIsEmptyWithoutAnActiveGame) {
        GameManager::GetInstance().Shutdown();
        EXPECT_FALSE(CoreManager::GetControlService().Roots().Resolve(kGameRootName));
    }

    TEST_F(GameControlTest, RegisteringGameControlAgainReplacesTheGameRoot) {
        ControlService service;
        RegisterGameControl(service);
        RegisterGameControl(service);

        EXPECT_EQ(service.Roots().Names().size(), 1U);
        EXPECT_TRUE(service.Roots().Resolve(kGameRootName));
    }

    TEST(GameControlDefaultsTest, SetStateIsUnsupportedUnlessTheGameOverridesIt) {
        MinimalControl control;

        const auto result = SetStateText(control, "{}");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, String(GameControlErrors::kUnsupported));
    }

    TEST(GameControlDefaultsTest, TheReplayStartIsAnEmptyObjectUnlessTheGameOverridesIt) {
        MinimalControl control;

        const auto start = ReplayStartText(control);

        ASSERT_TRUE(start.has_value());
        EXPECT_EQ(*start, String("{}"));
    }

    TEST(GameControlTextTest, TextThatIsNotJsonIsInvalidParamsWithTheMessageOfTheTape) {
        FakeControllableGame game;

        const auto action = PerformActionText(game, "{\"type\":");
        const auto reset = ResetText(game, "nope");

        ASSERT_FALSE(action.has_value());
        EXPECT_EQ(action.error().code, String(ControlErrors::kInvalidParams));
        EXPECT_EQ(action.error().message.substr(0, 9), String("not JSON:"));
        ASSERT_FALSE(reset.has_value());
        EXPECT_EQ(reset.error().code, String(ControlErrors::kInvalidParams));
        EXPECT_EQ(game.counter, 0);
    }

    TEST(GameControlTextTest, TheOutcomeAndTheStateAreTheTextTheGameWrote) {
        FakeControllableGame game;

        const auto outcome = PerformActionText(game, R"({"type":"inc"})");
        const auto state = StateText(game);

        ASSERT_TRUE(outcome.has_value());
        EXPECT_EQ(*outcome, String(R"({"counter":1})"));
        ASSERT_TRUE(state.has_value());
        EXPECT_EQ(*state, String(R"({"counter":1})"));
    }

    TEST(GameControlDefaultsTest, AGameIsNotControllableByDefault) {
        FakePlainGame plain;

        EXPECT_EQ(plain.AsControllable(), nullptr);
    }

    TEST_F(GameControlTest, ListGamesShowsEveryRegisteredGameAndTheActiveOne) {
        const ControlReply response = Call("list_games");

        ASSERT_TRUE(Succeeded(response));
        const JsonView result = Result(response);
        ASSERT_TRUE(result.IsObject());
        EXPECT_EQ(StringOf(result, "active"), String("FakeControllable"));

        const JsonView games = MemberOf(result, "games");
        ASSERT_TRUE(games.IsArray());
        ASSERT_EQ(games.Size(), 3u);  // the two fakes and ReplayTestGame
        int controllable = 0;
        int plain = 0;
        for (const JsonView game : games.Items()) {
            const String name = StringOf(game, "name");
            EXPECT_TRUE(MemberOf(game, "scene").TextEquals(""));
            if (name == "FakeControllable") {
                EXPECT_EQ(BoolOf(game, "controllable"), true);
                ++controllable;
            } else if (name == "FakePlain") {
                EXPECT_EQ(BoolOf(game, "controllable"), false);
                ++plain;
            }
        }
        EXPECT_EQ(controllable, 1);
        EXPECT_EQ(plain, 1);
    }

    TEST_F(GameControlTest, ListGamesWithoutAnActiveGameLeavesActiveOut) {
        GameManager::GetInstance().Shutdown();

        const ControlReply response = Call("list_games");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_FALSE(MemberOf(Result(response), "active").IsValid());
    }

    TEST_F(GameControlTest, SwitchGameActivatesTheNamedGame) {
        const ControlReply response = Call("switch_game", R"({"name":"FakePlain"})");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(StringOf(Result(response), "active"), String("FakePlain"));
        ASSERT_NE(GameManager::GetInstance().GetActive(), nullptr);
        EXPECT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakePlain"_intern);
    }

    TEST_F(GameControlTest, SwitchGameRefusesAnUnknownOrMissingName) {
        EXPECT_EQ(Call("switch_game", R"({"name":"NoSuchGame"})").ErrorCode(), "unknown_game");
        EXPECT_EQ(Call("switch_game").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("switch_game", R"({"name":7})").ErrorCode(), "invalid_params");
        EXPECT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakeControllable"_intern);
    }

    TEST_F(GameControlTest, SwitchGameDoesNotInternTheNameOfAClient) {
        EXPECT_EQ(Call("switch_game", R"({"name":"client_only_game_7f3a91"})").ErrorCode(), "unknown_game");
        EXPECT_FALSE(PoolString::Find(StringView{"client_only_game_7f3a91"}).has_value());
    }

    TEST_F(GameControlTest, GetStateWrapsTheStateOfTheGame) {
        Find<FakeControllableGame>()->counter = 5;
        Find<FakeControllableGame>()->version = 9;

        const ControlReply response = Call("get_state");

        ASSERT_TRUE(Succeeded(response));
        const JsonView result = Result(response);
        ASSERT_TRUE(result.IsObject());
        EXPECT_EQ(StringOf(result, "game"), String("FakeControllable"));
        EXPECT_EQ(IntOf(result, "version"), 9);
        const JsonView state = MemberOf(result, "state");
        ASSERT_TRUE(state.IsObject());
        EXPECT_EQ(IntOf(state, "counter"), 5);
    }

    TEST_F(GameControlTest, ListActionsWrapsTheActionsOfTheGame) {
        const ControlReply response = Call("list_actions");

        ASSERT_TRUE(Succeeded(response));
        const JsonView actions = MemberOf(Result(response), "actions");
        ASSERT_TRUE(actions.IsArray());
        ASSERT_EQ(actions.Size(), 1u);
        EXPECT_EQ(StringOf(ItemAt(actions, 0), "type"), String("inc"));
    }

    TEST_F(GameControlTest, PerformActionTakesTheActionAsTheParamsAndAnswersWithTheNewVersion) {
        const ControlReply before = Call("get_state");
        ASSERT_TRUE(Succeeded(before));
        const int64_t versionBefore = IntOf(Result(before), "version").value_or(-1);

        const ControlReply response = Call("perform_action", R"({"type":"inc"})");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 1);
        const JsonView result = Result(response);
        ASSERT_TRUE(result.IsObject());
        EXPECT_EQ(IntOf(result, "version"), versionBefore + 1);
        const JsonView outcome = MemberOf(result, "outcome");
        ASSERT_TRUE(outcome.IsObject());
        EXPECT_EQ(IntOf(outcome, "counter"), 1);
    }

    TEST_F(GameControlTest, PerformActionPassesTheErrorOfTheGameOn) {
        const ControlReply illegalResponse = Call("perform_action", R"({"type":"dec"})");
        const ControlReply emptyResponse = Call("perform_action");

        EXPECT_FALSE(Succeeded(illegalResponse));
        EXPECT_EQ(illegalResponse.ErrorCode(), "illegal_action");
        EXPECT_EQ(emptyResponse.ErrorCode(), "invalid_params");
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 0);
    }

    TEST_F(GameControlTest, AnActionAcceptedWithoutAnOutcomeIsAnInternalError) {
        Find<FakeControllableGame>()->silentAction = true;
        const ControlReply response = Call("perform_action", R"({"type":"inc"})");

        EXPECT_FALSE(Succeeded(response));
        EXPECT_EQ(response.ErrorCode(), "internal_error");
    }

    TEST_F(GameControlTest, TheParamsOfAGameAreReadDecodedAndCheckedWhateverTheTextSays) {
        const auto callText = [&](StringView method, StringView paramsText) { return Call(method, paramsText); };

        // An escape stands for its character, so the action below is "inc"
        EXPECT_TRUE(Succeeded(callText("perform_action", R"({"type":"\u0069nc"})")));
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 1);

        // An integer past 64 bits is no integer for the helper, and the game answers invalid_params instead of wrapping it
        EXPECT_EQ(callText("reset", R"({"start":18446744073709551616})").ErrorCode(), "invalid_params");
        EXPECT_EQ(callText("reset", R"({"start":1.5})").ErrorCode(), "invalid_params");
        EXPECT_EQ(callText("perform_action", R"({"type":["inc"]})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 1);
    }

    TEST_F(GameControlTest, PipelinedActionsRunInOrderBeforeTheStateIsRead) {
        ControlService& service = CoreManager::GetControlService();
        Vector<ControlReply> responses;
        auto collect = [&responses](const String& text) { responses.push_back(ControlReply(StringView(text.c_str(), text.size()))); };
        const auto post = [&](int64_t id, StringView method, StringView paramsText) { service.Post(MakeRequestText(id, method, paramsText), collect); };

        post(1, "perform_action", R"({"type":"inc"})");
        post(2, "perform_action", R"({"type":"inc"})");
        post(3, "perform_action", R"({"type":"inc"})");
        post(4, "get_state", {});
        service.Drain();

        ASSERT_EQ(responses.size(), 4u);
        const JsonView state = MemberOf(Result(responses[3]), "state");
        ASSERT_TRUE(state.IsObject());
        EXPECT_EQ(IntOf(state, "counter"), 3);
    }

    TEST_F(GameControlTest, ResetPassesTheOptionsToTheGame) {
        Find<FakeControllableGame>()->counter = 8;

        const ControlReply response = Call("reset", R"({"start":4})");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 4);
        EXPECT_EQ(Find<FakeControllableGame>()->plainResetCalls, 0);  // the controllable path, not IGame::Reset()
        EXPECT_TRUE(MemberOf(Result(response), "version").IsValid());
    }

    TEST_F(GameControlTest, ResetRefusesBadOptionsAndChangesNothing) {
        Find<FakeControllableGame>()->counter = 8;

        const ControlReply response = Call("reset", R"({"start":"four"})");

        EXPECT_EQ(response.ErrorCode(), "invalid_params");
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 8);
    }

    TEST_F(GameControlTest, ResetOfAGameThatCannotBeControlledCallsIGameReset) {
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);

        const ControlReply response = Call("reset");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Find<FakePlainGame>()->resetCalls, 1);
        EXPECT_EQ(StringOf(Result(response), "game"), String("FakePlain"));
        EXPECT_FALSE(MemberOf(Result(response), "version").IsValid());  // it has no version to report
    }

    TEST_F(GameControlTest, SetStateLoadsTheStateOfTheGame) {
        const ControlReply response = Call("set_state", R"({"counter":41})");

        ASSERT_TRUE(Succeeded(response));
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 41);
        EXPECT_EQ(Call("set_state").ErrorCode(), "invalid_params");
        EXPECT_EQ(Find<FakeControllableGame>()->counter, 41);
    }

    TEST_F(GameControlTest, GameMethodsOfAGameThatCannotBeControlledSayNotControllable) {
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);

        for (const char* method : {"get_state", "list_actions", "perform_action", "set_state"}) {
            EXPECT_EQ(Call(method).ErrorCode(), "not_controllable") << method;
        }
    }

    TEST_F(GameControlTest, GameMethodsWithoutAnActiveGameSayNoActiveGame) {
        GameManager::GetInstance().Shutdown();

        for (const char* method : {"get_state", "list_actions", "perform_action", "set_state", "reset"}) {
            EXPECT_EQ(Call(method).ErrorCode(), "no_active_game") << method;
        }
    }

    namespace {

        /// Collects what the active control service pushes to its clients, and stops doing so when it goes out of scope
        class EventCollector {
        public:
            EventCollector() {
                CoreManager::GetControlService().SetEventSink([this](const String& text) { events.push_back(ControlReply(StringView(text.c_str(), text.size()))); });
            }

            ~EventCollector() {
                CoreManager::GetControlService().SetEventSink(nullptr);
            }

            EventCollector(const EventCollector&) = delete;
            EventCollector& operator=(const EventCollector&) = delete;

            Vector<ControlReply> events;
        };

        String EventName(const ControlReply& event) {
            return StringOf(event.View(), "event");
        }

        JsonView EventData(const ControlReply& event) {
            return MemberOf(event.View(), "data");
        }

    }  // namespace

    TEST_F(GameControlTest, StateChangeIsPushedAtTheEndOfTheFrame) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();  // the first look only records the version

        const ControlReply acted = Call("perform_action", R"({"type":"inc"})");
        ASSERT_TRUE(Succeeded(acted));
        EXPECT_TRUE(collector.events.empty());  // not before the frame ends
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 1u);
        EXPECT_EQ(EventName(collector.events[0]), "state_changed");
        const JsonView data = EventData(collector.events[0]);
        ASSERT_TRUE(data.IsObject());
        EXPECT_EQ(StringOf(data, "game"), String("FakeControllable"));
        EXPECT_EQ(IntOf(data, "version").value_or(-1), 1);
        const JsonView state = MemberOf(data, "state");
        ASSERT_TRUE(state.IsObject());
        EXPECT_EQ(IntOf(state, "counter").value_or(-1), 1);
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
        Find<FakeControllableGame>()->version = 7;  // moved before anybody looked
        EventCollector collector;

        service.EndFrame();

        EXPECT_TRUE(collector.events.empty());
    }

    TEST_F(GameControlTest, OneChangeIsPushedOnce) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        Find<FakeControllableGame>()->version += 5;  // a change of any size is one event
        service.EndFrame();
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 1u);
        EXPECT_EQ(IntOf(EventData(collector.events[0]), "version").value_or(-1), 5);
    }

    TEST_F(GameControlTest, AChangeThatNoClientMadeIsPushedToo) {
        // A click of a person moves the version without a request
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        Find<FakeControllableGame>()->counter = 41;
        ++Find<FakeControllableGame>()->version;
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 1u);
        const JsonView state = MemberOf(EventData(collector.events[0]), "state");
        ASSERT_TRUE(state.IsObject());
        EXPECT_EQ(IntOf(state, "counter").value_or(-1), 41);
    }

    TEST_F(GameControlTest, ResetAndSetStateArePushedToo) {
        ControlService& service = CoreManager::GetControlService();
        EventCollector collector;
        service.EndFrame();

        ASSERT_TRUE(Succeeded(Call("reset", R"({"start":10})")));
        service.EndFrame();
        ASSERT_TRUE(Succeeded(Call("set_state", R"({"counter":20})")));
        service.EndFrame();

        ASSERT_EQ(collector.events.size(), 2u);
        EXPECT_EQ(IntOf(MemberOf(EventData(collector.events[0]), "state"), "counter").value_or(-1), 10);
        EXPECT_EQ(IntOf(MemberOf(EventData(collector.events[1]), "state"), "counter").value_or(-1), 20);
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

        ++Find<FakeControllableGame>()->version;
        service.EndFrame();

        EXPECT_EQ(collector.events.size(), 1u);
    }

}  // namespace BECore::Tests
