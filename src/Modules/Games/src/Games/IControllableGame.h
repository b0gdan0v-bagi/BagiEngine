#pragma once

#include <BECore/Control/Json.h>

namespace BECore {

    /// Error codes of the game methods of the control API (see Games/CLAUDE.md). A game adds its own where it needs them.
    namespace GameControlErrors {
        inline constexpr eastl::string_view kNoActiveGame = "no_active_game";
        inline constexpr eastl::string_view kNotControllable = "not_controllable";
        inline constexpr eastl::string_view kUnknownGame = "unknown_game";
        inline constexpr eastl::string_view kNoActiveModel = "no_active_model";
        inline constexpr eastl::string_view kIllegalAction = "illegal_action";
        inline constexpr eastl::string_view kUnsupported = "unsupported";
    }  // namespace GameControlErrors

    /// Events that the game layer pushes to the clients of the control API
    namespace GameControlEvents {
        /// {"game", "version", "state"}: the state version of the active game moved (published at the end of a frame)
        inline constexpr eastl::string_view kStateChanged = "state_changed";
    }  // namespace GameControlEvents

    /**
     * @brief What a game offers to an external controller (a program or a model): its state, its legal actions and a way to act.
     *
     * IGame::AsControllable() hands it out. The methods of GameControl (get_state, list_actions, perform_action, ...) call it on
     * the main thread, in frame stage 1b, so a call never races with the game's logic. The game decides what its state and its
     * actions look like; both are plain JSON, readable by a model.
     *
     * Everything that comes from the client (the parameters of PerformAction, Reset and SetState) is untrusted: read it with the
     * checked helpers of BECore/Control/Json.h, never with the throwing accessors of nlohmann (BECore/Control/CLAUDE.md), and
     * answer a bad value with an error, not an assert.
     *
     * Not a BE_CLASS: a game derives from IGame first and owns an adapter that implements this interface.
     */
    class IControllableGame {
    public:
        virtual ~IControllableGame() = default;

        /// The whole state a controller needs to choose an action (the board, the score, whose turn it is, ...)
        [[nodiscard]] virtual std::expected<Json, ControlError> GetState() const = 0;

        /// A JSON array with every action that is legal now; each element is accepted as is by PerformAction()
        [[nodiscard]] virtual std::expected<Json, ControlError> GetActions() const = 0;

        /**
         * @brief Applies @p action, an object that the game described in GetActions().
         *
         * @return What the action did (for example the points it gave), or an error: illegal_action for an action that
         *         is not legal now, invalid_params for one that is malformed.
         */
        virtual std::expected<Json, ControlError> PerformAction(const Json& action) = 0;

        /// Starts a new game. @p options is an object whose members are the game's (Match3: "seed"); it may be empty
        virtual std::expected<void, ControlError> Reset(const Json& options) = 0;

        /// Loads a state (Chess: {"fen": ".."}). The default answers unsupported
        virtual std::expected<void, ControlError> SetState(const Json& state);

        /// Grows with every change a controller has to see (a new game, a committed action); never goes back
        [[nodiscard]] virtual uint64_t GetStateVersion() const = 0;
    };

}  // namespace BECore
