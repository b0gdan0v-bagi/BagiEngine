#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Clipboard/ClipboardControl.h>
#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Common/Printers.h>
#include <Events/ClipboardEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A `clipboard_set` request of @p text
        String SetRequest(int64_t id, StringView text) {
            String params;
            JsonWriter writer(params);
            writer.BeginObject();
            writer.Member("text", text);
            writer.EndObject();
            return MakeRequestText(id, "clipboard_set", StringView(params.c_str(), params.size()));
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
            // A request the case gave up on (a timeout) still waits in the process's clipboard, with a callback into this fixture: the read
            // ends here, while the fixture lives, or the next case's Update would ask its backend and call into a dead service
            for (int frame = 0; CoreManager::GetClipboard().HasPending() && frame < kFrameGuard; ++frame) {
                CoreManager::GetClipboard().Update();
            }
            EXPECT_FALSE(CoreManager::GetClipboard().HasPending());
            _scope.reset();
            CoreManager::GetTime().Reset();
        }

        FakeClipboard& Fake() {
            return *_fake;
        }

        void Post(const String& text) {
            service.Post(text, [this](String reply) { responses.emplace_back(StringView(reply.c_str(), reply.size())); });
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

        JsonView ResultOf(size_t index) const {
            return index < responses.size() ? responses[index].Result() : JsonView();
        }

        String ErrorCode(size_t index) const {
            return index < responses.size() ? responses[index].ErrorCode() : String();
        }

        ControlService service;
        Vector<ControlReply> responses;

    private:
        IntrusivePtr<FakeClipboard> _fake;
        Optional<ScopedClipboardBackend> _scope;
    };

    TEST_F(ClipboardControlTest, SetThenGetGivesTheTextBack) {
        Post(SetRequest(1, "room 4711"));
        Post(MakeRequestText(2, "clipboard_get"));
        RunFramesUntilAnswered(2);

        ASSERT_EQ(responses.size(), 2u);
        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_EQ(BoolOf(ResultOf(0), "ok"), Optional<bool>(true));
        EXPECT_EQ(Fake().Content(), "room 4711");
        ASSERT_TRUE(ResultOf(1).IsObject());
        EXPECT_EQ(StringOf(ResultOf(1), "status"), "ok");
        EXPECT_EQ(StringOf(ResultOf(1), "text"), "room 4711");
    }

    TEST_F(ClipboardControlTest, AGetAnswersALongTextWholeWithoutCuttingIt) {
        // Past the small-string buffer, and a large one: the answer looks at the held text, so a cut or a dangling view would show here
        for (const size_t size : {size_t{64}, size_t{100} * 1024}) {
            responses.clear();
            const String text(size, 'x');
            Fake().SetContent(text);
            Post(MakeRequestText(1, "clipboard_get"));
            RunFramesUntilAnswered(1);

            ASSERT_TRUE(ResultOf(0).IsObject()) << size;
            EXPECT_EQ(StringOf(ResultOf(0), "status"), "ok") << size;
            EXPECT_EQ(StringOf(ResultOf(0), "text"), text) << size;
        }
    }

    TEST_F(ClipboardControlTest, GetOnAnEmptyClipboardIsEmpty) {
        Post(MakeRequestText(1, "clipboard_get"));
        RunFramesUntilAnswered(1);

        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "empty");
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "");
    }

    TEST_F(ClipboardControlTest, GetWaitsForADeferredRead) {
        Fake().SetContent("slow");
        Fake().SetDeferred(5);
        Post(MakeRequestText(1, "clipboard_get"));
        const int frames = RunFramesUntilAnswered(1);

        EXPECT_GT(frames, 5);
        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "ok");
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "slow");
    }

    TEST_F(ClipboardControlTest, GetOfARefusedClipboardIsRefused) {
        Fake().SetContent("secret");
        Fake().SetRefuse(true);
        Post(MakeRequestText(1, "clipboard_get"));
        RunFramesUntilAnswered(1);

        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "refused");
        EXPECT_EQ(StringOf(ResultOf(0), "text"), "");
    }

    TEST_F(ClipboardControlTest, WithNoBackendTheAnswerIsUnavailableAndSetIsNotOk) {
        CoreManager::GetClipboard().SetBackend({});
        Post(MakeRequestText(1, "clipboard_get"));
        Post(SetRequest(2, "x"));
        RunFramesUntilAnswered(2);

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(StringOf(ResultOf(0), "status"), "unavailable");
        ASSERT_TRUE(ResultOf(1).IsObject());
        EXPECT_EQ(BoolOf(ResultOf(1), "ok"), Optional<bool>(false));
    }

    TEST_F(ClipboardControlTest, AGetThatNeverArrivesFailsWithATimeout) {
        Fake().SetDeferred(static_cast<int>(kMaxClipboardWaitFrames) * 2);
        Post(MakeRequestText(1, "clipboard_get"));
        const int frames = RunFramesUntilAnswered(1);

        EXPECT_EQ(ErrorCode(0), "clipboard_timeout");
        EXPECT_EQ(frames, static_cast<int>(kMaxClipboardWaitFrames));
    }

    TEST_F(ClipboardControlTest, SetWithoutAStringTextIsInvalidParams) {
        Post(MakeRequestText(1, "clipboard_set"));
        Post(MakeRequestText(2, "clipboard_set", R"({"text":5})"));
        RunFramesUntilAnswered(2);

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(ErrorCode(0), "invalid_params");
        EXPECT_EQ(ErrorCode(1), "invalid_params");
        EXPECT_EQ(Fake().WriteCount(), 0);
    }

    TEST_F(ClipboardControlTest, PasteGivesOnePasteEventBeforeTheAnswer) {
        PasteRecorder recorder;
        Fake().SetContent("ABC");
        Post(MakeRequestText(1, "clipboard_paste"));
        RunFramesUntilAnswered(1);

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(0), "");
        EXPECT_EQ(recorder.pastes, (Vector<String>{"ABC"}));
    }

    TEST_F(ClipboardControlTest, PasteOfAnEmptyClipboardEmitsNothing) {
        PasteRecorder recorder;
        Post(MakeRequestText(1, "clipboard_paste"));
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
        Post(MakeRequestText(1, "clipboard_get"));

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
