#include "String.h"

namespace Core {

    std::vector<std::string> Split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        
        if (str.empty()) {
            return result;
        }

        size_t start = 0;
        size_t pos = 0;

        while ((pos = str.find(delimiter, start)) != std::string::npos) {
            result.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }

        // Добавляем последнюю часть после последнего разделителя
        result.push_back(str.substr(start));

        return result;
    }

}  // namespace Core

