#pragma once

namespace BECore {

    /**
     * @brief Configuration for SDL window creation
     *
     * Loaded via BE_CLASS reflection from the window config node.
     * Fields mirror the XML attributes previously parsed manually in SDLMainWindow.
     */
    struct SDLWindowConfig {
        BE_CLASS(SDLWindowConfig)

        /// Empty: the title is composed from the active game (ComposeWindowTitle); a game's own config may set one
        BE_REFLECT_FIELD eastl::string _title;
        BE_REFLECT_FIELD int _width = 800;
        BE_REFLECT_FIELD int _height = 600;
        BE_REFLECT_FIELD eastl::string _windowFlags;
    };

}  // namespace BECore
