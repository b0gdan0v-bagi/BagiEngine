#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/Printers.h>
#include <Events/ClipboardEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Collects the pastes that reach the game while it is alive
        class PasteRecorder : public SubscriptionHolder {
        public:
            PasteRecorder() {
                Subscribe<ClipboardEvents::PasteEvent, &PasteRecorder::OnPaste>(this);
            }

            eastl::vector<eastl::string> pastes;

        private:
            void OnPaste(const ClipboardEvents::PasteEvent& event) {
                pastes.push_back(event.text);
            }
        };

        /// Pauses the game for the length of a test and gives it back
        class ScopedPause {
        public:
            ScopedPause() {
                CoreManager::GetPause().SetPaused(true);
            }

            ~ScopedPause() {
                CoreManager::GetPause().SetPaused(false);
            }

            ScopedPause(const ScopedPause&) = delete;
            ScopedPause& operator=(const ScopedPause&) = delete;
        };

    }  // namespace

    class ClipboardTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _fake = New<FakeClipboard>();
            _scope.emplace(IntrusivePtr<IClipboardBackend>(_fake));
        }

        void TearDown() override {
            _scope.reset();
        }

        Clipboard& Board() {
            return CoreManager::GetClipboard();
        }

        /// Asks for the text and records every answer in the order of arrival
        void Ask() {
            Board().RequestText([this](const ClipboardText& answer) { answers.push_back(answer); });
        }

        eastl::vector<ClipboardText> answers;

    private:
        IntrusivePtr<FakeClipboard> _fake;
        std::optional<ScopedClipboardBackend> _scope;

    protected:
        FakeClipboard& Fake() {
            return *_fake;
        }
    };

    TEST_F(ClipboardTest, ATextWrittenIsReadBack) {
        EXPECT_TRUE(Board().IsAvailable());
        EXPECT_TRUE(Board().SetText("hello мир"));
        EXPECT_EQ(Fake().Content(), "hello мир");

        Ask();
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].status, ClipboardStatus::Ok);
        EXPECT_EQ(answers[0].text, "hello мир");
    }

    TEST_F(ClipboardTest, AnEmptyClipboardIsEmpty) {
        Ask();
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].status, ClipboardStatus::Empty);
        EXPECT_TRUE(answers[0].text.empty());
    }

    TEST_F(ClipboardTest, ARefusedReadAndWriteAreValues) {
        Fake().SetContent("secret");
        Fake().SetRefuse(true);

        EXPECT_FALSE(Board().SetText("x"));
        Ask();
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].status, ClipboardStatus::Refused);
        EXPECT_TRUE(answers[0].text.empty());
    }

    TEST_F(ClipboardTest, AClipboardThatIsNotAvailableAnswersUnavailable) {
        Fake().SetAvailable(false);

        EXPECT_FALSE(Board().IsAvailable());
        EXPECT_FALSE(Board().SetText("x"));
        Ask();
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].status, ClipboardStatus::Unavailable);
        EXPECT_EQ(Fake().WriteCount(), 0);
        EXPECT_EQ(Fake().ReadCount(), 0);
    }

    TEST_F(ClipboardTest, NoBackendIsUnavailableAndAnsweredLater) {
        Board().SetBackend({});

        EXPECT_FALSE(Board().IsAvailable());
        EXPECT_FALSE(Board().SetText("x"));
        Ask();
        EXPECT_TRUE(answers.empty());  // not inside the call
        EXPECT_TRUE(Board().HasPending());
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].status, ClipboardStatus::Unavailable);
        EXPECT_FALSE(Board().HasPending());
    }

    TEST_F(ClipboardTest, ACallbackNeverRunsInsideTheRequest) {
        Fake().SetContent("a");

        Ask();
        EXPECT_TRUE(answers.empty());
        Board().Update();
        EXPECT_EQ(answers.size(), 1u);
    }

    TEST_F(ClipboardTest, ARequestsDuringOneDeferredReadShareItsResultInOrder) {
        Fake().SetContent("shared");
        Fake().SetDeferred(2);
        eastl::vector<int> order;
        Board().RequestText([&](const ClipboardText&) { order.push_back(1); });
        Board().Update();
        Board().RequestText([&](const ClipboardText&) { order.push_back(2); });
        Board().Update();
        EXPECT_TRUE(order.empty());
        Board().Update();

        EXPECT_EQ(order, (eastl::vector<int>{1, 2}));
        EXPECT_EQ(Fake().ReadCount(), 1);
        EXPECT_FALSE(Board().HasPending());
    }

    TEST_F(ClipboardTest, ACallbackThatAsksAgainIsAnsweredAtTheNextUpdate) {
        Fake().SetContent("again");
        int calls = 0;
        ClipboardCallback callback = [&](const ClipboardText&) {
            ++calls;
            if (calls < 3) {
                Board().RequestText([&](const ClipboardText&) { ++calls; });
            }
        };
        Board().RequestText(callback);

        Board().Update();
        EXPECT_EQ(calls, 1);
        EXPECT_TRUE(Board().HasPending());
        Board().Update();
        EXPECT_EQ(calls, 2);
        EXPECT_FALSE(Board().HasPending());
    }

    TEST_F(ClipboardTest, ARequestPasteEmitsOnePasteEventOnOk) {
        PasteRecorder recorder;
        Fake().SetContent("ABC");

        Board().RequestPaste();
        EXPECT_TRUE(recorder.pastes.empty());
        Board().Update();
        Board().Update();

        EXPECT_EQ(recorder.pastes, (eastl::vector<eastl::string>{"ABC"}));
    }

    TEST_F(ClipboardTest, ARequestPasteOnEmptyOrRefusedEmitsNothing) {
        PasteRecorder recorder;

        Board().RequestPaste();
        Board().Update();
        Fake().SetContent("ABC");
        Fake().SetRefuse(true);
        Board().RequestPaste();
        Board().Update();

        EXPECT_TRUE(recorder.pastes.empty());
    }

    TEST_F(ClipboardTest, APasteTheBackendAnnouncesBecomesAPasteEvent) {
        PasteRecorder recorder;
        Fake().QueuePasted("from the page");

        Board().Update();

        EXPECT_EQ(recorder.pastes, (eastl::vector<eastl::string>{"from the page"}));
    }

    TEST_F(ClipboardTest, AnnouncedPastesAreDroppedWhilePaused) {
        PasteRecorder recorder;
        {
            const ScopedPause pause;
            Fake().QueuePasted("lost");
            Board().Update();
        }
        Board().Update();

        EXPECT_TRUE(recorder.pastes.empty());
    }

    TEST_F(ClipboardTest, ARequestedPasteIsNotDroppedWhilePaused) {
        PasteRecorder recorder;
        Fake().SetContent("kept");
        const ScopedPause pause;

        Board().RequestPaste();
        Board().Update();

        EXPECT_EQ(recorder.pastes, (eastl::vector<eastl::string>{"kept"}));
    }

    TEST_F(ClipboardTest, ATextOverTheLimitIsRefused) {
        const eastl::string tooLong(kMaxClipboardBytes + 1, 'x');
        EXPECT_FALSE(Board().SetText(tooLong));
        EXPECT_EQ(Fake().WriteCount(), 0);

        const eastl::string exact(kMaxClipboardBytes, 'x');
        EXPECT_TRUE(Board().SetText(exact));

        Fake().SetContent(tooLong);
        Ask();
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].status, ClipboardStatus::Refused);
        EXPECT_TRUE(answers[0].text.empty());
    }

    TEST_F(ClipboardTest, TheBackendIsAskedAgainWhenItIsReplacedDuringARead) {
        Fake().SetContent("old");
        Fake().SetDeferred(5);
        Ask();
        Board().Update();

        auto other = New<FakeClipboard>("new");
        Board().SetBackend(IntrusivePtr<IClipboardBackend>(other));
        Board().Update();

        ASSERT_EQ(answers.size(), 1u);
        EXPECT_EQ(answers[0].text, "new");
    }

    TEST_F(ClipboardTest, TheScopedBackendPutsTheOldOneBack) {
        IClipboardBackend* installed = Board().GetBackend().Get();
        {
            auto other = New<FakeClipboard>();
            const ScopedClipboardBackend scope{IntrusivePtr<IClipboardBackend>(other)};
            EXPECT_EQ(Board().GetBackend().Get(), other.Get());
        }
        EXPECT_EQ(Board().GetBackend().Get(), installed);
    }

    TEST_F(ClipboardTest, StatusNamesAreTheWordsOfTheControlApi) {
        EXPECT_EQ(ToString(ClipboardStatus::Ok), "ok");
        EXPECT_EQ(ToString(ClipboardStatus::Empty), "empty");
        EXPECT_EQ(ToString(ClipboardStatus::Refused), "refused");
        EXPECT_EQ(ToString(ClipboardStatus::Unavailable), "unavailable");
    }

    TEST_F(ClipboardTest, AnUpdateWithNothingPendingChangesNothing) {
        EXPECT_FALSE(Board().HasPending());
        Board().Update();
        EXPECT_FALSE(Board().HasPending());
    }

#if BE_PROFILER_ALLOC
    TEST_F(ClipboardTest, AnUpdateWithNothingPendingAllocatesNothing) {
        Board().Update();  // whatever the first call makes
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        for (int i = 0; i < 100; ++i) {
            Board().Update();
        }
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
    }
#endif

}  // namespace BECore::Tests
