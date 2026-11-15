#pragma once

namespace BECore {

    class ControlService;

    /**
     * @brief Registers the game methods of the control API in @p service.
     *
     * The methods are list_games, switch_game, reset, get_state, list_actions, perform_action and set_state; their params and
     * results are in Games/CLAUDE.md ("Control API").
     *
     * It also installs a frame end hook that pushes {"event":"state_changed","data":{"game","version","state"}} when the state
     * version of the active game moved (an action, a reset, a click of a person); see GameControlEvents.
     *
     * The methods act on GameManager's active game, looked up on every call. Errors: no_active_game, not_controllable,
     * unknown_game, plus the game's own (GameControlErrors in IControllableGame.h). GameManager::Initialize() calls this.
     * Registering twice replaces the handlers.
     */
    void RegisterGameControl(ControlService& service);

}  // namespace BECore
