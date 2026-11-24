#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// Error codes of the game methods of the control API (see Games/CLAUDE.md). A game adds its own where it needs them.
    namespace GameControlErrors {
        inline constexpr StringView kNoActiveGame = "no_active_game";
        inline constexpr StringView kNotControllable = "not_controllable";
        inline constexpr StringView kUnknownGame = "unknown_game";
        inline constexpr StringView kNoActiveModel = "no_active_model";
        inline constexpr StringView kIllegalAction = "illegal_action";
        inline constexpr StringView kUnsupported = "unsupported";
        inline constexpr StringView kReplayNeedsManualClock = "replay_needs_manual_clock";
    }  // namespace GameControlErrors

    /// Events that the game layer pushes to the clients of the control API
    namespace GameControlEvents {
        /// {"game", "version", "state"}: the state version of the active game moved (published at the end of a frame)
        inline constexpr StringView kStateChanged = "state_changed";
    }  // namespace GameControlEvents

    /**
     * @brief What a game offers to an external controller (a program or a model): its state, its legal actions and a way to act.
     *
     * IGame::AsControllable() hands it out. The methods of GameControl (get_state, list_actions, perform_action, ...) call it on
     * the main thread, in frame stage 1b, so a call never races with the game's logic.
     *
     * Everything that comes from the client (the parameters of PerformAction, Reset and SetState) is untrusted: read the `JsonView` with
     * the checked helpers of BECore/Control/ControlParams.h (a wrong type, an escape the helper cannot honour or an integer past 64 bits reads as
     * nothing), and answer a bad value with an error, not an assert. A view is valid for the call only.
     *
     * Every method that writes (WriteState, WriteActions, PerformAction, WriteReplayStart) writes **exactly one value** into the writer it is given
     * (`Null()` for "nothing to say") and returns success, or returns an error, and whatever it wrote is then dropped. A method that returns success
     * and leaves the value incomplete is an `internal_error` of the call, and nothing of it is recorded or sent.
     *
     * Not a BE_CLASS: a game derives from IGame first and owns an adapter that implements this interface.
     */
    class IControllableGame {
    public:
        virtual ~IControllableGame() = default;

        /// Writes the whole state a controller needs to choose an action (the board, the score, whose turn it is, ...): what `get_state`, the `state_changed` push and
        /// replay read
        [[nodiscard]] virtual std::expected<void, ControlError> WriteState(JsonWriter& writer) const = 0;

        /// Writes a JSON array with every action that is legal now; each element is accepted as is by PerformAction()
        [[nodiscard]] virtual std::expected<void, ControlError> WriteActions(JsonWriter& writer) const = 0;

        /**
         * @brief Applies @p action, an object that the game described in WriteActions(), and writes what it did into @p outcome (for example the points it gave).
         *
         * @return an error instead: illegal_action for an action that is not legal now, invalid_params for one that is malformed.
         */
        virtual std::expected<void, ControlError> PerformAction(JsonView action, JsonWriter& outcome) = 0;

        /// Starts a new game. @p options is an object whose members are the game's (e.g. "seed"); it may be empty
        virtual std::expected<void, ControlError> Reset(JsonView options) = 0;

        /// Loads a state (a game's own form, e.g. {"fen": ".."} for Chess). The default answers unsupported
        virtual std::expected<void, ControlError> SetState(JsonView state);

        /// Grows with every change a controller has to see (a new game, a committed action); never goes back
        [[nodiscard]] virtual uint64_t GetStateVersion() const = 0;

        /**
         * @brief Whether a recorded session of this game plays back to the same state (Games/CLAUDE.md, "Record and replay").
         *
         * True only when all randomness comes from the seed of Reset() and delayed tasks follow game time. The default is false.
         */
        [[nodiscard]] virtual bool IsReplayable() const;

        /// Writes the options of Reset() that start this same game again, the seed it drew included, as one object; read right after a Reset(). The default writes `{}`
        [[nodiscard]] virtual std::expected<void, ControlError> WriteReplayStart(JsonWriter& writer) const;
    };

}  // namespace BECore
