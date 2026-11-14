#include <Common/Printers.h>
#include <Match3/Game/Match3GameModel.h>
#include <Match3/Game/MatchFinder.h>

/**
 * Coverage of what the external control API needs from Match3GameModel:
 *   - WouldSwapMatch / GetLegalSwaps tell the truth about TrySwap and never change the model.
 *   - The seed, with the swaps made, fixes the board and the score, on every platform.
 *   - The version grows with every change a controller has to see.
 */
namespace BECore::Match3::Tests {

    namespace {

        Vector<std::string> BoardRows(const Match3GameModel& model) {
            Vector<std::string> rows;
            for (int8_t row = 0; row < kBoardSize; ++row) {
                std::string line;
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    line.push_back(kGemLetters[static_cast<size_t>(model.GetCellAt(Cell{col, row}))]);
                }
                rows.push_back(std::move(line));
            }
            return rows;
        }

        /// Every 4-neighbour pair of the board once: a cell with its right and its lower neighbour
        Vector<Pair<Cell, Cell>> AllNeighbourPairs() {
            Vector<Pair<Cell, Cell>> pairs;
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    if (col + 1 < kBoardSize) {
                        pairs.emplace_back(Cell{col, row}, Cell{static_cast<int8_t>(col + 1), row});
                    }
                    if (row + 1 < kBoardSize) {
                        pairs.emplace_back(Cell{col, row}, Cell{col, static_cast<int8_t>(row + 1)});
                    }
                }
            }
            return pairs;
        }

        constexpr uint32_t kSeeds[] = {0u, 1u, 7u, 42u, 123u, 1337u, 9999u};

    }  // namespace

    TEST(Match3LegalSwapsTest, WouldSwapMatchChangesNothing) {
        Match3GameModel model;
        model.Reset(42);
        const auto boardBefore = BoardRows(model);
        const uint64_t versionBefore = model.GetVersion();

        for (const auto& [a, b] : AllNeighbourPairs()) {
            (void)model.WouldSwapMatch(a, b);
        }
        (void)model.GetLegalSwaps();

        EXPECT_EQ(BoardRows(model), boardBefore);
        EXPECT_EQ(model.GetScore(), 0);
        EXPECT_EQ(model.GetVersion(), versionBefore);
        EXPECT_FALSE(model.HasSelection());
    }

    TEST(Match3LegalSwapsTest, LegalSwapsAreExactlyTheSwapsThatTrySwapCommits) {
        for (uint32_t seed : kSeeds) {
            Match3GameModel model;
            model.Reset(seed);

            // The brute force: try every neighbour pair on a copy of the model
            Vector<Pair<Cell, Cell>> expected;
            for (const auto& [a, b] : AllNeighbourPairs()) {
                Match3GameModel copy = model;
                if (copy.TrySwap(a, b)) {
                    expected.emplace_back(a, b);
                }
            }

            const auto legal = model.GetLegalSwaps();
            ASSERT_EQ(legal.size(), expected.size()) << "seed " << seed;
            for (size_t index = 0; index < legal.size(); ++index) {
                EXPECT_EQ(legal[index].first, expected[index].first) << "seed " << seed << " swap " << index;
                EXPECT_EQ(legal[index].second, expected[index].second) << "seed " << seed << " swap " << index;
            }
        }
    }

    TEST(Match3LegalSwapsTest, WouldSwapMatchAgreesWithTrySwapForEveryPairAndBothDirections) {
        for (uint32_t seed : kSeeds) {
            Match3GameModel model;
            model.Reset(seed);

            for (const auto& [a, b] : AllNeighbourPairs()) {
                Match3GameModel forward = model;
                Match3GameModel backward = model;
                const bool would = model.WouldSwapMatch(a, b);

                EXPECT_EQ(would, forward.TrySwap(a, b)) << "seed " << seed;
                EXPECT_EQ(would, model.WouldSwapMatch(b, a)) << "seed " << seed;
                EXPECT_EQ(would, backward.TrySwap(b, a)) << "seed " << seed;
            }
        }
    }

    TEST(Match3LegalSwapsTest, RandomStartingBoardsHaveMovesToPlay) {
        for (uint32_t seed : kSeeds) {
            Match3GameModel model;
            model.Reset(seed);

            EXPECT_FALSE(model.GetLegalSwaps().empty()) << "seed " << seed;
        }
    }

    TEST(Match3LegalSwapsTest, WouldSwapMatchRejectsCellsOutsideTheBoardAndFarCells) {
        Match3GameModel model;
        model.Reset(42);

        EXPECT_FALSE(model.WouldSwapMatch(Cell{-1, 0}, Cell{0, 0}));
        EXPECT_FALSE(model.WouldSwapMatch(Cell{0, 0}, Cell{0, -1}));
        EXPECT_FALSE(model.WouldSwapMatch(Cell{7, 7}, Cell{8, 7}));
        EXPECT_FALSE(model.WouldSwapMatch(Cell{7, 7}, Cell{7, 8}));
        EXPECT_FALSE(model.WouldSwapMatch(Cell{0, 0}, Cell{2, 0}));  // not neighbours
        EXPECT_FALSE(model.WouldSwapMatch(Cell{0, 0}, Cell{1, 1}));  // diagonal
        EXPECT_FALSE(model.WouldSwapMatch(Cell{3, 3}, Cell{3, 3}));  // the same cell
    }

    TEST(Match3LegalSwapsTest, BoardWithNoMovesHasNoLegalSwaps) {
        Match3GameModel model;
        // Diagonal stripes: no run of three, and no swap makes one
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                model.SetCellForTest(Cell{col, row}, static_cast<GemType>(1 + (col + row) % kGemTypeCount));
            }
        }

        ASSERT_TRUE(MatchFinder::FindMatches(model.GetBoard()).empty());
        EXPECT_TRUE(model.GetLegalSwaps().empty());
    }

    TEST(Match3LegalSwapsTest, SeedIsRememberedAndTheVersionGrowsWithEveryChange) {
        Match3GameModel model;
        const uint64_t afterConstruction = model.GetVersion();
        EXPECT_GT(afterConstruction, 0u);  // the constructor resets the game

        model.Reset(5);
        EXPECT_EQ(model.GetSeed(), 5u);
        EXPECT_GT(model.GetVersion(), afterConstruction);

        // A swap that makes no match is reverted and is not a change
        const uint64_t beforeSwaps = model.GetVersion();
        const auto legal = model.GetLegalSwaps();
        ASSERT_FALSE(legal.empty());
        EXPECT_FALSE(model.TrySwap(Cell{0, 0}, Cell{5, 5}));
        EXPECT_EQ(model.GetVersion(), beforeSwaps);

        ASSERT_TRUE(model.TrySwap(legal.front().first, legal.front().second));
        EXPECT_GT(model.GetVersion(), beforeSwaps);

        const uint64_t afterSwap = model.GetVersion();
        model.Reset(5);
        EXPECT_GT(model.GetVersion(), afterSwap);  // never goes back, not even to the same board
    }

    TEST(Match3LegalSwapsTest, SameSeedGivesTheSameBoard) {
        for (uint32_t seed : kSeeds) {
            Match3GameModel first;
            Match3GameModel second;
            first.Reset(seed);
            second.Reset(seed);

            EXPECT_EQ(BoardRows(first), BoardRows(second)) << "seed " << seed;
        }
    }

    TEST(Match3LegalSwapsTest, DifferentSeedsGiveDifferentBoards) {
        Match3GameModel first;
        Match3GameModel second;
        first.Reset(1);
        second.Reset(2);

        EXPECT_NE(BoardRows(first), BoardRows(second));
    }

    TEST(Match3LegalSwapsTest, SameSeedAndSameSwapsGiveTheSameBoardAndScore) {
        Match3GameModel first;
        Match3GameModel second;
        first.Reset(2024);
        second.Reset(2024);

        // A replay: always take the first legal swap (the refills after a cascade come from the RNG too)
        for (int move = 0; move < 30; ++move) {
            const auto legal = first.GetLegalSwaps();
            ASSERT_EQ(legal.size(), second.GetLegalSwaps().size()) << "move " << move;
            if (legal.empty()) {
                break;
            }
            ASSERT_TRUE(first.TrySwap(legal.front().first, legal.front().second));
            ASSERT_TRUE(second.TrySwap(legal.front().first, legal.front().second));
            ASSERT_EQ(BoardRows(first), BoardRows(second)) << "move " << move;
        }

        EXPECT_EQ(first.GetScore(), second.GetScore());
        EXPECT_GT(first.GetScore(), 0);
    }

    // The boards below come from std::mt19937 and a plain modulo, written down from an independent implementation of
    // MT19937 (init_genrand seeding, checked against the 1st and the 10000th value that the C++ standard fixes). They must
    // be the same on every platform and standard library: a replay or a controller depends on it.
    TEST(Match3LegalSwapsTest, SeedGivesTheSameBoardOnEveryPlatform) {
        struct Golden {
            uint32_t seed;
            Vector<std::string> rows;
        };
        const Golden goldens[] = {
            {1337, {"OOYBPOPY", "RYYRGBRB", "PBGOPRYR", "RGRGPYPG", "RPGPOOGY", "BRGGRPGR", "PPYYRBBG", "RBROPBYR"}},
            {0, {"YGPROGOO", "OGPYRGBY", "RRYGYPPY", "YPOPGROR", "PROGBPPB", "YGBOOPOO", "YOBPPOPY", "PBYPOBGR"}},
            {42, {"RPBBRPBY", "BPBBYRGB", "POGOYOPP", "OPGOORRO", "OPBPYBRY", "ROGGPPBG", "PRGBGBOO", "YYRYRYBR"}},
        };

        for (const Golden& golden : goldens) {
            Match3GameModel model;
            model.Reset(golden.seed);

            EXPECT_EQ(BoardRows(model), golden.rows) << "seed " << golden.seed;
        }
    }

}  // namespace BECore::Match3::Tests
