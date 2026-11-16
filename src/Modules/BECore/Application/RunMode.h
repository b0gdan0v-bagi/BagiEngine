#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Utils/EnumUtils.h>
#include <SDL3/SDL_keycode.h>
#include <cstdint>
#include <expected>

#if !defined(BE_WITH_EDITOR)
#error "BE_WITH_EDITOR is defined by cmake/BagiEngine.cmake (add_compile_definitions); build through CMake"
#endif

namespace BECore {

    /**
     * @brief How the executable runs, chosen by the `mode` attribute of config/ApplicationConfig.xml or, for one run, by `--editor` / `--game`.
     *
     * Editor: ImGui, the editor panels and the scene shown in the viewport window (config/WidgetsConfig.xml).
     * Game: no ImGui; the scene is drawn straight into the back buffer (config/GameWidgetsConfig.xml).
     *
     * The config only asks for a mode (ApplicationConfig::_mode: the requested mode); the build decides which one runs
     * (ResolveRunMode: the resolved mode). A build without the editor (BE_WITH_EDITOR=0) has only Game.
     */
    CORE_ENUM(RunMode, uint8_t, Editor, Game)

    /// Flags of the command line that override the file's `mode` for one run (RequestedRunMode)
    namespace RunModeArguments {
        inline constexpr StringView kEditor = "--editor";
        inline constexpr StringView kGame = "--game";
    }  // namespace RunModeArguments

    /// The mode asked for this run: `--editor` or `--game` of @p args, else @p fileMode (ApplicationConfig::_mode); both flags
    /// together are an error, the message for the log. The file is never written, so a run in the other mode leaves it as it is
    [[nodiscard]] std::expected<RunMode, String> RequestedRunMode(RunMode fileMode, Span<const String> args);

    /// True when this build contains the editor panels (BE_WITH_EDITOR, see cmake/BagiEngine.cmake).
    inline constexpr bool kEditorBuilt = BE_WITH_EDITOR != 0;

    /// The mode that runs in this build: @p requested, except that Editor becomes Game (with one warning) when !kEditorBuilt.
    RunMode ResolveRunMode(RunMode requested);

    RunMode OtherRunMode(RunMode mode);

    /// Name of the config that lists the widgets created in @p mode.
    PoolString GetWidgetsConfigName(RunMode mode);

    /// The chord that switches Game and Editor at run time: Ctrl+Shift+E (left or right Ctrl and Shift).
    /// A chord and not a bare F-key: F-keys are media keys on a Mac laptop and a game may bind them.
    /// SDL3 gives the keycode of the key with no modifier applied ('e' = SDLK_E, not 'E'); with the default hint
    /// SDL_HINT_KEYCODE_OPTIONS ("french_numbers,latin_letters") the E key is SDLK_E on non-Latin layouts too.
    inline constexpr SDL_Keycode kEditorToggleKey = SDLK_E;
    inline constexpr SDL_Keymod kEditorToggleRequiredMods[] = {SDL_KMOD_CTRL, SDL_KMOD_SHIFT};
    /// Alt or the GUI key (Cmd, Windows) held makes it another shortcut. Num Lock, Caps Lock and the like are ignored.
    inline constexpr SDL_Keymod kEditorToggleForbiddenMods = SDL_KMOD_ALT | SDL_KMOD_GUI;

    /// True for the key-down of the switch chord (@p key and @p mod as in SDL_KeyboardEvent) that is not a key repeat.
    bool IsEditorToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat);

}  // namespace BECore
