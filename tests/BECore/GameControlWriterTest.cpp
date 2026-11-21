// get_state, list_actions and the state_changed push write through the writing forms of IControllableGame (WriteState, WriteActions): the bytes of the
// default form, of an overriding form and of the old path through GetState() / GetActions() are the same.

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int64_t kRequestId = 5;

        class GameControlWriterTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
                RegisterGameControl(service);
            }

            void TearDown() override {
                service.Shutdown();
                GameManager::GetInstance().Shutdown();
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

            /// The text of the answer to @p method
            String Ask(StringView method) {
                String text;
                service.Post(MakeRequest(kRequestId, method), [&text](const String& reply) { text = reply; });
                service.Drain();
                return text;
            }

            /// The pushes of the sink, as text
            void CollectPushes() {
                service.SetEventSink([this](const String& text) { pushes.emplace_back(text.c_str(), text.size()); });
            }

            /// The old path: GetState() / GetActions() as a `Json` document, the text a `Reply(Json)` of it makes
            static String ReferenceReply(const char* member, const Json& value) {
                Json result = Json::object();
                result["game"] = "FakeControllable";
                result[member] = value;
                result["version"] = Fake().version;
                return MakeOkResponse(Json(kRequestId), std::move(result));
            }

            static String ReferencePush(const Json& data) {
                Json event = Json::object();
                event["data"] = data;
                event["event"] = "state_changed";
                return DumpJson(event);
            }

            ControlService service;
            Vector<String> pushes;
            Vector<String> answers;
        };

    }  // namespace

    TEST_F(GameControlWriterTest, GetStateAndListActionsAreTheBytesOfTheJsonPathForTheDefaultAndTheOverridingForm) {
        for (const bool handWritten : {false, true}) {
            Fake().handWritten = handWritten;
            Fake().counter = 9;
            ++Fake().version;

            EXPECT_EQ(Ask("get_state"), ReferenceReply("state", *Fake().GetState())) << "handWritten " << handWritten;
            EXPECT_EQ(Ask("list_actions"), ReferenceReply("actions", *Fake().GetActions())) << "handWritten " << handWritten;
        }
    }

    TEST_F(GameControlWriterTest, TheOverridingFormIsTheOneCalledAndTheDefaultIsNotCounted) {
        Ask("get_state");
        Ask("list_actions");
        EXPECT_EQ(Fake().writeStateCalls, 0);
        EXPECT_EQ(Fake().writeActionsCalls, 0);

        Fake().handWritten = true;
        Ask("get_state");
        Ask("list_actions");
        Ask("list_actions");

        EXPECT_EQ(Fake().writeStateCalls, 1);
        EXPECT_EQ(Fake().writeActionsCalls, 2);
    }

    TEST_F(GameControlWriterTest, AnErrorFromTheWritingFormIsTheErrorReplyWithNothingOfWhatWasWritten) {
        Fake().failWrites = true;

        EXPECT_EQ(Ask("get_state"), MakeErrorResponse(Json(kRequestId), MakeControlError("write_failed", "the state cannot be written")));
        EXPECT_EQ(Ask("list_actions"), MakeErrorResponse(Json(kRequestId), MakeControlError("write_failed", "the actions cannot be written")));

        Fake().failWrites = false;  // the scratch of the service is intact after the rollbacks
        EXPECT_EQ(Ask("get_state"), ReferenceReply("state", *Fake().GetState()));
    }

    TEST_F(GameControlWriterTest, ThePushIsTheBytesOfTheJsonPathForTheDefaultAndTheOverridingForm) {
        CollectPushes();
        service.EndFrame();  // the first look only records the version

        Vector<String> references;
        for (const bool handWritten : {false, true}) {
            Fake().handWritten = handWritten;
            Fake().AddToCounter(3);
            Json data = Json::object();
            data["game"] = "FakeControllable";
            data["state"] = *Fake().GetState();
            data["version"] = Fake().version;
            references.push_back(ReferencePush(data));
            service.EndFrame();
        }

        ASSERT_EQ(pushes.size(), 2u);
        EXPECT_EQ(pushes[0], references[0]);
        EXPECT_EQ(pushes[1], references[1]);
    }

    TEST_F(GameControlWriterTest, AStateThatFailsLeavesStateOutOfThePushAndThePushStillGoes) {
        CollectPushes();
        service.EndFrame();
        Fake().failWrites = true;
        Fake().AddToCounter(1);

        service.EndFrame();

        ASSERT_EQ(pushes.size(), 1u);
        EXPECT_EQ(pushes[0], String(R"({"data":{"game":"FakeControllable","version":1},"event":"state_changed"})"));
    }

    TEST_F(GameControlWriterTest, AnObserverOfRunUntilStillSeesStateChangedWithAJsonDataThatHasTheState) {
        Fake().handWritten = true;
        service.EndFrame();  // records the version
        service.Post(MakeRequest(1, "run_until",
                                 [] {
                                     Json equal = Json::object();
                                     equal["game"] = "FakeControllable";
                                     Json event = Json::object();
                                     event["name"] = "state_changed";
                                     event["equal"] = std::move(equal);
                                     Json condition = Json::object();
                                     condition["event"] = std::move(event);
                                     Json params = Json::object();
                                     params["condition"] = std::move(condition);
                                     params["maxFrames"] = 3;
                                     return params;
                                 }()),
                     [this](const String& text) { answers.emplace_back(text.c_str(), text.size()); });
        service.Drain();
        ASSERT_TRUE(answers.empty());  // deferred until a frame ends

        Fake().AddToCounter(4);
        service.EndFrame();

        ASSERT_EQ(answers.size(), 1u);
        const auto answer = ParseJson(answers[0]);
        ASSERT_TRUE(answer.has_value());
        const Json* result = FindJsonMember(*answer, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(BoolOf(result, "met"), true);
        EXPECT_EQ(StringOf(result, "reason"), String("event"));
    }

    TEST_F(GameControlWriterTest, AnObserverGetsTheStateAsAJson) {
        Fake().handWritten = true;
        Vector<Json> seen;
        const auto id = service.AddPublishObserver([&seen](StringView event, const Json& data) {
            if (event == GameControlEvents::kStateChanged) {
                seen.push_back(data);
            }
        });
        service.EndFrame();

        Fake().AddToCounter(6);
        service.EndFrame();
        service.RemovePublishObserver(id);

        ASSERT_EQ(seen.size(), 1u);
        const Json* state = FindJsonMember(seen[0], "state");
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(GetJsonInt(*state, "counter").value_or(-1), 6);
        EXPECT_EQ(StringOf(&seen[0], "game"), String("FakeControllable"));
    }

    TEST_F(GameControlWriterTest, AGameThatCannotBeControlledPushesNothing) {
        CollectPushes();
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);

        service.EndFrame();
        service.EndFrame();

        EXPECT_TRUE(pushes.empty());
    }

    TEST_F(GameControlWriterTest, ASecondPushOfAnOverridingFormIntoTheKeptScratchAllocatesNothing) {
        size_t sent = 0;
        service.SetEventSink([&sent](const String& text) { sent += text.size(); });
        Fake().handWritten = true;
        service.EndFrame();
        Fake().AddToCounter(1);
        service.EndFrame();  // grows the scratches
        Fake().AddToCounter(1);
        const size_t before = sent;

        ExpectNoAllocations([this] { service.EndFrame(); });

        EXPECT_GT(sent, before);
    }

}  // namespace BECore::Tests
