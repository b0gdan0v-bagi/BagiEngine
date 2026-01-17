#include "String.h"

namespace BECore {

    String::SmallStrVector String::Split(std::string_view str, char delimiter) {
        SmallStrVector result;

        if (str.empty()) {
            return result;
        }

        size_t start = 0;
        size_t pos = 0;

        while ((pos = str.find(delimiter, start)) != std::string::npos) {
            result.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }

        result.push_back(str.substr(start));

        return result;
    }
}  // namespace BECore
