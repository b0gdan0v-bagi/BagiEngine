// get_state, list_actions and the state_changed push write through the writing forms of IControllableGame (WriteState, WriteActions): the bytes are
// those the same documents had as a `Json` (keys in byte order).

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
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
                service.Post(MakeRequestText(kRequestId, method), [&text](const String& reply) { text = reply; });
                service.Drain();
                return text;
            }

            /// The pushes of the sink, as text
            void CollectPushes() {
                service.SetEventSink([this](const String& text) { pushes.emplace_back(text.c_str(), text.size()); });
            }

            /// The answer to get_state with the counter of the fake and the version it has now
            static String StateReply(int counter) {
                return Format(R"({{"id":{},"ok":true,"result":{{"game":"FakeControllable","state":{{"counter":{}}},"version":{}}}}})", kRequestId, counter, Fake().version);
            }

            /// The answer to list_actions
            static String ActionsReply() {
                return Format(R"({{"id":{},"ok":true,"result":{{"actions":[{{"type":"inc"}}],"game":"FakeControllable","version":{}}}}})", kRequestId, Fake().version);
            }

            ControlService service;
            Vector<String> pushes;
            Vector<String> answers;
        };

    }  // namespace

    TEST_F(GameControlWriterTest, GetStateAndListActionsAreTheBytesTheDocumentsHadAsAJson) {
        Fake().counter = 9;
        ++Fake().version;

        EXPECT_EQ(Ask("get_state"), StateReply(9));
        EXPECT_EQ(Ask("list_actions"), ActionsReply());
    }

    TEST_F(GameControlWriterTest, EachRequestCallsTheWritingFormOnce) {
        Ask("get_state");
        Ask("list_actions");
        Ask("list_actions");

        EXPECT_EQ(Fake().writeStateCalls, 1);
        EXPECT_EQ(Fake().writeActionsCalls, 2);
    }

    TEST_F(GameControlWriterTest, AnErrorFromTheWritingFormIsTheErrorReplyWithNothingOfWhatWasWritten) {
        Fake().failWrites = true;

        EXPECT_EQ(Ask("get_state"), MakeErrorResponse(ToString(kRequestId), MakeControlError("write_failed", "the state cannot be written")));
        EXPECT_EQ(Ask("list_actions"), MakeErrorResponse(ToString(kRequestId), MakeControlError("write_failed", "the actions cannot be written")));

        Fake().failWrites = false;  // the scratch of the service is intact after the rollbacks
        EXPECT_EQ(Ask("get_state"), StateReply(0));
    }

    TEST_F(GameControlWriterTest, ThePushIsTheBytesTheEventHadAsAJson) {
        CollectPushes();
        service.EndFrame();  // the first look only records the version

        Fake().AddToCounter(3);
        service.EndFrame();

        ASSERT_EQ(pushes.size(), 1u);
        EXPECT_EQ(pushes[0], Format(R"({{"data":{{"game":"FakeControllable","state":{{"counter":3}},"version":{}}},"event":"state_changed"}})", Fake().version));
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

    TEST_F(GameControlWriterTest, AnObserverOfRunUntilSeesStateChangedWithTheState) {
        service.EndFrame();  // records the version
        service.Post(MakeRequestText(1, "run_until", R"({"condition":{"event":{"equal":{"game":"FakeControllable"},"name":"state_changed"}},"maxFrames":3})"),
                     [this](const String& text) { answers.emplace_back(text.c_str(), text.size()); });
        service.Drain();
        ASSERT_TRUE(answers.empty());  // deferred until a frame ends

        Fake().AddToCounter(4);
        service.EndFrame();

        ASSERT_EQ(answers.size(), 1u);
        const ControlReply answer(StringView(answers[0].c_str(), answers[0].size()));
        ASSERT_TRUE(answer.Result().IsObject());
        EXPECT_EQ(BoolOf(answer.Result(), "met"), true);
        EXPECT_EQ(StringOf(answer.Result(), "reason"), String("event"));
    }

    TEST_F(GameControlWriterTest, AnObserverGetsTheStateAsAView) {
        Vector<ControlReply> seen;  // the data of each event, kept as a document of its own: the view is valid for the call only
        const auto id = service.AddPublishObserver([&seen](StringView event, JsonView data) {
            if (event == GameControlEvents::kStateChanged) {
                seen.emplace_back(data.Raw());
            }
        });
        service.EndFrame();

        Fake().AddToCounter(6);
        service.EndFrame();
        service.RemovePublishObserver(id);

        ASSERT_EQ(seen.size(), 1u);
        const JsonView state = MemberOf(seen[0].View(), "state");
        ASSERT_TRUE(state.IsObject());
        EXPECT_EQ(IntOf(state, "counter").value_or(-1), 6);
        EXPECT_EQ(StringOf(seen[0].View(), "game"), String("FakeControllable"));
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
        service.EndFrame();
        Fake().AddToCounter(1);
        service.EndFrame();  // grows the scratches
        Fake().AddToCounter(1);
        const size_t before = sent;

        ExpectNoAllocations([this] { service.EndFrame(); });

        EXPECT_GT(sent, before);
    }

    TEST_F(GameControlWriterTest, ASecondGetStateIntoTheKeptScratchAllocatesNothing) {
        size_t sent = 0;
        const auto count = [&sent](const String& text) { sent += text.size(); };
        const String request = MakeRequestText(kRequestId, "get_state");
        service.Post(request, count);
        service.Drain();  // grows the scratch
        service.Post(request, count);
        const size_t before = sent;

        ExpectNoAllocations([this] { service.Drain(); });

        EXPECT_GT(sent, before);
    }

}  // namespace BECore::Tests
