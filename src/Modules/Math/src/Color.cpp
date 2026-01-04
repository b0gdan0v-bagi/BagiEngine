#include "Color.h"

namespace Math {

    std::optional<Color> Color::ParseColorFromString(std::string_view data) {
        if (data.empty()) {
            return {};
        }

        // Разделяем строку по запятым
        std::vector<std::string_view> tokens;
        size_t start = 0;
        size_t pos = 0;
        while ((pos = data.find(',', start)) != std::string::npos) {
            tokens.push_back(data.substr(start, pos - start));
            start = pos + 1;
        }
        tokens.push_back(data.substr(start));

        if (tokens.size() != 4) {
            return {};
        }

        const auto fromStringView = [](std::string_view s) -> unsigned char {
            unsigned char result;
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
            if (ec == std::errc{}) {
                return result;
            }
            return 0;
        };

        return Color{fromStringView(tokens[0]), fromStringView(tokens[1]), fromStringView(tokens[2]), fromStringView(tokens[3])};
    }

}  // namespace Math

