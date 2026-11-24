#include <BECore/Control/ControlParams.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/ReplayTestGame.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Common/ControlledCalls.h>
#include <Common/EngineEnvironment.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/GameText.h>
#include <Games/ReplayFile.h>
#include <Games/ReplayPlayer.h>
#include <Games/SessionRecorder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 0.25;  // exact in binary
        constexpr int kLockFrames = 2;          // kFrameSeconds * kLockFrames is the lock of `arm`

        class ReplayPlayerTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _path = TestTempPath(Format("replay_player_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()));
                CoreManager::GetTime().Reset();
                ReplayTestGame::RestartSeedCounter();
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("ReplayTest"_intern);
            }

            void TearDown() override {
                SessionRecorder::DestroyInstance();
                GameManager::GetInstance().Shutdown();
                CoreManager::GetTime().Reset();
                RemoveFileIfPresent(_path);
            }

            static ReplayTestGame& Game() {
                return *GameManager::GetInstance().GetActive()->Cast<ReplayTestGame>();
            }

            /// One frame of game time on the recorded game, the way an application loop would pass it
            static void Frame() {
                CoreManager::GetTime().Advance(kFrameSeconds);
                Game().Update(kFrameSeconds);
            }

            [[nodiscard]] String ReadRecording() const {
                return ReadFile(_path);
            }

            /// reset, roll, arm, a wait of the lock, roll, set_state, roll: the arm / timer gap is in the file (6 entries)
            String RecordSession() {
                EXPECT_TRUE(SessionRecorder::GetInstance().Start(_path.View(), *GameManager::GetInstance().GetActive(), "session").has_value());
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

            /// Plays @p player into @p game; the gap before each entry passes on the clock and on the game unless @p passGaps is false
            static ReplayStep Play(ReplayPlayer& player, ReplayTestGame& game, bool passGaps = true) {
                while (!player.IsDone()) {
                    const ReplayGap gap = player.PeekGap();
                    if (passGaps && gap.frames > 0) {
                        const double dt = gap.seconds / static_cast<double>(gap.frames);
                        for (uint64_t i = 0; i < gap.frames; ++i) {
                            CoreManager::GetTime().Advance(dt);
                            game.Update(dt);
                        }
                    }
                    if (auto step = player.Apply(game); !step) {
                        return step;
                    }
                }
                return {};
            }

            Path _path;
        };

    }  // namespace

    TEST_F(ReplayPlayerTest, ARecordedSessionReplaysToTheSameState) {
        const String text = RecordSession();
        const String recordedState = *StateText(Game());
        const JsonDoc recorded(recordedState);
        ASSERT_EQ(Require(FindParam(recorded, "rolls")).Size(), 3u);

        ReplayPlayer player;
        ASSERT_TRUE(player.Load(text, "ReplayTest").has_value());
        EXPECT_EQ(player.GetEntryCount(), 6u);
        ReplayTestGame fresh;
        const ReplayStep result = Play(player, fresh);

        EXPECT_TRUE(result.has_value()) << (result ? "" : ReplayMismatchText(result.error()).c_str());
        EXPECT_TRUE(player.IsDone());
        EXPECT_EQ(*StateText(fresh), recordedState);
    }

    TEST_F(ReplayPlayerTest, ThePlayerAnswersTheGapOfTheNextEntryAndIsDoneAtTheEnd) {
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(RecordSession(), "ReplayTest").has_value());
        ReplayTestGame fresh;

        EXPECT_EQ(player.PeekGap(), (ReplayGap{0, 0.0}));
        ASSERT_TRUE(player.Apply(fresh).has_value());
        EXPECT_EQ(player.PeekGap(), (ReplayGap{1, kFrameSeconds}));  // the frame before the first roll
        ASSERT_TRUE(player.Apply(fresh).has_value());
        EXPECT_EQ(player.PeekGap(), (ReplayGap{0, 0.0}));
        ASSERT_TRUE(player.Apply(fresh).has_value());
        EXPECT_EQ(player.PeekGap(), (ReplayGap{kLockFrames, kLockFrames * kFrameSeconds}));
    }

    TEST_F(ReplayPlayerTest, WithoutTheGapTheFirstRefusedActionIsTheMismatch) {
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(RecordSession(), "ReplayTest").has_value());
        ReplayTestGame fresh;

        const ReplayStep result = Play(player, fresh, false);

        ASSERT_FALSE(result.has_value());
        const ReplayMismatch& mismatch = result.error();
        EXPECT_EQ(mismatch.kind, ReplayMismatchKind::Refused);
        EXPECT_EQ(mismatch.index, 3u);       // the roll after the arm
        EXPECT_EQ(mismatch.entry.line, 5u);  // line 1 is the header
        EXPECT_EQ(mismatch.entry.op, ReplayOp::Action);
        ASSERT_TRUE(mismatch.error.has_value());
        EXPECT_EQ(mismatch.error->code, String(GameControlErrors::kIllegalAction));
        EXPECT_TRUE(player.IsDone());
    }

    TEST_F(ReplayPlayerTest, AChangedOutcomeIsOutcomeDiffers) {
        auto parsed = ParseReplay(RecordSession());
        ASSERT_TRUE(parsed.has_value());
        ReplayEntry& roll = parsed->entries.at(1);
        ASSERT_TRUE(roll.outcome.has_value());
        const JsonDoc original(StringView(roll.outcome->c_str(), roll.outcome->size()));
        const int value = static_cast<int>(GetParamInt(original.Root(), "value").value_or(0));
        roll.outcome = Format("{{\"value\":{}}}", value % 6 + 1);
        String edited = Format("{}\n", FormatReplayHeader(parsed->header));
        for (const ReplayEntry& entry : parsed->entries) {
            AppendFormat(edited, "{}\n", FormatReplayEntry(entry));
        }

        ReplayPlayer player;
        ASSERT_TRUE(player.Load(edited, "ReplayTest").has_value());
        ReplayTestGame fresh;
        const ReplayStep result = Play(player, fresh);

        ASSERT_FALSE(result.has_value());
        const ReplayMismatch& mismatch = result.error();
        EXPECT_EQ(mismatch.kind, ReplayMismatchKind::OutcomeDiffers);
        EXPECT_EQ(mismatch.index, 1u);
        EXPECT_EQ(mismatch.entry.line, 3u);
        ASSERT_TRUE(mismatch.expected.has_value());
        ASSERT_TRUE(mismatch.actual.has_value());
        EXPECT_EQ(*mismatch.expected, *roll.outcome);
        const JsonDoc actual(StringView(mismatch.actual->c_str(), mismatch.actual->size()));
        EXPECT_EQ(GetParamInt(actual.Root(), "value"), value);
        EXPECT_FALSE(mismatch.error.has_value());
    }

    TEST_F(ReplayPlayerTest, AFileWithoutOutcomesChecksOnlyThatTheActionsAreAccepted) {
        const String text = R"({"replay":1,"game":"ReplayTest","engine":"x"})"
                            "\n"
                            R"({"op":"reset","params":{"seed":4},"gap":{"frames":0,"seconds":0}})"
                            "\n"
                            R"({"op":"action","params":{"type":"roll"},"gap":{"frames":0,"seconds":0}})"
                            "\n";
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(text, "ReplayTest").has_value());
        ReplayTestGame fresh;

        EXPECT_TRUE(Play(player, fresh).has_value());
        const JsonDoc state(*StateText(fresh));
        EXPECT_EQ(Require(FindParam(state, "rolls")).Size(), 1u);
    }

    TEST_F(ReplayPlayerTest, AGameThatTakesAnotherStartIsStartDiffers) {
        // A game that drew its own seed for the reset: the file asks for 21, the game answers another one
        class StubbornGame : public ReplayTestGame {
        public:
            std::expected<void, ControlError> Reset(JsonView) override {
                return ReplayTestGame::Reset(JsonDoc("{}"));
            }
        };
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(RecordSession(), "ReplayTest").has_value());
        StubbornGame stubborn;

        const ReplayStep result = Play(player, stubborn);

        ASSERT_FALSE(result.has_value());
        const ReplayMismatch& mismatch = result.error();
        EXPECT_EQ(mismatch.kind, ReplayMismatchKind::StartDiffers);
        EXPECT_EQ(mismatch.index, 0u);
        EXPECT_EQ(mismatch.entry.line, 2u);
        ASSERT_TRUE(mismatch.expected.has_value());
        ASSERT_TRUE(mismatch.actual.has_value());
        EXPECT_EQ(*mismatch.expected, R"({"seed":21})");
        EXPECT_NE(*mismatch.actual, *mismatch.expected);
    }

    TEST_F(ReplayPlayerTest, AGameThatAcceptsAnActionWithoutAnOutcomeIsRefusedAsInternalError) {
        class SilentGame : public ReplayTestGame {
        public:
            std::expected<void, ControlError> PerformAction(JsonView, JsonWriter&) override {
                return {};
            }
        };
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(RecordSession(), "ReplayTest").has_value());
        SilentGame silent;

        const ReplayStep result = Play(player, silent);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, ReplayMismatchKind::Refused);
        ASSERT_TRUE(result.error().error.has_value());
        EXPECT_EQ(result.error().error->code, String(ControlErrors::kInternalError));
    }

    TEST_F(ReplayPlayerTest, AResetTheGameRefusesIsRefused) {
        const String text = R"({"replay":1,"game":"ReplayTest","engine":"x"})"
                            "\n"
                            R"({"op":"reset","params":{"seed":"x"},"gap":{"frames":0,"seconds":0}})"
                            "\n";
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(text, "ReplayTest").has_value());
        ReplayTestGame fresh;

        const ReplayStep result = Play(player, fresh);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, ReplayMismatchKind::Refused);
        ASSERT_TRUE(result.error().error.has_value());
        EXPECT_EQ(result.error().error->code, String(ControlErrors::kInvalidParams));
    }

    TEST_F(ReplayPlayerTest, AFileOfAnotherGameOrAFileThatDoesNotParseIsRefusedWithItsLine) {
        const String text = RecordSession();
        ReplayPlayer player;

        const auto other = player.Load(text, "Chess");
        ASSERT_FALSE(other.has_value());
        EXPECT_EQ(other.error().line, 1u);
        EXPECT_NE(other.error().message.find("ReplayTest"), String::npos);

        const auto broken = player.Load(R"({"replay":1,"game":"ReplayTest","engine":"x"})"
                                        "\n{not json}\n",
                                        "ReplayTest");
        ASSERT_FALSE(broken.has_value());
        EXPECT_EQ(broken.error().line, 2u);
    }

    TEST_F(ReplayPlayerTest, PlayingARecordedFileDoesNotRecord) {
        const String text = RecordSession();
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(text, "ReplayTest").has_value());
        ASSERT_TRUE(SessionRecorder::GetInstance().Start(_path.View(), *GameManager::GetInstance().GetActive()).has_value());

        EXPECT_TRUE(Play(player, Game()).has_value());

        EXPECT_EQ(SessionRecorder::GetInstance().GetEntryCount(), 0u);
        SessionRecorder::GetInstance().Stop();
    }

    TEST_F(ReplayPlayerTest, TheMismatchAnswerCarriesTheEntryAndWhatHappened) {
        ReplayPlayer player;
        ASSERT_TRUE(player.Load(RecordSession(), "ReplayTest").has_value());
        ReplayTestGame fresh;
        const ReplayStep result = Play(player, fresh, false);
        ASSERT_FALSE(result.has_value());

        const JsonDoc parsed(ReplayMismatchText(result.error()));
        ASSERT_TRUE(parsed.IsParsed());
        const JsonView answer = parsed.Root();

        EXPECT_EQ(IntOf(answer, "index"), 3);
        EXPECT_EQ(IntOf(answer, "line"), 5);
        EXPECT_EQ(StringOf(answer, "kind"), "refused");
        const JsonView entry = MemberOf(answer, "entry");
        ASSERT_TRUE(entry.IsObject());
        EXPECT_EQ(StringOf(entry, "op"), "action");
        const JsonView error = MemberOf(answer, "error");
        ASSERT_TRUE(error.IsObject());
        EXPECT_EQ(StringOf(error, "code"), "illegal_action");
        EXPECT_FALSE(MemberOf(answer, "expected").IsValid());
    }

}  // namespace BECore::Tests
