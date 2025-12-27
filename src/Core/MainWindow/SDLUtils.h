#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

    /**
     * Утилиты для работы с SDL
     */
    class SDLUtils {
    public:
        /**
         * Преобразует строку с именами флагов SDL в SDL_WindowFlags
         * Поддерживает форматы:
         * - "SDL_WINDOW_FULLSCREEN|SDL_WINDOW_RESIZABLE" (pipe-separated)
         * - "SDL_WINDOW_FULLSCREEN,SDL_WINDOW_RESIZABLE" (comma-separated)
         * - "SDL_WINDOW_FULLSCREEN SDL_WINDOW_RESIZABLE" (space-separated)
         * 
         * @param flagsString Строка с именами флагов
         * @return SDL_WindowFlags с объединенными флагами
         */
        static SDL_WindowFlags ParseWindowFlags(std::string_view flagsString);

        /**
         * Преобразует имя флага в его числовое значение
         * @param flagName Имя флага (например, "SDL_WINDOW_FULLSCREEN")
         * @return Значение флага или 0, если имя не распознано
         */
        static SDL_WindowFlags GetWindowFlagValue(std::string_view flagName);

    private:
        /**
         * Разделяет строку по разделителям (|, , или пробел)
         * @param str Строка для разделения
         * @return Вектор подстрок
         */
        static std::vector<std::string> SplitFlagsString(std::string_view str);
    };

}  // namespace Core

