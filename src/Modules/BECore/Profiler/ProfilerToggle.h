#pragma once

#include <SDL3/SDL_keycode.h>

namespace BECore {

    /// The profiler's views exist in a build with the editor or the profiler, not in a plain Release
    inline constexpr bool kProfilerViewsBuilt = BE_WITH_EDITOR != 0 || BE_PROFILER != 0;

    /// The stats build (ENGINE_PROFILER_STATS): the profiler is on in a Release build so that a shipped page or executable can show the overlay
    inline constexpr bool kProfilerStatsBuild = BE_PROFILER_STATS != 0;

    /// The chord that shows and hides the profiler's views: Ctrl+Shift+P (left or right Ctrl and Shift; the Game overlay in Game mode,
    /// the Profiler panel in the editor). Read from the raw SDL event like Ctrl+Shift+E (RunMode.h), so it also works over a text field.
    inline constexpr SDL_Keycode kProfilerToggleKey = SDLK_P;

    /// True for the key-down of the chord (@p key and @p mod as in SDL_KeyboardEvent) that is not a key repeat; Alt or the GUI key
    /// held makes it another shortcut
    bool IsProfilerToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat);

    /// The chord that saves the ring's last seconds as a trace while the Game overlay is shown: Ctrl+Shift+T, the modifier rules of the toggle
    inline constexpr SDL_Keycode kProfilerExportKey = SDLK_T;

    bool IsProfilerExportChord(SDL_Keycode key, SDL_Keymod mod, bool repeat);

}  // namespace BECore
