#pragma once

#include <BECore/Tests/ITest.h>
#include <Match3/Game/Match3Types.h>

namespace BECore::Match3::Tests {

    /**
     * @brief Coverage for Match3Board / MatchFinder / Match3GameModel.
     *
     * MVP scope:
     *   - Initial board has no preexisting matches (deterministic seed).
     *   - MatchFinder detects horizontal, vertical and intersecting matches.
     *   - TrySwap accepts matching swaps and reverts non-matching ones.
     *   - Cascade increments the score across multiple resolve passes.
     */
    class Match3GameModelTest : public BECore::Tests::ITest {
        BE_CLASS(Match3GameModelTest)
    public:
        Match3GameModelTest() = default;
        ~Match3GameModelTest() override = default;

        bool Run() override;
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

    private:
        static constexpr void TestCompileTime() {
            static_assert(Cell::IsInside(0, 0));
            static_assert(Cell::IsInside(kBoardSize - 1, kBoardSize - 1));
            static_assert(!Cell::IsInside(-1, 0));
            static_assert(!Cell::IsInside(0, kBoardSize));
            static_assert(Cell{3, 4}.Index() == 4 * kBoardSize + 3);
            static_assert(Cell::FromIndex(3 * kBoardSize + 5) == (Cell{5, 3}));
            static_assert(kGemTypeCount == 6);
        }

        bool TestInitialBoardHasNoMatches();
        bool TestFindMatchesHorizontalThree();
        bool TestFindMatchesVerticalThree();
        bool TestFindMatchesLShape();
        bool TestTrySwapAcceptsMatchingSwap();
        bool TestTrySwapRevertsNonMatchingSwap();
        bool TestTrySwapRejectsNonAdjacent();
        bool TestCascadeIncreasesScore();
    };

    static_assert(BECore::Tests::ValidTest<Match3GameModelTest>);

}  // namespace BECore::Match3::Tests
