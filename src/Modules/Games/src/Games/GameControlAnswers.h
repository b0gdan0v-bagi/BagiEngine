#pragma once

#include <BECore/Control/ControlClock.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// One game of `list_games`
    struct GameListedGame {
        BE_CLASS(GameListedGame)

        BE_REFLECT_FIELD bool controllable = false;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD PoolString scene;
    };

    /// `list_games`: no `active` member while no game is active
    struct GameListAnswer {
        BE_CLASS(GameListAnswer)

        BE_REFLECT_FIELD Optional<PoolString> active;
        BE_REFLECT_FIELD Vector<GameListedGame> games;
    };

    /// `switch_game`
    struct GameSwitchAnswer {
        BE_CLASS(GameSwitchAnswer)

        BE_REFLECT_FIELD PoolString active;
    };

    /// `reset` and `set_state`: the game and its state version. `reset` of a game that cannot be controlled has no state version, hence no `version` member
    struct GameVersionAnswer {
        BE_CLASS(GameVersionAnswer)

        BE_REFLECT_FIELD PoolString game;
        BE_REFLECT_FIELD Optional<uint64_t> version;
    };

    /// `run_until`: the clock of `step` and how the wait ended. `game` and `version` are left out when no game can be read (an `event` wait needs none)
    struct GameRunUntilAnswer : ControlClock {
        BE_CLASS(GameRunUntilAnswer)

        BE_REFLECT_FIELD uint32_t frames = 0;
        BE_REFLECT_FIELD bool met = false;
        BE_REFLECT_FIELD Optional<PoolString> game;
        BE_REFLECT_FIELD Optional<PoolString> reason;
        BE_REFLECT_FIELD Optional<uint64_t> version;
    };

}  // namespace BECore
