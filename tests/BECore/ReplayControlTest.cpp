#include <BECore/Application/CommandLine.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/ReplayTestGame.h>
#include <BECore/Time/GamePause.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Common/ControlledCalls.h>
#include <Common/EngineEnvironment.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ApplicationEvents.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/GameText.h>
#include <Games/ReplayControl.h>
#include <Games/ReplayFile.h>
#include <Games/SessionRecorder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        class ReplayControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _path = TestTempPath(Format("replay_control_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()));
                const Span<const String> arguments = CoreManager::GetCommandLine().GetArguments();
                _arguments.assign(arguments.begin(), arguments.end());
                CoreManager::GetTime().Reset();
                ReplayTestGame::RestartSeedCounter();
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("ReplayTest"_intern);
            }

            void TearDown() override {
                SessionRecorder::DestroyInstance();
                GameManager::GetInstance().Shutdown();
                CoreManager::GetCommandLine().Set(_arguments);
                CoreManager::GetTime().Reset();
                RemoveFileIfPresent(_path);
            }

            [[nodiscard]] String PathText() const {
                return String(_path.View());
            }

            /// The params that name a file: its @p path and, when given, the JSON value @p titleText as the title
            static String PathParamsOf(StringView path, StringView titleText = {}) {
                String text;
                JsonWriter writer(text);
                writer.BeginObject();
                writer.Member("path", path);
                if (!titleText.empty()) {
                    writer.Key("title");
                    writer.Raw(titleText);
                }
                writer.EndObject();
                return text;
            }

            [[nodiscard]] String PathParams(StringView titleText = {}) const {
                return PathParamsOf(PathText(), titleText);
            }

            [[nodiscard]] String ReadRecording() const {
                return ReadFile(_path);
            }

            [[nodiscard]] ParsedReplay ParseRecording() const {
                const String text = ReadRecording();
                auto parsed = ParseReplay(text);
                EXPECT_TRUE(parsed.has_value()) << (parsed ? "" : DescribeReplayError(parsed.error()).c_str());
                return parsed ? std::move(*parsed) : ParsedReplay{};
            }

            /// A control request the way a client sends it, answered on the spot
            static ControlReply Call(StringView method, StringView paramsText = "{}") {
                return CallControlText(CoreManager::GetControlService(), 1, method, paramsText);
            }

            static void SetCommandLine(std::initializer_list<const char*> arguments) {
                Vector<String> list;
                for (const char* argument : arguments) {
                    list.push_back(String(argument));
                }
                CoreManager::GetCommandLine().Set(list);
            }

            Path _path;
            Vector<String> _arguments;
        };

        /// The member names of an object in the order they were written: the generated writer writes them sorted, by design
        Vector<String> KeysOf(JsonView object) {
            Vector<String> keys;
            for (const JsonMember member : object.Members()) {
                String key;
                member.key.ReadText(key);
                keys.push_back(key);
            }
            return keys;
        }

    }  // namespace

    TEST_F(ReplayControlTest, TheAcceptedCallsAreInTheFileAndTheRefusedOnesAreNot) {
        const ControlReply started = Call("record_start", PathParams());
        ASSERT_TRUE(started.IsOk());
        const JsonView startedResult = started.Result();
        ASSERT_TRUE(startedResult.IsObject());
        EXPECT_EQ(StringOf(startedResult, "game"), "ReplayTest");
        EXPECT_EQ(StringOf(startedResult, "path"), PathText());

        ASSERT_TRUE(Call("reset", R"({"seed":9})").IsOk());
        ASSERT_TRUE(Call("perform_action", R"({"type":"roll"})").IsOk());
        ASSERT_TRUE(Call("perform_action", R"({"type":"arm"})").IsOk());
        EXPECT_EQ(Call("perform_action", R"({"type":"roll"})").ErrorCode(), "illegal_action");
        ASSERT_TRUE(Call("set_state", R"({"rolls":[1,2]})").IsOk());
        EXPECT_FALSE(Call("reset", R"({"seed":"x"})").IsOk());

        const ControlReply stopped = Call("record_stop");
        ASSERT_TRUE(stopped.IsOk());
        ASSERT_TRUE(stopped.Result().IsObject());
        EXPECT_EQ(IntOf(stopped.Result(), "entries").value_or(-1), 4);

        const ParsedReplay replay = ParseRecording();
        ASSERT_EQ(replay.entries.size(), 4u);
        EXPECT_EQ(replay.entries[0].op, ReplayOp::Reset);
        EXPECT_EQ(replay.entries[0].params, R"({"seed":9})");
        EXPECT_EQ(replay.entries[1].op, ReplayOp::Action);
        EXPECT_EQ(replay.entries[2].params, R"({"type":"arm"})");
        EXPECT_EQ(replay.entries[3].op, ReplayOp::State);
    }

    TEST_F(ReplayControlTest, TheRecordingAnswersHaveExactlyTheirKeys) {
        const ControlReply started = Call("record_start", PathParams());
        ASSERT_TRUE(started.IsOk());
        EXPECT_EQ(KeysOf(started.Result()), (Vector<String>{"game", "path"}));

        ASSERT_TRUE(Call("reset", R"({"seed":9})").IsOk());
        const ControlReply stopped = Call("record_stop");
        ASSERT_TRUE(stopped.IsOk());
        EXPECT_EQ(String(stopped.Result().Raw()), "{\"entries\":1}");
    }

    TEST_F(ReplayControlTest, ATitleLandsInTheHeader) {
        ASSERT_TRUE(Call("record_start", PathParams(R"("a short session")")).IsOk());
        ASSERT_TRUE(Call("record_stop").IsOk());

        EXPECT_EQ(ParseRecording().header.title, "a short session");
    }

    TEST_F(ReplayControlTest, RecordStartRefusesWhatItCannotDo) {
        EXPECT_EQ(Call("record_start").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("record_start", R"({"path":""})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("record_start", R"({"path":5})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("record_start", PathParams("5")).ErrorCode(), "invalid_params");
        const String missing = String((_path / "no" / "such" / "folder.jsonl").View());
        EXPECT_EQ(Call("record_start", PathParamsOf(missing)).ErrorCode(), "invalid_params");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());

        ASSERT_TRUE(Call("record_start", PathParams()).IsOk());
        EXPECT_EQ(Call("record_start", PathParams()).ErrorCode(), "invalid_params");
        EXPECT_TRUE(SessionRecorder::GetInstance().IsRecording());
    }

    TEST_F(ReplayControlTest, AGameThatIsNotReplayableAnswersUnsupportedAndOneThatIsNotControllableNot) {
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakeControllable"_intern);
        EXPECT_EQ(Call("record_start", PathParams()).ErrorCode(), "unsupported");
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakePlain"_intern);
        EXPECT_EQ(Call("record_start", PathParams()).ErrorCode(), "not_controllable");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(PathExists(_path));
    }

    TEST_F(ReplayControlTest, RecordStopWithoutARecordingIsInvalidParams) {
        EXPECT_EQ(Call("record_stop").ErrorCode(), "invalid_params");
    }

    TEST_F(ReplayControlTest, TheMethodsAreDescribed) {
        bool start = false;
        bool stop = false;
        for (const auto& listing : CoreManager::GetControlService().GetMethods()) {
            start = start || listing.name == "record_start";
            stop = stop || listing.name == "record_stop";
        }
        EXPECT_TRUE(start);
        EXPECT_TRUE(stop);
    }

    TEST_F(ReplayControlTest, TheRecordFlagStartsTheRecordingOnTheActiveGame) {
        SetCommandLine({"--record", "--other"});
        StartRecordingFromCommandLine();  // a flag without a file
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        SetCommandLine({"--record="});
        StartRecordingFromCommandLine();
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        SetCommandLine({"--control"});
        StartRecordingFromCommandLine();
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());

        const String flag = Format("--record={}", PathText());
        CoreManager::GetCommandLine().Set(Vector<String>{flag});
        StartRecordingFromCommandLine();
        ASSERT_TRUE(SessionRecorder::GetInstance().IsRecording());
        ASSERT_TRUE(Call("reset", R"({"seed":2})").IsOk());
        EXPECT_EQ(SessionRecorder::GetInstance().Stop(), 1u);

        EXPECT_EQ(ParseRecording().header.game, "ReplayTest");
    }

    TEST_F(ReplayControlTest, TheRecordFlagOnAGameThatCannotRecordLeavesNoRecording) {
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        const String flag = Format("--record={}", PathText());
        CoreManager::GetCommandLine().Set(Vector<String>{flag});

        StartRecordingFromCommandLine();

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(PathExists(_path));
    }

    // The default game of the test binary is the first registered one, which this test does not choose: it holds the flag to what that game can do
    TEST_F(ReplayControlTest, InitializeReadsTheRecordFlagForTheDefaultGame) {
        GameManager::GetInstance().Shutdown();
        const String flag = Format("--record={}", PathText());
        CoreManager::GetCommandLine().Set(Vector<String>{flag});

        GameManager::GetInstance().Initialize();

        ASSERT_NE(GameManager::GetInstance().GetActive(), nullptr);
        EXPECT_EQ(SessionRecorder::GetInstance().IsRecording(), GameManager::GetInstance().GetActive()->GetName() == "ReplayTest"_intern);
    }

    TEST_F(ReplayControlTest, ShutdownClosesARecordingThatRuns) {
        ASSERT_TRUE(Call("record_start", PathParams()).IsOk());
        ASSERT_TRUE(Call("reset", R"({"seed":2})").IsOk());

        GameManager::GetInstance().Shutdown();

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_EQ(ParseRecording().entries.size(), 1u);
    }

    namespace {

        /// Counts the QuitEvents that the "quit" method emits
        class QuitCounter : public SubscriptionHolder {
        public:
            using SubscriptionHolder::Subscribe;

            int count = 0;

            void OnQuit(const ApplicationEvents::QuitEvent&) {
                ++count;
            }
        };

        constexpr double kFrameSeconds = 0.25;  // exact in binary
        constexpr int kLockFrames = 2;          // kFrameSeconds * kLockFrames is the lock of `arm`

        /// The `replay` method on a service of its own that runs the manual clock, with the frames played by hand like an application loop would
        class ReplayPlayControlTest : public ReplayControlTest {
        protected:
            void SetUp() override {
                ReplayControlTest::SetUp();
                RegisterControlBuiltins(_service);
                RegisterGameControl(_service);
                _service.SetPause(&_pause);
                _service.SetManualClock(true);
                _pause.SetPaused(true);
            }

            void TearDown() override {
                _service.Shutdown();
                ReplayControlTest::TearDown();
            }

            static ReplayTestGame& Game() {
                return *GameManager::GetInstance().GetActive()->Cast<ReplayTestGame>();
            }

            /// One frame of game time on the game, the way an application loop passes it
            static void Frame() {
                CoreManager::GetTime().Advance(kFrameSeconds);
                Game().Update(kFrameSeconds);
            }

            /// reset, roll, arm, a wait of the lock, roll, set_state, roll: the arm / timer gap is in the file (6 entries, 3 frames of gaps)
            String RecordSession() {
                EXPECT_TRUE(SessionRecorder::GetInstance().Start(PathText(), *GameManager::GetInstance().GetActive(), "session").has_value());
                EXPECT_TRUE(ControlledReset(Game(), JsonDoc(R"({"seed":21})")).has_value());
                Frame();
                EXPECT_TRUE(ControlledAction(Game(), JsonDoc(R"({"type":"roll"})")).has_value());
                EXPECT_TRUE(ControlledAction(Game(), JsonDoc(R"({"type":"arm"})")).has_value());
                for (int i = 0; i < kLockFrames; ++i) {
                    Frame();
                }
                EXPECT_TRUE(ControlledAction(Game(), JsonDoc(R"({"type":"roll"})")).has_value());
                EXPECT_TRUE(ControlledSetState(Game(), JsonDoc(R"({"rolls":[6,6]})")).has_value());
                EXPECT_TRUE(ControlledAction(Game(), JsonDoc(R"({"type":"roll"})")).has_value());
                SessionRecorder::GetInstance().Stop();
                return ReadRecording();
            }

            void WriteFile(StringView text) const {
                Tests::WriteFile(_path, text);
            }

            void Collect(const String& text) {
                _responses.push_back(ControlReply(StringView(text.c_str(), text.size())));
            }

            void PostReplay(int64_t id, StringView paramsText) {
                _service.Post(MakeRequestText(id, "replay", paramsText), [this](const String& text) { Collect(text); });
            }

            /// The application loop of the manual clock until @p answers answers are in: a frame runs while a step is due, the requests run in it
            void RunFrames(size_t answers, int guard = 1000) {
                for (int frames = 0; _responses.size() < answers && frames < guard; ++frames) {
                    if (!_pause.IsStepDue()) {
                        _service.Drain();  // the idle cycle
                        if (!_pause.IsStepDue()) {
                            return;
                        }
                    }
                    const FrameDecision frame = _pause.BeginFrame();
                    CoreManager::GetTime().Advance(frame.seconds);
                    Game().Update(frame.seconds);
                    _service.Drain();
                    _service.EndFrame();
                }
            }

            /// Posts a `replay` of the file and plays frames until it is answered
            const ControlReply& Replay(int guard = 1000) {
                const size_t answered = _responses.size();
                PostReplay(static_cast<int64_t>(answered) + 1, PathParams());
                RunFrames(answered + 1, guard);
                return _responses.back();
            }

            ControlService _service;
            GamePause _pause;
            Vector<ControlReply> _responses;
        };

    }  // namespace

    TEST_F(ReplayPlayControlTest, ARecordedSessionReplaysToTheSameState) {
        const String text = RecordSession();
        const String recordedState = *StateText(Game());
        ASSERT_TRUE(ResetText(Game(), R"({"seed":99})").has_value());
        ASSERT_NE(*StateText(Game()), recordedState);

        const ControlReply& response = Replay();

        ASSERT_TRUE(response.IsOk()) << String(response.Text()).c_str();
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "ok").value_or(false));
        EXPECT_EQ(IntOf(result, "entries").value_or(-1), 6);
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 1 + kLockFrames);
        EXPECT_EQ(KeysOf(result), (Vector<String>{"deltaTime", "entries", "fixedDeltaTime", "frame", "frames", "ok", "totalTime"}));
        EXPECT_EQ(*StateText(Game()), recordedState);
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(_service.IsStepDue());
    }

    TEST_F(ReplayPlayControlTest, AGapThatWasCutShortIsTheMismatchAtItsEntry) {
        String text = RecordSession();
        const String lock = "\"gap\":{\"frames\":2,\"seconds\":0.5}";
        const size_t at = text.find(lock);
        ASSERT_NE(at, String::npos) << text.c_str();
        text.replace(at, lock.size(), "\"gap\":{\"frames\":1,\"seconds\":0.25}");
        WriteFile(text);

        const ControlReply& response = Replay();

        ASSERT_TRUE(response.IsOk()) << String(response.Text()).c_str();  // a mismatch is a result, not a protocol error
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());
        EXPECT_FALSE(BoolOf(result, "ok").value_or(true));
        EXPECT_EQ(IntOf(result, "entries").value_or(-1), 3);
        const JsonView mismatch = MemberOf(result, "mismatch");
        EXPECT_EQ(IntOf(mismatch, "index").value_or(-1), 3);
        EXPECT_EQ(IntOf(mismatch, "line").value_or(-1), 5);
        EXPECT_EQ(StringOf(mismatch, "kind"), "refused");
        const JsonView error = MemberOf(mismatch, "error");
        ASSERT_TRUE(error.IsObject());
        EXPECT_EQ(StringOf(error, "code"), "illegal_action");
    }

    TEST_F(ReplayPlayControlTest, AChangedOutcomeIsTheMismatchAtItsEntry) {
        String text = RecordSession();
        const size_t at = text.find("\"outcome\"");
        ASSERT_NE(at, String::npos);
        // The first roll of the file answers a number of the dice: another number is another outcome
        const String key = "\"value\":";
        const size_t number = text.find(key, at);
        ASSERT_NE(number, String::npos) << text.c_str();
        const size_t digit = number + key.size();
        text[digit] = text[digit] == '1' ? '2' : '1';
        WriteFile(text);

        const ControlReply& response = Replay();

        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject()) << String(response.Text()).c_str();
        EXPECT_FALSE(BoolOf(result, "ok").value_or(true));
        EXPECT_EQ(StringOf(MemberOf(result, "mismatch"), "kind"), "outcome_differs");
    }

    TEST_F(ReplayPlayControlTest, AGapLongerThanOneStepIsSteppedInChunks) {
        constexpr int kGapFrames = static_cast<int>(kMaxStepFrames) + 4;
        WriteFile(Format("{{\"replay\":1,\"game\":\"ReplayTest\",\"engine\":\"x\"}}\n"
                         "{{\"op\":\"reset\",\"params\":{{\"seed\":1}},\"gap\":{{\"frames\":0,\"seconds\":0}}}}\n"
                         "{{\"op\":\"action\",\"params\":{{\"type\":\"roll\"}},\"gap\":{{\"frames\":{},\"seconds\":0}}}}\n",
                         kGapFrames));

        const ControlReply& response = Replay(kGapFrames + 10);

        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject()) << String(response.Text()).c_str();
        EXPECT_TRUE(BoolOf(result, "ok").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), kGapFrames);
    }

    TEST_F(ReplayPlayControlTest, WithoutTheManualClockTheMethodFails) {
        RecordSession();
        _service.SetManualClock(false);

        PostReplay(1, PathParams());
        _service.Drain();

        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(_responses[0].ErrorCode(), "replay_needs_manual_clock");
    }

    TEST_F(ReplayPlayControlTest, AFileThatCannotBeUsedIsInvalidParamsWithItsLine) {
        PostReplay(1, R"({})");
        PostReplay(2, R"({"path":""})");
        PostReplay(3, PathParamsOf(String((_path / "no" / "such.jsonl").View())));
        WriteFile("{\"replay\":1,\"game\":\"ReplayTest\",\"engine\":\"x\"}\n{\"op\":\"jump\",\"params\":{},\"gap\":{\"frames\":0,\"seconds\":0}}\n");
        PostReplay(4, PathParams());
        _service.Drain();
        WriteFile("{\"replay\":1,\"game\":\"Other\",\"engine\":\"x\"}\n");
        PostReplay(5, PathParams());
        _service.Drain();

        ASSERT_EQ(_responses.size(), 5u);
        for (const ControlReply& response : _responses) {
            EXPECT_EQ(response.ErrorCode(), "invalid_params") << String(response.Text()).c_str();
        }
        EXPECT_NE(_responses[3].ErrorMessage().find("line 2"), String::npos);
    }

    TEST_F(ReplayPlayControlTest, AFileOverTheLimitIsRefusedByItsSizeBeforeItIsRead) {
        constexpr uintmax_t kOverTheLimit = uintmax_t{64} * 1024 * 1024 + 1;
        WriteFile("");
        ResizeFile(_path, kOverTheLimit);  // sparse: nothing of it is written
        PostReplay(1, PathParams());
        _service.Drain();

        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(_responses[0].ErrorCode(), "invalid_params");
        EXPECT_NE(_responses[0].ErrorMessage().find("is larger than"), String::npos);
    }

    TEST_F(ReplayPlayControlTest, AReplayIsRefusedWhileARecordingRunsAndForAGameThatIsNotReplayable) {
        RecordSession();
        ASSERT_TRUE(SessionRecorder::GetInstance().Start((_path.Parent() / "replay_other.jsonl").View(), *GameManager::GetInstance().GetActive()).has_value());
        PostReplay(1, PathParams());
        _service.Drain();
        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(_responses[0].ErrorCode(), "invalid_params");
        SessionRecorder::GetInstance().Stop();
        RemoveFileIfPresent(_path.Parent() / "replay_other.jsonl");

        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        PostReplay(2, PathParams());
        _service.Drain();
        ASSERT_EQ(_responses.size(), 2u);
        EXPECT_EQ(_responses[1].ErrorCode(), "unsupported");
    }

    TEST_F(ReplayPlayControlTest, AQuitPostedBehindTheReplayStillRuns) {
        RecordSession();
        QuitCounter counter;
        counter.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&counter);
        PostReplay(1, PathParams());
        _service.Post(R"({"id":2,"method":"quit"})", [this](const String& text) { Collect(text); });
        _service.Drain();

        EXPECT_EQ(counter.count, 1);
        ASSERT_EQ(_responses.size(), 1u);  // the replay still waits for its frames
        EXPECT_EQ(_responses[0].IdText(), "2");
        EXPECT_TRUE(_service.IsStepDue());
    }

    TEST_F(ReplayPlayControlTest, ShutdownInTheMiddleAnswersShuttingDownAndLeavesNothingBehind) {
        RecordSession();
        PostReplay(1, PathParams());
        _service.Drain();
        ASSERT_TRUE(_responses.empty());
        ASSERT_TRUE(_service.IsStepDue());

        _service.Shutdown();

        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(_responses[0].ErrorCode(), "shutting_down");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_EQ(_service.GetPublishObserverCount(), 0u);
    }

    TEST_F(ReplayPlayControlTest, ReplayIsDescribed) {
        bool found = false;
        for (const auto& listing : _service.GetMethods()) {
            found = found || listing.name == "replay";
        }
        EXPECT_TRUE(found);
    }

}  // namespace BECore::Tests
