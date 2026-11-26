#pragma once

#include <BECore/PoolString/StaticPoolString.h>

namespace BECore::Demo {

    /// The names the demo game and its tests share; the scene is the one of config/SceneConfig.xml.
    struct DemoNames {
        static constexpr auto scene = "DemoScene"_intern;
        static constexpr auto game = "Demo"_intern;
    };

}  // namespace BECore::Demo
