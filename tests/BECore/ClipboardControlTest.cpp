#include <BECore/Clipboard/ClipboardControl.h>
#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/Printers.h>
#include <Events/ClipboardEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String MakeRequest(int64_t id, std::string_view method, const Json& params = Json()) {
            Json request = Json::object();
            request["id"] = id;
            request["method"] = std::string(method);
            if (!params.is_null()) {
                request["params"] = params;
            }
            return DumpJson(request);
        }

        Json TextParams(std::string_view text) {
            Json params = Json::object();
            params["text"] = std::string(text);
            return params;
        }

        std::string StringOf(const Json* object, StringView key) {
            if (object == nullptr) {
                return {};
            }
            const auto value = GetJsonString(*object, key);
            return value ? std::string(value->c_str(), value->size()) : std::string();
        }

        constexpr int kFrameGuard = 1000;  // a loop that does not end would hang the test run

    }  // namespace

    class ClipboardControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _fake = New<FakeClipboard>();
            _scope.emplace(IntrusivePtr<IClipboardBackend>(_fake));
            RegisterClipboardControl(service);
        }

        void TearDown() override {
            _scope.reset();
            CoreManager::GetTime().Reset();
        }

        FakeClipboard& Fake() {
            return *_fake;
        }

        void Post(const String& text) {
            service.Post(text, [this](String reply) {
                auto parsed = ParseJson(reply);
                responses.push_back(parsed ? std::move(*parsed) : Json());
            });
        }

        /// The order of CoreManager::OnGameCycle: the clipboard answers, then the requests run, then the frame ends. Returns the frames it took
        int RunFramesUntilAnswered(size_t answers) {
            int frames = 0;
            while (responses.size() < answers && frames < kFrameGuard) {
                CoreManager::GetClipboard().Update();
                service.Drain();
                service.EndFrame();
                ++frames;
            }
            return frames;
        }

        const Json* ResultOf(size_t index) const {
            return index < responses.size() ? FindJsonMember(responses[index], "result") : nullptr;
        }

        std::string ErrorCode(size_t index) const {
            if (index >= responses.size()) {
                return {};
            }
            return StringOf(FindJsonMember(responses[index], "error"), "code");
        }

        ControlService service;
        std::vector<Json> responses;

    private:
        IntrusivePtr<FakeClipboard> _fake;
        Optional<ScopedClipboardBackend> _scope;
    };

    TEST_F(ClipboardControlTest, SetThenGetGivesTheTextBack) {
        Post(MakeRequest(1, "clipboard_set", TextParams("room 4711")));
        Post(MakeRequest(2, "clipboard_get"));
        RunFramesUntilAnswered(2);

        ASSERT_EQ(responses.size(), 2u);
        ASSERT_NE(ResultOf(0), nullptr);
        EXPECT_EQ(GetJsonBool(*ResultOf(0), "ok"), Optional<bool>(true));
        EXPECT_EQ(Fake().Content(), "room 4711");
        ASSERT_NE(ResultOf(1), nullptr);
        EXPECT_EQ(StringOf(ResultOf(1), "status"), "ok");
        EXPECT_EQ(StringOf(ResultOf(1), "text"), "room 4711");
    }

    TEST_F(ClipboardControlTest, GetOnAnEmptyClipboardIsEmpty) {
        Post(MakeRequest(1, "clipboard_get"));
        RunFramesUntilAnswered(1);

        ASSERT_NE(ResultOf(0), nullptr);
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "empty");
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "");
    }

    TEST_F(ClipboardControlTest, GetWaitsForADeferredRead) {
        Fake().SetContent("slow");
        Fake().SetDeferred(5);
        Post(MakeRequest(1, "clipboard_get"));
        const int frames = RunFramesUntilAnswered(1);

        EXPECT_GT(frames, 5);
        ASSERT_NE(ResultOf(0), nullptr);
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "ok");
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "slow");
    }

    TEST_F(ClipboardControlTest, GetOfARefusedClipboardIsRefused) {
        Fake().SetContent("secret");
        Fake().SetRefuse(true);
        Post(MakeRequest(1, "clipboard_get"));
        RunFramesUntilAnswered(1);

        ASSERT_NE(ResultOf(0), nullptr);
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "refused");
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "");
    }

    TEST_F(ClipboardControlTest, WithNoBackendTheAnswerIsUnavailableAndSetIsNotOk) {
        CoreManager::GetClipboard().SetBackend({});
        Post(MakeRequest(1, "clipboard_get"));
        Post(MakeRequest(2, "clipboard_set", TextParams("x")));
        RunFramesUntilAnswered(2);

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "unavailable");
        ASSERT_NE(ResultOf(1), nullptr);
        EXPECT_EQ(GetJsonBool(*ResultOf(1), "ok"), Optional<bool>(false));
    }

    TEST_F(ClipboardControlTest, AGetThatNeverArrivesFailsWithATimeout) {
        Fake().SetDeferred(static_cast<int>(kMaxClipboardWaitFrames) * 2);
        Post(MakeRequest(1, "clipboard_get"));
        const int frames = RunFramesUntilAnswered(1);

        EXPECT_EQ(ErrorCode(0), "clipboard_timeout");
        EXPECT_EQ(frames, static_cast<int>(kMaxClipboardWaitFrames));
    }

    TEST_F(ClipboardControlTest, SetWithoutAStringTextIsInvalidParams) {
        Json notAString = Json::object();
        notAString["text"] = 5;
        Post(MakeRequest(1, "clipboard_set"));
        Post(MakeRequest(2, "clipboard_set", notAString));
        RunFramesUntilAnswered(2);

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(ErrorCode(0), "invalid_params");
        EXPECT_EQ(ErrorCode(1), "invalid_params");
        EXPECT_EQ(Fake().WriteCount(), 0);
    }

    TEST_F(ClipboardControlTest, PasteGivesOnePasteEventBeforeTheAnswer) {
        PasteRecorder recorder;
        Fake().SetContent("ABC");
        Post(MakeRequest(1, "clipboard_paste"));
        RunFramesUntilAnswered(1);

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(0), "");
        EXPECT_EQ(recorder.pastes, (eastl::vector<String>{"ABC"}));
    }

    TEST_F(ClipboardControlTest, PasteOfAnEmptyClipboardEmitsNothing) {
        PasteRecorder recorder;
        Post(MakeRequest(1, "clipboard_paste"));
        RunFramesUntilAnswered(1);

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(recorder.pastes.empty());
    }

    TEST_F(ClipboardControlTest, TheReadsWorkWithTheManualClock) {
        service.SetManualClock(true);
        GamePause pause;
        pause.SetPaused(true);
        service.SetPause(&pause);
        Fake().SetContent("manual");
        Fake().SetDeferred(2);
        Post(MakeRequest(1, "clipboard_get"));

        // The loop of the application: the idle cycle drains, a frame runs only while a deferred call is due
        for (int pass = 0; pass < kFrameGuard && responses.empty(); ++pass) {
            service.Drain();
            if (!service.IsStepDue()) {
                continue;
            }
            CoreManager::GetClipboard().Update();
            service.Drain();
            service.EndFrame();
        }

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "manual");
        service.SetPause(nullptr);  // the pause above goes away first
    }

}  // namespace BECore::Tests
