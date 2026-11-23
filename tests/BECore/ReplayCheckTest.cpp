#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Random/Rng.h>
#include <BECore/ReplayTestGame.h>
#include <BECore/Time/Time.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/ReplayCheck.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/ReplayFile.h>
#include <Games/SessionRecorder.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 0.25;  // exact in binary
        constexpr int kLockFrames = 2;          // kFrameSeconds * kLockFrames is the lock of `arm`
        constexpr StringView kGameName = "ReplayTest";
        constexpr int kDieFaces = 6;
        constexpr int kSeed = 8;  // its draws are 5, 6, 2: the last differs from the 6s that `state` sets

        /// The committed recording of ReplayTestGame: read-only data of the repository
        std::filesystem::path CommittedReplay() {
            return std::filesystem::path(__FILE__).parent_path() / "replays" / "replay_test_game.jsonl";
        }

        class ReplayCheckTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _path = TestTempPath(Format("replay_check_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()));
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

            static void Frame() {
                CoreManager::GetTime().Advance(kFrameSeconds);
                Game().Update(kFrameSeconds);
            }

            static String ReadFile(const std::filesystem::path& path) {
                std::ifstream file(path, std::ios::binary);  //-NoStd: see the include
                std::ostringstream text;                     //-NoStd: see the include
                text << file.rdbuf();
                return FromStdString(text.str());
            }

            void WriteFile(StringView text) const {
                Tests::WriteFile(_path, text);
            }

            /// The session of the committed file: reset, roll, arm, a wait of the lock, roll, set_state, roll
            String RecordSession() {
                EXPECT_TRUE(SessionRecorder::GetInstance().Start(FromStdString(_path.string()), *GameManager::GetInstance().GetActive(), "replay test game").has_value());
                EXPECT_TRUE(ControlledReset(Game(), Params(Format("{{\"seed\":{}}}", kSeed))).has_value());
                Frame();
                EXPECT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());
                EXPECT_TRUE(ControlledAction(Game(), Params(R"({"type":"arm"})")).has_value());
                for (int i = 0; i < kLockFrames; ++i) {
                    Frame();
                }
                EXPECT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());
                EXPECT_TRUE(ControlledSetState(Game(), Params(R"({"rolls":[6,6]})")).has_value());
                EXPECT_TRUE(ControlledAction(Game(), Params(R"({"type":"roll"})")).has_value());
                SessionRecorder::GetInstance().Stop();
                return ReadFile(_path);
            }

            static void Step(ReplayTestGame& game, double seconds) {
                CoreManager::GetTime().Advance(seconds);
                game.Update(seconds);
            }

            std::filesystem::path _path;
        };

    }  // namespace

    TEST_F(ReplayCheckTest, TheCommittedFileIsWhatTheRecorderWritesNow) {
        const String fresh = RecordSession();
        const auto committed = ParseReplay(ReadFile(CommittedReplay()));
        const auto recorded = ParseReplay(fresh);
        ASSERT_TRUE(committed.has_value()) << DescribeReplayError(committed.error()).c_str();
        ASSERT_TRUE(recorded.has_value());

        // The engine version of the header is the one that wrote the file: it moves with every release
        EXPECT_EQ(committed->header.game, recorded->header.game);
        EXPECT_EQ(committed->header.title, recorded->header.title);
        EXPECT_EQ(committed->entries.size(), recorded->entries.size());
        for (size_t i = 0; i < std::min(committed->entries.size(), recorded->entries.size()); ++i) {
            EXPECT_EQ(FormatReplayEntry(committed->entries[i]), FormatReplayEntry(recorded->entries[i])) << "entry " << i;
        }
        if (::testing::Test::HasFailure()) {
            ADD_FAILURE() << "the format or the game changed: if that is meant, replace tests/BECore/replays/replay_test_game.jsonl with\n" << fresh.c_str();
        }
    }

    TEST_F(ReplayCheckTest, TheMacroComparesTheFinalStateOfTheCommittedFile) {
        ReplayTestGame played;
        const auto step = [&played](double seconds) { Step(played, seconds); };
        // The last roll is the third draw of the seed; the entry `state` has set [6, 6] before it
        Rng rng(kSeed);
        [[maybe_unused]] const int first = rng.UniformInt(1, kDieFaces);
        [[maybe_unused]] const int second = rng.UniformInt(1, kDieFaces);
        const int last = rng.UniformInt(1, kDieFaces);
        Json expected = Json::object();
        expected["seed"] = kSeed;
        expected["rolls"] = Json::array({6, 6, last});
        expected["locked"] = false;

        EXPECT_REPLAY_FINAL_STATE(played, CommittedReplay(), expected, step);
    }

    TEST_F(ReplayCheckTest, AFinalStateThatDiffersFailsTheTest) {
        ReplayTestGame played;
        const auto step = [&played](double seconds) { Step(played, seconds); };
        const Json wrong = Params(Format("{{\"seed\":{},\"rolls\":[1],\"locked\":false}}", kSeed));

        EXPECT_NONFATAL_FAILURE(EXPECT_REPLAY_FINAL_STATE(played, CommittedReplay(), wrong, step), "the state after");
    }

    TEST_F(ReplayCheckTest, ADefaultFrameLeavesTheGameAloneSoATimerThatNeedsItIsTheFirstMismatch) {
        ReplayTestGame played;

        const auto result = PlayReplayFile(played, kGameName, CommittedReplay());

        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error().mismatch.has_value());
        const ReplayMismatch& mismatch = *result.error().mismatch;
        EXPECT_EQ(mismatch.kind, ReplayMismatchKind::Refused);
        EXPECT_EQ(mismatch.index, 3u);  // the roll after the arm: the lock never ran down
        EXPECT_EQ(mismatch.entry.line, 5u);
        EXPECT_NE(result.error().message.find("entry 3 (line 5)"), String::npos) << result.error().message.c_str();
        EXPECT_NE(result.error().message.find("refused"), String::npos);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 1u + kLockFrames);  // the default frame advanced the clock for every frame of the gaps
    }

    TEST_F(ReplayCheckTest, AFileThatCannotBeUsedIsAFailureWithoutAMismatch) {
        ReplayTestGame played;

        const auto missing = PlayReplayFile(played, kGameName, _path);
        ASSERT_FALSE(missing.has_value());
        EXPECT_FALSE(missing.error().mismatch.has_value());

        WriteFile("{not json}\n");
        const auto broken = PlayReplayFile(played, kGameName, _path);
        ASSERT_FALSE(broken.has_value());
        EXPECT_FALSE(broken.error().mismatch.has_value());
        EXPECT_NE(broken.error().message.find("line 1"), String::npos) << broken.error().message.c_str();

        const auto other = PlayReplayFile(played, "Chess", CommittedReplay());
        ASSERT_FALSE(other.has_value());
        EXPECT_FALSE(other.error().mismatch.has_value());
    }

    TEST_F(ReplayCheckTest, AGameThatIsNotControllableFailsTheMacro) {
        class Uncontrollable : public IGame {
        public:
            PoolString GetName() const override {
                return "ReplayTest"_intern;
            }
            PoolString GetSceneName() const override {
                return PoolString();
            }
        } game;

        EXPECT_NONFATAL_FAILURE(EXPECT_REPLAY_FINAL_STATE(game, CommittedReplay(), Json::object()), "cannot be controlled");
    }

}  // namespace BECore::Tests
