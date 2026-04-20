#pragma once

namespace BECore::Match3 {

    /// Six gem colors plus None for empty cells (used during cascades).
    CORE_ENUM(GemType, uint8_t, None, Red, Orange, Yellow, Green, Blue, Purple)

    /// Square board side length. The board is always kBoardSize x kBoardSize.
    inline constexpr int8_t kBoardSize = 8;

    /// Number of playable gem types (excludes GemType::None).
    inline constexpr int8_t kGemTypeCount = 6;

    /// Minimum number of identical-colored gems in a row/column to form a match.
    inline constexpr int8_t kMinMatchLength = 3;

    /**
     * @brief Board cell coordinate. Origin is the top-left corner: col 0, row 0.
     *
     * Stored as int8 so off-board math (Cell{-1, 8}) is representable without UB;
     * use IsInside() to validate.
     */
    struct Cell {
        int8_t col = 0;
        int8_t row = 0;

        constexpr bool operator==(const Cell& other) const noexcept {
            return col == other.col && row == other.row;
        }
        constexpr bool operator!=(const Cell& other) const noexcept {
            return !(*this == other);
        }

        [[nodiscard]] static constexpr bool IsInside(int col, int row) noexcept {
            return col >= 0 && col < kBoardSize && row >= 0 && row < kBoardSize;
        }

        [[nodiscard]] constexpr bool IsInside() const noexcept {
            return IsInside(col, row);
        }

        [[nodiscard]] constexpr int Index() const noexcept {
            return row * kBoardSize + col;
        }

        static constexpr Cell FromIndex(int index) noexcept {
            return Cell{static_cast<int8_t>(index % kBoardSize), static_cast<int8_t>(index / kBoardSize)};
        }
    };

}  // namespace BECore::Match3
