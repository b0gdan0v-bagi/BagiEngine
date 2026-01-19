#include "SDLUtils.h"

#include <BECore/Utils/String.h>

namespace BECore {

    namespace {
        struct FlagEntry {
            eastl::string_view name;
            SDL_WindowFlags value;
        };

        template <size_t N>
        consteval std::array<FlagEntry, N> SortFlagEntryFlags(std::array<FlagEntry, N> data) {
            std::sort(data.begin(), data.end(), [](const FlagEntry& a, const FlagEntry& b) { return a.name < b.name; });
            return data;
        }
    }  // namespace

    SDL_WindowFlags SDLUtils::ParseWindowFlags(eastl::string_view flagsString) {
        if (flagsString.empty()) {
            return 0;
        }

        // Разделяем строку по разделителям (для имен флагов)
        const auto& flagNames = BECore::String::Split(flagsString, '|');

        SDL_WindowFlags result = 0;
        for (const auto& flagName : flagNames) {
            SDL_WindowFlags flagValue = GetWindowFlagValue(flagName);
            result |= flagValue;
        }

        return result;
    }

    SDL_WindowFlags SDLUtils::GetWindowFlagValue(eastl::string_view flagName) {
        // Маппинг имен флагов на их значения
        static constexpr auto flagTable = SortFlagEntryFlags(std::array<FlagEntry, 26>{{
            {.name = "SDL_WINDOW_FULLSCREEN", .value = SDL_WINDOW_FULLSCREEN},
            {.name = "SDL_WINDOW_OPENGL", .value = SDL_WINDOW_OPENGL},
            {.name = "SDL_WINDOW_OCCLUDED", .value = SDL_WINDOW_OCCLUDED},
            {.name = "SDL_WINDOW_HIDDEN", .value = SDL_WINDOW_HIDDEN},
            {.name = "SDL_WINDOW_BORDERLESS", .value = SDL_WINDOW_BORDERLESS},
            {.name = "SDL_WINDOW_RESIZABLE", .value = SDL_WINDOW_RESIZABLE},
            {.name = "SDL_WINDOW_MINIMIZED", .value = SDL_WINDOW_MINIMIZED},
            {.name = "SDL_WINDOW_MAXIMIZED", .value = SDL_WINDOW_MAXIMIZED},
            {.name = "SDL_WINDOW_MOUSE_GRABBED", .value = SDL_WINDOW_MOUSE_GRABBED},
            {.name = "SDL_WINDOW_INPUT_FOCUS", .value = SDL_WINDOW_INPUT_FOCUS},
            {.name = "SDL_WINDOW_MOUSE_FOCUS", .value = SDL_WINDOW_MOUSE_FOCUS},
            {.name = "SDL_WINDOW_EXTERNAL", .value = SDL_WINDOW_EXTERNAL},
            {.name = "SDL_WINDOW_MODAL", .value = SDL_WINDOW_MODAL},
            {.name = "SDL_WINDOW_HIGH_PIXEL_DENSITY", .value = SDL_WINDOW_HIGH_PIXEL_DENSITY},
            {.name = "SDL_WINDOW_MOUSE_CAPTURE", .value = SDL_WINDOW_MOUSE_CAPTURE},
            {.name = "SDL_WINDOW_MOUSE_RELATIVE_MODE", .value = SDL_WINDOW_MOUSE_RELATIVE_MODE},
            {.name = "SDL_WINDOW_ALWAYS_ON_TOP", .value = SDL_WINDOW_ALWAYS_ON_TOP},
            {.name = "SDL_WINDOW_UTILITY", .value = SDL_WINDOW_UTILITY},
            {.name = "SDL_WINDOW_TOOLTIP", .value = SDL_WINDOW_TOOLTIP},
            {.name = "SDL_WINDOW_POPUP_MENU", .value = SDL_WINDOW_POPUP_MENU},
            {.name = "SDL_WINDOW_KEYBOARD_GRABBED", .value = SDL_WINDOW_KEYBOARD_GRABBED},
            {.name = "SDL_WINDOW_FILL_DOCUMENT", .value = SDL_WINDOW_FILL_DOCUMENT},
            {.name = "SDL_WINDOW_VULKAN", .value = SDL_WINDOW_VULKAN},
            {.name = "SDL_WINDOW_METAL", .value = SDL_WINDOW_METAL},
            {.name = "SDL_WINDOW_TRANSPARENT", .value = SDL_WINDOW_TRANSPARENT},
            {.name = "SDL_WINDOW_NOT_FOCUSABLE", .value = SDL_WINDOW_NOT_FOCUSABLE},
        }});

        auto it = std::ranges::lower_bound(flagTable, flagName, {}, &FlagEntry::name);

        if (it != flagTable.end() && it->name == flagName) {
            return it->value;
        }

        return 0;
    }
}  // namespace BECore
