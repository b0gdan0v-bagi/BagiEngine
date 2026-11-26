#pragma once

#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Types/Types.h>

namespace BECore::Demo {

    /// The names the demo's code shares with config/SceneConfig.xml and the tests.
    struct DemoNames {
        static constexpr auto scene = "DemoScene"_intern;
        static constexpr auto game = "Demo"_intern;
        static constexpr auto animateVar = "demoAnimate"_intern;
        static constexpr auto emissionVar = "demoEmission"_intern;
        static constexpr auto animateAction = "DemoAnimate"_intern;
        static constexpr auto emissionAction = "DemoEmission"_intern;
        // The values of animateVar: the defaults of a UIToggleComponent
        static constexpr StringView on = "true";
        static constexpr StringView off = "false";
    };

}  // namespace BECore::Demo
