#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    class ControlService;
    class IControllableGame;

    /// The control root of the active game (`ControlService::Roots()`); no active game is an empty reference
    inline constexpr StringView kGameRootName = "game";

    /**
     * @brief Registers the game methods of the control API in @p service.
     *
     * The methods are list_games, switch_game, reset, get_state, list_actions, perform_action, set_state and run_until (frames until
     * a condition of the state, its version or a pushed event holds); their params and results are in Games/CLAUDE.md ("Control API").
     * The recording methods record_start and record_stop (ReplayControl.h) are registered with them.
     *
     * It also installs a frame end hook that pushes {"event":"state_changed","data":{"game","version","state"}} when the state
     * version of the active game moved (an action, a reset, a click of a person); see GameControlEvents.
     *
     * It registers the control root `game`, the active game as a `TypedRef`, for the by-name methods.
     *
     * The methods act on GameManager's active game, looked up on every call. Errors: no_active_game, not_controllable,
     * unknown_game, plus the game's own (GameControlErrors in IControllableGame.h). GameManager::Initialize() calls this.
     * Registering twice replaces the handlers.
     */
    void RegisterGameControl(ControlService& service);

    /**
     * @brief The three calls that change a game, with the report to SessionRecorder that makes them part of a recording.
     *
     * They call the adapter and, only when it accepts, report (a refused call is never recorded), before the caller answers. The
     * control methods reset, perform_action and set_state go through them, and so does a person's input that reaches the same
     * adapter calls. ControlledReset reports IControllableGame::GetReplayStart(), not @p options, so a seed the game drew is in the file.
     * A game never reports a call twice: input that came through a control method is already reported.
     */
    std::expected<void, ControlError> ControlledReset(IControllableGame& game, const Json& options);
    std::expected<Json, ControlError> ControlledAction(IControllableGame& game, const Json& action);
    std::expected<void, ControlError> ControlledSetState(IControllableGame& game, const Json& state);

}  // namespace BECore
