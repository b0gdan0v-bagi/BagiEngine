#pragma once

namespace BECore {

    /// The title of the engine's own window when no game names it
    inline constexpr eastl::string_view kEngineWindowTitle = "BagiEngine";

    /// The suffix of the title in the editor: "<Game> — BagiEngine Editor" (the dash is U+2014)
    inline constexpr eastl::string_view kEditorWindowTitle = "BagiEngine Editor";

    /**
     * @brief The title of the main window.
     *
     * Editor mode: "<Game> — BagiEngine Editor", or "BagiEngine Editor" while no game is active.
     * Game mode: @p configTitle when the game's own SDLWindowConfig sets one, else the game's name, else "BagiEngine".
     *
     * @param configTitle the `title` of SDLWindowConfig (empty when the config sets none)
     * @param gameName GetName() of the active game (empty when there is none)
     * @param editorMode the editor run mode is on
     */
    eastl::string ComposeWindowTitle(eastl::string_view configTitle, eastl::string_view gameName, bool editorMode);

}  // namespace BECore
