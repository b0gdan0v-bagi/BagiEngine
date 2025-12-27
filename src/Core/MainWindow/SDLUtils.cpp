#include "SDLUtils.h"

#include <boost/algorithm/string.hpp>
#include <unordered_map>

namespace Core {

    SDL_WindowFlags SDLUtils::ParseWindowFlags(std::string_view flagsString) {
        if (flagsString.empty()) {
            return 0;
        }

        // Удаляем пробелы в начале и конце
        std::string trimmed = boost::trim_copy(std::string(flagsString));
        if (trimmed.empty()) {
            return 0;
        }

        // Проверяем, является ли строка числом (для обратной совместимости)
        // Если строка содержит только цифры, пробелы и знаки +/-, пробуем распарсить как число
        bool isNumeric = true;
        for (char c : trimmed) {
            if (!std::isdigit(static_cast<unsigned char>(c)) && c != ' ' && c != '+' && c != '-' && c != 'x' && c != 'X') {
                isNumeric = false;
                break;
            }
        }

        if (isNumeric) {
            // Удаляем все пробелы для парсинга
            std::string numStr = trimmed;
            numStr.erase(std::remove(numStr.begin(), numStr.end(), ' '), numStr.end());
            if (!numStr.empty()) {
                // Используем strtoull, который не бросает исключения
                char* endPtr = nullptr;
                errno = 0;
                unsigned long long value = std::strtoull(numStr.c_str(), &endPtr, 0);
                // Проверяем, что парсинг успешен (endPtr указывает на конец строки и нет ошибки)
                if (endPtr != nullptr && *endPtr == '\0' && errno == 0) {
                    return static_cast<SDL_WindowFlags>(value);
                }
            }
        }

        // Разделяем строку по разделителям (для имен флагов)
        std::vector<std::string> flagNames = SplitFlagsString(trimmed);

        SDL_WindowFlags result = 0;
        for (const auto& flagName : flagNames) {
            SDL_WindowFlags flagValue = GetWindowFlagValue(flagName);
            result |= flagValue;
        }

        return result;
    }

    SDL_WindowFlags SDLUtils::GetWindowFlagValue(std::string_view flagName) {
        // Удаляем пробелы
        std::string trimmed = boost::trim_copy(std::string(flagName));
        
        // Маппинг имен флагов на их значения
        static const std::unordered_map<std::string, SDL_WindowFlags> flagMap = {
            {"SDL_WINDOW_FULLSCREEN", SDL_WINDOW_FULLSCREEN},
            {"SDL_WINDOW_OPENGL", SDL_WINDOW_OPENGL},
            {"SDL_WINDOW_OCCLUDED", SDL_WINDOW_OCCLUDED},
            {"SDL_WINDOW_HIDDEN", SDL_WINDOW_HIDDEN},
            {"SDL_WINDOW_BORDERLESS", SDL_WINDOW_BORDERLESS},
            {"SDL_WINDOW_RESIZABLE", SDL_WINDOW_RESIZABLE},
            {"SDL_WINDOW_MINIMIZED", SDL_WINDOW_MINIMIZED},
            {"SDL_WINDOW_MAXIMIZED", SDL_WINDOW_MAXIMIZED},
            {"SDL_WINDOW_MOUSE_GRABBED", SDL_WINDOW_MOUSE_GRABBED},
            {"SDL_WINDOW_INPUT_FOCUS", SDL_WINDOW_INPUT_FOCUS},
            {"SDL_WINDOW_MOUSE_FOCUS", SDL_WINDOW_MOUSE_FOCUS},
            {"SDL_WINDOW_EXTERNAL", SDL_WINDOW_EXTERNAL},
            {"SDL_WINDOW_MODAL", SDL_WINDOW_MODAL},
            {"SDL_WINDOW_HIGH_PIXEL_DENSITY", SDL_WINDOW_HIGH_PIXEL_DENSITY},
            {"SDL_WINDOW_MOUSE_CAPTURE", SDL_WINDOW_MOUSE_CAPTURE},
            {"SDL_WINDOW_MOUSE_RELATIVE_MODE", SDL_WINDOW_MOUSE_RELATIVE_MODE},
            {"SDL_WINDOW_ALWAYS_ON_TOP", SDL_WINDOW_ALWAYS_ON_TOP},
            {"SDL_WINDOW_UTILITY", SDL_WINDOW_UTILITY},
            {"SDL_WINDOW_TOOLTIP", SDL_WINDOW_TOOLTIP},
            {"SDL_WINDOW_POPUP_MENU", SDL_WINDOW_POPUP_MENU},
            {"SDL_WINDOW_KEYBOARD_GRABBED", SDL_WINDOW_KEYBOARD_GRABBED},
            {"SDL_WINDOW_FILL_DOCUMENT", SDL_WINDOW_FILL_DOCUMENT},
            {"SDL_WINDOW_VULKAN", SDL_WINDOW_VULKAN},
            {"SDL_WINDOW_METAL", SDL_WINDOW_METAL},
            {"SDL_WINDOW_TRANSPARENT", SDL_WINDOW_TRANSPARENT},
            {"SDL_WINDOW_NOT_FOCUSABLE", SDL_WINDOW_NOT_FOCUSABLE},
        };

        auto it = flagMap.find(trimmed);
        if (it != flagMap.end()) {
            return it->second;
        }

        return 0;
    }

    std::vector<std::string> SDLUtils::SplitFlagsString(std::string_view str) {
        std::vector<std::string> result;
        std::string strCopy(str);

        // Пробуем разделить по |
        if (strCopy.find('|') != std::string::npos) {
            boost::split(result, strCopy, boost::is_any_of("|"), boost::token_compress_off);
        }
        // Пробуем разделить по запятой
        else if (strCopy.find(',') != std::string::npos) {
            boost::split(result, strCopy, boost::is_any_of(","), boost::token_compress_off);
        }
        // Разделяем по пробелам
        else {
            boost::split(result, strCopy, boost::is_any_of(" \t\n\r"), boost::token_compress_on);
        }

        // Удаляем пустые строки и обрезаем пробелы
        result.erase(
            std::remove_if(result.begin(), result.end(),
                [](const std::string& s) {
                    return boost::trim_copy(s).empty();
                }),
            result.end()
        );

        // Обрезаем пробелы в каждой строке
        for (auto& s : result) {
            s = boost::trim_copy(s);
        }

        return result;
    }

}  // namespace Core

