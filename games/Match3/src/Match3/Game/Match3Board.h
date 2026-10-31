#pragma once

#include <Match3/Game/Match3Types.h>

namespace BECore::Match3 {

    /**
     * @brief Pure 8x8 grid of GemType. No rules — just storage and basic accessors.
     *
     * Cells are addressed by (col, row); both run 0..kBoardSize-1.
     */
    class Match3Board {
    public:
        Match3Board() = default;

        /// Wipe every cell to GemType::None.
        void Clear() noexcept;

        [[nodiscard]] GemType Get(Cell c) const noexcept;
        [[nodiscard]] GemType Get(int col, int row) const noexcept;
        void Set(Cell c, GemType g) noexcept;
        void Set(int col, int row, GemType g) noexcept;

        /// Swap the contents of two cells. No adjacency or legality check — the
        /// caller is expected to validate via Match3GameModel.
        void Swap(Cell a, Cell b) noexcept;

        [[nodiscard]] bool IsEmpty(Cell c) const noexcept {
            return Get(c) == GemType::None;
        }

    private:
        eastl::array<GemType, static_cast<size_t>(kBoardSize) * static_cast<size_t>(kBoardSize)> _cells{};
    };

}  // namespace BECore::Match3
