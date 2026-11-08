#pragma once

namespace BECore {

    /// `<defaultGame name="..."/>` of config/GamesConfig.xml: the game GameManager::Initialize() activates.
    struct DefaultGameConfig {
        BE_CLASS(DefaultGameConfig)

        BE_REFLECT_FIELD PoolString _name;  ///< GetName() of an IGame; empty: the first registered game
    };

    /// The root of config/GamesConfig.xml, read through the reflection by GameManager::Initialize().
    /// The file is optional, and a missing `defaultGame` leaves the name empty.
    struct GamesConfig {
        BE_CLASS(GamesConfig)

        BE_REFLECT_FIELD DefaultGameConfig _defaultGame;
    };

}  // namespace BECore
