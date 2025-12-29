#include "Color.h"

namespace Math {

    Color Color::ParseColorFromString(const std::string& colorStr, const Color& defaultValue) {
        if (colorStr.empty()) {
            return defaultValue;
        }

        std::vector<unsigned char> components;

        // Определяем разделитель (запятая или пробел)
        bool useComma = colorStr.find(',') != std::string::npos;
        char delimiter = useComma ? ',' : ' ';

        // Используем функцию Split для разделения строки
        auto tokens = Core::Split(colorStr, delimiter);

        for (const auto& token : tokens) {
            // Удаляем пробелы в начале и конце
            size_t first = token.find_first_not_of(" \t");
            if (first == std::string::npos) {
                continue;
            }

            size_t last = token.find_last_not_of(" \t");
            std::string trimmed = token.substr(first, last - first + 1);

            if (trimmed.empty()) {
                continue;
            }

            // Парсим число без исключений
            char* endPtr = nullptr;
            errno = 0;
            long value = std::strtol(trimmed.c_str(), &endPtr, 10);

            // Проверяем успешность парсинга
            if (endPtr != nullptr && *endPtr == '\0' && errno == 0) {
                components.push_back(static_cast<unsigned char>(std::clamp(value, 0L, 255L)));
            }
        }

        // Если успешно распарсили 4 компонента (RGBA), используем их
        if (components.size() == 4) {
            return Color(components[0], components[1], components[2], components[3]);
        } else if (components.size() == 3) {
            // Если только RGB, используем альфа = 255
            return Color(components[0], components[1], components[2], 255);
        }

        return defaultValue;
    }

}  // namespace Math

