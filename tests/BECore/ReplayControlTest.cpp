#include <BECore/Application/CommandLine.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/ReplayTestGame.h>
#include <BECore/Time/GamePause.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ApplicationEvents.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
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
                std::error_code ignored;
                std::filesystem::remove(_path, ignored);
            }

            static Json Params(StringView text) {
                auto parsed = ParseJson(text);
                return parsed ? std::move(*parsed) : Json();
            }

            [[nodiscard]] String PathText() const {
                return FromStdString(_path.string());
            }

            [[nodiscard]] Json PathParams() const {
                Json params = Json::object();
                params["path"] = MakeJsonString(PathText());
                return params;
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
            static Json Call(StringView method, const Json& params) {
                Json request = Json::object();
                request["id"] = 1;
                request["method"] = ToStdString(method);
                request["params"] = params;
                Json response;
                CoreManager::GetControlService().Post(DumpJson(request), [&response](String text) {
                    auto parsed = ParseJson(text);
                    if (parsed) {
                        response = std::move(*parsed);
                    }
                });
                CoreManager::GetControlService().Drain();
                return response;
            }

            static void SetCommandLine(std::initializer_list<const char*> arguments) {
                Vector<String> list;
                for (const char* argument : arguments) {
                    list.push_back(String(argument));
                }
                CoreManager::GetCommandLine().Set(list);
            }

            std::filesystem::path _path;
            Vector<String> _arguments;
        };

        const Json* FindResult(const Json& response) {
            return FindJsonMember(response, "result");
        }

        /// The member names of an object, sorted: the DOM sorts them, and the generated writer does the same by design
        Vector<String> KeysOf(const Json& object) {
            Vector<String> keys;
            for (const auto& member : object.items()) {
                keys.push_back(FromStdString(member.key()));
            }
            return keys;
        }

    }  // namespace

    TEST_F(ReplayControlTest, TheAcceptedCallsAreInTheFileAndTheRefusedOnesAreNot) {
        const Json started = Call("record_start", PathParams());
        ASSERT_TRUE(IsOk(started));
        const Json* startedResult = FindResult(started);
        ASSERT_NE(startedResult, nullptr);
        EXPECT_EQ(GetJsonString(*startedResult, "game").value_or(String()), "ReplayTest");
        EXPECT_EQ(GetJsonString(*startedResult, "path").value_or(String()), PathText());

        ASSERT_TRUE(IsOk(Call("reset", Params(R"({"seed":9})"))));
        ASSERT_TRUE(IsOk(Call("perform_action", Params(R"({"type":"roll"})"))));
        ASSERT_TRUE(IsOk(Call("perform_action", Params(R"({"type":"arm"})"))));
        EXPECT_EQ(ErrorCode(Call("perform_action", Params(R"({"type":"roll"})"))), "illegal_action");
        ASSERT_TRUE(IsOk(Call("set_state", Params(R"({"rolls":[1,2]})"))));
        EXPECT_FALSE(IsOk(Call("reset", Params(R"({"seed":"x"})"))));

        const Json stopped = Call("record_stop", Json::object());
        ASSERT_TRUE(IsOk(stopped));
        ASSERT_NE(FindResult(stopped), nullptr);
        EXPECT_EQ(GetJsonInt(Require(FindResult(stopped)), "entries").value_or(-1), 4);

        const ParsedReplay replay = ParseRecording();
        ASSERT_EQ(replay.entries.size(), 4u);
        EXPECT_EQ(replay.entries[0].op, ReplayOp::Reset);
        EXPECT_EQ(replay.entries[0].params, R"({"seed":9})");
        EXPECT_EQ(replay.entries[1].op, ReplayOp::Action);
        EXPECT_EQ(replay.entries[2].params, R"({"type":"arm"})");
        EXPECT_EQ(replay.entries[3].op, ReplayOp::State);
    }

    TEST_F(ReplayControlTest, TheRecordingAnswersHaveExactlyTheirKeys) {
        const Json started = Call("record_start", PathParams());
        ASSERT_TRUE(IsOk(started));
        EXPECT_EQ(KeysOf(Require(FindResult(started))), (Vector<String>{"game", "path"}));

        ASSERT_TRUE(IsOk(Call("reset", Params(R"({"seed":9})"))));
        const Json stopped = Call("record_stop", Json::object());
        ASSERT_TRUE(IsOk(stopped));
        EXPECT_EQ(DumpJson(Require(FindResult(stopped))), "{\"entries\":1}");
    }

    TEST_F(ReplayControlTest, ATitleLandsInTheHeader) {
        Json params = PathParams();
        params["title"] = "a short session";

        ASSERT_TRUE(IsOk(Call("record_start", params)));
        ASSERT_TRUE(IsOk(Call("record_stop", Json::object())));

        EXPECT_EQ(ParseRecording().header.title, "a short session");
    }

    TEST_F(ReplayControlTest, RecordStartRefusesWhatItCannotDo) {
        EXPECT_EQ(ErrorCode(Call("record_start", Json::object())), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("record_start", Params(R"({"path":""})"))), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("record_start", Params(R"({"path":5})"))), "invalid_params");
        Json badTitle = PathParams();
        badTitle["title"] = 5;
        EXPECT_EQ(ErrorCode(Call("record_start", badTitle)), "invalid_params");
        const String missing = FromStdString((_path / "no" / "such" / "folder.jsonl").string());
        Json unwritable = Json::object();
        unwritable["path"] = MakeJsonString(missing);
        EXPECT_EQ(ErrorCode(Call("record_start", unwritable)), "invalid_params");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());

        ASSERT_TRUE(IsOk(Call("record_start", PathParams())));
        EXPECT_EQ(ErrorCode(Call("record_start", PathParams())), "invalid_params");
        EXPECT_TRUE(SessionRecorder::GetInstance().IsRecording());
    }

    TEST_F(ReplayControlTest, AGameThatIsNotReplayableAnswersUnsupportedAndOneThatIsNotControllableNot) {
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakeControllable"_intern);
        EXPECT_EQ(ErrorCode(Call("record_start", PathParams())), "unsupported");
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakePlain"_intern);
        EXPECT_EQ(ErrorCode(Call("record_start", PathParams())), "not_controllable");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(std::filesystem::exists(_path));
    }

    TEST_F(ReplayControlTest, RecordStopWithoutARecordingIsInvalidParams) {
        EXPECT_EQ(ErrorCode(Call("record_stop", Json::object())), "invalid_params");
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
        ASSERT_TRUE(IsOk(Call("reset", Params(R"({"seed":2})"))));
        EXPECT_EQ(SessionRecorder::GetInstance().Stop(), 1u);

        EXPECT_EQ(ParseRecording().header.game, "ReplayTest");
    }

    TEST_F(ReplayControlTest, TheRecordFlagOnAGameThatCannotRecordLeavesNoRecording) {
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        const String flag = Format("--record={}", PathText());
        CoreManager::GetCommandLine().Set(Vector<String>{flag});

        StartRecordingFromCommandLine();

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(std::filesystem::exists(_path));
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
        ASSERT_TRUE(IsOk(Call("record_start", PathParams())));
        ASSERT_TRUE(IsOk(Call("reset", Params(R"({"seed":2})"))));

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

            void PostReplay(int64_t id, const Json& params) {
                Json request = Json::object();
                request["id"] = id;
                request["method"] = "replay";
                request["params"] = params;
                _service.Post(DumpJson(request), [this](String text) {
                    auto parsed = ParseJson(text);
                    _responses.push_back(parsed ? std::move(*parsed) : Json());
                });
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
            const Json& Replay(int guard = 1000) {
                const size_t answered = _responses.size();
                PostReplay(static_cast<int64_t>(answered) + 1, PathParams());
                RunFrames(answered + 1, guard);
                return _responses.back();
            }

            ControlService _service;
            GamePause _pause;
            Vector<Json> _responses;
        };

        const Json& MismatchOf(const Json& result) {
            static const Json kNone;
            const Json* mismatch = FindJsonMember(result, "mismatch");
            return mismatch != nullptr ? *mismatch : kNone;
        }

    }  // namespace

    TEST_F(ReplayPlayControlTest, ARecordedSessionReplaysToTheSameState) {
        const String text = RecordSession();
        const Json recordedState = *Game().GetState();
        ASSERT_TRUE(Game().Reset(Params(R"({"seed":99})")).has_value());
        ASSERT_NE(*Game().GetState(), recordedState);

        const Json& response = Replay();

        ASSERT_TRUE(IsOk(response)) << DumpJson(response).c_str();
        const Json* result = FindResult(response);
        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "ok").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "entries").value_or(-1), 6);
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 1 + kLockFrames);
        EXPECT_EQ(KeysOf(*result), (Vector<String>{"deltaTime", "entries", "fixedDeltaTime", "frame", "frames", "ok", "totalTime"}));
        EXPECT_EQ(*Game().GetState(), recordedState);
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

        const Json& response = Replay();

        ASSERT_TRUE(IsOk(response)) << DumpJson(response).c_str();  // a mismatch is a result, not a protocol error
        const Json* result = FindResult(response);
        ASSERT_NE(result, nullptr);
        EXPECT_FALSE(GetJsonBool(*result, "ok").value_or(true));
        EXPECT_EQ(GetJsonInt(*result, "entries").value_or(-1), 3);
        const Json& mismatch = MismatchOf(*result);
        EXPECT_EQ(GetJsonInt(mismatch, "index").value_or(-1), 3);
        EXPECT_EQ(GetJsonInt(mismatch, "line").value_or(-1), 5);
        EXPECT_EQ(GetJsonString(mismatch, "kind").value_or(String()), "refused");
        const Json* error = FindJsonMember(mismatch, "error");
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(GetJsonString(*error, "code").value_or(String()), "illegal_action");
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

        const Json& response = Replay();

        const Json* result = FindResult(response);
        ASSERT_NE(result, nullptr) << DumpJson(response).c_str();
        EXPECT_FALSE(GetJsonBool(*result, "ok").value_or(true));
        EXPECT_EQ(GetJsonString(MismatchOf(*result), "kind").value_or(String()), "outcome_differs");
    }

    TEST_F(ReplayPlayControlTest, AGapLongerThanOneStepIsSteppedInChunks) {
        constexpr int kGapFrames = static_cast<int>(kMaxStepFrames) + 4;
        WriteFile(Format("{{\"replay\":1,\"game\":\"ReplayTest\",\"engine\":\"x\"}}\n"
                         "{{\"op\":\"reset\",\"params\":{{\"seed\":1}},\"gap\":{{\"frames\":0,\"seconds\":0}}}}\n"
                         "{{\"op\":\"action\",\"params\":{{\"type\":\"roll\"}},\"gap\":{{\"frames\":{},\"seconds\":0}}}}\n",
                         kGapFrames));

        const Json& response = Replay(kGapFrames + 10);

        const Json* result = FindResult(response);
        ASSERT_NE(result, nullptr) << DumpJson(response).c_str();
        EXPECT_TRUE(GetJsonBool(*result, "ok").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), kGapFrames);
    }

    TEST_F(ReplayPlayControlTest, WithoutTheManualClockTheMethodFails) {
        RecordSession();
        _service.SetManualClock(false);

        PostReplay(1, PathParams());
        _service.Drain();

        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(ErrorCode(_responses[0]), "replay_needs_manual_clock");
    }

    TEST_F(ReplayPlayControlTest, AFileThatCannotBeUsedIsInvalidParamsWithItsLine) {
        PostReplay(1, Params(R"({})"));
        PostReplay(2, Params(R"({"path":""})"));
        Json missing = Json::object();
        missing["path"] = MakeJsonString(FromStdString((_path / "no" / "such.jsonl").string()));
        PostReplay(3, missing);
        WriteFile("{\"replay\":1,\"game\":\"ReplayTest\",\"engine\":\"x\"}\n{\"op\":\"jump\",\"params\":{},\"gap\":{\"frames\":0,\"seconds\":0}}\n");
        PostReplay(4, PathParams());
        _service.Drain();
        WriteFile("{\"replay\":1,\"game\":\"Other\",\"engine\":\"x\"}\n");
        PostReplay(5, PathParams());
        _service.Drain();

        ASSERT_EQ(_responses.size(), 5u);
        for (const Json& response : _responses) {
            EXPECT_EQ(ErrorCode(response), "invalid_params") << DumpJson(response).c_str();
        }
        const Json* error = FindJsonMember(_responses[3], "error");
        ASSERT_NE(error, nullptr);
        EXPECT_NE(GetJsonString(*error, "message").value_or(String()).find("line 2"), String::npos);
    }

    TEST_F(ReplayPlayControlTest, AFileOverTheLimitIsRefusedByItsSizeBeforeItIsRead) {
        constexpr uintmax_t kOverTheLimit = uintmax_t{64} * 1024 * 1024 + 1;
        WriteFile("");
        std::error_code error;
        std::filesystem::resize_file(_path, kOverTheLimit, error);  // sparse: nothing of it is written
        ASSERT_FALSE(error) << error.message();
        PostReplay(1, PathParams());
        _service.Drain();

        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(ErrorCode(_responses[0]), "invalid_params");
        const Json* errorObject = FindJsonMember(_responses[0], "error");
        ASSERT_NE(errorObject, nullptr);
        EXPECT_NE(GetJsonString(*errorObject, "message").value_or(String()).find("is larger than"), String::npos);
    }

    TEST_F(ReplayPlayControlTest, AReplayIsRefusedWhileARecordingRunsAndForAGameThatIsNotReplayable) {
        RecordSession();
        ASSERT_TRUE(SessionRecorder::GetInstance().Start(FromStdString((_path.parent_path() / "replay_other.jsonl").string()), *GameManager::GetInstance().GetActive()).has_value());
        PostReplay(1, PathParams());
        _service.Drain();
        ASSERT_EQ(_responses.size(), 1u);
        EXPECT_EQ(ErrorCode(_responses[0]), "invalid_params");
        SessionRecorder::GetInstance().Stop();
        std::error_code ignored;
        std::filesystem::remove(_path.parent_path() / "replay_other.jsonl", ignored);

        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        PostReplay(2, PathParams());
        _service.Drain();
        ASSERT_EQ(_responses.size(), 2u);
        EXPECT_EQ(ErrorCode(_responses[1]), "unsupported");
    }

    TEST_F(ReplayPlayControlTest, AQuitPostedBehindTheReplayStillRuns) {
        RecordSession();
        QuitCounter counter;
        counter.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&counter);
        PostReplay(1, PathParams());
        _service.Post(R"({"id":2,"method":"quit"})", [this](String text) {
            auto parsed = ParseJson(text);
            _responses.push_back(parsed ? std::move(*parsed) : Json());
        });
        _service.Drain();

        EXPECT_EQ(counter.count, 1);
        ASSERT_EQ(_responses.size(), 1u);  // the replay still waits for its frames
        EXPECT_EQ(GetJsonInt(_responses[0], "id").value_or(-1), 2);
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
        EXPECT_EQ(ErrorCode(_responses[0]), "shutting_down");
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
