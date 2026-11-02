#pragma once

#include <Games/IControllableGame.h>
#include <Match3/Game/Match3GameModel.h>

namespace BECore::Match3 {

    /// The board a controller drives: the model and the seed that a Reset() without options uses
    struct Match3ControlTarget {
        Match3GameModel* model = nullptr;  // null: no board is active (its scene is not the active one)
        uint32_t defaultSeed = 0;
        /// Called after a controller started a game (a successful Reset or action): the game shows its board, so a screenshot shows what is being played.
        /// Match3Game switches from the main menu to the scene of the board. Empty in a test that has no scenes.
        std::function<void()> enterGame;
    };

    /**
     * @brief The external control API of Match3: the state of the board, the swaps that are legal now, and a way to swap.
     *
     * The model belongs to Match3BoardComponent, which exists only while the Match3 scene is active, so the adapter asks
     * a provider for it on every call (Match3Game hands it the active component; a test hands it its own model).
     * Without a model every method answers no_active_model.
     *
     * State: {"size": 8, "board": ["ROYGBPRO", ... 8 rows, row 0 on top], "score", "seed", "legalSwapCount", "status"}.
     * A cell is one letter: R red, O orange, Y yellow, G green, B blue, P purple, '.' empty. status is "playing" or "no_moves".
     * Actions: {"type": "swap", "from": {"col", "row"}, "to": {"col", "row"}}, every legal pair once; PerformAction accepts the
     * cells in either order and answers {"scoreGained", "score"}. Reset takes an optional {"seed": 0..4294967295}.
     *
     * A Reset and an action that went through call the target's `enterGame`: a controller that plays is in the game, not in its menu.
     * GetState and GetActions do not.
     */
    class Match3GameControl : public IControllableGame {
    public:
        using TargetProvider = std::function<Match3ControlTarget()>;

        explicit Match3GameControl(TargetProvider provider);

        [[nodiscard]] std::expected<Json, ControlError> GetState() const override;
        [[nodiscard]] std::expected<Json, ControlError> GetActions() const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        TargetProvider _provider;
    };

}  // namespace BECore::Match3
