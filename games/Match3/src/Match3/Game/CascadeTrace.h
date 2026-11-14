#pragma once

#include <Match3/Game/Match3Board.h>
#include <Match3/Game/Match3Types.h>

namespace BECore::Match3 {

    /// A gem that kept its place on the board and slid down in one column during gravity
    struct GemFall {
        int8_t col = 0;
        int8_t fromRow = 0;
        int8_t toRow = 0;
    };

    /**
     * @brief A gem that the refill put into a column. @c fromRow is negative: the row above the top edge the gem
     *        comes from, so the new gems of one column keep their spacing and all fall the same number of rows.
     */
    struct GemRefill {
        int8_t col = 0;
        int8_t toRow = 0;
        int8_t fromRow = 0;
        GemType gem = GemType::None;
    };

    /// One round of the cascade: the matched cells go, the gems above fall, the holes at the top are refilled
    struct CascadeStep {
        Vector<Cell> removed;
        Vector<GemFall> falls;  ///< In the order gravity moved them: column by column, in each column the lowest gem first
        Vector<GemRefill> refills;
    };

    /**
     * @brief What one committed swap did, as plain data (no time, no positions in pixels): the two swapped cells and
     *        every round of the cascade. The model records it so that a component can show how the board got from
     *        the one before the swap to the one after it; the end board alone cannot tell (the refilled gems come from
     *        the RNG).
     */
    struct SwapTrace {
        uint64_t version = 0;  ///< The model version this trace belongs to (`Match3GameModel::GetVersion()` after the swap)
        Cell a;
        Cell b;
        Vector<CascadeStep> steps;
    };

    /// Apply a trace to the board BEFORE the swap and return the board AFTER it (swap, then step by step)
    [[nodiscard]] Match3Board ReplayTrace(Match3Board board, const SwapTrace& trace);

}  // namespace BECore::Match3
