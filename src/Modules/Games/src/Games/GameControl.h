#pragma once

namespace BECore {

    class ControlService;

    /**
     * @brief Registers the game methods of the control API in @p service.
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | list_games | none | {"active": name or null, "games": [{"name", "scene", "controllable"}]} |
     * | switch_game | {"name"} | {"active": name} |
     * | reset | the game's options (e.g. a seed) | {"game", "version"} (a game that is not controllable gets a plain IGame::Reset and no version) |
     * | get_state | none | {"game", "version", "state"} |
     * | list_actions | none | {"game", "version", "actions": [..]} |
     * | perform_action | the action object itself, as listed by list_actions | {"game", "version", "outcome"} |
     * | set_state | the game's state (a game's own form, e.g. {"fen"} for Chess) | {"game", "version"} |
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
