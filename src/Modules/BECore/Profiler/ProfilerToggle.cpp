#include "ProfilerToggle.h"

#include <BECore/Application/RunMode.h>

namespace BECore {

    bool IsProfilerToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat) {
        // The modifiers of the editor switch (Ctrl and Shift, no Alt or GUI key) on another key
        if (repeat || key != kProfilerToggleKey || (mod & kEditorToggleForbiddenMods) != 0) {
            return false;
        }
        for (const SDL_Keymod required : kEditorToggleRequiredMods) {
            if ((mod & required) == 0) {
                return false;
            }
        }
        return true;
    }

}  // namespace BECore
