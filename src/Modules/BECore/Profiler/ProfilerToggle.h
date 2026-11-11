#pragma once

#include <SDL3/SDL_keycode.h>

namespace BECore {

    /// The profiler's views exist in a build with the editor or the profiler, not in a plain Release
    inline constexpr bool kProfilerViewsBuilt = BE_WITH_EDITOR != 0 || BE_PROFILER != 0;

    /// The chord that shows and hides the profiler's views: Ctrl+Shift+P (left or right Ctrl and Shift; the Game overlay in Game mode,
    /// the Profiler panel in the editor). Read from the raw SDL event like Ctrl+Shift+E (RunMode.h), so it also works over a text field.
    /// Ctrl+P alone is the command palette of the editor; with Shift it is free and reserved in the hotkey map.
    inline constexpr SDL_Keycode kProfilerToggleKey = SDLK_P;

    /// True for the key-down of the chord (@p key and @p mod as in SDL_KeyboardEvent) that is not a key repeat; Alt or the GUI key
    /// held makes it another shortcut
    bool IsProfilerToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat);

}  // namespace BECore
