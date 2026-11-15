#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Configuration for SDL window creation
     */
    struct SDLWindowConfig {
        BE_CLASS(SDLWindowConfig)

        /// Empty: the title is composed from the active game (ComposeWindowTitle); a game's own config may set one
        BE_REFLECT_FIELD String _title;
        BE_REFLECT_FIELD int _width = 800;
        BE_REFLECT_FIELD int _height = 600;
        BE_REFLECT_FIELD String _windowFlags;
    };

}  // namespace BECore
