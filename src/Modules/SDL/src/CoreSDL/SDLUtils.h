#pragma once

#include <BECore/Types/Types.h>
#include <SDL3/SDL.h>

namespace BECore {

    class SDLUtils {
    public:
        static SDL_WindowFlags ParseWindowFlags(StringView flagsString);
        static SDL_WindowFlags GetWindowFlagValue(StringView flagName);

        /// Every flag name `GetWindowFlagValue` knows, sorted: the one list the editor offers and the loader reads.
        static Vector<String> GetWindowFlagNames();
    };

}  // namespace BECore
