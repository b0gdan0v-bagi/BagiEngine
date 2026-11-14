#pragma once

#include <SDL3/SDL.h>

namespace BECore {

    class SDLUtils {
    public:
        static SDL_WindowFlags ParseWindowFlags(StringView flagsString);
        static SDL_WindowFlags GetWindowFlagValue(StringView flagName);
    };

}  // namespace BECore
