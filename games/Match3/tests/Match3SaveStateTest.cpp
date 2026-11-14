#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Match3/Game/Match3GameModel.h>
#include <Match3/Game/Match3SaveState.h>

/**
 * The Match3 save:
 *   - CaptureState / RestoreState: the board, score, seed and generator come back, play goes on exactly as without the
 *     save, a refused state changes nothing.
 *   - SaveMatch3 / LoadMatch3 through a SaveGameService on a temp folder (a local FileSystem, no engine services).
 */
namespace BECore::Match3::Tests {

    namespace {

        /// Play the first legal swap of the model; false when there is none
        bool PlayFirstLegalSwap(Match3GameModel& model) {
            const auto swaps = model.GetLegalSwaps();
            if (swaps.empty()) {
                return false;
            }
            return model.TrySwap(swaps[0].first, swaps[0].second);
        }

        bool SameState(const Match3SaveState& a, const Match3SaveState& b) {
            return a.seed == b.seed && a.score == b.score && a.board == b.board && a.rng == b.rng;
        }

        /// The text of a generator whose 624 words are zero but the first, which is @p first: the state that gives only zeros when @p first is below 2^31
        String ZeroGeneratorText(uint32_t first) {
            String text = Format("{}", first);
            for (size_t word = 1; word < std::mt19937::state_size; ++word) {
                text.append(" 0");
            }
            return text;
        }

        /// Reset to the seed and play two swaps, so that the score and the generator have moved
        void PlayTwoSwaps(Match3GameModel& model, uint32_t seed) {
            model.Reset(seed);
            EXPECT_TRUE(PlayFirstLegalSwap(model));
            EXPECT_TRUE(PlayFirstLegalSwap(model));
        }

    }  // namespace

    TEST(Match3SaveStateModelTest, CaptureHasBoardLettersSeedAndScore) {
        Match3GameModel model;
        PlayTwoSwaps(model, 11);
        const Match3SaveState state = model.CaptureState();

        EXPECT_EQ(state.seed, 11u);
        EXPECT_EQ(state.score, model.GetScore());
        ASSERT_EQ(state.board.size(), 64u);
        for (int row = 0; row < kBoardSize; ++row) {
            for (int col = 0; col < kBoardSize; ++col) {
                const char letter = state.board[static_cast<size_t>(row * kBoardSize + col)];
                EXPECT_EQ(letter, kGemLetters[static_cast<size_t>(model.GetCellAt(Cell{static_cast<int8_t>(col), static_cast<int8_t>(row)}))]);
                EXPECT_NE(letter, '.');
            }
        }
        EXPECT_FALSE(state.rng.empty());
    }

    TEST(Match3SaveStateModelTest, CaptureThenRestoreIntoFreshModelGivesTheSameGame) {
        Match3GameModel original;
        PlayTwoSwaps(original, 7);
        const Match3SaveState state = original.CaptureState();

        Match3GameModel restored;
        restored.Reset(999);
        const auto result = restored.RestoreState(state);
        ASSERT_TRUE(result.has_value());

        EXPECT_EQ(restored.GetSeed(), original.GetSeed());
        EXPECT_EQ(restored.GetScore(), original.GetScore());
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                EXPECT_EQ(restored.GetCellAt(Cell{col, row}), original.GetCellAt(Cell{col, row}));
            }
        }
        EXPECT_TRUE(SameState(restored.CaptureState(), state));
    }

    TEST(Match3SaveStateModelTest, PlayAfterRestoreContinuesExactlyAsWithoutTheSave) {
        for (const uint32_t seed : {0u, 1u, 42u, 12345u}) {
            Match3GameModel original;
            PlayTwoSwaps(original, seed);

            Match3GameModel restored;
            ASSERT_TRUE(restored.RestoreState(original.CaptureState()).has_value());

            for (int step = 0; step < 3; ++step) {
                ASSERT_TRUE(PlayFirstLegalSwap(original)) << "seed " << seed << " step " << step;
                ASSERT_TRUE(PlayFirstLegalSwap(restored)) << "seed " << seed << " step " << step;
                // The board, the score and the generator (the refills of the next swap) are the same after every swap
                EXPECT_TRUE(SameState(original.CaptureState(), restored.CaptureState())) << "seed " << seed << " step " << step;
                EXPECT_EQ(original.GetScore(), restored.GetScore());
            }
        }
    }

    TEST(Match3SaveStateModelTest, RestoreGrowsTheVersionAndClearsSelectionAndTrace) {
        Match3GameModel model;
        PlayTwoSwaps(model, 7);
        const Match3SaveState state = model.CaptureState();
        ASSERT_NE(model.GetLastSwap(), nullptr);
        model.OnCellClicked(Cell{3, 3});
        ASSERT_TRUE(model.HasSelection());

        const uint64_t before = model.GetVersion();
        ASSERT_TRUE(model.RestoreState(state).has_value());

        EXPECT_EQ(model.GetVersion(), before + 1);
        EXPECT_FALSE(model.HasSelection());
        EXPECT_EQ(model.GetLastSwap(), nullptr);

        Match3GameModel fresh;
        const uint64_t freshBefore = fresh.GetVersion();
        ASSERT_TRUE(fresh.RestoreState(state).has_value());
        EXPECT_GT(fresh.GetVersion(), freshBefore);
    }

    TEST(Match3SaveStateModelTest, RefusedStateReturnsAnErrorAndChangesNothing) {
        Match3GameModel model;
        PlayTwoSwaps(model, 7);
        model.OnCellClicked(Cell{1, 1});
        const Match3SaveState good = model.CaptureState();
        const uint64_t version = model.GetVersion();

        struct Case {
            const char* name;
            void (*damage)(Match3SaveState&);
        };
        const Case cases[] = {
            {"board of 63 characters", [](Match3SaveState& s) { s.board.pop_back(); }},
            {"board of 65 characters", [](Match3SaveState& s) { s.board.push_back('R'); }},
            {"empty board", [](Match3SaveState& s) { s.board.clear(); }},
            {"unknown letter", [](Match3SaveState& s) { s.board[10] = 'X'; }},
            {"lower case letter", [](Match3SaveState& s) { s.board[10] = 'r'; }},
            {"empty cell", [](Match3SaveState& s) { s.board[10] = '.'; }},
            {"a match on the board", [](Match3SaveState& s) { s.board[0] = s.board[1] = s.board[2] = 'R'; }},
            {"negative score", [](Match3SaveState& s) { s.score = -1; }},
            {"empty generator text", [](Match3SaveState& s) { s.rng.clear(); }},
            {"garbage generator text", [](Match3SaveState& s) { s.rng = "not a generator"; }},
            {"generator text with letters", [](Match3SaveState& s) { s.rng[0] = 'x'; }},
            {"generator text with a tail", [](Match3SaveState& s) { s.rng.append(" 1"); }},
            {"generator number out of range", [](Match3SaveState& s) { s.rng.insert(0, "99999999999"); }},
            {"generator text cut short", [](Match3SaveState& s) { s.rng.resize(s.rng.size() / 2); }},
            {"all-zero generator", [](Match3SaveState& s) { s.rng = ZeroGeneratorText(0); }},
            {"generator of zeros after a small first word", [](Match3SaveState& s) { s.rng = ZeroGeneratorText(1); }},
        };

        for (const Case& c : cases) {
            Match3SaveState bad = good;
            c.damage(bad);
            const auto result = model.RestoreState(bad);
            ASSERT_FALSE(result.has_value()) << c.name;
            EXPECT_FALSE(result.error().empty()) << c.name;
            EXPECT_TRUE(SameState(model.CaptureState(), good)) << c.name;
            EXPECT_EQ(model.GetVersion(), version) << c.name;
            EXPECT_TRUE(model.HasSelection()) << c.name;
            EXPECT_NE(model.GetLastSwap(), nullptr) << c.name;
        }
    }

    /// A local FileSystem and a temp folder per test: no global services, so the tests run beside others
    class Match3SaveFileTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _base = BECore::Tests::TestTempDirectory("BagiMatch3SaveTest");
            std::filesystem::remove_all(_base);
            std::filesystem::create_directories(_base);
            _service = std::make_unique<SaveGameService>(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_base / "saves"));
        }

        void TearDown() override {
            _service.reset();
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        std::filesystem::path _base;
        FileSystem _fileSystem;
        std::unique_ptr<SaveGameService> _service;
    };

    TEST_F(Match3SaveFileTest, SavedGameIsLoadedIntoAFreshModelAndPlayContinues) {
        Match3GameModel original;
        PlayTwoSwaps(original, 2024);
        ASSERT_TRUE(SaveMatch3(*_service, "quick", original).has_value());
        EXPECT_TRUE(_service->Exists("quick"));

        const auto peek = _service->Peek("quick");
        ASSERT_TRUE(peek.has_value());
        EXPECT_EQ(peek->game, "Match3");
        EXPECT_EQ(peek->version, kMatch3SaveVersion);

        Match3GameModel loaded;
        const auto info = LoadMatch3(*_service, "quick", loaded);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->version, kMatch3SaveVersion);
        EXPECT_TRUE(SameState(loaded.CaptureState(), original.CaptureState()));

        for (int step = 0; step < 3; ++step) {
            ASSERT_TRUE(PlayFirstLegalSwap(original));
            ASSERT_TRUE(PlayFirstLegalSwap(loaded));
            EXPECT_TRUE(SameState(loaded.CaptureState(), original.CaptureState())) << "step " << step;
        }
    }

    TEST_F(Match3SaveFileTest, MissingSlotIsNotFoundAndTheModelIsKept) {
        Match3GameModel model;
        PlayTwoSwaps(model, 7);
        const Match3SaveState before = model.CaptureState();
        const uint64_t version = model.GetVersion();

        const auto info = LoadMatch3(*_service, "nothing", model);
        ASSERT_FALSE(info.has_value());
        EXPECT_EQ(info.error().kind, SaveErrorKind::NotFound);
        EXPECT_TRUE(SameState(model.CaptureState(), before));
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST_F(Match3SaveFileTest, FileWithAnInvalidBoardIsCorruptAndTheModelIsKept) {
        Match3GameModel model;
        PlayTwoSwaps(model, 7);
        Match3SaveState damaged = model.CaptureState();
        damaged.board[5] = '.';
        ASSERT_TRUE(_service->Save("damaged", kMatch3SaveSchema, damaged).has_value());

        const Match3SaveState before = model.CaptureState();
        const uint64_t version = model.GetVersion();
        const auto info = LoadMatch3(*_service, "damaged", model);
        ASSERT_FALSE(info.has_value());
        EXPECT_EQ(info.error().kind, SaveErrorKind::Corrupt);
        EXPECT_FALSE(info.error().detail.empty());
        EXPECT_TRUE(SameState(model.CaptureState(), before));
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST_F(Match3SaveFileTest, FileOfAnotherGameIsWrongGame) {
        Match3GameModel model;
        PlayTwoSwaps(model, 7);
        constexpr SaveSchema kOtherGame{"Chess", 1, 1};
        ASSERT_TRUE(_service->Save("other", kOtherGame, model.CaptureState()).has_value());

        const Match3SaveState before = model.CaptureState();
        const auto info = LoadMatch3(*_service, "other", model);
        ASSERT_FALSE(info.has_value());
        EXPECT_EQ(info.error().kind, SaveErrorKind::WrongGame);
        EXPECT_TRUE(SameState(model.CaptureState(), before));
    }

    TEST_F(Match3SaveFileTest, FileOfANewerVersionIsVersionTooNew) {
        Match3GameModel model;
        PlayTwoSwaps(model, 7);
        const SaveSchema newer{kMatch3SaveGame, kMatch3SaveVersion + 1, kMatch3SaveVersion};
        ASSERT_TRUE(_service->Save("newer", newer, model.CaptureState()).has_value());

        const auto info = LoadMatch3(*_service, "newer", model);
        ASSERT_FALSE(info.has_value());
        EXPECT_EQ(info.error().kind, SaveErrorKind::VersionTooNew);
    }

}  // namespace BECore::Match3::Tests
