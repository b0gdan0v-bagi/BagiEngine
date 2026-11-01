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
         * @param seed Deterministic RNG seed. Same seed -> same starting board, on every platform: the gems come from
         *             std::mt19937 (its sequence is fixed by the standard) through a plain modulo, not from a std
         *             distribution (those differ between standard libraries). The same seed and the same swaps give
         *             the same board and score, which is what a replay or an external controller relies on.
         */
        void Reset(uint32_t seed = 0);

        /// Seed of the last Reset()
        [[nodiscard]] uint32_t GetSeed() const noexcept {
            return _seed;
        }

        /// Counts the changes a controller has to see: every Reset() and every committed swap. Never goes back
        [[nodiscard]] uint64_t GetVersion() const noexcept {
            return _version;
        }

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
         * @brief Would TrySwap(a, b) commit? Does not change the model (it tries the swap on a copy of the board).
         *
         * False for cells outside the board and for cells that are not 4-neighbour adjacent.
         */
        [[nodiscard]] bool WouldSwapMatch(Cell a, Cell b) const;

        /**
         * @brief Every swap that TrySwap would commit, each pair once: a cell with its right and its lower neighbour
         *        (`first` is the left or upper cell). Row by row, left to right, right neighbour before lower.
         */
        [[nodiscard]] eastl::vector<eastl::pair<Cell, Cell>> GetLegalSwaps() const;

        /// True when @p a and @p b are 4-neighbours (they differ by one in exactly one coordinate)
        [[nodiscard]] static bool IsAdjacent(Cell a, Cell b) noexcept;

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
        [[nodiscard]] GemType RandomGem();
        void GenerateInitialBoard();
        void ApplyGravity();
        void RefillTop();

        Match3Board _board;
        eastl::optional<Cell> _selected;
        int _score = 0;
        uint32_t _seed = 0;
        uint64_t _version = 0;
        std::mt19937 _rng;
    };

}  // namespace BECore::Match3
