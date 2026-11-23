#include "ProfilerToggle.h"

#include <BECore/Application/RunMode.h>

namespace BECore {

    namespace {

        // The modifiers of the editor switch (Ctrl and Shift, no Alt or GUI key) on another key
        bool IsChord(SDL_Keycode expected, SDL_Keycode key, SDL_Keymod mod, bool repeat) {
            if (repeat || key != expected || (mod & kEditorToggleForbiddenMods) != 0) {
                return false;
            }
            for (const SDL_Keymod required : kEditorToggleRequiredMods) {
                if ((mod & required) == 0) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    bool IsProfilerToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat) {
        return IsChord(kProfilerToggleKey, key, mod, repeat);
    }

    bool IsProfilerExportChord(SDL_Keycode key, SDL_Keymod mod, bool repeat) {
        return IsChord(kProfilerExportKey, key, mod, repeat);
    }

}  // namespace BECore
