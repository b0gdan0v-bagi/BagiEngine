#pragma once

#include <BECore/PoolString/StaticPoolString.h>

namespace BECore::Stress {

    /// The names the stress game and its tests share; the scenes are those of config/SceneConfig.xml.
    struct StressNames {
        static constexpr auto sprites = "StressSprites"_intern;
        static constexpr auto entities = "StressEntities"_intern;
        static constexpr auto ui = "StressUi"_intern;
        static constexpr auto game = "Stress"_intern;
        static constexpr auto objectsCounter = "Stress objects"_intern;
    };

}  // namespace BECore::Stress
