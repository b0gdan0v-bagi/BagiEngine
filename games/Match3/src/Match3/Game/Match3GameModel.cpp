#include "Match3GameModel.h"

#include <Match3/Game/MatchFinder.h>

namespace BECore::Match3 {

    Match3GameModel::Match3GameModel() {
        Reset(0);
    }

    void Match3GameModel::Reset(uint32_t seed) {
        _seed = seed;
        _rng.seed(seed);
        _score = 0;
        _selected.reset();
        GenerateInitialBoard();
        ++_version;
    }

    void Match3GameModel::OnCellClicked(Cell c) {
        if (!c.IsInside() || _board.Get(c) == GemType::None) {
            return;
        }

        if (!_selected.has_value()) {
            _selected = c;
            return;
        }

        const Cell prev = *_selected;
        if (prev == c) {
            _selected.reset();
            return;
        }

        if (IsAdjacent(prev, c)) {
            (void)TrySwap(prev, c);
            _selected.reset();
            return;
        }

        _selected = c;
    }

    bool Match3GameModel::TrySwap(Cell a, Cell b) {
        if (!a.IsInside() || !b.IsInside() || !IsAdjacent(a, b)) {
            return false;
        }

        _board.Swap(a, b);
        if (!MatchFinder::HasAnyMatch(_board)) {
            _board.Swap(a, b);
            return false;
        }

        ResolveCascade();
        ++_version;
        return true;
    }

    bool Match3GameModel::WouldSwapMatch(Cell a, Cell b) const {
        if (!a.IsInside() || !b.IsInside() || !IsAdjacent(a, b)) {
            return false;
        }

        Match3Board trial = _board;
        trial.Swap(a, b);
        return MatchFinder::HasAnyMatch(trial);
    }

    eastl::vector<eastl::pair<Cell, Cell>> Match3GameModel::GetLegalSwaps() const {
        eastl::vector<eastl::pair<Cell, Cell>> swaps;
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                const Cell from{col, row};
                const Cell right{static_cast<int8_t>(col + 1), row};
                const Cell below{col, static_cast<int8_t>(row + 1)};
                if (WouldSwapMatch(from, right)) {
                    swaps.emplace_back(from, right);
                }
                if (WouldSwapMatch(from, below)) {
                    swaps.emplace_back(from, below);
                }
            }
        }
        return swaps;
    }

    int Match3GameModel::ResolveCascade() {
        int totalRemoved = 0;
        while (true) {
            const auto matched = MatchFinder::FindMatches(_board);
            if (matched.empty()) {
                break;
            }
            for (const Cell c : matched) {
                _board.Set(c, GemType::None);
            }
            totalRemoved += static_cast<int>(matched.size());
            _score += static_cast<int>(matched.size());

            ApplyGravity();
            RefillTop();
        }
        return totalRemoved;
    }

    bool Match3GameModel::IsAdjacent(Cell a, Cell b) noexcept {
        const int dc = static_cast<int>(a.col) - static_cast<int>(b.col);
        const int dr = static_cast<int>(a.row) - static_cast<int>(b.row);
        const int adc = dc < 0 ? -dc : dc;
        const int adr = dr < 0 ? -dr : dr;
        return (adc + adr) == 1;
    }

    GemType Match3GameModel::RandomGem() {
        // Not std::uniform_int_distribution: its output differs between standard libraries, mt19937's does not.
        // The modulo bias (6 does not divide 2^32) is below 1.5e-9 per gem, irrelevant for a game.
        return static_cast<GemType>(1 + _rng() % static_cast<uint32_t>(kGemTypeCount));
    }

    void Match3GameModel::GenerateInitialBoard() {
        _board.Clear();
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                while (true) {
                    const GemType candidate = RandomGem();
                    const bool horizontalRun = (col >= 2) && _board.Get(col - 1, row) == candidate && _board.Get(col - 2, row) == candidate;
                    const bool verticalRun = (row >= 2) && _board.Get(col, row - 1) == candidate && _board.Get(col, row - 2) == candidate;
                    if (!horizontalRun && !verticalRun) {
                        _board.Set(col, row, candidate);
                        break;
                    }
                }
            }
        }
    }

    void Match3GameModel::ApplyGravity() {
        // Convention: row 0 is the top of the board, gems fall toward higher row indices.
        for (int8_t col = 0; col < kBoardSize; ++col) {
            int8_t writeRow = kBoardSize - 1;
            for (int8_t row = kBoardSize - 1; row >= 0; --row) {
                const GemType g = _board.Get(col, row);
                if (g != GemType::None) {
                    if (writeRow != row) {
                        _board.Set(col, writeRow, g);
                        _board.Set(col, row, GemType::None);
                    }
                    --writeRow;
                }
            }
        }
    }

    void Match3GameModel::RefillTop() {
        for (int8_t col = 0; col < kBoardSize; ++col) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                if (_board.Get(col, row) == GemType::None) {
                    _board.Set(col, row, RandomGem());
                }
            }
        }
    }

}  // namespace BECore::Match3
