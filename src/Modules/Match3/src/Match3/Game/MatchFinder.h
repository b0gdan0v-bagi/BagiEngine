#pragma once

#include <Match3/Game/Match3Types.h>

namespace BECore::Match3 {

    class Match3Board;

    /**
     * @brief Stateless detector of horizontal/vertical runs of >= kMinMatchLength
     *        identical, non-empty gems on a Match3Board.
     */
    class MatchFinder {
    public:
        /**
         * @brief Return every cell that participates in any horizontal or vertical
         *        match of length kMinMatchLength or more. Each cell appears at
         *        most once even if it sits in both a row and column match.
         *
         * Empty cells (GemType::None) are never reported.
         */
        [[nodiscard]] static eastl::vector<Cell> FindMatches(const Match3Board& board);

        /**
         * @brief True if the board contains at least one match. Faster than
         *        FindMatches() when only the existence is needed.
         */
        [[nodiscard]] static bool HasAnyMatch(const Match3Board& board);
    };

}  // namespace BECore::Match3
