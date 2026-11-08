#pragma once

#include <SDL3/SDL_keycode.h>

#if !defined(BE_WITH_EDITOR)
#error "BE_WITH_EDITOR is defined by cmake/BagiEngine.cmake (add_compile_definitions); build through CMake"
#endif

namespace BECore {

    /**
     * @brief How the executable runs, chosen by the `mode` attribute of config/ApplicationConfig.xml.
     *
     * Editor: ImGui, the editor panels and the scene shown in the viewport window (config/WidgetsConfig.xml).
     * Game: no ImGui; the scene is drawn straight into the back buffer (config/GameWidgetsConfig.xml).
     *
     * The config only asks for a mode (ReadRunMode: the requested mode); the build decides which one runs
     * (ResolveRunMode: the resolved mode). A build without the editor (BE_WITH_EDITOR=0) has only Game.
     */
    CORE_ENUM(RunMode, uint8_t, Editor, Game)

    /// True when this build contains the editor panels (BE_WITH_EDITOR, see cmake/BagiEngine.cmake).
    inline constexpr bool kEditorBuilt = BE_WITH_EDITOR != 0;

    /// Reads the `mode` attribute of the ApplicationConfig root. A missing or unknown value gives RunMode::Editor.
    /// This is the requested mode; pass it through ResolveRunMode before acting on it.
    RunMode ReadRunMode(const XmlNode& applicationConfig);

    /// The mode that runs in this build: @p requested, except that Editor becomes Game (with one warning) when !kEditorBuilt.
    RunMode ResolveRunMode(RunMode requested);

    /// The other mode: Editor for Game and Game for Editor.
    RunMode OtherRunMode(RunMode mode);

    /// Name of the config that lists the widgets created in @p mode.
    PoolString GetWidgetsConfigName(RunMode mode);

    /// The chord that switches Game and Editor at run time: Ctrl+Shift+E (left or right Ctrl and Shift).
    /// A chord and not a bare F-key: F-keys are media keys on a Mac laptop and a game may bind them.
    /// SDL3 gives the keycode of the key with no modifier applied ('e' = SDLK_E, not 'E'); with the default hint
    /// SDL_HINT_KEYCODE_OPTIONS ("latin_letters") the E key is SDLK_E on non-Latin layouts too.
    inline constexpr SDL_Keycode kEditorToggleKey = SDLK_E;
    inline constexpr SDL_Keymod kEditorToggleRequiredMods[] = {SDL_KMOD_CTRL, SDL_KMOD_SHIFT};
    /// Alt or the GUI key (Cmd, Windows) held makes it another shortcut. Num Lock, Caps Lock and the like are ignored.
    inline constexpr SDL_Keymod kEditorToggleForbiddenMods = SDL_KMOD_ALT | SDL_KMOD_GUI;

    /// True for the key-down of the switch chord (@p key and @p mod as in SDL_KeyboardEvent) that is not a key repeat.
    bool IsEditorToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat);

}  // namespace BECore
