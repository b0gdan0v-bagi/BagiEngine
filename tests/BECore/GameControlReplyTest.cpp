// The answers of the game methods are pinned as text: the bytes a client has always got (keys alphabetical, the version of the state after the call).

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Time/Time.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 1.0 / 64;  // a power of two keeps the sums exact

        class GameControlReplyTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetTime().Reset();
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
                RegisterGameControl(service);
            }

            void TearDown() override {
                service.Shutdown();
                GameManager::GetInstance().Shutdown();
                CoreManager::GetTime().Reset();
            }

            static FakeControllableGame& Fake() {
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakeControllableGame>()) {
                        return *fake;
                    }
                }
                ADD_FAILURE() << "no FakeControllableGame is registered";
                static FakeControllableGame missing;
                return missing;
            }

            /// Posts one request and returns the text of the answer ({} while it is deferred)
            String Ask(StringView method, StringView paramsText = {}) {
                String text;
                service.Post(MakeRequestText(++lastId, method, paramsText), [&text](const String& reply) { text = reply; });
                service.Drain();
                return text;
            }

            /// Posts a deferred request and plays real-clock frames until it is answered
            String AskAndWait(StringView method, StringView paramsText) {
                String text;
                service.Post(MakeRequestText(++lastId, method, paramsText), [&text](const String& reply) { text = reply; });
                constexpr int kGuard = 100;
                for (int frame = 0; frame < kGuard && text.empty(); ++frame) {
                    CoreManager::GetTime().Advance(kFrameSeconds);
                    service.Drain();
                    service.EndFrame();
                }
                return text;
            }

            /// The `params` of run_until over @p conditionText (a JSON object)
            static String RunUntilParams(StringView conditionText, int64_t maxFrames) {
                return Format(R"({{"condition":{},"maxFrames":{}}})", conditionText, maxFrames);
            }

            ControlService service;
            int64_t lastId = 0;
        };

        bool Same(const String& text, StringView expected) {
            return StringView(text) == expected;
        }

    }  // namespace

    TEST_F(GameControlReplyTest, ListGamesAnswersTheGamesAndTheActiveOne) {
        const String text = Ask("list_games");

        EXPECT_TRUE(Same(
            text,
            R"json({"id":1,"ok":true,"result":{"active":"FakeControllable","games":[{"controllable":true,"name":"FakeControllable","scene":""},{"controllable":false,"name":"FakePlain","scene":""},{"controllable":true,"name":"ReplayTest","scene":""}]}})json"))
            << text.c_str();
    }

    TEST_F(GameControlReplyTest, ListGamesWithoutAnActiveGameLeavesActiveOut) {
        GameManager::GetInstance().Shutdown();

        const String text = Ask("list_games");

        EXPECT_TRUE(Same(text, R"json({"id":1,"ok":true,"result":{"games":[]}})json")) << text.c_str();
    }

    TEST_F(GameControlReplyTest, SwitchGameAnswersTheNewActiveGame) {
        const String text = Ask("switch_game", R"({"name":"FakePlain"})");

        EXPECT_TRUE(Same(text, R"json({"id":1,"ok":true,"result":{"active":"FakePlain"}})json")) << text.c_str();
    }

    TEST_F(GameControlReplyTest, ResetAnswersTheGameAndTheNewVersion) {
        const String controllable = Ask("reset", R"({"start":4})");
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        const String plain = Ask("reset");

        EXPECT_TRUE(Same(controllable, R"json({"id":1,"ok":true,"result":{"game":"FakeControllable","version":1}})json")) << controllable.c_str();
        EXPECT_TRUE(Same(plain, R"json({"id":2,"ok":true,"result":{"game":"FakePlain"}})json")) << plain.c_str();
    }

    TEST_F(GameControlReplyTest, SetStateAnswersTheGameAndTheNewVersion) {
        const String text = Ask("set_state", R"({"counter":41})");

        EXPECT_TRUE(Same(text, R"json({"id":1,"ok":true,"result":{"game":"FakeControllable","version":1}})json")) << text.c_str();
    }

    TEST_F(GameControlReplyTest, PerformActionAnswersTheOutcomeAndTheVersionAfterTheAction) {
        const String first = Ask("perform_action", R"({"type":"inc"})");
        const String second = Ask("perform_action", R"({"type":"inc"})");

        EXPECT_TRUE(Same(first, R"json({"id":1,"ok":true,"result":{"game":"FakeControllable","outcome":{"counter":1},"version":1}})json")) << first.c_str();
        EXPECT_TRUE(Same(second, R"json({"id":2,"ok":true,"result":{"game":"FakeControllable","outcome":{"counter":2},"version":2}})json")) << second.c_str();
    }

    TEST_F(GameControlReplyTest, RunUntilAnswersTheClockAndTheGameWhenAPointerIsMet) {
        Fake().counter = 7;

        const String text = Ask("run_until", RunUntilParams(R"({"equals":7,"pointer":"/state/counter"})", 5));

        EXPECT_TRUE(Same(
            text,
            R"json({"id":1,"ok":true,"result":{"deltaTime":0.0,"fixedDeltaTime":0.01666666753590107,"frame":0,"frames":0,"game":"FakeControllable","met":true,"reason":"pointer","totalTime":0.0,"version":0}})json"))
            << text.c_str();
    }

    TEST_F(GameControlReplyTest, RunUntilAnswersWhenTheVersionMoved) {
        CoreManager::GetTime().Advance(kFrameSeconds);
        service.SetFrameEndHook("test", [this] { ++Fake().version; });

        const String text = AskAndWait("run_until", RunUntilParams(R"({"versionMoved":true})", 5));

        EXPECT_TRUE(Same(
            text,
            R"json({"id":1,"ok":true,"result":{"deltaTime":0.015625,"fixedDeltaTime":0.01666666753590107,"frame":2,"frames":1,"game":"FakeControllable","met":true,"reason":"versionMoved","totalTime":0.03125,"version":1}})json"))
            << text.c_str();
    }

    TEST_F(GameControlReplyTest, RunUntilAtTheFrameLimitAnswersMetFalse) {
        const String text = AskAndWait("run_until", RunUntilParams(R"({"equals":99,"pointer":"/state/counter"})", 2));

        EXPECT_TRUE(Same(
            text,
            R"json({"id":1,"ok":true,"result":{"deltaTime":0.015625,"fixedDeltaTime":0.01666666753590107,"frame":2,"frames":2,"game":"FakeControllable","met":false,"totalTime":0.03125,"version":0}})json"))
            << text.c_str();
    }

    TEST_F(GameControlReplyTest, RunUntilWithoutAnActiveGameLeavesTheGameOut) {
        GameManager::GetInstance().Shutdown();

        const String text = AskAndWait("run_until", RunUntilParams(R"({"event":{"name":"never_pushed"}})", 2));

        EXPECT_TRUE(Same(text, R"json({"id":1,"ok":true,"result":{"deltaTime":0.015625,"fixedDeltaTime":0.01666666753590107,"frame":2,"frames":2,"met":false,"totalTime":0.03125}})json"))
            << text.c_str();
    }

    TEST_F(GameControlReplyTest, TheEnvelopeOfPerformActionAllocatesNothingBeyondTheActionItself) {
        service.RegisterHandler("action_only", [](ControlCall& call) {
            String outcome;
            if (const auto performed = ControlledAction(Fake(), call.Params(), outcome); !performed) {
                call.Fail(performed.error());
                return;
            }
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        // Post() parses the request, Drain() makes the call and the game builds its outcome: all of it is the same for both methods, so only the writing of
        // the answer differs. A first round of each has grown the reply buffer
        const auto drained = [&](StringView method) {
            service.Post(MakeRequestText(1, method, R"({"type":"inc"})"), [](const String&) {});
            return CountAllocations([this] { service.Drain(); });
        };
        drained("perform_action");
        drained("action_only");

        const AllocCount performed = drained("perform_action");
        const AllocCount bare = drained("action_only");

        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_EQ(performed.count, bare.count);
        EXPECT_EQ(performed.bytes, bare.bytes);
    }

}  // namespace BECore::Tests
