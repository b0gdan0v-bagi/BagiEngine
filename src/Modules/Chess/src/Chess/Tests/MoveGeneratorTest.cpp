#include "MoveGeneratorTest.h"

#include <Chess/Game/ChessBoard.h>
#include <Chess/Game/MoveGenerator.h>
#include <EASTL/algorithm.h>
#include <Generated/MoveGeneratorTest.gen.hpp>

namespace BECore::Chess::Tests {

    namespace {

        /// True if @p target appears anywhere in @p moves.
        bool Contains(const eastl::vector<Square>& moves, Square target) {
            return eastl::find(moves.begin(), moves.end(), target) != moves.end();
        }

    }  // namespace

    bool MoveGeneratorTest::Run() {
        TestCompileTime();
        LOG_INFO("[MoveGeneratorTest] Compile-time tests OK");
        LOG_INFO("[MoveGeneratorTest] Starting runtime tests...");

        ASSERT(TestPawnFromStart());
        ASSERT(TestPawnBlocked());
        ASSERT(TestPawnCapture());
        ASSERT(TestKnightOpening());
        ASSERT(TestRookOpenAndBlocked());
        ASSERT(TestBishopDiagonals());
        ASSERT(TestQueenCombination());
        ASSERT(TestKingNeighbors());
        ASSERT(TestStartingPositionMoveCounts());
        ASSERT(TestCaptureRemovesPiece());

        LOG_INFO("[MoveGeneratorTest] All tests passed!");
        return true;
    }

    bool MoveGeneratorTest::TestPawnFromStart() {
        ChessBoard board;
        board.ResetToStart();
        // e2 = file 4, rank 1.
        const auto moves = MoveGenerator::Generate(board, Square{4, 1});
        ASSERT(moves.size() == 2);
        ASSERT(Contains(moves, (Square{4, 2})));
        ASSERT(Contains(moves, (Square{4, 3})));
        return true;
    }

    bool MoveGeneratorTest::TestPawnBlocked() {
        ChessBoard board;
        board.Set(Square{4, 1}, Piece{PieceType::Pawn, PieceColor::White});
        board.Set(Square{4, 2}, Piece{PieceType::Pawn, PieceColor::Black});
        const auto moves = MoveGenerator::Generate(board, Square{4, 1});
        ASSERT(moves.empty());
        return true;
    }

    bool MoveGeneratorTest::TestPawnCapture() {
        ChessBoard board;
        board.Set(Square{4, 3}, Piece{PieceType::Pawn, PieceColor::White});
        board.Set(Square{3, 4}, Piece{PieceType::Pawn, PieceColor::Black});
        board.Set(Square{5, 4}, Piece{PieceType::Pawn, PieceColor::Black});
        const auto moves = MoveGenerator::Generate(board, Square{4, 3});
        ASSERT(Contains(moves, (Square{4, 4})));  // forward push
        ASSERT(Contains(moves, (Square{3, 4})));  // diagonal capture left
        ASSERT(Contains(moves, (Square{5, 4})));  // diagonal capture right
        ASSERT(moves.size() == 3);                // not 4: not on starting rank, no double push
        return true;
    }

    bool MoveGeneratorTest::TestKnightOpening() {
        ChessBoard board;
        board.ResetToStart();
        // b1 knight = file 1, rank 0. Should reach a3 and c3 only (d2 blocked by pawn).
        const auto moves = MoveGenerator::Generate(board, Square{1, 0});
        ASSERT(moves.size() == 2);
        ASSERT(Contains(moves, (Square{0, 2})));
        ASSERT(Contains(moves, (Square{2, 2})));
        return true;
    }

    bool MoveGeneratorTest::TestRookOpenAndBlocked() {
        ChessBoard board;
        board.Set(Square{4, 4}, Piece{PieceType::Rook, PieceColor::White});
        // Lone rook in the middle of an empty board reaches 14 squares (7 + 7).
        const auto moves = MoveGenerator::Generate(board, Square{4, 4});
        ASSERT(moves.size() == 14);

        // Block one direction with a friendly piece — that ray is fully cut.
        board.Set(Square{4, 6}, Piece{PieceType::Pawn, PieceColor::White});
        const auto blocked = MoveGenerator::Generate(board, Square{4, 4});
        ASSERT(!Contains(blocked, (Square{4, 6})));
        ASSERT(!Contains(blocked, (Square{4, 7})));
        ASSERT(Contains(blocked, (Square{4, 5})));

        // Replace with an enemy piece — rook can capture it but not pass through.
        board.Set(Square{4, 6}, Piece{PieceType::Pawn, PieceColor::Black});
        const auto enemyBlock = MoveGenerator::Generate(board, Square{4, 4});
        ASSERT(Contains(enemyBlock, (Square{4, 6})));
        ASSERT(!Contains(enemyBlock, (Square{4, 7})));
        return true;
    }

    bool MoveGeneratorTest::TestBishopDiagonals() {
        ChessBoard board;
        board.Set(Square{3, 3}, Piece{PieceType::Bishop, PieceColor::White});
        const auto moves = MoveGenerator::Generate(board, Square{3, 3});
        // From d4 a lone bishop reaches 13 squares.
        ASSERT(moves.size() == 13);
        ASSERT(Contains(moves, (Square{0, 0})));
        ASSERT(Contains(moves, (Square{6, 0})));
        ASSERT(Contains(moves, (Square{0, 6})));
        ASSERT(Contains(moves, (Square{7, 7})));
        // No straight moves.
        ASSERT(!Contains(moves, (Square{3, 5})));
        ASSERT(!Contains(moves, (Square{5, 3})));
        return true;
    }

    bool MoveGeneratorTest::TestQueenCombination() {
        ChessBoard board;
        board.Set(Square{3, 3}, Piece{PieceType::Queen, PieceColor::White});
        const auto moves = MoveGenerator::Generate(board, Square{3, 3});
        // Lone queen on d4 reaches 27 squares (rook 14 + bishop 13).
        ASSERT(moves.size() == 27);
        return true;
    }

    bool MoveGeneratorTest::TestKingNeighbors() {
        ChessBoard board;
        board.Set(Square{4, 4}, Piece{PieceType::King, PieceColor::White});
        const auto moves = MoveGenerator::Generate(board, Square{4, 4});
        ASSERT(moves.size() == 8);

        // Corner — king reaches 3 squares.
        ChessBoard corner;
        corner.Set(Square{0, 0}, Piece{PieceType::King, PieceColor::White});
        const auto cornerMoves = MoveGenerator::Generate(corner, Square{0, 0});
        ASSERT(cornerMoves.size() == 3);
        ASSERT(Contains(cornerMoves, (Square{1, 0})));
        ASSERT(Contains(cornerMoves, (Square{0, 1})));
        ASSERT(Contains(cornerMoves, (Square{1, 1})));
        return true;
    }

    bool MoveGeneratorTest::TestStartingPositionMoveCounts() {
        ChessBoard board;
        board.ResetToStart();
        // Sum of all White pseudo-legal moves at the opening position is 20.
        size_t total = 0;
        for (int rank = 0; rank < 2; ++rank) {
            for (int file = 0; file < 8; ++file) {
                total += MoveGenerator::Generate(board, Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}).size();
            }
        }
        ASSERT(total == 20);
        return true;
    }

    bool MoveGeneratorTest::TestCaptureRemovesPiece() {
        ChessBoard board;
        board.Set(Square{4, 3}, Piece{PieceType::Pawn, PieceColor::White});
        board.Set(Square{3, 4}, Piece{PieceType::Pawn, PieceColor::Black});
        ASSERT(!board.Get(Square{3, 4}).IsEmpty());
        board.Move(Square{4, 3}, Square{3, 4});
        ASSERT(board.Get(Square{4, 3}).IsEmpty());
        const Piece captured = board.Get(Square{3, 4});
        ASSERT(captured.type == PieceType::Pawn);
        ASSERT(captured.color == PieceColor::White);
        return true;
    }

}  // namespace BECore::Chess::Tests
