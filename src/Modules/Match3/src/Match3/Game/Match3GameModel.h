#pragma once

#include <EASTL/optional.h>
#include <Match3/Game/Match3Board.h>
#include <Match3/Game/Match3Types.h>
#include <random>

namespace BECore::Match3 {

    /**
     * @brief Stateful Bejeweled-style match game wrapper around Match3Board.
     *
     * Owns the board, the running score, the (at most one) currently selected
     * cell, and a deterministic RNG seeded by Reset(). All gameplay is exposed
     * through three calls:
     *
     *   - Reset(seed)     - new game, optionally deterministic.
     *   - OnCellClicked   - drives selection / swap flow from the UI.
     *   - TrySwap         - direct programmatic swap (used by tests + UI helper).
     *
     * The model never animates: a successful swap immediately resolves the
     * full cascade (remove -> gravity -> refill -> repeat) so callers see a
     * stable board after every public call.
     */
    class Match3GameModel {
    public:
        Match3GameModel();

        /**
         * @brief Restart the game.
         *
         * Generates a fresh board with no preexisting matches (each cell is
         * re-rolled until it does not equal the two cells immediately to the
         * left or above). Resets score and selection.
         *
         * @param seed Deterministic RNG seed. Same seed -> same starting board.
         */
        void Reset(uint32_t seed = 0);

        [[nodiscard]] const Match3Board& GetBoard() const noexcept {
            return _board;
        }
        [[nodiscard]] GemType GetCellAt(Cell c) const noexcept {
            return _board.Get(c);
        }
        [[nodiscard]] int GetScore() const noexcept {
            return _score;
        }
        [[nodiscard]] bool HasSelection() const noexcept {
            return _selected.has_value();
        }
        [[nodiscard]] eastl::optional<Cell> GetSelected() const noexcept {
            return _selected;
        }

        /**
         * @brief Click handler. Drives the two-step selection / swap flow:
         *
         *   1. No selection      + click in-bounds, non-empty -> select.
         *   2. Selection at S    + click on S                  -> deselect.
         *   3. Selection at S    + click on adjacent T         -> TrySwap(S, T)
         *      (selection always cleared, regardless of swap success).
         *   4. Selection at S    + click on non-adjacent       -> reselect at click.
         */
        void OnCellClicked(Cell c);

        /**
         * @brief Swap two cells if they are 4-neighbour adjacent.
         *
         * If the swap creates at least one match, the full cascade is resolved
         * immediately and the score is updated. If not, the swap is reverted
         * and the score is unchanged.
         *
         * @return true if the swap was committed (created a match).
         */
        bool TrySwap(Cell a, Cell b);

        /**
         * @brief Resolve any matches currently on the board (remove -> gravity
         *        -> refill -> repeat). Used by tests and Reset() recovery; the
         *        normal play path goes through TrySwap().
         *
         * @return number of cells removed across all cascade steps.
         */
        int ResolveCascade();

        /// Random-access setter for tests. Bypasses cascade resolution; the
        /// caller is responsible for keeping the board in a valid state.
        void SetCellForTest(Cell c, GemType g) noexcept {
            _board.Set(c, g);
        }
        void ResetScoreForTest() noexcept {
            _score = 0;
        }

    private:
        [[nodiscard]] static bool IsAdjacent(Cell a, Cell b) noexcept;
        [[nodiscard]] GemType RandomGem();
        void GenerateInitialBoard();
        void ApplyGravity();
        void RefillTop();

        Match3Board _board;
        eastl::optional<Cell> _selected;
        int _score = 0;
        std::mt19937 _rng;
    };

}  // namespace BECore::Match3
