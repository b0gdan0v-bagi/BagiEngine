#include <Common/Printers.h>
#include <EASTL/algorithm.h>
#include <Match3/Game/Match3Board.h>
#include <Match3/Game/Match3GameModel.h>
#include <Match3/Game/Match3Types.h>
#include <Match3/Game/MatchFinder.h>

/**
 * Coverage for Match3Board / MatchFinder / Match3GameModel.
 *
 * MVP scope:
 *   - Initial board has no preexisting matches (deterministic seed).
 *   - MatchFinder detects horizontal, vertical and intersecting matches.
 *   - TrySwap accepts matching swaps and reverts non-matching ones.
 *   - Cascade increments the score across multiple resolve passes.
 */
namespace BECore::Match3::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(Cell::IsInside(0, 0));
    static_assert(Cell::IsInside(kBoardSize - 1, kBoardSize - 1));
    static_assert(!Cell::IsInside(-1, 0));
    static_assert(!Cell::IsInside(0, kBoardSize));
    static_assert(Cell{3, 4}.Index() == 4 * kBoardSize + 3);
    static_assert(Cell::FromIndex(3 * kBoardSize + 5) == (Cell{5, 3}));
    static_assert(kGemTypeCount == 6);

    namespace {

        bool Contains(const Vector<Cell>& cells, Cell target) {
            return eastl::find(cells.begin(), cells.end(), target) != cells.end();
        }

        /// Wipe the model's board and set only the cells provided. Helper used by
        /// tests to construct deterministic positions without fighting the random
        /// initial generator.
        void SetupBoard(Match3GameModel& model, std::initializer_list<eastl::pair<Cell, GemType>> cells) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    model.SetCellForTest(Cell{col, row}, GemType::None);
                }
            }
            for (const auto& [c, g] : cells) {
                model.SetCellForTest(c, g);
            }
            model.ResetScoreForTest();
        }

    }  // namespace

    TEST(Match3GameModelTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(Match3GameModelTest, InitialBoardHasNoMatches) {
        // Repeat across several seeds so we cover both lucky and unlucky RNG paths.
        for (uint32_t seed : {0u, 1u, 7u, 42u, 123u, 9999u}) {
            Match3GameModel model;
            model.Reset(seed);
            const auto matches = MatchFinder::FindMatches(model.GetBoard());
            EXPECT_TRUE(matches.empty()) << "seed " << seed;
        }
    }

    TEST(Match3GameModelTest, FindMatchesHorizontalThree) {
        Match3Board board;
        board.Set(2, 4, GemType::Red);
        board.Set(3, 4, GemType::Red);
        board.Set(4, 4, GemType::Red);

        const auto matches = MatchFinder::FindMatches(board);
        EXPECT_EQ(matches.size(), 3u);
        EXPECT_TRUE(Contains(matches, Cell{2, 4}));
        EXPECT_TRUE(Contains(matches, Cell{3, 4}));
        EXPECT_TRUE(Contains(matches, Cell{4, 4}));

        // Adjacent gem of a different type does not extend the match.
        board.Set(5, 4, GemType::Blue);
        const auto matches2 = MatchFinder::FindMatches(board);
        EXPECT_EQ(matches2.size(), 3u);
        EXPECT_FALSE(Contains(matches2, Cell{5, 4}));
    }

    TEST(Match3GameModelTest, FindMatchesVerticalThree) {
        Match3Board board;
        board.Set(1, 0, GemType::Green);
        board.Set(1, 1, GemType::Green);
        board.Set(1, 2, GemType::Green);
        board.Set(1, 3, GemType::Green);

        const auto matches = MatchFinder::FindMatches(board);
        EXPECT_EQ(matches.size(), 4u);
        EXPECT_TRUE(Contains(matches, Cell{1, 0}));
        EXPECT_TRUE(Contains(matches, Cell{1, 3}));
    }

    TEST(Match3GameModelTest, FindMatchesLShape) {
        Match3Board board;
        // Horizontal triple along row 4.
        board.Set(0, 4, GemType::Yellow);
        board.Set(1, 4, GemType::Yellow);
        board.Set(2, 4, GemType::Yellow);
        // Vertical triple along col 0 sharing (0, 4).
        board.Set(0, 5, GemType::Yellow);
        board.Set(0, 6, GemType::Yellow);

        const auto matches = MatchFinder::FindMatches(board);
        EXPECT_EQ(matches.size(), 5u);
        EXPECT_TRUE(Contains(matches, Cell{0, 4}));
        EXPECT_TRUE(Contains(matches, Cell{1, 4}));
        EXPECT_TRUE(Contains(matches, Cell{2, 4}));
        EXPECT_TRUE(Contains(matches, Cell{0, 5}));
        EXPECT_TRUE(Contains(matches, Cell{0, 6}));
    }

    TEST(Match3GameModelTest, TrySwapAcceptsMatchingSwap) {
        Match3GameModel model;
        // Row 4 = [R, R, G, R, ...]: swap (2,4)<->(3,4) yields [R,R,R,R].
        SetupBoard(model, {
                              {Cell{0, 4}, GemType::Red},
                              {Cell{1, 4}, GemType::Red},
                              {Cell{2, 4}, GemType::Green},
                              {Cell{3, 4}, GemType::Red},
                          });

        const bool committed = model.TrySwap(Cell{2, 4}, Cell{3, 4});
        EXPECT_TRUE(committed);
        // At least the four red gems were scored; the cascade refill may or may
        // not produce additional matches, so we only assert a lower bound.
        EXPECT_GE(model.GetScore(), 4);
    }

    TEST(Match3GameModelTest, TrySwapRevertsNonMatchingSwap) {
        Match3GameModel model;
        // Two isolated, differently-coloured cells: swap cannot create a match
        // because every other cell on the board is GemType::None.
        SetupBoard(model, {
                              {Cell{3, 3}, GemType::Red},
                              {Cell{4, 3}, GemType::Blue},
                          });

        // Snapshot the (mostly-empty) board so we can prove it was reverted.
        Vector<GemType> before;
        before.reserve(static_cast<size_t>(kBoardSize) * static_cast<size_t>(kBoardSize));
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                before.push_back(model.GetCellAt(Cell{col, row}));
            }
        }

        const bool committed = model.TrySwap(Cell{3, 3}, Cell{4, 3});
        EXPECT_FALSE(committed);
        EXPECT_EQ(model.GetScore(), 0);
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                EXPECT_TRUE(model.GetCellAt(Cell{col, row}) == before[static_cast<size_t>(row * kBoardSize + col)])
                    << "cell (" << static_cast<int>(col) << ", " << static_cast<int>(row) << ") was not reverted";
            }
        }
    }

    TEST(Match3GameModelTest, TrySwapRejectsNonAdjacent) {
        Match3GameModel model;
        SetupBoard(model, {
                              {Cell{0, 0}, GemType::Red},
                              {Cell{2, 0}, GemType::Red},
                              {Cell{1, 0}, GemType::Red},
                          });
        // Diagonal swap is not 4-neighbour adjacent: must fail without touching score.
        const bool committed = model.TrySwap(Cell{0, 0}, Cell{1, 1});
        EXPECT_FALSE(committed);
        EXPECT_EQ(model.GetScore(), 0);
    }

    TEST(Match3GameModelTest, CascadeIncreasesScore) {
        Match3GameModel model;
        // Construct a position where removing the bottom row triggers gravity
        // that brings down a second matchable group.
        //
        // Column 0:   . . . . . . R G        (top row 0 .. bottom row 7)
        // Column 1:   . . . . . . R G
        // Column 2:   . . . . . . R G
        // Column 3:   . . . . . . . B
        // Column 4:   . . . . . . . B
        // Column 5:   . . . . . . . B
        //
        // Bottom row [G, G, G, B, B, B] is two horizontal triples: the first
        // resolve pass scores 6 cells, then gravity drops the [R, R, R] from
        // row 6 down into row 7, forming a fresh horizontal triple that the
        // second cascade pass scores again.
        SetupBoard(model, {
                              {Cell{0, 6}, GemType::Red},
                              {Cell{1, 6}, GemType::Red},
                              {Cell{2, 6}, GemType::Red},
                              {Cell{0, 7}, GemType::Green},
                              {Cell{1, 7}, GemType::Green},
                              {Cell{2, 7}, GemType::Green},
                              {Cell{3, 7}, GemType::Blue},
                              {Cell{4, 7}, GemType::Blue},
                              {Cell{5, 7}, GemType::Blue},
                          });

        const int removed = model.ResolveCascade();
        // First pass scores 6 (two triples in the bottom row); second pass
        // scores at least 3 more (the dropped reds). Refill randomness can add
        // more, so >= is the tight lower bound.
        EXPECT_GE(removed, 9);
        EXPECT_GE(model.GetScore(), 9);
    }

}  // namespace BECore::Match3::Tests
