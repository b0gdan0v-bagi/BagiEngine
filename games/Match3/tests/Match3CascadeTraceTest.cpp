#include <Common/Printers.h>
#include <Match3/Game/CascadeTrace.h>
#include <Match3/Game/Match3GameModel.h>
#include <Match3/Game/MatchFinder.h>

/**
 * Coverage of what Match3GameModel records about a swap (SwapTrace) and of ReplayTrace:
 *   - the board before the swap plus the trace gives the board after it, for every legal swap of several seeds;
 *   - the removed cells add up to the score, a refill starts above the board and falls as far as its column was emptied;
 *   - a hand-made board pins the steps of a two-step cascade by value;
 *   - the trace belongs to the version it was recorded at, and a stale one never survives a Reset or a rejected swap;
 *   - the recording changes nothing else: the goldens of Match3LegalSwapsTest and Match3GameModelTest are the proof.
 */
namespace BECore::Match3::Tests {

    namespace {

        bool SameBoard(const Match3Board& a, const Match3Board& b) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    if (a.Get(col, row) != b.Get(col, row)) {
                        return false;
                    }
                }
            }
            return true;
        }

        /// A full board without a single match: the gem of (col, row) is 1 + (col + 2 * row) % 6
        void FillQuietBoard(Match3GameModel& model) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    model.SetCellForTest(Cell{col, row}, static_cast<GemType>(1 + (col + 2 * row) % kGemTypeCount));
                }
            }
            model.ResetScoreForTest();
        }

        /// A neighbour pair whose swap TrySwap rejects
        bool FindRejectedSwap(const Match3GameModel& model, Cell& a, Cell& b) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col + 1 < kBoardSize; ++col) {
                    a = Cell{col, row};
                    b = Cell{static_cast<int8_t>(col + 1), row};
                    if (!model.WouldSwapMatch(a, b)) {
                        return true;
                    }
                }
            }
            return false;
        }

        constexpr uint32_t kSeeds[] = {0u, 1u, 7u, 42u, 123u, 9999u};

    }  // namespace

    TEST(Match3CascadeTraceTest, ReplayGivesTheBoardAfterEveryLegalSwap) {
        int swapsChecked = 0;
        for (const uint32_t seed : kSeeds) {
            Match3GameModel start;
            start.Reset(seed);
            for (const auto& [a, b] : start.GetLegalSwaps()) {
                Match3GameModel model = start;
                const Match3Board before = model.GetBoard();
                ASSERT_TRUE(model.TrySwap(a, b));

                const SwapTrace* trace = model.GetLastSwap();
                ASSERT_NE(trace, nullptr) << "seed " << seed;
                EXPECT_TRUE(SameBoard(ReplayTrace(before, *trace), model.GetBoard())) << "seed " << seed << " swap " << int(a.col) << "," << int(a.row);
                ++swapsChecked;
            }
        }
        EXPECT_GT(swapsChecked, 50);
    }

    TEST(Match3CascadeTraceTest, RemovedCellsAddUpToTheScoreGained) {
        for (const uint32_t seed : kSeeds) {
            Match3GameModel start;
            start.Reset(seed);
            for (const auto& [a, b] : start.GetLegalSwaps()) {
                Match3GameModel model = start;
                ASSERT_TRUE(model.TrySwap(a, b));
                const SwapTrace* trace = model.GetLastSwap();
                ASSERT_NE(trace, nullptr);

                size_t removed = 0;
                for (const CascadeStep& step : trace->steps) {
                    EXPECT_FALSE(step.removed.empty());
                    removed += step.removed.size();
                }
                EXPECT_EQ(static_cast<int>(removed), model.GetScore() - start.GetScore()) << "seed " << seed;
                EXPECT_FALSE(trace->steps.empty());
            }
        }
    }

    TEST(Match3CascadeTraceTest, RefillsStartAboveTheBoardAndFallAsFarAsTheirColumnWasEmptied) {
        for (const uint32_t seed : kSeeds) {
            Match3GameModel start;
            start.Reset(seed);
            for (const auto& [a, b] : start.GetLegalSwaps()) {
                Match3GameModel model = start;
                ASSERT_TRUE(model.TrySwap(a, b));
                const SwapTrace* trace = model.GetLastSwap();
                ASSERT_NE(trace, nullptr);

                for (const CascadeStep& step : trace->steps) {
                    int refillsOfColumn[kBoardSize] = {};
                    for (const GemRefill& refill : step.refills) {
                        ++refillsOfColumn[refill.col];
                    }
                    for (const GemRefill& refill : step.refills) {
                        EXPECT_LT(refill.fromRow, 0);
                        EXPECT_GE(refill.toRow, 0);
                        EXPECT_NE(refill.gem, GemType::None);
                        EXPECT_EQ(refill.toRow - refill.fromRow, refillsOfColumn[refill.col]);
                    }
                    for (const GemFall& fall : step.falls) {
                        EXPECT_GT(fall.toRow, fall.fromRow);
                    }
                    // Every removed cell is refilled or filled by a fall: the step leaves no hole
                    EXPECT_EQ(step.refills.size(), step.removed.size());
                }
            }
        }
    }

    TEST(Match3CascadeTraceTest, TwoStepCascadeIsPinnedByValue) {
        Match3GameModel model;
        model.Reset(5);
        FillQuietBoard(model);

        // Column 0 gets three reds in rows 5..7 once (0,7) and (1,7) trade places; the blue that comes down to (0,7)
        // from (0,4) then makes a row of three blues with (1,7) and (2,7)
        model.SetCellForTest(Cell{0, 5}, GemType::Red);
        model.SetCellForTest(Cell{0, 6}, GemType::Red);
        model.SetCellForTest(Cell{1, 7}, GemType::Red);
        model.SetCellForTest(Cell{0, 7}, GemType::Blue);
        model.SetCellForTest(Cell{2, 7}, GemType::Blue);
        model.SetCellForTest(Cell{0, 4}, GemType::Blue);
        model.SetCellForTest(Cell{3, 7}, GemType::Green);
        ASSERT_TRUE(MatchFinder::FindMatches(model.GetBoard()).empty());
        const Match3Board before = model.GetBoard();

        ASSERT_TRUE(model.TrySwap(Cell{0, 7}, Cell{1, 7}));
        const SwapTrace* trace = model.GetLastSwap();
        ASSERT_NE(trace, nullptr);
        EXPECT_EQ(trace->a, (Cell{0, 7}));
        EXPECT_EQ(trace->b, (Cell{1, 7}));
        ASSERT_GE(trace->steps.size(), 2u);

        const CascadeStep& first = trace->steps[0];
        ASSERT_EQ(first.removed.size(), 3u);
        EXPECT_EQ(first.removed[0], (Cell{0, 5}));
        EXPECT_EQ(first.removed[1], (Cell{0, 6}));
        EXPECT_EQ(first.removed[2], (Cell{0, 7}));

        // The five gems above fall three rows, the lowest first
        ASSERT_EQ(first.falls.size(), 5u);
        for (size_t i = 0; i < first.falls.size(); ++i) {
            const int8_t fromRow = static_cast<int8_t>(4 - i);
            EXPECT_EQ(first.falls[i].col, 0);
            EXPECT_EQ(first.falls[i].fromRow, fromRow);
            EXPECT_EQ(first.falls[i].toRow, fromRow + 3);
        }

        // Three new gems drop in above the top edge, one row apart, into rows 0..2
        ASSERT_EQ(first.refills.size(), 3u);
        for (size_t i = 0; i < first.refills.size(); ++i) {
            EXPECT_EQ(first.refills[i].col, 0);
            EXPECT_EQ(first.refills[i].toRow, static_cast<int8_t>(i));
            EXPECT_EQ(first.refills[i].fromRow, static_cast<int8_t>(i) - 3);
        }

        // The blue of (0,4) now sits in (0,7): the second round clears the row of blues (more only if a refill matched)
        const CascadeStep& second = trace->steps[1];
        for (const Cell blue : {Cell{0, 7}, Cell{1, 7}, Cell{2, 7}}) {
            EXPECT_NE(eastl::find(second.removed.begin(), second.removed.end(), blue), second.removed.end()) << int(blue.col);
        }

        EXPECT_TRUE(SameBoard(ReplayTrace(before, *trace), model.GetBoard()));
        EXPECT_GE(model.GetScore(), 6);
    }

    TEST(Match3CascadeTraceTest, TraceBelongsToTheVersionOfTheSwap) {
        Match3GameModel model;
        model.Reset(3);
        ASSERT_FALSE(model.GetLegalSwaps().empty());

        const auto [a, b] = model.GetLegalSwaps().front();
        const uint64_t versionBefore = model.GetVersion();
        ASSERT_TRUE(model.TrySwap(a, b));
        const SwapTrace* trace = model.GetLastSwap();
        ASSERT_NE(trace, nullptr);
        EXPECT_EQ(trace->version, model.GetVersion());
        EXPECT_EQ(trace->version, versionBefore + 1);
    }

    TEST(Match3CascadeTraceTest, ClickedSwapRecordsItToo) {
        Match3GameModel model;
        model.Reset(3);
        const auto [a, b] = model.GetLegalSwaps().front();
        model.OnCellClicked(a);
        EXPECT_EQ(model.GetLastSwap(), nullptr);
        model.OnCellClicked(b);
        const SwapTrace* trace = model.GetLastSwap();
        ASSERT_NE(trace, nullptr);
        EXPECT_EQ(trace->version, model.GetVersion());
    }

    TEST(Match3CascadeTraceTest, EmptyAfterResetAndAfterARejectedSwap) {
        Match3GameModel model;
        model.Reset(11);
        EXPECT_EQ(model.GetLastSwap(), nullptr);

        const auto [a, b] = model.GetLegalSwaps().front();
        ASSERT_TRUE(model.TrySwap(a, b));
        ASSERT_NE(model.GetLastSwap(), nullptr);

        // A rejected swap clears the old trace: it must not be taken for a new one
        Cell ra;
        Cell rb;
        ASSERT_TRUE(FindRejectedSwap(model, ra, rb));
        const uint64_t version = model.GetVersion();
        EXPECT_FALSE(model.TrySwap(ra, rb));
        EXPECT_EQ(model.GetLastSwap(), nullptr);
        EXPECT_EQ(model.GetVersion(), version);

        // A Reset clears the trace of a committed swap
        model.Reset(11);
        const auto [a2, b2] = model.GetLegalSwaps().front();
        ASSERT_TRUE(model.TrySwap(a2, b2));
        ASSERT_NE(model.GetLastSwap(), nullptr);
        model.Reset(11);
        EXPECT_EQ(model.GetLastSwap(), nullptr);
    }

    TEST(Match3CascadeTraceTest, ResolveCascadeAloneRecordsNothing) {
        Match3GameModel model;
        model.Reset(2);
        FillQuietBoard(model);
        model.SetCellForTest(Cell{0, 7}, GemType::Red);
        model.SetCellForTest(Cell{1, 7}, GemType::Red);
        model.SetCellForTest(Cell{2, 7}, GemType::Red);

        EXPECT_GE(model.ResolveCascade(), 3);
        EXPECT_EQ(model.GetLastSwap(), nullptr);
    }

}  // namespace BECore::Match3::Tests
