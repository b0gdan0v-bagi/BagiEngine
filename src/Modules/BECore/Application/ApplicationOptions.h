#pragma once

#include <BECore/MainWindow/WindowSimulation.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief What the application asks of the window and the renderer that the ApplicationFabric creates.
     *
     * Application fills it from the start-up flags (ControlSettings) and hands it to ApplicationFabric::Create().
     */
    struct ApplicationOptions {
        /// No window: the video driver is a dummy one and the renderer is the SDL software renderer, whatever the config says. The game is seen through a screenshot
        bool headless = false;
        /// The start size and the pixel density of a headless window (`--window`, `--density`); a value that is not given stays the config's size at density 1
        Optional<WindowSize> windowSize;
        Optional<float> density;
    };

}  // namespace BECore
