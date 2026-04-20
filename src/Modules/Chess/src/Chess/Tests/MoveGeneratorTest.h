#pragma once

#include <BECore/Tests/ITest.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess::Tests {

    /**
     * @brief Pseudo-legal move generation coverage for ChessBoard + MoveGenerator.
     *
     * MVP scope — same restrictions as the gameplay layer: no castling, no
     * en-passant, no pawn promotion, no check/checkmate. We only verify that
     * each piece kind reaches the squares allowed by basic movement rules and
     * is correctly blocked by friendly / enemy pieces.
     */
    class MoveGeneratorTest : public BECore::Tests::ITest {
        BE_CLASS(MoveGeneratorTest)
    public:
        MoveGeneratorTest() = default;
        ~MoveGeneratorTest() override = default;

        bool Run() override;
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

    private:
        static constexpr void TestCompileTime() {
            // Square index round-trip is constexpr.
            static_assert(Square::IsInside(0, 0));
            static_assert(Square::IsInside(7, 7));
            static_assert(!Square::IsInside(-1, 0));
            static_assert(!Square::IsInside(0, 8));
            static_assert(Square{3, 4}.Index() == 4 * 8 + 3);
            static_assert(Square::FromIndex(35) == (Square{3, 4}));
        }

        bool TestPawnFromStart();
        bool TestPawnBlocked();
        bool TestPawnCapture();
        bool TestKnightOpening();
        bool TestRookOpenAndBlocked();
        bool TestBishopDiagonals();
        bool TestQueenCombination();
        bool TestKingNeighbors();
        bool TestStartingPositionMoveCounts();
        bool TestCaptureRemovesPiece();
    };

    static_assert(BECore::Tests::ValidTest<MoveGeneratorTest>);

}  // namespace BECore::Chess::Tests
