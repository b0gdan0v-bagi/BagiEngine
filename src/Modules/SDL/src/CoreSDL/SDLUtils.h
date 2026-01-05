#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <string_view>

namespace Core {

    class SDLUtils {
    public:
        static SDL_WindowFlags ParseWindowFlags(std::string_view flagsString);
        static SDL_WindowFlags GetWindowFlagValue(std::string_view flagName);
    };

}  // namespace Core


