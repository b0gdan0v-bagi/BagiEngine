#include "Match3Board.h"

namespace BECore::Match3 {

    void Match3Board::Clear() noexcept {
        _cells.fill(GemType::None);
    }

    GemType Match3Board::Get(Cell c) const noexcept {
        if (!c.IsInside()) {
            return GemType::None;
        }
        return _cells[static_cast<size_t>(c.Index())];
    }

    GemType Match3Board::Get(int col, int row) const noexcept {
        return Get(Cell{static_cast<int8_t>(col), static_cast<int8_t>(row)});
    }

    void Match3Board::Set(Cell c, GemType g) noexcept {
        if (!c.IsInside()) {
            return;
        }
        _cells[static_cast<size_t>(c.Index())] = g;
    }

    void Match3Board::Set(int col, int row, GemType g) noexcept {
        Set(Cell{static_cast<int8_t>(col), static_cast<int8_t>(row)}, g);
    }

    void Match3Board::Swap(Cell a, Cell b) noexcept {
        if (!a.IsInside() || !b.IsInside()) {
            return;
        }
        const auto ai = static_cast<size_t>(a.Index());
        const auto bi = static_cast<size_t>(b.Index());
        const GemType tmp = _cells[ai];
        _cells[ai] = _cells[bi];
        _cells[bi] = tmp;
    }

}  // namespace BECore::Match3
