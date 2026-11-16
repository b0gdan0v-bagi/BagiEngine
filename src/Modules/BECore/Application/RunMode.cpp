#include "RunMode.h"

namespace BECore {

    RunMode ResolveRunMode(RunMode requested) {
        if (requested == RunMode::Editor && !kEditorBuilt) {
            LOG_WARNING("Run mode: Editor requested, but this build has no editor (BE_WITH_EDITOR=OFF), starting in Game mode");
            return RunMode::Game;
        }
        return requested;
    }

    std::expected<RunMode, String> RequestedRunMode(RunMode fileMode, Span<const String> args) {
        Optional<RunMode> flag;
        for (const String& arg : args) {
            const StringView argument(arg.data(), arg.size());
            Optional<RunMode> asked;
            if (argument == RunModeArguments::kEditor) {
                asked = RunMode::Editor;
            } else if (argument == RunModeArguments::kGame) {
                asked = RunMode::Game;
            }
            if (asked && flag && *flag != *asked) {
                return std::unexpected(Format("{} and {} together: pick one run mode", RunModeArguments::kEditor, RunModeArguments::kGame));
            }
            if (asked) {
                flag = asked;
            }
        }
        return flag.value_or(fileMode);
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
