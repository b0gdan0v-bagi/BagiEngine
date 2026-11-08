#include "RunMode.h"

namespace BECore {

    RunMode ReadRunMode(const XmlNode& applicationConfig) {
        const auto modeName = applicationConfig.ParseAttribute<eastl::string_view>("mode");
        if (!modeName) {
            return RunMode::Editor;
        }

        const auto mode = EnumUtils<RunMode>::Cast(*modeName);
        if (!mode) {
            LOG_WARNING("ApplicationConfig: unknown mode '{}', starting in Editor mode"_format(*modeName));
            return RunMode::Editor;
        }
        return *mode;
    }

    RunMode ResolveRunMode(RunMode requested) {
        if (requested == RunMode::Editor && !kEditorBuilt) {
            LOG_WARNING("Run mode: Editor requested, but this build has no editor (BE_WITH_EDITOR=OFF), starting in Game mode");
            return RunMode::Game;
        }
        return requested;
    }

    RunMode OtherRunMode(RunMode mode) {
        return mode == RunMode::Editor ? RunMode::Game : RunMode::Editor;
    }

    PoolString GetWidgetsConfigName(RunMode mode) {
        if (mode == RunMode::Game) {
            return "GameWidgetsConfig"_intern;
        }
        return "WidgetsConfig"_intern;
    }

    bool IsEditorToggle(SDL_Keycode key, SDL_Keymod mod, bool repeat) {
        if (repeat || key != kEditorToggleKey || (mod & kEditorToggleForbiddenMods) != 0) {
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
