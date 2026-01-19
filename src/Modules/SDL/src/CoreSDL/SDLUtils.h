#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <string_view>

namespace BECore {

    class SDLUtils {
    public:
        static SDL_WindowFlags ParseWindowFlags(eastl::string_view flagsString);
        static SDL_WindowFlags GetWindowFlagValue(eastl::string_view flagName);
    };

}  // namespace BECore


