#include "MatchFinder.h"

#include <Match3/Game/Match3Board.h>

namespace BECore::Match3 {

    namespace {

        // Bitset wide enough for kBoardSize x kBoardSize cells. eastl::bitset is
        // overkill for 64 bits — a single uint64_t is sufficient and keeps the
        // hot path branchless.
        static_assert(static_cast<size_t>(kBoardSize) * static_cast<size_t>(kBoardSize) <= 64, "MatchFinder dedup bitmask assumes board fits in 64 cells");

    }  // namespace

    eastl::vector<Cell> MatchFinder::FindMatches(const Match3Board& board) {
        eastl::vector<Cell> result;
        uint64_t marked = 0;

        for (int8_t row = 0; row < kBoardSize; ++row) {
            int8_t runStart = 0;
            for (int8_t col = 1; col <= kBoardSize; ++col) {
                const GemType prev = board.Get(runStart, row);
                const bool atEnd = (col == kBoardSize);
                const bool breaks = atEnd || board.Get(col, row) != prev || prev == GemType::None;
                if (breaks) {
                    if (prev != GemType::None && (col - runStart) >= kMinMatchLength) {
                        for (int8_t k = runStart; k < col; ++k) {
                            const Cell cell{k, row};
                            const uint64_t bit = uint64_t{1} << cell.Index();
                            if ((marked & bit) == 0) {
                                marked |= bit;
                                result.push_back(cell);
                            }
                        }
                    }
                    runStart = col;
                }
            }
        }

        for (int8_t col = 0; col < kBoardSize; ++col) {
            int8_t runStart = 0;
            for (int8_t row = 1; row <= kBoardSize; ++row) {
                const GemType prev = board.Get(col, runStart);
                const bool atEnd = (row == kBoardSize);
                const bool breaks = atEnd || board.Get(col, row) != prev || prev == GemType::None;
                if (breaks) {
                    if (prev != GemType::None && (row - runStart) >= kMinMatchLength) {
                        for (int8_t k = runStart; k < row; ++k) {
                            const Cell cell{col, k};
                            const uint64_t bit = uint64_t{1} << cell.Index();
                            if ((marked & bit) == 0) {
                                marked |= bit;
                                result.push_back(cell);
                            }
                        }
                    }
                    runStart = row;
                }
            }
        }

        return result;
    }

    bool MatchFinder::HasAnyMatch(const Match3Board& board) {
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col + kMinMatchLength <= kBoardSize; ++col) {
                const GemType g = board.Get(col, row);
                if (g == GemType::None) {
                    continue;
                }
                bool all = true;
                for (int8_t k = 1; k < kMinMatchLength; ++k) {
                    if (board.Get(col + k, row) != g) {
                        all = false;
                        break;
                    }
                }
                if (all) {
                    return true;
                }
            }
        }
        for (int8_t col = 0; col < kBoardSize; ++col) {
            for (int8_t row = 0; row + kMinMatchLength <= kBoardSize; ++row) {
                const GemType g = board.Get(col, row);
                if (g == GemType::None) {
                    continue;
                }
                bool all = true;
                for (int8_t k = 1; k < kMinMatchLength; ++k) {
                    if (board.Get(col, row + k) != g) {
                        all = false;
                        break;
                    }
                }
                if (all) {
                    return true;
                }
            }
        }
        return false;
    }

}  // namespace BECore::Match3
