#pragma once

#include <BECore/Application/RenderBackendChoice.h>
#include <BECore/Types/Types.h>
#include <SDL3/SDL.h>

namespace BECore {

    /// What one run asks of the window flags (ApplicationOptions: `--headless`, `--hidden-window`, `--backend`)
    struct WindowStartOptions {
        bool headless = false;
        bool hidden = false;
        RenderBackendChoice backend = RenderBackendChoice::Auto;
    };

    class SDLUtils {
    public:
        static SDL_WindowFlags ParseWindowFlags(StringView flagsString);
        static SDL_WindowFlags GetWindowFlagValue(StringView flagName);

        /**
         * @brief The flags of the config changed for this run; pure, so a test needs no window.
         *
         * Hidden adds SDL_WINDOW_HIDDEN and drops SDL_WINDOW_MAXIMIZED and SDL_WINDOW_HIGH_PIXEL_DENSITY: the output is the config's size
         * in pixels on every display, so the numbers of two machines compare. SdlGpu drops SDL_WINDOW_VULKAN (a Metal renderer must not
         * share a Vulkan view), Vulkan adds it, Auto leaves the config's. A headless window never has it (the dummy driver has no surface).
         */
        [[nodiscard]] static SDL_WindowFlags ApplyStartOptions(SDL_WindowFlags flags, const WindowStartOptions& options);

        /// Every flag name `GetWindowFlagValue` knows, sorted: the one list the editor offers and the loader reads.
        static Vector<String> GetWindowFlagNames();
    };

}  // namespace BECore
