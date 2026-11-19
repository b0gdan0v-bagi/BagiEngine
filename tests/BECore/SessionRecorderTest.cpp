#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/ReplayTestGame.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/ReplayFile.h>
#include <Games/SessionRecorder.h>
#include <filesystem>
#include <fstream>  //-NoStd: the test reads the file the recorder wrote
#include <gtest/gtest.h>
#include <sstream>  //-NoStd: the test reads the file the recorder wrote

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 0.25;  // exact in binary, so gaps compare with EXPECT_EQ

        class SessionRecorderTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _path = TestTempPath(Format("session_recorder_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()));
                CoreManager::GetTime().Reset();
                ReplayTestGame::RestartSeedCounter();
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("ReplayTest"_intern);
            }

            void TearDown() override {
                SessionRecorder::DestroyInstance();
                GameManager::GetInstance().Shutdown();
                CoreManager::GetTime().Reset();
                std::error_code ignored;
                std::filesystem::remove(_path, ignored);
            }

            static ReplayTestGame& Game() {
                return *GameManager::GetInstance().GetActive()->Cast<ReplayTestGame>();
            }

            static Json Params(StringView text) {
                auto parsed = ParseJson(text);
                return parsed ? std::move(*parsed) : Json();
            }

            [[nodiscard]] String PathText() const {
                return FromStdString(_path.string());
            }

            std::expected<void, ControlError> StartRecording(StringView title = {}) {
                return SessionRecorder::GetInstance().Start(PathText(), *GameManager::GetInstance().GetActive(), title);
            }

            [[nodiscard]] String ReadRecording() const {
                std::ifstream file(_path, std::ios::binary);  //-NoStd: see the include
                std::ostringstream text;                      //-NoStd: see the include
                text << file.rdbuf();
                return FromStdString(text.str());
            }

            [[nodiscard]] ParsedReplay ParseRecording() const {
                const String text = ReadRecording();
                auto parsed = ParseReplay(text);
                EXPECT_TRUE(parsed.has_value()) << (parsed ? "" : DescribeReplayError(parsed.error()).c_str());
                return parsed ? std::move(*parsed) : ParsedReplay{};
            }

            static void AdvanceFrames(int frames) {
                for (int i = 0; i < frames; ++i) {
                    CoreManager::GetTime().Advance(kFrameSeconds);
                }
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

            std::filesystem::path _path;
        };

    }  // namespace

    TEST_F(SessionRecorderTest, AResetAnActionAndASetStateAreWritten) {
        ASSERT_TRUE(StartRecording("three calls").has_value());
        IControllableGame& game = Game();

        ASSERT_TRUE(ControlledReset(game, Params(R"({})")).has_value());
        const auto rolled = ControlledAction(game, Params(R"({"type":"roll"})"));
        ASSERT_TRUE(rolled.has_value());
        ASSERT_TRUE(ControlledSetState(game, Params(R"({"rolls":[2,3]})")).has_value());
        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 3u);
        SessionRecorder::GetInstance().Stop();

        const ParsedReplay replay = ParseRecording();
        EXPECT_EQ(replay.header.game, "ReplayTest");
        EXPECT_EQ(replay.header.title, "three calls");
        ASSERT_EQ(replay.entries.size(), 3u);
        EXPECT_EQ(replay.entries[0].op, ReplayOp::Reset);
        EXPECT_EQ(replay.entries[0].params, Params(R"({"seed":1000})"));  // what the game drew, not what the caller sent
        EXPECT_EQ(replay.entries[1].op, ReplayOp::Action);
        EXPECT_EQ(replay.entries[1].params, Params(R"({"type":"roll"})"));
        ASSERT_TRUE(replay.entries[1].outcome.has_value());
        EXPECT_EQ(*replay.entries[1].outcome, *rolled);
        EXPECT_EQ(replay.entries[2].op, ReplayOp::State);
        EXPECT_EQ(replay.entries[2].params, Params(R"({"rolls":[2,3]})"));
    }

    TEST_F(SessionRecorderTest, TheSeedOfTheCallerIsWhatTheFileHolds) {
        ASSERT_TRUE(StartRecording().has_value());

        ASSERT_TRUE(ControlledReset(Game(), Params(R"({"seed":77})")).has_value());
        SessionRecorder::GetInstance().Stop();

        EXPECT_EQ(ParseRecording().entries.at(0).params, Params(R"({"seed":77})"));
    }

    TEST_F(SessionRecorderTest, ARefusedCallIsNotWritten) {
        ASSERT_TRUE(StartRecording().has_value());
        IControllableGame& game = Game();
        ASSERT_TRUE(ControlledReset(game, Params(R"({"seed":1})")).has_value());
        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"arm"})")).has_value());

        EXPECT_FALSE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());  // locked
        EXPECT_FALSE(ControlledAction(game, Params(R"({"type":5})")).has_value());       // malformed
        EXPECT_FALSE(ControlledSetState(game, Params(R"({"rolls":"x"})")).has_value());
        EXPECT_FALSE(ControlledReset(game, Params(R"({"seed":"x"})")).has_value());

        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 2u);
    }

    TEST_F(SessionRecorderTest, ACallBeforeTheFirstResetIsDropped) {
        ASSERT_TRUE(StartRecording().has_value());
        IControllableGame& game = Game();

        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());
        ASSERT_TRUE(ControlledSetState(game, Params(R"({"rolls":[1]})")).has_value());
        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 0u);

        ASSERT_TRUE(ControlledReset(game, Params(R"({"seed":3})")).has_value());
        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());
        SessionRecorder::GetInstance().Stop();

        const ParsedReplay replay = ParseRecording();
        ASSERT_EQ(replay.entries.size(), 2u);
        EXPECT_EQ(replay.entries[0].op, ReplayOp::Reset);
    }

    TEST_F(SessionRecorderTest, TheGapIsTheGameTimeSinceTheEntryBefore) {
        IControllableGame& game = Game();
        AdvanceFrames(3);  // before the recording: not part of any gap
        ASSERT_TRUE(StartRecording().has_value());
        ASSERT_TRUE(ControlledReset(game, Params(R"({"seed":1})")).has_value());
        AdvanceFrames(2);
        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());
        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());
        AdvanceFrames(1);
        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());
        SessionRecorder::GetInstance().Stop();

        const ParsedReplay replay = ParseRecording();
        ASSERT_EQ(replay.entries.size(), 4u);
        EXPECT_EQ(replay.entries[0].gap, (ReplayGap{0, 0.0}));
        EXPECT_EQ(replay.entries[1].gap, (ReplayGap{2, 2 * kFrameSeconds}));
        EXPECT_EQ(replay.entries[2].gap, (ReplayGap{0, 0.0}));
        EXPECT_EQ(replay.entries[3].gap, (ReplayGap{1, kFrameSeconds}));
    }

    TEST_F(SessionRecorderTest, AClockThatWentBackIsAGapOfNothing) {
        IControllableGame& game = Game();
        ASSERT_TRUE(StartRecording().has_value());
        AdvanceFrames(4);
        ASSERT_TRUE(ControlledReset(game, Params(R"({"seed":1})")).has_value());
        CoreManager::GetTime().Reset();
        ASSERT_TRUE(ControlledAction(game, Params(R"({"type":"roll"})")).has_value());
        SessionRecorder::GetInstance().Stop();

        EXPECT_EQ(ParseRecording().entries.at(1).gap, (ReplayGap{0, 0.0}));
    }

    TEST_F(SessionRecorderTest, StopLeavesAFileThatReadsBackAndEndsTheRecording) {
        ASSERT_TRUE(StartRecording().has_value());
        EXPECT_TRUE(SessionRecorder::GetInstance().IsRecording());
        ASSERT_TRUE(ControlledReset(Game(), Params(R"({"seed":1})")).has_value());

        EXPECT_EQ(SessionRecorder::GetInstance().Stop(), 1u);

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 1u);
        EXPECT_EQ(ParseRecording().entries.size(), 1u);
        ASSERT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());  // after the end: not written
        EXPECT_EQ(ParseRecording().entries.size(), 1u);
        EXPECT_EQ(SessionRecorder::GetInstance().Stop(), 1u);
    }

    TEST_F(SessionRecorderTest, TheFileIsOnDiskAfterEveryEntry) {
        ASSERT_TRUE(StartRecording().has_value());

        ASSERT_TRUE(ControlledReset(Game(), Params(R"({"seed":1})")).has_value());

        EXPECT_EQ(ParseRecording().entries.size(), 1u);  // read while the recording still runs
    }

    TEST_F(SessionRecorderTest, AGameThatIsNotReplayableOrNotControllableIsRefused) {
        for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
            if (game->GetName() == "ReplayTest"_intern) {
                continue;
            }
            const auto started = SessionRecorder::GetInstance().Start(PathText(), *game);
            ASSERT_FALSE(started.has_value()) << String(game->GetName().ToStringView()).c_str();
            EXPECT_EQ(started.error().code, game->AsControllable() != nullptr ? String(GameControlErrors::kUnsupported) : String(GameControlErrors::kNotControllable));
            EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        }
        EXPECT_FALSE(std::filesystem::exists(_path));
    }

    TEST_F(SessionRecorderTest, APathThatCannotBeWrittenStopsTheRecordingAndNotTheGame) {
        const String missing = FromStdString((_path / "no" / "such" / "folder.jsonl").string());

        const auto started = SessionRecorder::GetInstance().Start(missing, *GameManager::GetInstance().GetActive());

        ASSERT_FALSE(started.has_value());
        EXPECT_EQ(started.error().code, String(ControlErrors::kInvalidParams));
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_TRUE(ControlledReset(Game(), Params(R"({"seed":1})")).has_value());
        EXPECT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());
        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 0u);
        EXPECT_FALSE(SessionRecorder::GetInstance().Start("", *GameManager::GetInstance().GetActive()).has_value());
    }

    TEST_F(SessionRecorderTest, ASecondStartWhileRecordingIsRefusedAndKeepsTheFirst) {
        ASSERT_TRUE(StartRecording().has_value());
        ASSERT_TRUE(ControlledReset(Game(), Params(R"({"seed":1})")).has_value());

        const auto again = StartRecording();

        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error().code, String(ControlErrors::kInvalidParams));
        EXPECT_TRUE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 1u);
    }

    TEST_F(SessionRecorderTest, AStartReplacesTheFileOfAnEarlierRecording) {
        ASSERT_TRUE(StartRecording().has_value());
        ASSERT_TRUE(ControlledReset(Game(), Params(R"({"seed":1})")).has_value());
        SessionRecorder::GetInstance().Stop();

        ASSERT_TRUE(StartRecording().has_value());
        SessionRecorder::GetInstance().Stop();

        EXPECT_EQ(ParseRecording().entries.size(), 0u);
        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 0u);
    }

    TEST_F(SessionRecorderTest, TheControlMethodsAndTheDirectCallsWriteTheSameFile) {
        ASSERT_TRUE(StartRecording().has_value());
        ASSERT_TRUE(GetJsonBool(Call("reset", Params(R"({"seed":5})")), "ok").value_or(false));
        ASSERT_TRUE(GetJsonBool(Call("perform_action", Params(R"({"type":"roll"})")), "ok").value_or(false));
        ASSERT_TRUE(GetJsonBool(Call("set_state", Params(R"({"rolls":[4]})")), "ok").value_or(false));
        EXPECT_FALSE(GetJsonBool(Call("perform_action", Params(R"({"type":"jump"})")), "ok").value_or(true));
        SessionRecorder::GetInstance().Stop();
        const String throughMethods = ReadRecording();

        ASSERT_TRUE(StartRecording().has_value());
        ASSERT_TRUE(ControlledReset(Game(), Params(R"({"seed":5})")).has_value());
        ASSERT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());
        ASSERT_TRUE(ControlledSetState(Game(), Params(R"({"rolls":[4]})")).has_value());
        SessionRecorder::GetInstance().Stop();

        EXPECT_EQ(ReadRecording(), throughMethods);
    }

    TEST_F(SessionRecorderTest, SwitchingGameEndsTheRecordingButSwitchingToTheActiveOneDoesNot) {
        ASSERT_TRUE(StartRecording().has_value());
        ASSERT_TRUE(GetJsonBool(Call("switch_game", Params(R"({"name":"ReplayTest"})")), "ok").value_or(false));
        EXPECT_TRUE(SessionRecorder::GetInstance().IsRecording());

        ASSERT_TRUE(GetJsonBool(Call("switch_game", Params(R"({"name":"FakePlain"})")), "ok").value_or(false));

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
    }

    TEST_F(SessionRecorderTest, AGameThatIsNotRecordingCostsTheCallersNothing) {
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());

        EXPECT_TRUE(ControlledReset(Game(), Params(R"({})")).has_value());
        EXPECT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());

        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 0u);
        EXPECT_FALSE(std::filesystem::exists(_path));
    }

}  // namespace BECore::Tests
